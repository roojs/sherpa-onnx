// sherpa-onnx/csrc/offline-websocket-declared-size-test.cc
//
// Copyright (c)  2026  sherpa-onnx contributors
//
// These tests exercise OfflineWebsocketServer::OnMessage in
// offline-websocket-server-impl.cc through a real websocketpp connection.
// They fail while a client-declared byte count can size the buffer and the
// following bytes are copied without a bounds check. They pass when every
// copy is rejected before it writes past that buffer.
//
// Build (AddressSanitizer is part of the target):
//   cmake --build <build> --target offline-websocket-declared-size-test
//   ./<build>/bin/offline-websocket-declared-size-test

#include "sherpa-onnx/csrc/offline-websocket-server-impl.h"

#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "asio.hpp"  // NOLINT
#include "websocketpp/client.hpp"
#include "websocketpp/config/asio_no_tls_client.hpp"
#include "websocketpp/uri.hpp"

extern "C" const char *__asan_default_options() {
  return "detect_leaks=0:halt_on_error=1:abort_on_error=1";
}

namespace {

using Client = websocketpp::client<websocketpp::config::asio_client>;

enum class Expect {
  kStayOpen,
  kServerClose,
  kServerText,
};

struct Case {
  const char *name;
  std::vector<std::string> messages;
  Expect expect;
  int deadline_ms;
};

bool IsLittleEndian() {
  const uint16_t value = 1;
  return *reinterpret_cast<const uint8_t *>(&value) == 1;
}

void AppendLe32(std::string *out, int32_t value) {
  const uint32_t bits = static_cast<uint32_t>(value);
  out->push_back(static_cast<char>(bits & 0xffu));
  out->push_back(static_cast<char>((bits >> 8) & 0xffu));
  out->push_back(static_cast<char>((bits >> 16) & 0xffu));
  out->push_back(static_cast<char>((bits >> 24) & 0xffu));
}

// First binary frame: little-endian sample rate, little-endian declared
// byte count, then audio_bytes of payload. The server reads those int32s
// with a native load; this host is little endian, so the bytes match.
std::string FirstFrame(int32_t sample_rate, int32_t expected,
                       size_t audio_bytes, char fill) {
  std::string out;
  out.reserve(8 + audio_bytes);
  AppendLe32(&out, sample_rate);
  AppendLe32(&out, expected);
  out.append(audio_bytes, fill);
  return out;
}

std::string AudioFrame(size_t audio_bytes, char fill) {
  return std::string(audio_bytes, fill);
}

struct Observed {
  bool opened = false;
  bool failed = false;
  bool server_closed = false;
  bool got_text = false;
  std::string fail_reason;
};

int Check(const Case &test, const Observed &seen, bool timed_out,
          const std::string &handler_exception) {
  if (!handler_exception.empty()) {
    std::cerr << "RESULT " << test.name
              << " bad: exception escaped the message handler: "
              << handler_exception << "\n";
    return 3;
  }
  if (!seen.opened) {
    std::cerr << "RESULT " << test.name << " bad: connection did not open ("
              << seen.fail_reason << ")\n";
    return 4;
  }
  if (test.expect == Expect::kStayOpen) {
    if (!seen.server_closed && !seen.got_text && timed_out) {
      std::cerr << "RESULT " << test.name << " ok\n";
      return 0;
    }
    std::cerr << "RESULT " << test.name
              << " bad: in-bounds audio was closed or recognized (closed="
              << seen.server_closed << " text=" << seen.got_text
              << " timeout=" << timed_out << ")\n";
    return 1;
  }
  if (test.expect == Expect::kServerText) {
    if (seen.got_text && !seen.server_closed) {
      std::cerr << "RESULT " << test.name << " ok\n";
      return 0;
    }
    std::cerr << "RESULT " << test.name
              << " bad: exact-size audio was not recognized (closed="
              << seen.server_closed << " text=" << seen.got_text
              << " timeout=" << timed_out << ")\n";
    return timed_out ? 2 : 1;
  }
  if (seen.server_closed && !seen.got_text) {
    std::cerr << "RESULT " << test.name << " ok\n";
    return 0;
  }
  std::cerr << "RESULT " << test.name
            << " bad: connection was not rejected (closed="
            << seen.server_closed << " text=" << seen.got_text
            << " timeout=" << timed_out << ")\n";
  return timed_out ? 2 : 1;
}

int RunCase(const Case &test) {
  asio::io_context io_conn;
  asio::io_context io_work;
  sherpa_onnx::OfflineWebsocketServerConfig config;
  config.log_file = "/dev/null";
  sherpa_onnx::OfflineWebsocketServer server(io_conn, io_work, config);
  server.Run(0);

  websocketpp::lib::error_code endpoint_ec;
  auto endpoint = server.GetServer().get_local_endpoint(endpoint_ec);
  if (endpoint_ec) {
    std::cerr << "RESULT " << test.name
              << " bad: listen failed: " << endpoint_ec.message() << "\n";
    return 4;
  }
  const uint16_t port = endpoint.port();

  std::string handler_exception;
  auto work_guard = asio::make_work_guard(io_work);
  std::thread io_thread([&io_conn, &handler_exception]() {
    try {
      io_conn.run();
    } catch (const std::exception &ex) {
      handler_exception = ex.what();
    } catch (...) {
      handler_exception = "unknown exception";
    }
  });
  std::thread work_thread([&io_work]() { io_work.run(); });

  Observed seen;
  bool timed_out = true;
  {
    asio::io_context client_io;
    Client client;
    client.clear_access_channels(websocketpp::log::alevel::all);
    client.clear_error_channels(websocketpp::log::elevel::all);
    client.init_asio(&client_io);

    // Set before client.stop() so the local shutdown is not counted as a
    // close frame from the server.
    bool stopping = false;
    client.set_open_handler([&client, &test, &seen](connection_hdl hdl) {
      seen.opened = true;
      websocketpp::lib::error_code ec;
      for (const std::string &message : test.messages) {
        client.send(hdl, message.data(), message.size(),
                    websocketpp::frame::opcode::binary, ec);
        if (ec) {
          seen.fail_reason = ec.message();
          break;
        }
      }
    });
    client.set_message_handler([&client, &seen, &timed_out, &stopping](
                                   connection_hdl, Client::message_ptr msg) {
      if (msg->get_opcode() == websocketpp::frame::opcode::text) {
        seen.got_text = true;
        timed_out = false;
        stopping = true;
        client.stop();
      }
    });
    client.set_close_handler(
        [&client, &seen, &timed_out, &stopping](connection_hdl) {
          if (stopping) {
            return;
          }
          seen.server_closed = true;
          timed_out = false;
          stopping = true;
          client.stop();
        });
    client.set_fail_handler(
        [&client, &seen, &timed_out, &stopping](connection_hdl hdl) {
          seen.failed = true;
          websocketpp::lib::error_code ec;
          auto con = client.get_con_from_hdl(hdl, ec);
          if (!ec && con) {
            seen.fail_reason = con->get_ec().message();
          }
          timed_out = false;
          stopping = true;
          client.stop();
        });

    websocketpp::uri uri(false, "127.0.0.1", port, "/");
    websocketpp::lib::error_code ec;
    Client::connection_ptr con = client.get_connection(uri.str(), ec);
    if (ec) {
      std::cerr << "RESULT " << test.name
                << " bad: get_connection: " << ec.message() << "\n";
      io_conn.stop();
      work_guard.reset();
      io_work.stop();
      io_thread.join();
      work_thread.join();
      return 4;
    }
    client.connect(con);

    asio::steady_timer timer(client_io);
    timer.expires_after(std::chrono::milliseconds(test.deadline_ms));
    timer.async_wait([&client, &stopping](const asio::error_code &) {
      stopping = true;
      client.stop();
    });
    client_io.run();
  }

  io_conn.stop();
  work_guard.reset();
  io_work.stop();
  io_thread.join();
  work_thread.join();
  return Check(test, seen, timed_out, handler_exception);
}

std::vector<Case> Cases() {
  const size_t kOneMegabyte = 1u << 20;
  std::vector<Case> cases;

  // Declared size matches the audio that follows the 8-byte header.
  cases.push_back(Case{
      "exact_fit_is_recognized",
      {FirstFrame(16000, 8, 8, 'A')},
      Expect::kServerText,
      2000,
  });

  // A short first chunk that does not fill the declared buffer stays open.
  cases.push_back(Case{
      "in_bounds_partial_stays_open",
      {FirstFrame(16000, 16, 8, 'A')},
      Expect::kStayOpen,
      400,
  });

  // The existing max-utterance check rejects a declared length past 300 s.
  cases.push_back(Case{
      "declared_size_over_max_is_rejected",
      {FirstFrame(16000, 100000000, 0, 'A')},
      Expect::kServerClose,
      2000,
  });

  cases.push_back(Case{
      "header_shorter_than_8_is_rejected",
      {std::string("\x01\x02\x03\x04", 4)},
      Expect::kServerClose,
      2000,
  });

  cases.push_back(Case{
      "zero_sample_rate_is_rejected",
      {FirstFrame(0, 16, 16, 'A')},
      Expect::kServerClose,
      2000,
  });

  cases.push_back(Case{
      "negative_sample_rate_is_rejected",
      {FirstFrame(-1, 16, 16, 'A')},
      Expect::kServerClose,
      2000,
  });

  // sample_rate 16000, declared size 4, then 1 MiB. 4 is under the
  // 300-second cap, so the server resizes to 4 bytes and copies the rest.
  cases.push_back(Case{
      "first_frame_declared_4_copies_1mb",
      {FirstFrame(16000, 4, kOneMegabyte, 'A')},
      Expect::kServerClose,
      2000,
  });

  // One byte past a declared size that passed the max-length check.
  cases.push_back(Case{
      "first_frame_one_byte_past_declared_size",
      {FirstFrame(16000, 8, 9, 'A')},
      Expect::kServerClose,
      2000,
  });

  // sample_rate 1 => max is 300*1*4 = 1200. Declared size 1200 is allowed.
  // The audio is longer than 1200, so the max check does not cover the copy.
  cases.push_back(Case{
      "declared_size_at_max_but_payload_longer",
      {FirstFrame(1, 1200, 1264, 'A')},
      Expect::kServerClose,
      2000,
  });

  // Declared size 0 is not a positive length. Extra bytes are copied into
  // the empty buffer. 0 is also the "header not received yet" sentinel.
  cases.push_back(Case{
      "zero_declared_size_copies_payload",
      {FirstFrame(16000, 0, 64, 'A')},
      Expect::kServerClose,
      2000,
  });

  // Negative declared size converts to a huge size_t in vector::resize.
  cases.push_back(Case{
      "negative_declared_size_is_rejected",
      {FirstFrame(16000, -1, 0, 'A')},
      Expect::kServerClose,
      2000,
  });

  // Not a multiple of 4. The lengths match, so this is not an overrun, but
  // the declared size is still not a whole number of samples.
  cases.push_back(Case{
      "non_multiple_of_4_is_rejected",
      {FirstFrame(16000, 6, 6, 'A')},
      Expect::kServerClose,
      2000,
  });

  // Header only (declared 8, zero audio). The next frame is the else branch
  // and is longer than the buffer.
  cases.push_back(Case{
      "continuation_longer_than_declared_size",
      {FirstFrame(16000, 8, 0, 'A'), AudioFrame(64, 'B')},
      Expect::kServerClose,
      2000,
  });

  // First frame fills 16 of 32 declared bytes. The follow-up is 24 bytes,
  // which is less than the declared size but past the bytes still free.
  // A check that only compares the new frame to the declared size misses it.
  cases.push_back(Case{
      "continuation_fits_declared_size_but_passes_offset",
      {FirstFrame(16000, 32, 16, 'A'), AudioFrame(24, 'B')},
      Expect::kServerClose,
      2000,
  });

  // Several follow-ups. The second is still inside the buffer; the third
  // writes past it and leaves the cursor unequal to the declared size.
  cases.push_back(Case{
      "continuation_third_frame_writes_past_end",
      {FirstFrame(16000, 32, 8, 'A'), AudioFrame(8, 'B'), AudioFrame(32, 'C')},
      Expect::kServerClose,
      2000,
  });

  return cases;
}

}  // namespace

