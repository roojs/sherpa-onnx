// sherpa-onnx/csrc/wave-reader-test.cc
//
// Copyright (c)  2025  Posit Software, PBC

#include "sherpa-onnx/csrc/wave-reader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
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

// PCM whose sample count is not a multiple of num_channels must be rejected
// before any channel vector is indexed. These tests fail while ReadWaveImpl
// sizes each channel with integer division and then steps by num_channels.
namespace {

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

// Canonical 44-byte PCM header plus payload. byte_rate and block_align match
// the fmt fields so the reader reaches the de-interleave loop.
std::string MakePcmWav(int32_t audio_format, int32_t bits_per_sample,
                       int32_t num_channels, int32_t sample_rate,
                       const std::string &payload) {
  const int32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
  const int32_t block_align = num_channels * bits_per_sample / 8;
  std::string out;
  out.append("RIFF");
  AppendLe32(&out, static_cast<uint32_t>(36 + payload.size()));
  out.append("WAVE");
  out.append("fmt ");
  AppendLe32(&out, 16);
  AppendLe16(&out, static_cast<uint16_t>(audio_format));
  AppendLe16(&out, static_cast<uint16_t>(num_channels));
  AppendLe32(&out, static_cast<uint32_t>(sample_rate));
  AppendLe32(&out, static_cast<uint32_t>(byte_rate));
  AppendLe16(&out, static_cast<uint16_t>(block_align));
  AppendLe16(&out, static_cast<uint16_t>(bits_per_sample));
  out.append("data");
  AppendLe32(&out, static_cast<uint32_t>(payload.size()));
  out.append(payload);
  return out;
}

std::string PayloadBytes(int32_t n, char fill) {
  return std::string(static_cast<size_t>(n), fill);
}

void ExpectStreamRejected(const std::string &wav) {
  int32_t sample_rate = -1;
  bool is_ok = false;
  // Same buffer-to-stream step as SherpaOnnxReadWaveFromBinaryData.
  std::istringstream is(std::string(wav.data(), wav.size()));
  std::vector<float> samples = ReadWave(is, &sample_rate, &is_ok);
  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
}

void ExpectFileRejected(const std::string &wav) {
  TempFile temp_file(".wav");
  {
    auto out = OpenOutputFile(temp_file.path(), std::ios::binary);
    out.write(wav.data(), static_cast<std::streamsize>(wav.size()));
  }

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<float> samples =
      ReadWave(temp_file.path(), &sample_rate, &is_ok);
  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
}

void ExpectMultiStreamRejected(const std::string &wav) {
  int32_t sample_rate = -1;
  bool is_ok = false;
  std::istringstream is(std::string(wav.data(), wav.size()));
  std::vector<std::vector<float>> samples =
      ReadWaveMultiChannel(is, &sample_rate, &is_ok);
  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
}

void ExpectMultiFileRejected(const std::string &wav) {
  TempFile temp_file(".wav");
  {
    auto out = OpenOutputFile(temp_file.path(), std::ios::binary);
    out.write(wav.data(), static_cast<std::streamsize>(wav.size()));
  }

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<std::vector<float>> samples =
      ReadWaveMultiChannel(temp_file.path(), &sample_rate, &is_ok);
  EXPECT_FALSE(is_ok);
  EXPECT_TRUE(samples.empty());
}

void ExpectAllEntriesRejected(const std::string &wav) {
  ExpectStreamRejected(wav);
  ExpectFileRejected(wav);
  ExpectMultiStreamRejected(wav);
  ExpectMultiFileRejected(wav);
}

std::string PartialWav(int32_t audio_format, int32_t bits_per_sample,
                       int32_t num_channels, int32_t data_bytes) {
  return MakePcmWav(audio_format, bits_per_sample, num_channels, 8000,
                    PayloadBytes(data_bytes, '\x11'));
}

}  // namespace

// 16-bit stereo, data size 2: one int16 sample. Channel vectors are resized
// to 0, then the loop writes ans[0][0], ans[1][0] and reads samples[1].
TEST(WaveReader, RejectInt16StereoOneSample) {
  ExpectAllEntriesRejected(PartialWav(1, 16, 2, 2));
}

// Each public reader is also invoked on its own so a crash in one entry does
// not hide another entry when the tests are filtered.
TEST(WaveReader, RejectInt16StereoOneSampleFromFile) {
  ExpectFileRejected(PartialWav(1, 16, 2, 2));
}

