#!/usr/bin/env bash
# Build and run the ffmpeg example frame-buffer tests.
# Exits non-zero while sherpa_decode_frame overflows its static array.

set -eu

cd "$(dirname "$0")"

if ! pkg-config --exists libavfilter libavformat libavcodec libswresample \
  libavutil; then
  echo "FFmpeg dev packages (libavfilter, libavformat, libavcodec," >&2
  echo "libswresample, libavutil) are required." >&2
  exit 1
fi

out="${TMPDIR:-/tmp}/sherpa-onnx-frame-buffer-overflow-test"
g++ -std=c++17 -fsanitize=address -fno-omit-frame-pointer -g -O1 -Wall -I.. \
  frame-buffer-overflow-test.cc -o "$out" \
  $(pkg-config --cflags --libs libavfilter libavformat libavcodec \
    libswresample libavutil)

export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:detect_leaks=0}"

cases=(
  safe-16k-wav
  ordinary-8k-wav
  crafted-block-align-wav
  flac-4096
  buffered-then-oversized-frame
)

fail=0
for c in "${cases[@]}"; do
  echo "=== $c ==="
  if "$out" "$c"; then
    echo "PASS $c"
  else
    echo "FAIL $c"
    fail=1
  fi
done

exit "$fail"
