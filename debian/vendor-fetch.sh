#!/usr/bin/env bash
# Download and unpack pinned cmake deps into debian/vendor/ for an offline
# dpkg-buildpackage. Network is allowed here only — not inside debian/rules.
#
# Includes nested deps pulled by kaldi-native-fbank / kaldi-decoder
# (kissfft, kaldifst, eigen).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENDOR_DIR="${SCRIPT_DIR}/vendor"
CACHE_DIR="${VENDOR_DIR}/.cache"
mkdir -p "$VENDOR_DIR" "$CACHE_DIR"

# name version url sha256 [extension=tar.gz]
fetch_one() {
  local name="$1"
  local version="$2"
  local url="$3"
  local sha256="$4"
  local ext="${5:-tar.gz}"
  local dest="${VENDOR_DIR}/${name}"
  local tarball="${CACHE_DIR}/${name}-${version}.${ext}"
  local stamp="${dest}/.sherpa-vendor-ok"

  if [ -f "$stamp" ] && [ -f "${dest}/CMakeLists.txt" ]; then
    echo "Using existing ${dest}" >&2
    return 0
  fi

  if [ ! -f "$tarball" ]; then
    echo "Downloading ${name} ${version}" >&2
    curl -fL --retry 3 --retry-delay 2 -o "${tarball}.partial" "$url"
    mv "${tarball}.partial" "$tarball"
  else
    echo "Using cached tarball ${tarball}" >&2
  fi

  echo "${sha256}  ${tarball}" | sha256sum -c -

  rm -rf "$dest"
  mkdir -p "$dest"
  case "$ext" in
    zip) unzip -q "$tarball" -d "${dest}.extract"
         # zip lays out <dirname>/... — move inner tree to dest
         local inner
         inner="$(find "${dest}.extract" -mindepth 1 -maxdepth 1 -type d | head -n1)"
         if [ -z "$inner" ]; then
           echo "zip had no top-level directory: ${tarball}" >&2
           exit 1
         fi
         # move contents including hidden
         shopt -s dotglob
         mv "$inner"/* "$dest"/
         shopt -u dotglob
         rm -rf "${dest}.extract"
         ;;
    *) tar -xzf "$tarball" -C "$dest" --strip-components=1 ;;
  esac
  touch "$stamp"
  echo "Ready ${dest}" >&2
}

# Top-level sherpa FetchContent pins (cmake/*.cmake)
fetch_one \
  kaldi-native-fbank \
  1.22.3 \
  "https://github.com/csukuangfj/kaldi-native-fbank/archive/refs/tags/v1.22.3.tar.gz" \
  "9176cc66fc7ce1edf85cf355b06e320c57db6297df74277f575183468893cf61"

fetch_one \
  kaldi-decoder \
  0.3.0 \
  "https://github.com/k2-fsa/kaldi-decoder/archive/refs/tags/v0.3.0.tar.gz" \
  "b9f34cfb4fd3b1344100eead79ef4d37aa15962274b9e3056de345021f76a1b0"

fetch_one \
  simple-sentencepiece \
  0.7 \
  "https://github.com/pkufool/simple-sentencepiece/archive/refs/tags/v0.7.tar.gz" \
  "1748a822060a35baa9f6609f84efc8eb54dc0e74b9ece3d82367b7119fdc75af"

# Nested: kaldi-native-fbank → kissfft
fetch_one \
  kissfft \
  febd4caeed32e33ad8b2e0bb5ea77542c40f18ec \
  "https://github.com/mborgerding/kissfft/archive/febd4caeed32e33ad8b2e0bb5ea77542c40f18ec.zip" \
  "497103e664168ebe39580b757adbe616f6cf85a16572af581ca7bc42d0ab13fd" \
  zip

# Nested: kaldi-decoder → kaldifst
fetch_one \
  kaldifst \
  1.8.0 \
  "https://github.com/k2-fsa/kaldifst/archive/refs/tags/v1.8.0.tar.gz" \
  "3f247b7e5a2409071202f5e2bc6200060f66728c0a3443c03923ad2723e040b3"

# Nested: kaldi-decoder → eigen (pin 5.0.1; distro eigen is older)
fetch_one \
  eigen \
  5.0.1 \
  "https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.tar.gz" \
  "e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec"

# Nested: kaldifst / sherpa → openfst (sherpa cmake pin)
fetch_one \
  openfst \
  1.8.5-2026-07-09 \
  "https://github.com/csukuangfj/openfst/archive/refs/tags/v1.8.5-2026-07-09.tar.gz" \
  "2ff712a32952fcb01d351121a6bc8ccf4fdc6b2aa06ce8df2b3095dedd518c0e"

echo "All vendor trees ready under ${VENDOR_DIR}" >&2
