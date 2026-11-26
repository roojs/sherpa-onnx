// sherpa-onnx/csrc/wave-reader.cc
//
// Copyright (c)  2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/wave-reader.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "sherpa-onnx/csrc/file-utils.h"
#include "sherpa-onnx/csrc/macros.h"

namespace sherpa_onnx {
namespace {

// Chunk sizes are unsigned and only ever move the stream forward, so every
// skipped chunk consumes at least its 8-byte header and the loop that skips
// chunks always terminates. A size that runs past the end of the stream
// fails the stream.
void SkipBytes(std::istream &is, uint32_t num_bytes) {
  // std::streamsize is 32-bit on some platforms
  constexpr uint32_t kMaxStep = 1u << 30;

  while (is && num_bytes > 0) {
    uint32_t step = std::min(num_bytes, kMaxStep);
    is.ignore(step);
    if (is.gcount() != static_cast<std::streamsize>(step)) {
      is.setstate(std::istream::failbit);
      return;
    }
    num_bytes -= step;
  }
}

// Read the samples of all complete frames in num_bytes bytes. The buffer
// grows only as data actually arrives, so a size field larger than the
// stream fails the read instead of allocating num_bytes up front.
template <typename T>
bool ReadSamples(std::istream &is, uint32_t num_bytes, int32_t num_channels,
                 std::vector<T> *samples) {
  constexpr size_t kBlockSize = (1 << 20) / sizeof(T);

  const size_t frame_size = sizeof(T) * num_channels;
  const size_t n = num_bytes / frame_size * num_channels;

  samples->clear();
  while (samples->size() < n) {
    size_t offset = samples->size();
    size_t count = std::min(kBlockSize, n - offset);
    samples->resize(offset + count);
    is.read(reinterpret_cast<char *>(samples->data() + offset),
            count * sizeof(T));
    if (!is) {
      samples->clear();
      return false;
    }
  }

  return true;
}

// see http://soundfile.sapp.org/doc/WaveFormat/
//
// Note: We assume little endian here
// TODO(fangjun): Support big endian
struct WaveHeader {
  // See
  // https://en.wikipedia.org/wiki/WAV#Metadata
  // and
  // https://www.robotplanet.dk/audio/wav_meta_data/riff_mci.pdf
  void SeekToDataChunk(std::istream &is) {
    //                              a t a d
    while (is && subchunk2_id != 0x61746164) {
      // const char *p = reinterpret_cast<const char *>(&subchunk2_id);
      // printf("Skip chunk (%x): %c%c%c%c of size: %u\n", subchunk2_id, p[0],
      //        p[1], p[2], p[3], subchunk2_size);
      SkipBytes(is, subchunk2_size);
      is.read(reinterpret_cast<char *>(&subchunk2_id), sizeof(int32_t));
      is.read(reinterpret_cast<char *>(&subchunk2_size), sizeof(int32_t));
    }
  }

