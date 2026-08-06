# Offline packaging: use debian/vendor trees instead of FetchContent.
# Selected when debian/rules prepends debian/cmake to CMAKE_MODULE_PATH.

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/kaldi-native-fbank")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(KALDI_NATIVE_FBANK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KALDI_NATIVE_FBANK_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(KALDI_NATIVE_FBANK_ENABLE_CHECK OFF CACHE BOOL "" FORCE)

set(kaldi_native_fbank_SOURCE_DIR "${_src}")
set(kaldi_native_fbank_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/kaldi-native-fbank-build")

message(STATUS "debian vendor kaldi-native-fbank: ${kaldi_native_fbank_SOURCE_DIR}")

# BUILD_SHARED_LIBS is CACHE. Plain set() does not override CACHE; CACHE-only
# FORCE can still be shadowed by a normal-scope ON. Nested includes (kissfft)
# that restore ON must not run while we need static fbank — set both scopes
# OFF for the whole add_subdirectory, and only restore afterward.
set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

set(_cmake_warn_deprecated_bak "${CMAKE_WARN_DEPRECATED}")
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
add_subdirectory(${kaldi_native_fbank_SOURCE_DIR} ${kaldi_native_fbank_BINARY_DIR} EXCLUDE_FROM_ALL)
set(CMAKE_WARN_DEPRECATED "${_cmake_warn_deprecated_bak}" CACHE BOOL "" FORCE)

get_target_property(_knf_type kaldi-native-fbank-core TYPE)
if(NOT _knf_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR
    "debian packaging requires kaldi-native-fbank-core as STATIC "
    "(got ${_knf_type}); knf::* would not embed into libsherpa-onnx-c-api.so")
endif()

if(TARGET kissfft AND (CMAKE_C_COMPILER_ID MATCHES "Clang"))
  target_compile_options(kissfft PRIVATE -Wno-cast-align)
endif()

set_target_properties(kaldi-native-fbank-core
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(_build_shared_libs_bak)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS ON)
endif()

target_include_directories(kaldi-native-fbank-core
  INTERFACE
    ${kaldi_native_fbank_SOURCE_DIR}/
)
