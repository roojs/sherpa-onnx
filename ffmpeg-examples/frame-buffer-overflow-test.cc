// ffmpeg-examples/frame-buffer-overflow-test.cc
//
// Copyright (c)  2026  sherpa-onnx contributors

// Drives the real sherpa-onnx-ffmpeg decode loop. The recognizer calls are
// stubbed because a zero-length flush returns before the copy that overflows;
// the copy itself is the code under test.
//
// These tests fail while sherpa_decode_frame writes past its 3200-float
// staging array. They pass once every decoded sample is handed to the
// recognizer without that overflow. At most one full staging buffer (3200
// samples) may remain unflushed, which is the handler's existing behavior
// for a trailing partial chunk.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define main ffmpeg_example_main
#include "sherpa-onnx-ffmpeg.c"  // NOLINT
#undef main

std::vector<std::vector<float> > g_accepted;

namespace {

constexpr int kStagingCapacity = 3200;
constexpr int kTailSamples = 4800;

const char *kFilter =
    "aresample=16000,aformat=sample_fmts=s16:channel_layouts=mono";

void Fail(const std::string &msg) {
  std::cerr << "FAIL: " << msg << "\n";
  std::exit(1);
}

std::string AvErr(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

void WriteWav(const std::string &path, int sample_rate, int block_align,
              const std::vector<int16_t> &samples) {
  FILE *fp = std::fopen(path.c_str(), "wb");
  if (!fp) Fail("fopen " + path);
  const int data_bytes = static_cast<int>(samples.size() * sizeof(int16_t));
  const int riff_size = 36 + data_bytes;
  const auto w16 = [fp](int v) {
    uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    std::fwrite(b, 1, 2, fp);
  };
  const auto w32 = [fp](int v) {
    uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                    static_cast<uint8_t>(v >> 16),
                    static_cast<uint8_t>(v >> 24)};
    std::fwrite(b, 1, 4, fp);
  };
  std::fwrite("RIFF", 1, 4, fp);
  w32(riff_size);
  std::fwrite("WAVE", 1, 4, fp);
  std::fwrite("fmt ", 1, 4, fp);
  w32(16);
  w16(1);
  w16(1);
  w32(sample_rate);
  w32(sample_rate * block_align);
  w16(block_align);
  w16(16);
  std::fwrite("data", 1, 4, fp);
  w32(data_bytes);
  if (std::fwrite(samples.data(), sizeof(int16_t), samples.size(), fp) !=
      samples.size()) {
    Fail("short wav write");
  }
  std::fclose(fp);
}

std::vector<int16_t> Ramp(int n) {
  std::vector<int16_t> s(n);
  for (int i = 0; i < n; ++i) {
    s[i] = static_cast<int16_t>((i * 17) % 30000 + 1);
  }
  return s;
}

void WriteFlac(const std::string &path, const std::vector<int16_t> &pcm) {
  const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
  if (!codec) Fail("flac encoder missing");
  AVFormatContext *oc = nullptr;
  int ret = avformat_alloc_output_context2(&oc, nullptr, "flac", path.c_str());
  if (ret < 0) Fail(AvErr(ret));
  AVStream *st = avformat_new_stream(oc, nullptr);
  if (!st) Fail("no flac stream");
  AVCodecContext *ctx = avcodec_alloc_context3(codec);
  if (!ctx) Fail("no flac context");
  ctx->sample_fmt = AV_SAMPLE_FMT_S16;
  ctx->sample_rate = 16000;
  ctx->time_base = AVRational{1, 16000};
  ctx->frame_size = 4096;
  av_channel_layout_default(&ctx->ch_layout, 1);
  if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
    ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  ret = avcodec_open2(ctx, codec, nullptr);
  if (ret < 0) Fail("flac open: " + AvErr(ret));
  ret = avcodec_parameters_from_context(st->codecpar, ctx);
  if (ret < 0) Fail(AvErr(ret));
  st->time_base = AVRational{1, ctx->sample_rate};
  ret = avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE);
  if (ret < 0) Fail(AvErr(ret));
  ret = avformat_write_header(oc, nullptr);
  if (ret < 0) Fail(AvErr(ret));

