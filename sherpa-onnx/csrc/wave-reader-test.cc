// sherpa-onnx/csrc/wave-reader-test.cc
//
// Copyright (c)  2025  Posit Software, PBC

#include "sherpa-onnx/csrc/wave-reader.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "sherpa-onnx/csrc/file-utils.h"

namespace sherpa_onnx {

// RAII helper class for managing temporary test files
class TempFile {
 public:
  TempFile() : TempFile("") {}

  explicit TempFile(const std::string &suffix) {
#if defined(_WIN32)
    char temp_path[MAX_PATH];
    char temp_file[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);
    GetTempFileNameA(temp_path, "sot", 0, temp_file);
    path_ = temp_file;
    if (!suffix.empty()) {
      path_ += suffix;
      std::remove(temp_file);  // Remove the file without suffix
    }
#else
    char temp_template[] = "/tmp/sherpa_onnx_test_XXXXXX";
    int fd = mkstemp(temp_template);
    if (fd != -1) {
      close(fd);
      path_ = temp_template;
      if (!suffix.empty()) {
        path_ += suffix;
        std::remove(temp_template);  // Remove the file without suffix
      }
    }
#endif
  }

  ~TempFile() {
    if (!path_.empty()) {
      std::remove(path_.c_str());
    }
  }

  const char *path() const { return path_.c_str(); }