int main(int argc, char **argv) {
  if (!IsLittleEndian()) {
    std::cerr << "server loads the declared size with a native int32 load\n";
    return 1;
  }

  const std::vector<Case> cases = Cases();
  int failures = 0;
  int ran = 0;
  for (const Case &test : cases) {
    if (argc == 2 && std::strcmp(argv[1], test.name) != 0) {
      continue;
    }
    ++ran;
    const pid_t pid = fork();
    if (pid < 0) {
      std::cerr << "RESULT " << test.name << " bad: fork failed\n";
      return 1;
    }
    if (pid == 0) {
      const int code = RunCase(test);
      std::fflush(nullptr);
      _exit(code);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
      std::cerr << "RESULT " << test.name << " bad: waitpid failed\n";
      return 1;
    }
    if (WIFSIGNALED(status)) {
      std::cerr << "RESULT " << test.name << " bad: child died with signal "
                << WTERMSIG(status) << "\n";
      ++failures;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      std::cerr << "RESULT " << test.name << " bad: child exit " << code
                << "\n";
      ++failures;
    }
  }
  if (ran == 0) {
    std::cerr << "no test named " << (argc == 2 ? argv[1] : "") << "\n";
    return 1;
  }
  std::cerr << failures << " of " << ran << " failed\n";
  return failures == 0 ? 0 : 1;
}
