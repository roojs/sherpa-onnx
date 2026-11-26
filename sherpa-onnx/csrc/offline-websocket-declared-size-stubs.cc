// sherpa-onnx/csrc/offline-websocket-declared-size-stubs.cc
//
// Copyright (c)  2026  sherpa-onnx contributors
//
// Test-only stand-ins for the recognizer. offline-websocket-server-impl.cc
// constructs an OfflineRecognizer while starting the server, before any
// client bytes are read. The overflow under test happens in OnMessage, which
// is compiled from that file, and it does not call the recognizer unless the
// declared byte count already matches the bytes copied into the buffer.

#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Include the C++ headers that the sherpa headers pull in before renaming
// the private keyword, so those headers keep their real access specifiers.
#include <algorithm>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>

#define private public
#include "sherpa-onnx/csrc/offline-recognizer.h"
#include "sherpa-onnx/csrc/offline-stream.h"
#undef private

namespace sherpa_onnx {

class OfflineRecognizerImpl {};
class OfflineStream::Impl {};

std::ofstream OpenOutputFile(const std::string &filename,
                             std::ios_base::openmode mode) {
  // Same behavior as file-utils.cc on non-Windows: open the path as given.
  return std::ofstream(filename, mode);
}

void ParseOptions::Register(const std::string &name, int32_t *ptr,
                            const std::string &doc) {
  (void)name;
  (void)ptr;
  (void)doc;
}

void ParseOptions::Register(const std::string &name, float *ptr,
                            const std::string &doc) {
  (void)name;
  (void)ptr;
  (void)doc;
}

void ParseOptions::Register(const std::string &name, std::string *ptr,
                            const std::string &doc) {
  (void)name;
  (void)ptr;
  (void)doc;
}

void OfflineRecognizerConfig::Register(ParseOptions *po) { (void)po; }

bool OfflineRecognizerConfig::Validate() const { return true; }

OfflineRecognizer::OfflineRecognizer(const OfflineRecognizerConfig &config) {
  (void)config;
}

OfflineRecognizer::~OfflineRecognizer() = default;

std::unique_ptr<OfflineStream> OfflineRecognizer::CreateStream() const {
  return std::unique_ptr<OfflineStream>(
      new OfflineStream(FeatureExtractorConfig()));
}

void OfflineRecognizer::DecodeStreams(OfflineStream **ss, int32_t n) const {
  (void)ss;
  (void)n;
}

OfflineStream::OfflineStream(const FeatureExtractorConfig &config,
                             ContextGraphPtr context_graph) {
  (void)config;
  (void)context_graph;
}

OfflineStream::~OfflineStream() = default;

void OfflineStream::AcceptWaveform(int32_t sampling_rate, const float *waveform,
                                   int32_t n) const {
  (void)sampling_rate;
  (void)waveform;
  (void)n;
}

const OfflineRecognitionResult &OfflineStream::GetResult() const {
  static const OfflineRecognitionResult result;
  return result;
}

std::string OfflineRecognitionResult::AsJsonString() const {
  return "{\"text\":\"\"}";
}

}  // namespace sherpa_onnx