 private:
  std::string path_;
};

TEST(WaveReader, TestNonWavFile) {
  // Create a temporary file with non-WAV content (e.g., webm-like header)
  TempFile temp_file(".webm");

  {
    auto out = OpenOutputFile(temp_file.path(), std::ios::binary);
    // Write some content that doesn't start with RIFF
    // (webm files typically start with EBML header: 0x1a45dfa3)
    const unsigned char webm_header[] = {
        0x1a, 0x45, 0xdf, 0xa3,  // EBML header signature (NOT RIFF)
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x42, 0x86, 0x81, 0x01,
        // Add some more bytes to make it look like a real file
        0x42, 0xf7, 0x81, 0x01, 0x42, 0xf2, 0x81, 0x04, 'w', 'e', 'b', 'm'};
    out.write(reinterpret_cast<const char *>(webm_header), sizeof(webm_header));
  }

  // Test C++ API - should not segfault
  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<float> samples = ReadWave(temp_file.path(), &sample_rate, &is_ok);

  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
  EXPECT_EQ(sample_rate, -1);
}

TEST(WaveReader, TestNonExistentFile) {
  // Generate a unique path but don't create the file
  TempFile temp_file(".wav");

  // Test C++ API - should not segfault
  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<float> samples = ReadWave(temp_file.path(), &sample_rate, &is_ok);

  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
  EXPECT_EQ(sample_rate, -1);
}

TEST(WaveReader, TestTruncatedWaveFile) {
  // Create a temporary file with truncated WAV header
  TempFile temp_file(".wav");

  {
    auto out = OpenOutputFile(temp_file.path(), std::ios::binary);
    // Write only partial WAV header (less than 44 bytes required)
    const unsigned char partial_wav[] = {
        'R',  'I',  'F',
        'F',  // chunk_id
        0x00, 0x00, 0x00,
        0x00,  // chunk_size
        'W',  'A',  'V',
        'E'  // format
             // Missing the rest of the header
    };
    out.write(reinterpret_cast<const char *>(partial_wav), sizeof(partial_wav));
  }

  // Test C++ API - should not segfault
  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<float> samples = ReadWave(temp_file.path(), &sample_rate, &is_ok);

  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
  EXPECT_EQ(sample_rate, -1);
}

// SeekToDataChunk uses the on-disk chunk size as a signed seek offset. A size
// that does not advance the stream makes the reader read that header again
// and never return. Rejection must finish quickly and must not throw.
struct WaveReadStatus {
  bool threw = false;
  bool is_ok = true;
  int32_t sample_rate = -1;
  int32_t num_channels = 0;
  std::vector<float> samples;
};

void AppendLe16(std::string *out, uint16_t value) {
  out->push_back(static_cast<char>(value & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
}

void AppendLe32(std::string *out, uint32_t value) {
  out->push_back(static_cast<char>(value & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 24) & 0xff));
}

// 16 kHz, mono. audio_format 1 is integer PCM; 3 is float PCM.
std::string PcmWavePrefix(int16_t audio_format, int16_t bits_per_sample) {
  const int32_t sample_rate = 16000;
  const int16_t num_channels = 1;
  std::string out = "RIFF";
  AppendLe32(&out, 36);
  out.append("WAVE");
  out.append("fmt ");
  AppendLe32(&out, 16);
  AppendLe16(&out, static_cast<uint16_t>(audio_format));
  AppendLe16(&out, static_cast<uint16_t>(num_channels));
  AppendLe32(&out, static_cast<uint32_t>(sample_rate));
  const int32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
  AppendLe32(&out, static_cast<uint32_t>(byte_rate));
  const int16_t block_align = num_channels * bits_per_sample / 8;
  AppendLe16(&out, static_cast<uint16_t>(block_align));
  AppendLe16(&out, static_cast<uint16_t>(bits_per_sample));
  return out;
}

std::string ListChunkBackToHeader() {
  std::string out = PcmWavePrefix(1, 16);
  out.append("LIST");
  // Size -8 seeks from the end of this header back to its start.
  AppendLe32(&out, 0xfffffff8u);
  return out;
}

std::string CyclicChunkHeaders() {
  std::string out = PcmWavePrefix(1, 16);
  // size 0 leaves the cursor on the next header, whose size -16 seeks back
  // onto this one.
  out.append("LIST");
  AppendLe32(&out, 0);
  out.append("LIST");
  AppendLe32(&out, 0xfffffff0u);
  return out;
}

template <typename Fn>
bool RunBounded(Fn fn, WaveReadStatus *status) {
  std::packaged_task<void()> task([fn = std::move(fn), status]() mutable {
    try {
      fn(status);
    } catch (...) {
      status->threw = true;
    }
  });
  auto future = task.get_future();
  std::thread worker(std::move(task));
  if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
    worker.detach();
    ADD_FAILURE() << "wave reader did not return within 2 seconds";
    return false;
  }
  worker.join();
  future.get();
  return true;
}

template <typename Fn>
void ExpectRejected(Fn fn) {
  WaveReadStatus status;
  if (!RunBounded(std::move(fn), &status)) {
    return;
  }
  EXPECT_FALSE(status.threw);
  EXPECT_FALSE(status.is_ok);
  EXPECT_TRUE(status.samples.empty());
  EXPECT_EQ(status.num_channels, 0);
}

void ReadMonoBytes(const std::string &bytes, WaveReadStatus *status) {
  // Same wrapping as SherpaOnnxReadWaveFromBinaryData.
  std::istringstream is(std::string(bytes.data(), bytes.size()));
  status->samples = ReadWave(is, &status->sample_rate, &status->is_ok);
  status->num_channels = status->samples.empty() ? 0 : 1;
}

void ReadMonoFile(const std::string &filename, WaveReadStatus *status) {
  status->samples = ReadWave(filename, &status->sample_rate, &status->is_ok);
  status->num_channels = status->samples.empty() ? 0 : 1;
}

void ReadMultiBytes(const std::string &bytes, WaveReadStatus *status) {
  std::istringstream is(std::string(bytes.data(), bytes.size()));
  auto channels =
      ReadWaveMultiChannel(is, &status->sample_rate, &status->is_ok);
  status->num_channels = static_cast<int32_t>(channels.size());
  if (!channels.empty()) {
    status->samples = channels[0];
  }
}

void ReadMultiFile(const std::string &filename, WaveReadStatus *status) {
  auto channels =
      ReadWaveMultiChannel(filename, &status->sample_rate, &status->is_ok);
  status->num_channels = static_cast<int32_t>(channels.size());
  if (!channels.empty()) {
    status->samples = channels[0];
  }
}

void WriteBytes(const std::string &filename, const std::string &bytes) {
  auto out = OpenOutputFile(filename, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

TEST(WaveReader, RejectsNegativeListChunkFromStream) {
  const std::string bytes = ListChunkBackToHeader();
  ExpectRejected(
      [bytes](WaveReadStatus *status) { ReadMonoBytes(bytes, status); });
}

TEST(WaveReader, RejectsNegativeListChunkFromFile) {
  const std::string bytes = ListChunkBackToHeader();
  TempFile temp_file(".wav");
  WriteBytes(temp_file.path(), bytes);
  const std::string path = temp_file.path();
  ExpectRejected(
      [path](WaveReadStatus *status) { ReadMonoFile(path, status); });
}

TEST(WaveReader, RejectsNegativeListChunkMultiChannelStream) {
  const std::string bytes = ListChunkBackToHeader();
  ExpectRejected(
      [bytes](WaveReadStatus *status) { ReadMultiBytes(bytes, status); });
}

TEST(WaveReader, RejectsNegativeListChunkMultiChannelFile) {
  const std::string bytes = ListChunkBackToHeader();
  TempFile temp_file(".wav");
  WriteBytes(temp_file.path(), bytes);
  const std::string path = temp_file.path();
  ExpectRejected(
      [path](WaveReadStatus *status) { ReadMultiFile(path, status); });
}

TEST(WaveReader, RejectsCyclicChunkSeek) {
  const std::string bytes = CyclicChunkHeaders();
  ExpectRejected(
      [bytes](WaveReadStatus *status) { ReadMonoBytes(bytes, status); });
}

TEST(WaveReader, RejectsNegativeDataChunkSize) {
  struct Case {
    const char *name;
    int16_t audio_format;
    int16_t bits_per_sample;
    uint32_t data_size;
    bool skip_list_first;
  };
  const Case cases[] = {
      {"pcm16-minus-2", 1, 16, 0xfffffffeu, false},
      {"pcm16-minus-1", 1, 16, 0xffffffffu, false},
      {"pcm8-minus-1", 1, 8, 0xffffffffu, false},
      {"pcm32-minus-4", 1, 32, 0xfffffffcu, false},
      {"float32-minus-4", 3, 32, 0xfffffffcu, false},
      {"pcm16-after-list", 1, 16, 0xfffffffeu, true},
  };

  for (const auto &item : cases) {
    SCOPED_TRACE(item.name);
    std::string bytes = PcmWavePrefix(item.audio_format, item.bits_per_sample);
    if (item.skip_list_first) {
      bytes.append("LIST");
      AppendLe32(&bytes, 4);
      bytes.append("abcd");
    }
    bytes.append("data");
    AppendLe32(&bytes, item.data_size);
    ExpectRejected(
        [bytes](WaveReadStatus *status) { ReadMonoBytes(bytes, status); });
  }
}

TEST(WaveReader, ReadsWaveWithSkippedChunk) {
  std::string bytes = PcmWavePrefix(1, 16);
  bytes.append("LIST");
  AppendLe32(&bytes, 4);
  bytes.append("abcd");
  bytes.append("data");
  AppendLe32(&bytes, 2);
  AppendLe16(&bytes, 16384);

  WaveReadStatus status;
  ASSERT_TRUE(RunBounded(
      [bytes](WaveReadStatus *out) { ReadMonoBytes(bytes, out); }, &status));
  EXPECT_FALSE(status.threw);
  EXPECT_TRUE(status.is_ok);
  EXPECT_EQ(status.sample_rate, 16000);
  ASSERT_EQ(status.samples.size(), 1u);
  EXPECT_NEAR(status.samples[0], 0.5f, 1e-6);
}

TEST(WaveReader, ReadsWaveWithZeroSizeChunk) {
  std::string bytes = PcmWavePrefix(1, 16);
  bytes.append("LIST");
  AppendLe32(&bytes, 0);
  bytes.append("data");
  AppendLe32(&bytes, 2);
  AppendLe16(&bytes, 16384);

  WaveReadStatus status;
  ASSERT_TRUE(RunBounded(
      [bytes](WaveReadStatus *out) { ReadMonoBytes(bytes, out); }, &status));
  EXPECT_FALSE(status.threw);
  EXPECT_TRUE(status.is_ok);
  EXPECT_EQ(status.sample_rate, 16000);
  ASSERT_EQ(status.samples.size(), 1u);
  EXPECT_NEAR(status.samples[0], 0.5f, 1e-6);
}

}  // namespace sherpa_onnx
