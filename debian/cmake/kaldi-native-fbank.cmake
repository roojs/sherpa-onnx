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

if(BUILD_SHARED_LIBS)
  set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
endif()

set(_cmake_warn_deprecated_bak "${CMAKE_WARN_DEPRECATED}")
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
add_subdirectory(${kaldi_native_fbank_SOURCE_DIR} ${kaldi_native_fbank_BINARY_DIR} EXCLUDE_FROM_ALL)
set(CMAKE_WARN_DEPRECATED "${_cmake_warn_deprecated_bak}" CACHE BOOL "" FORCE)

if(TARGET kissfft AND (CMAKE_C_COMPILER_ID MATCHES "Clang"))
  target_compile_options(kissfft PRIVATE -Wno-cast-align)
endif()

if(_build_shared_libs_bak)
  set_target_properties(kaldi-native-fbank-core
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_VISIBILITY_PRESET hidden
      CXX_VISIBILITY_PRESET hidden
  )
  set(BUILD_SHARED_LIBS ON)
endif()

target_include_directories(kaldi-native-fbank-core
  INTERFACE
    ${kaldi_native_fbank_SOURCE_DIR}/
)