TEST(WaveReader, RejectInt16StereoOneSampleMultiChannelStream) {
  ExpectMultiStreamRejected(PartialWav(1, 16, 2, 2));
}

TEST(WaveReader, RejectInt16StereoOneSampleMultiChannelFile) {
  ExpectMultiFileRejected(PartialWav(1, 16, 2, 2));
}

// 16-bit stereo, three samples. The first frame fits; frame 1 indexes past
// both the sample buffer and each channel vector of length 1.
TEST(WaveReader, RejectInt16StereoTrailingSample) {
  ExpectAllEntriesRejected(PartialWav(1, 16, 2, 6));
}

TEST(WaveReader, RejectInt8StereoOneSample) {
  ExpectAllEntriesRejected(PartialWav(1, 8, 2, 1));
}

TEST(WaveReader, RejectInt8StereoTrailingSample) {
  ExpectAllEntriesRejected(PartialWav(1, 8, 2, 3));
}

TEST(WaveReader, RejectInt32StereoOneSample) {
  ExpectAllEntriesRejected(PartialWav(1, 32, 2, 4));
}

TEST(WaveReader, RejectInt32StereoTrailingSample) {
  ExpectAllEntriesRejected(PartialWav(1, 32, 2, 12));
}

TEST(WaveReader, RejectFloat32StereoOneSample) {
  ExpectAllEntriesRejected(PartialWav(3, 32, 2, 4));
}

TEST(WaveReader, RejectFloat32StereoTrailingSample) {
  ExpectAllEntriesRejected(PartialWav(3, 32, 2, 12));
}

// Remainder against a channel count other than 2.
TEST(WaveReader, RejectInt16ThreeChannelsExtraSample) {
  ExpectAllEntriesRejected(PartialWav(1, 16, 3, 8));  // 4 samples, 3 channels
}

TEST(WaveReader, RejectInt8ThreeChannelsExtraSample) {
  ExpectAllEntriesRejected(PartialWav(1, 8, 3, 4));
}

TEST(WaveReader, RejectInt32ThreeChannelsExtraSample) {
  ExpectAllEntriesRejected(PartialWav(1, 32, 3, 16));  // 4 samples
}

TEST(WaveReader, RejectFloat32ThreeChannelsExtraSample) {
  ExpectAllEntriesRejected(PartialWav(3, 32, 3, 16));
}

namespace {

std::vector<std::vector<float>> ReadMultiFromBytes(const std::string &wav,
                                                   int32_t *sample_rate,
                                                   bool *is_ok) {
  std::istringstream is(std::string(wav.data(), wav.size()));
  return ReadWaveMultiChannel(is, sample_rate, is_ok);
}

void AppendInt16Sample(std::string *payload, int16_t sample) {
  AppendLe16(payload, static_cast<uint16_t>(sample));
}

}  // namespace