  AVFrame *frame = av_frame_alloc();
  AVPacket *pkt = av_packet_alloc();
  int offset = 0;
  int pts = 0;
  while (offset < static_cast<int>(pcm.size())) {
    int left = static_cast<int>(pcm.size()) - offset;
    int take = left < ctx->frame_size ? left : ctx->frame_size;
    frame->nb_samples = take;
    frame->format = ctx->sample_fmt;
    frame->sample_rate = ctx->sample_rate;
    av_channel_layout_copy(&frame->ch_layout, &ctx->ch_layout);
    if (av_frame_get_buffer(frame, 0) < 0) Fail("flac frame buffer");
    std::memcpy(frame->data[0], pcm.data() + offset, take * sizeof(int16_t));
    frame->pts = pts;
    pts += take;
    offset += take;
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) Fail("flac send: " + AvErr(ret));
    while ((ret = avcodec_receive_packet(ctx, pkt)) >= 0) {
      av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
      pkt->stream_index = st->index;
      if (av_interleaved_write_frame(oc, pkt) < 0) Fail("flac write");
      av_packet_unref(pkt);
    }
    av_frame_unref(frame);
  }
  avcodec_send_frame(ctx, nullptr);
  while ((ret = avcodec_receive_packet(ctx, pkt)) >= 0) {
    av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
    pkt->stream_index = st->index;
    if (av_interleaved_write_frame(oc, pkt) < 0) Fail("flac flush write");
    av_packet_unref(pkt);
  }
  av_write_trailer(oc);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avio_closep(&oc->pb);
  avformat_free_context(oc);
}

float SampleToFloat(int16_t s) { return s / 32768.; }

struct Decoded {
  std::vector<std::vector<int16_t> > frames;
  std::vector<float> samples;
};

Decoded DecodeLikeExample(const std::string &path) {
  AVFormatContext *fmt = nullptr;
  int ret = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
  if (ret < 0) Fail("open " + path + ": " + AvErr(ret));
  if (avformat_find_stream_info(fmt, nullptr) < 0) Fail("stream info");
  const AVCodec *dec = nullptr;
  int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
  if (idx < 0) Fail("no audio stream");
  AVCodecContext *ctx = avcodec_alloc_context3(dec);
  avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar);
  if (avcodec_open2(ctx, dec, nullptr) < 0) Fail("decoder open");
  if (ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
    av_channel_layout_default(&ctx->ch_layout, ctx->ch_layout.nb_channels);
  }

  char args[512];
  AVRational tb = fmt->streams[idx]->time_base;
  int n = std::snprintf(
      args, sizeof(args),
      "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=", tb.num,
      tb.den, ctx->sample_rate, av_get_sample_fmt_name(ctx->sample_fmt));
  av_channel_layout_describe(&ctx->ch_layout, args + n, sizeof(args) - n);

  AVFilterGraph *graph = avfilter_graph_alloc();
  const AVFilter *abuffersrc = avfilter_get_by_name("abuffer");
  const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
  AVFilterContext *src = nullptr;
  AVFilterContext *sink = nullptr;
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *inputs = avfilter_inout_alloc();
  if (avfilter_graph_create_filter(&src, abuffersrc, "in", args, nullptr,
                                   graph) < 0) {
    Fail("abuffer");
  }
  if (avfilter_graph_create_filter(&sink, abuffersink, "out", nullptr, nullptr,
                                   graph) < 0) {
    Fail("abuffersink");
  }
  const enum AVSampleFormat out_fmts[] = {AV_SAMPLE_FMT_S16,
                                          AV_SAMPLE_FMT_NONE};
  const int out_rates[] = {16000, -1};
  av_opt_set_int_list(sink, "sample_fmts", out_fmts, -1,
                      AV_OPT_SEARCH_CHILDREN);
  av_opt_set(sink, "ch_layouts", "mono", AV_OPT_SEARCH_CHILDREN);
  av_opt_set_int_list(sink, "sample_rates", out_rates, -1,
                      AV_OPT_SEARCH_CHILDREN);
  outputs->name = av_strdup("in");
  outputs->filter_ctx = src;
  outputs->pad_idx = 0;
  inputs->name = av_strdup("out");
  inputs->filter_ctx = sink;
  inputs->pad_idx = 0;
  if (avfilter_graph_parse_ptr(graph, kFilter, &inputs, &outputs, nullptr) <
      0) {
    Fail("filter parse");
  }
  if (avfilter_graph_config(graph, nullptr) < 0) Fail("filter config");

  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  AVFrame *filt = av_frame_alloc();
  Decoded out;
  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index != idx) {
      av_packet_unref(pkt);
      continue;
    }
    if (avcodec_send_packet(ctx, pkt) < 0) Fail("send packet");
    while ((ret = avcodec_receive_frame(ctx, frame)) >= 0) {
      if (av_buffersrc_add_frame_flags(src, frame, AV_BUFFERSRC_FLAG_KEEP_REF) <
          0) {
        Fail("buffersrc");
      }
      while ((ret = av_buffersink_get_frame(sink, filt)) >= 0) {
        if (filt->format != AV_SAMPLE_FMT_S16) Fail("expected s16");
        const int16_t *p = reinterpret_cast<const int16_t *>(filt->data[0]);
        out.frames.emplace_back(p, p + filt->nb_samples);
        for (int i = 0; i < filt->nb_samples; ++i) {
          out.samples.push_back(SampleToFloat(p[i]));
        }
        av_frame_unref(filt);
      }
      av_frame_unref(frame);
    }
    av_packet_unref(pkt);
  }
  avfilter_graph_free(&graph);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
  av_packet_free(&pkt);
  av_frame_free(&frame);
  av_frame_free(&filt);
  if (out.samples.empty()) Fail("decoder produced no samples");
  return out;
}