  int32_t chunk_id;
  int32_t chunk_size;
  int32_t format;
  int32_t subchunk1_id;
  uint32_t subchunk1_size;
  int16_t audio_format;
  int16_t num_channels;
  int32_t sample_rate;
  int32_t byte_rate;
  int16_t block_align;
  int16_t bits_per_sample;
  int32_t subchunk2_id;    // a tag of this chunk
  uint32_t subchunk2_size;  // size of subchunk2
};
static_assert(sizeof(WaveHeader) == 44);

/*
sox int16-1-channel-zh.wav -b 8 int8-1-channel-zh.wav

sox int16-1-channel-zh.wav -c 2 int16-2-channel-zh.wav

we use audacity to generate int32-1-channel-zh.wav and float32-1-channel-zh.wav
because sox uses WAVE_FORMAT_EXTENSIBLE, which is not easy to support
in sherpa-onnx.
 */

// Read a wave file of mono-channel.
// Return its samples normalized to the range [-1, 1).
std::vector<std::vector<float>> ReadWaveImpl(std::istream &is,
                                             int32_t *sampling_rate,
                                             bool *is_ok) {
  WaveHeader header = {};
  is.read(reinterpret_cast<char *>(&header.chunk_id), sizeof(header.chunk_id));

  //                        F F I R
  if (header.chunk_id != 0x46464952) {
    SHERPA_ONNX_LOGE("Expected chunk_id RIFF. Given: 0x%08x\n",
                     header.chunk_id);
    *is_ok = false;
    return {};
  }

  is.read(reinterpret_cast<char *>(&header.chunk_size),
          sizeof(header.chunk_size));

  is.read(reinterpret_cast<char *>(&header.format), sizeof(header.format));

  //                      E V A W
  if (header.format != 0x45564157) {
    SHERPA_ONNX_LOGE("Expected format WAVE. Given: 0x%08x\n", header.format);
    *is_ok = false;
    return {};
  }

  is.read(reinterpret_cast<char *>(&header.subchunk1_id),
          sizeof(header.subchunk1_id));

  is.read(reinterpret_cast<char *>(&header.subchunk1_size),
          sizeof(header.subchunk1_size));

  if (header.subchunk1_id == 0x4b4e554a) {
    // skip junk padding
    SkipBytes(is, header.subchunk1_size);

    is.read(reinterpret_cast<char *>(&header.subchunk1_id),
            sizeof(header.subchunk1_id));

    is.read(reinterpret_cast<char *>(&header.subchunk1_size),
            sizeof(header.subchunk1_size));
  }

  if (header.subchunk1_id != 0x20746d66) {
    SHERPA_ONNX_LOGE("Expected subchunk1_id 0x20746d66. Given: 0x%08x\n",
                     header.subchunk1_id);
    *is_ok = false;
    return {};
  }

  // NAudio uses 18
  // See https://github.com/naudio/NAudio/issues/1132
  if (header.subchunk1_size != 16 &&
      header.subchunk1_size != 18) {  // 16 for PCM
    SHERPA_ONNX_LOGE("Expected subchunk1_size 16. Given: %u\n",
                     header.subchunk1_size);
    *is_ok = false;
    return {};
  }

  is.read(reinterpret_cast<char *>(&header.audio_format),
          sizeof(header.audio_format));

  if (header.audio_format != 1 && header.audio_format != 3) {
    // 1 for integer PCM
    // 3 for floating point PCM
    // see https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html
    // and https://github.com/microsoft/DirectXTK/wiki/Wave-Formats
    SHERPA_ONNX_LOGE("Expected audio_format 1. Given: %d\n",
                     header.audio_format);

    if (header.audio_format == static_cast<int16_t>(0xfffe)) {
      SHERPA_ONNX_LOGE("We don't support WAVE_FORMAT_EXTENSIBLE files.");
    }

    *is_ok = false;
    return {};
  }

  is.read(reinterpret_cast<char *>(&header.num_channels),
          sizeof(header.num_channels));

  if (header.num_channels <= 0) {
    SHERPA_ONNX_LOGE("Invalid num_channels: %d. Expected > 0",
                     header.num_channels);
    *is_ok = false;
    return {};
  }

  is.read(reinterpret_cast<char *>(&header.sample_rate),
          sizeof(header.sample_rate));

  is.read(reinterpret_cast<char *>(&header.byte_rate),
          sizeof(header.byte_rate));

  is.read(reinterpret_cast<char *>(&header.block_align),
          sizeof(header.block_align));

  is.read(reinterpret_cast<char *>(&header.bits_per_sample),
          sizeof(header.bits_per_sample));

  if (header.byte_rate !=
      (header.sample_rate * header.num_channels * header.bits_per_sample / 8)) {
    SHERPA_ONNX_LOGE("Incorrect byte rate: %d. Expected: %d", header.byte_rate,
                     (header.sample_rate * header.num_channels *
                      header.bits_per_sample / 8));
    *is_ok = false;
    return {};
  }

  if (header.block_align !=
      (header.num_channels * header.bits_per_sample / 8)) {
    SHERPA_ONNX_LOGE("Incorrect block align: %d. Expected: %d\n",
                     header.block_align,
                     (header.num_channels * header.bits_per_sample / 8));
    *is_ok = false;
    return {};
  }

  if (header.bits_per_sample != 8 && header.bits_per_sample != 16 &&
      header.bits_per_sample != 32) {
    SHERPA_ONNX_LOGE("Expected bits_per_sample 8, 16 or 32. Given: %d\n",
                     header.bits_per_sample);
    *is_ok = false;
    return {};
  }

  if (header.subchunk1_size == 18) {
    // this is for NAudio. It puts extra bytes after bits_per_sample
    // See
    // https://github.com/naudio/NAudio/blob/master/NAudio.Core/Wave/WaveFormats/WaveFormat.cs#L223

    int16_t extra_size = -1;
    is.read(reinterpret_cast<char *>(&extra_size), sizeof(int16_t));
    if (extra_size != 0) {
      SHERPA_ONNX_LOGE(
          "Extra size should be 0 for wave from NAudio. Current extra size "
          "%d\n",
          extra_size);
      *is_ok = false;
      return {};
    }
  }

  is.read(reinterpret_cast<char *>(&header.subchunk2_id),
          sizeof(header.subchunk2_id));

  is.read(reinterpret_cast<char *>(&header.subchunk2_size),
          sizeof(header.subchunk2_size));

  header.SeekToDataChunk(is);
  if (!is) {
    *is_ok = false;
    return {};
  }

  *sampling_rate = header.sample_rate;

  std::vector<std::vector<float>> ans(header.num_channels);

  if (header.bits_per_sample == 16 && header.audio_format == 1) {
    // header.subchunk2_size contains the number of bytes in the data.
    // As we assume each sample contains two bytes, so it is divided by 2 here
    std::vector<int16_t> samples;
    if (!ReadSamples(is, header.subchunk2_size, header.num_channels,
                     &samples)) {
      SHERPA_ONNX_LOGE("Failed to read %u bytes", header.subchunk2_size);
      *is_ok = false;
      return {};
    }

    for (auto &v : ans) {
      v.resize(samples.size() / header.num_channels);
    }

    // samples are interleaved
    for (int32_t i = 0, k = 0; i < static_cast<int32_t>(samples.size());
         i += header.num_channels, ++k) {
      for (int32_t c = 0; c != header.num_channels; ++c) {
        ans[c][k] = samples[i + c] / 32768.;
      }
    }
  } else if (header.bits_per_sample == 8 && header.audio_format == 1) {
    // number of samples == number of bytes for 8-bit encoded samples
    //
    // For 8-bit encoded samples, they are unsigned!
    std::vector<uint8_t> samples;
    if (!ReadSamples(is, header.subchunk2_size, header.num_channels,
                     &samples)) {
      SHERPA_ONNX_LOGE("Failed to read %u bytes", header.subchunk2_size);
      *is_ok = false;
      return {};
    }

    for (auto &v : ans) {
      v.resize(samples.size() / header.num_channels);
    }

    // samples are interleaved
    for (int32_t i = 0, k = 0; i < static_cast<int32_t>(samples.size());
         i += header.num_channels, ++k) {
      for (int32_t c = 0; c != header.num_channels; ++c) {
        // Note(fangjun): We want to normalize each sample into the range [-1,
        // 1] Since each original sample is in the range [0, 256], dividing them
        // by 128 converts them to the range [0, 2]; so after subtracting 1, we
        // get the range [-1, 1]
        //
        ans[c][k] = samples[i + c] / 128. - 1;
      }
    }
  } else if (header.bits_per_sample == 32 && header.audio_format == 1) {
    // 32 here is for int32
    //
    // header.subchunk2_size contains the number of bytes in the data.
    // As we assume each sample contains 4 bytes, so it is divided by 4 here
    std::vector<int32_t> samples;
    if (!ReadSamples(is, header.subchunk2_size, header.num_channels,
                     &samples)) {
      SHERPA_ONNX_LOGE("Failed to read %u bytes", header.subchunk2_size);
      *is_ok = false;
      return {};
    }

    for (auto &v : ans) {
      v.resize(samples.size() / header.num_channels);
    }

    // samples are interleaved
    for (int32_t i = 0, k = 0; i < static_cast<int32_t>(samples.size());
         i += header.num_channels, ++k) {
      for (int32_t c = 0; c != header.num_channels; ++c) {
        ans[c][k] = static_cast<float>(samples[i + c]) / (1 << 31);
      }
    }
  } else if (header.bits_per_sample == 32 && header.audio_format == 3) {
    // 32 here is for float32
    //
    // header.subchunk2_size contains the number of bytes in the data.
    // As we assume each sample contains 4 bytes, so it is divided by 4 here
    std::vector<float> samples;
    if (!ReadSamples(is, header.subchunk2_size, header.num_channels,
                     &samples)) {
      SHERPA_ONNX_LOGE("Failed to read %u bytes", header.subchunk2_size);
      *is_ok = false;
      return {};
    }

    for (auto &v : ans) {
      v.resize(samples.size() / header.num_channels);
    }

    // samples are interleaved
    for (int32_t i = 0, k = 0; i < static_cast<int32_t>(samples.size());
         i += header.num_channels, ++k) {
      for (int32_t c = 0; c != header.num_channels; ++c) {
        ans[c][k] = samples[i + c];
      }
    }
  } else {
    SHERPA_ONNX_LOGE(
        "Unsupported %d bits per sample and audio format: %d. Supported values "
        "are: 8, 16, 32.",
        header.bits_per_sample, header.audio_format);
    *is_ok = false;
    return {};
  }

  *is_ok = true;
  return ans;
}

}  // namespace

std::vector<float> ReadWave(const std::string &filename, int32_t *sampling_rate,
                            bool *is_ok) {
  *is_ok = false;
  if (filename.empty()) {
    SHERPA_ONNX_LOGE("Filename is empty");
    return {};
  }

  if (!FileExists(filename)) {
    SHERPA_ONNX_LOGE("Filename '%s' does not exist", filename.c_str());
    return {};
  }

  auto is = OpenInputFile(filename, std::ios::binary);
  return ReadWave(is, sampling_rate, is_ok);
}

std::vector<float> ReadWave(std::istream &is, int32_t *sampling_rate,
                            bool *is_ok) {
  auto samples = ReadWaveImpl(is, sampling_rate, is_ok);

  if (!*is_ok || samples.empty()) {
    return {};
  }

  if (samples.size() > 1) {
    SHERPA_ONNX_LOGE(
        "Warning: %d channels are found. We only use the first channel.\n",
        static_cast<int32_t>(samples.size()));
  }

  return samples[0];
}

std::vector<std::vector<float>> ReadWaveMultiChannel(std::istream &is,
                                                     int32_t *sampling_rate,
                                                     bool *is_ok) {
  auto samples = ReadWaveImpl(is, sampling_rate, is_ok);
  return samples;
}

std::vector<std::vector<float>> ReadWaveMultiChannel(
    const std::string &filename, int32_t *sampling_rate, bool *is_ok) {
  *is_ok = false;
  if (filename.empty()) {
    SHERPA_ONNX_LOGE("Filename is empty");
    return {};
  }

  if (!FileExists(filename)) {
    SHERPA_ONNX_LOGE("Filename '%s' does not exist", filename.c_str());
    return {};
  }

  auto is = OpenInputFile(filename, std::ios::binary);
  return ReadWaveMultiChannel(is, sampling_rate, is_ok);
}

}  // namespace sherpa_onnx
