# Offline packaging: use debian/vendor trees instead of FetchContent.

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/kaldi-decoder")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(KALDI_DECODER_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(KALDI_DECODER_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(KALDIFST_BUILD_PYTHON OFF CACHE BOOL "" FORCE)

set(kaldi_decoder_SOURCE_DIR "${_src}")
set(kaldi_decoder_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/kaldi-decoder-build")

message(STATUS "debian vendor kaldi-decoder: ${kaldi_decoder_SOURCE_DIR}")

include_directories(${kaldi_decoder_SOURCE_DIR})

if(BUILD_SHARED_LIBS)
  set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
endif()

list(APPEND CMAKE_MODULE_PATH ${kaldi_decoder_SOURCE_DIR}/cmake)

add_subdirectory(${kaldi_decoder_SOURCE_DIR} ${kaldi_decoder_BINARY_DIR} EXCLUDE_FROM_ALL)

if(_build_shared_libs_bak)
  set_target_properties(
      kaldi-decoder-core
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_VISIBILITY_PRESET hidden
      CXX_VISIBILITY_PRESET hidden
  )
  set(BUILD_SHARED_LIBS ON)
endif()

if(WIN32 AND MSVC)
  target_compile_options(kaldi-decoder-core PUBLIC
    /wd4018
    /wd4291
  )
endif()

set_target_properties(kaldifst_core PROPERTIES OUTPUT_NAME "sherpa-onnx-kaldifst-core")

target_include_directories(kaldi-decoder-core
  INTERFACE
    ${kaldi_decoder_SOURCE_DIR}/
)