std::vector<float> AcceptedAudio() {
  if (g_accepted.empty()) Fail("recognizer received no audio");
  const std::vector<float> &tail = g_accepted.back();
  bool is_tail = static_cast<int>(tail.size()) == kTailSamples;
  if (is_tail) {
    for (float s : tail) {
      if (s != 0.f) is_tail = false;
    }
  }
  if (!is_tail) {
    Fail("example did not finish with the 4800-sample tail padding");
  }
  std::vector<float> all;
  for (size_t i = 0; i + 1 < g_accepted.size(); ++i) {
    all.insert(all.end(), g_accepted[i].begin(), g_accepted[i].end());
  }
  return all;
}

void ExpectDelivered(const Decoded &decoded, bool require_oversize) {
  int max_frame = 0;
  for (const auto &frame : decoded.frames) {
    const int n = static_cast<int>(frame.size());
    if (n > max_frame) max_frame = n;
    std::cerr << "filtered frame samples=" << n << "\n";
  }
  if (require_oversize && max_frame <= kStagingCapacity) {
    Fail("input never produced a frame larger than the staging buffer");
  }
  if (!require_oversize && max_frame > kStagingCapacity) {
    Fail("safe input produced an oversized frame");
  }
  const std::vector<float> got = AcceptedAudio();
  if (got.size() > decoded.samples.size()) {
    Fail("recognizer received more samples than the decoder produced");
  }
  for (size_t i = 0; i < got.size(); ++i) {
    if (got[i] != decoded.samples[i]) {
      Fail("accepted sample " + std::to_string(i) +
           " does not match the decoded frame");
    }
  }
  const size_t missing = decoded.samples.size() - got.size();
  if (missing > static_cast<size_t>(kStagingCapacity)) {
    Fail("dropped " + std::to_string(missing) +
         " samples; at most the unflushed staging buffer may be held");
  }
  if (require_oversize && got.size() <= static_cast<size_t>(kStagingCapacity)) {
    Fail("samples past the staging buffer were not delivered");
  }
  std::cerr << "accepted=" << got.size()
            << " decoded=" << decoded.samples.size()
            << " held=" << missing << "\n";
}

