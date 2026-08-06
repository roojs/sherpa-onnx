# Offline: debian/vendor/kaldifst instead of FetchContent.

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/kaldifst")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(KALDIFST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KALDIFST_BUILD_PYTHON OFF CACHE BOOL "" FORCE)

set(kaldifst_SOURCE_DIR "${_src}")
set(kaldifst_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/kaldifst-build")

message(STATUS "debian vendor kaldifst: ${kaldifst_SOURCE_DIR}")

list(APPEND CMAKE_MODULE_PATH ${kaldifst_SOURCE_DIR}/cmake)

set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${kaldifst_SOURCE_DIR} ${kaldifst_BINARY_DIR} EXCLUDE_FROM_ALL)

set_target_properties(kaldifst_core
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(_build_shared_libs_bak)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS ON)
endif()

target_include_directories(kaldifst_core
  PUBLIC
    ${kaldifst_SOURCE_DIR}/
)

set_target_properties(kaldifst_core PROPERTIES OUTPUT_NAME "kaldi-decoder-kaldi-fst-core")
