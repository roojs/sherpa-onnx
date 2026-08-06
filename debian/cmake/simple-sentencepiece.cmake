# Offline packaging: use debian/vendor trees instead of FetchContent.

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/simple-sentencepiece")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(SBPE_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SBPE_BUILD_PYTHON OFF CACHE BOOL "" FORCE)

set(simple-sentencepiece_SOURCE_DIR "${_src}")
set(simple-sentencepiece_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/simple-sentencepiece-build")

message(STATUS "debian vendor simple-sentencepiece: ${simple-sentencepiece_SOURCE_DIR}")

set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${simple-sentencepiece_SOURCE_DIR} ${simple-sentencepiece_BINARY_DIR} EXCLUDE_FROM_ALL)

if(TARGET ssentencepiece_core AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  target_compile_options(ssentencepiece_core PRIVATE -Wno-deprecated-declarations)
endif()

set_target_properties(ssentencepiece_core
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(_build_shared_libs_bak)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS ON)
endif()

target_include_directories(ssentencepiece_core
  PUBLIC
    ${simple-sentencepiece_SOURCE_DIR}/
)