void RunExample(const std::string &path) {
  char arg0[] = "sherpa-onnx-ffmpeg";
  char arg1[] = "tokens.txt";
  char arg2[] = "encoder.onnx";
  char arg3[] = "decoder.onnx";
  char arg4[] = "joiner.onnx";
  std::vector<char> media(path.begin(), path.end());
  media.push_back('\0');
  char *argv[] = {arg0, arg1, arg2, arg3, arg4, media.data()};
  g_accepted.clear();
  int rc = ffmpeg_example_main(6, argv);
  if (rc != 0) Fail("example main returned " + std::to_string(rc));
}

std::string TempDir() {
  char tmpl[] = "/tmp/sherpa-frame-XXXXXX";
  if (!mkdtemp(tmpl)) Fail("mkdtemp");
  return tmpl;
}

void RunFileCase(const std::string &path, bool oversize) {
  Decoded decoded = DecodeLikeExample(path);
  RunExample(path);
  ExpectDelivered(decoded, oversize);
}

AVFrame *FrameFromS16(const std::vector<int16_t> &pcm) {
  AVFrame *frame = av_frame_alloc();
  frame->nb_samples = static_cast<int>(pcm.size());
  frame->format = AV_SAMPLE_FMT_S16;
  frame->sample_rate = 16000;
  av_channel_layout_default(&frame->ch_layout, 1);
  if (av_frame_get_buffer(frame, 0) < 0) Fail("frame buffer");
  std::memcpy(frame->data[0], pcm.data(), pcm.size() * sizeof(int16_t));
  return frame;
}

void RunBufferedThenOversized(const std::string &path) {
  Decoded decoded = DecodeLikeExample(path);
  int max_frame = 0;
  for (const auto &frame : decoded.frames) {
    if (static_cast<int>(frame.size()) > max_frame) {
      max_frame = static_cast<int>(frame.size());
    }
  }
  if (max_frame <= kStagingCapacity) {
    Fail("need an oversized decoded frame");
  }
  const std::vector<int16_t> prime = Ramp(2000);
  std::vector<float> reference;
  reference.reserve(prime.size() + decoded.samples.size());
  for (int16_t s : prime) reference.push_back(SampleToFloat(s));
  reference.insert(reference.end(), decoded.samples.begin(),
                   decoded.samples.end());

  const SherpaOnnxOnlineRecognizer *recognizer =
      reinterpret_cast<const SherpaOnnxOnlineRecognizer *>(1);
  const SherpaOnnxOnlineStream *stream =
      reinterpret_cast<const SherpaOnnxOnlineStream *>(1);
  const SherpaOnnxDisplay *display =
      reinterpret_cast<const SherpaOnnxDisplay *>(1);
  int32_t segment_id = 0;
  g_accepted.clear();

  AVFrame *small = FrameFromS16(prime);
  sherpa_decode_frame(small, recognizer, stream, display, &segment_id);
  av_frame_free(&small);

  for (const auto &pcm : decoded.frames) {
    AVFrame *frame = FrameFromS16(pcm);
    sherpa_decode_frame(frame, recognizer, stream, display, &segment_id);
    av_frame_free(&frame);
  }

  std::vector<float> got;
  for (const auto &chunk : g_accepted) {
    got.insert(got.end(), chunk.begin(), chunk.end());
  }
  if (got.size() > reference.size()) Fail("buffered case accepted extra");
  for (size_t i = 0; i < got.size(); ++i) {
    if (got[i] != reference[i]) Fail("buffered case sample mismatch");
  }
  const size_t missing = reference.size() - got.size();
  if (missing > static_cast<size_t>(kStagingCapacity)) {
    Fail("buffered case dropped " + std::to_string(missing) + " samples");
  }
  if (got.size() <= static_cast<size_t>(kStagingCapacity)) {
    Fail("buffered case did not deliver samples past the staging buffer");
  }
  std::cerr << "buffered accepted=" << got.size()
            << " decoded=" << reference.size() << " held=" << missing << "\n";
}

}  // namespace