// Whole frames still de-interleave. Locks the accept path so a fix cannot
// reject every multi-channel file.
TEST(WaveReader, AcceptAlignedInt16Stereo) {
  std::string payload;
  AppendInt16Sample(&payload, 0);
  AppendInt16Sample(&payload, 16384);
  AppendInt16Sample(&payload, -32768);
  AppendInt16Sample(&payload, 32767);
  const std::string wav = MakePcmWav(1, 16, 2, 16000, payload);

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::istringstream mono_is(std::string(wav.data(), wav.size()));
  std::vector<float> mono = ReadWave(mono_is, &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  EXPECT_EQ(sample_rate, 16000);
  ASSERT_EQ(mono.size(), 2u);
  EXPECT_NEAR(mono[0], 0.f, 1e-6f);
  EXPECT_NEAR(mono[1], -1.f, 1e-6f);

  sample_rate = -1;
  is_ok = false;
  std::vector<std::vector<float>> channels =
      ReadMultiFromBytes(wav, &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  ASSERT_EQ(channels.size(), 2u);
  ASSERT_EQ(channels[0].size(), 2u);
  ASSERT_EQ(channels[1].size(), 2u);
  EXPECT_NEAR(channels[0][0], 0.f, 1e-6f);
  EXPECT_NEAR(channels[0][1], -1.f, 1e-6f);
  EXPECT_NEAR(channels[1][0], 16384.f / 32768.f, 1e-6f);
  EXPECT_NEAR(channels[1][1], 32767.f / 32768.f, 1e-6f);

  TempFile temp_file(".wav");
  {
    auto out = OpenOutputFile(temp_file.path(), std::ios::binary);
    out.write(wav.data(), static_cast<std::streamsize>(wav.size()));
  }
  sample_rate = -1;
  is_ok = false;
  std::vector<std::vector<float>> from_file =
      ReadWaveMultiChannel(temp_file.path(), &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  ASSERT_EQ(from_file.size(), 2u);
  ASSERT_EQ(from_file[0].size(), 2u);
  EXPECT_NEAR(from_file[0][1], -1.f, 1e-6f);
  EXPECT_NEAR(from_file[1][0], 16384.f / 32768.f, 1e-6f);
}

TEST(WaveReader, AcceptAlignedInt8Stereo) {
  const std::string wav =
      MakePcmWav(1, 8, 2, 8000, std::string("\x80\xff\x00\x40", 4));

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<std::vector<float>> channels =
      ReadMultiFromBytes(wav, &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  EXPECT_EQ(sample_rate, 8000);
  ASSERT_EQ(channels.size(), 2u);
  ASSERT_EQ(channels[0].size(), 2u);
  EXPECT_NEAR(channels[0][0], 0.f, 1e-6f);
  EXPECT_NEAR(channels[0][1], -1.f, 1e-6f);
  EXPECT_NEAR(channels[1][0], 255.f / 128.f - 1.f, 1e-6f);
  EXPECT_NEAR(channels[1][1], 64.f / 128.f - 1.f, 1e-6f);
}

TEST(WaveReader, AcceptAlignedFloat32Stereo) {
  std::string payload;
  const float values[] = {0.25f, -0.5f, 1.f, 0.f};
  for (float value : values) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendLe32(&payload, bits);
  }
  const std::string wav = MakePcmWav(3, 32, 2, 16000, payload);

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<std::vector<float>> channels =
      ReadMultiFromBytes(wav, &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  EXPECT_EQ(sample_rate, 16000);
  ASSERT_EQ(channels.size(), 2u);
  ASSERT_EQ(channels[0].size(), 2u);
  EXPECT_NEAR(channels[0][0], 0.25f, 1e-6f);
  EXPECT_NEAR(channels[0][1], 1.f, 1e-6f);
  EXPECT_NEAR(channels[1][0], -0.5f, 1e-6f);
  EXPECT_NEAR(channels[1][1], 0.f, 1e-6f);
}

TEST(WaveReader, AcceptAlignedInt32Stereo) {
  std::string payload;
  const int32_t values[] = {0, 1 << 30, 0, 0};
  for (int32_t value : values) {
    AppendLe32(&payload, static_cast<uint32_t>(value));
  }
  const std::string wav = MakePcmWav(1, 32, 2, 16000, payload);

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<std::vector<float>> channels =
      ReadMultiFromBytes(wav, &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  EXPECT_EQ(sample_rate, 16000);
  ASSERT_EQ(channels.size(), 2u);
  ASSERT_EQ(channels[0].size(), 2u);
  ASSERT_EQ(channels[1].size(), 2u);
  EXPECT_EQ(channels[0][0], 0.f);
  EXPECT_EQ(channels[1][1], 0.f);
  EXPECT_NE(channels[0][0], channels[1][0]);
}

TEST(WaveReader, AcceptMonoInt16FromFile) {
  std::string payload;
  AppendInt16Sample(&payload, 0);
  AppendInt16Sample(&payload, 16384);
  const std::string wav = MakePcmWav(1, 16, 1, 8000, payload);

  TempFile temp_file(".wav");
  {
    auto out = OpenOutputFile(temp_file.path(), std::ios::binary);
    out.write(wav.data(), static_cast<std::streamsize>(wav.size()));
  }

  int32_t sample_rate = -1;
  bool is_ok = false;
  std::vector<float> samples = ReadWave(temp_file.path(), &sample_rate, &is_ok);
  ASSERT_TRUE(is_ok);
  EXPECT_EQ(sample_rate, 8000);
  ASSERT_EQ(samples.size(), 2u);
  EXPECT_NEAR(samples[0], 0.f, 1e-6f);
  EXPECT_NEAR(samples[1], 0.5f, 1e-6f);
}

}  // namespace sherpa_onnx
