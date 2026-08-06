# ASR-trimmed C API packages for Fedora.
# Reuses debian/vendor-fetch.sh trees + debian/cmake overlays (offline FetchContent).
#
# Build from a git checkout (after ./debian/vendor-fetch.sh), e.g.:
#   ver=1.13.4
#   mkdir -p rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
#   tar --exclude=.git --exclude=build --exclude=rpmbuild \
#       --exclude=artifacts --exclude='*.deb' --exclude='*.rpm' \
#       -czf "rpmbuild/SOURCES/sherpa-onnx-${ver}.tar.gz" \
#       --transform "s,^,sherpa-onnx-${ver}/," .
#   cp rpm/sherpa-onnx.spec rpmbuild/SPECS/
#   rpmbuild -bb --define "_topdir $PWD/rpmbuild" rpmbuild/SPECS/sherpa-onnx.spec

Name:           sherpa-onnx
Version:        1.13.4
Release:        1%{?dist}
Summary:        Speech recognition C API on ONNX Runtime
License:        Apache-2.0
URL:            https://github.com/roojs/sherpa-onnx
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconf-pkg-config
BuildRequires:  onnxruntime-devel
BuildRequires:  json-devel

%description
sherpa-onnx provides speech recognition and related APIs on top of
ONNX Runtime. This source package builds the C API shared libraries.

%package -n libsherpa-onnx-c-api
Summary:        sherpa-onnx C API shared library

%description -n libsherpa-onnx-c-api
sherpa-onnx provides speech recognition and related APIs on top of
ONNX Runtime.

This package contains the shared C (and C++) API libraries.

%package -n libsherpa-onnx-c-api-devel
Summary:        Development files for sherpa-onnx C API
Requires:       libsherpa-onnx-c-api%{?_isa} = %{version}-%{release}
Requires:       onnxruntime-devel

%description -n libsherpa-onnx-c-api-devel
sherpa-onnx provides speech recognition and related APIs on top of
ONNX Runtime.

This package contains headers and pkg-config metadata for the C API.

%prep
%autosetup -n %{name}-%{version}

%build
test -f debian/vendor/kaldi-native-fbank/CMakeLists.txt \
  -a -f debian/vendor/kaldi-decoder/CMakeLists.txt \
  -a -f debian/vendor/simple-sentencepiece/CMakeLists.txt \
  -a -f debian/vendor/kissfft/CMakeLists.txt \
  -a -f debian/vendor/kaldifst/CMakeLists.txt \
  -a -f debian/vendor/eigen/CMakeLists.txt \
  -a -f debian/vendor/openfst/CMakeLists.txt \
  || { echo "debian/vendor missing — run debian/vendor-fetch.sh first" >&2; exit 1; }

%cmake -G Ninja \
  -DCMAKE_MODULE_PATH=%{_builddir}/%{buildsubdir}/debian/cmake \
  -DSHERPA_DEBIAN_VENDOR_DIR=%{_builddir}/%{buildsubdir}/debian/vendor \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DSHERPA_ONNX_ENABLE_C_API=ON \
  -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
  -DSHERPA_ONNX_ENABLE_TESTS=OFF \
  -DSHERPA_ONNX_ENABLE_CHECK=OFF \
  -DSHERPA_ONNX_ENABLE_BINARY=OFF \
  -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF \
  -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
  -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF \
  -DSHERPA_ONNX_ENABLE_JNI=OFF \
  -DSHERPA_ONNX_ENABLE_GPU=OFF \
  -DSHERPA_ONNX_ENABLE_TTS=OFF \
  -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF \
  -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=ON

%cmake_build

%install
%cmake_install

# Upstream install() often hardcodes lib/ and drops sherpa-onnx.pc under /usr.
# Relocate into lib64 + pkgconfig without touching upstream cmake.
libdir="%{buildroot}%{_libdir}"
mkdir -p "${libdir}/pkgconfig"

if [ -d "%{buildroot}/usr/lib" ]; then
  find "%{buildroot}/usr/lib" -maxdepth 1 -type f \
    \( -name 'libsherpa-onnx*.so*' -o -name 'libsherpa-onnx*.a' \) \
    -exec mv -t "${libdir}" {} + || true
fi

if [ -f "%{buildroot}/usr/sherpa-onnx.pc" ]; then
  mv "%{buildroot}/usr/sherpa-onnx.pc" "${libdir}/pkgconfig/sherpa-onnx.pc"
fi
if [ -f "%{buildroot}/usr/lib/pkgconfig/sherpa-onnx.pc" ]; then
  mv "%{buildroot}/usr/lib/pkgconfig/sherpa-onnx.pc" \
    "${libdir}/pkgconfig/sherpa-onnx.pc"
fi
rmdir "%{buildroot}/usr/lib/pkgconfig" 2>/dev/null || true

if [ -f "${libdir}/pkgconfig/sherpa-onnx.pc" ]; then
  sed -i 's|^libdir=${exec_prefix}/lib$|libdir=${exec_prefix}/%{_lib}|' \
    "${libdir}/pkgconfig/sherpa-onnx.pc"
fi

# Never ship a private onnxruntime copy.
rm -f "%{buildroot}/usr/lib"/libonnxruntime* \
  "%{buildroot}%{_libdir}"/libonnxruntime* \
  || true

%files -n libsherpa-onnx-c-api
%license LICENSE
%doc README.md
%{_libdir}/libsherpa-onnx-c-api.so*
%{_libdir}/libsherpa-onnx-cxx-api.so*

%files -n libsherpa-onnx-c-api-devel
%{_includedir}/sherpa-onnx/
%{_libdir}/pkgconfig/sherpa-onnx.pc

%changelog
* Thu Aug 06 2026 Alan Knowles <alan@roojs.com> - 1.13.4-1
- Initial RPM packaging of the C API shared libraries.