extern "C" {

const SherpaOnnxOnlineRecognizer *SherpaOnnxCreateOnlineRecognizer(
    const SherpaOnnxOnlineRecognizerConfig *) {
  return reinterpret_cast<const SherpaOnnxOnlineRecognizer *>(1);
}

void SherpaOnnxDestroyOnlineRecognizer(const SherpaOnnxOnlineRecognizer *) {}

const SherpaOnnxOnlineStream *SherpaOnnxCreateOnlineStream(
    const SherpaOnnxOnlineRecognizer *) {
  return reinterpret_cast<const SherpaOnnxOnlineStream *>(1);
}

void SherpaOnnxDestroyOnlineStream(const SherpaOnnxOnlineStream *) {}

void SherpaOnnxOnlineStreamAcceptWaveform(const SherpaOnnxOnlineStream *,
                                          int32_t, const float *samples,
                                          int32_t n) {
  if (n <= 0) return;
  g_accepted.emplace_back(samples, samples + n);
}

int32_t SherpaOnnxIsOnlineStreamReady(const SherpaOnnxOnlineRecognizer *,
                                     const SherpaOnnxOnlineStream *) {
  return 0;
}

void SherpaOnnxDecodeOnlineStream(const SherpaOnnxOnlineRecognizer *,
                                  const SherpaOnnxOnlineStream *) {}

const SherpaOnnxOnlineRecognizerResult *SherpaOnnxGetOnlineStreamResult(
    const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *) {
  static SherpaOnnxOnlineRecognizerResult r;
  std::memset(&r, 0, sizeof(r));
  r.text = "";
  r.json = "{}";
  return &r;
}

void SherpaOnnxDestroyOnlineRecognizerResult(
    const SherpaOnnxOnlineRecognizerResult *) {}

void SherpaOnnxOnlineStreamReset(const SherpaOnnxOnlineRecognizer *,
                                 const SherpaOnnxOnlineStream *) {}

void SherpaOnnxOnlineStreamInputFinished(const SherpaOnnxOnlineStream *) {}

int32_t SherpaOnnxOnlineStreamIsEndpoint(const SherpaOnnxOnlineRecognizer *,
                                        const SherpaOnnxOnlineStream *) {
  return 0;
}

const SherpaOnnxDisplay *SherpaOnnxCreateDisplay(int32_t) {
  return reinterpret_cast<const SherpaOnnxDisplay *>(1);
}

void SherpaOnnxDestroyDisplay(const SherpaOnnxDisplay *) {}

void SherpaOnnxPrint(const SherpaOnnxDisplay *, int32_t, const char *) {}

}  // extern "C"

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: frame-buffer-overflow-test <case>\n";
    return 2;
  }
  const std::string dir = TempDir();
  const std::string which = argv[1];
  if (which == "safe-16k-wav") {
    const std::string path = dir + "/safe.wav";
    WriteWav(path, 16000, 2, Ramp(8000));
    RunFileCase(path, false);
  } else if (which == "ordinary-8k-wav") {
    const std::string path = dir + "/8k.wav";
    WriteWav(path, 8000, 2, Ramp(8000));
    RunFileCase(path, true);
  } else if (which == "crafted-block-align-wav") {
    const std::string path = dir + "/big.wav";
    WriteWav(path, 16000, 65534, Ramp(65534));
    RunFileCase(path, true);
  } else if (which == "flac-4096") {
    const std::string path = dir + "/block.flac";
    WriteFlac(path, Ramp(8192));
    RunFileCase(path, true);
  } else if (which == "buffered-then-oversized-frame") {
    const std::string path = dir + "/big.wav";
    WriteWav(path, 16000, 65534, Ramp(65534));
    RunBufferedThenOversized(path);
  } else {
    Fail("unknown case " + which);
  }
  std::cerr << "ok " << which << "\n";
  return 0;
}
