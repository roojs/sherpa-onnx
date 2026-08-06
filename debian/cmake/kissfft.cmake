# Offline: debian/vendor/kissfft instead of FetchContent.

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/kissfft")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)

set(kissfft_SOURCE_DIR "${_src}")
set(kissfft_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/kissfft-build")

message(STATUS "debian vendor kissfft: ${kissfft_SOURCE_DIR}")

if(BUILD_SHARED_LIBS)
  set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
endif()

add_subdirectory(${kissfft_SOURCE_DIR} ${kissfft_BINARY_DIR} EXCLUDE_FROM_ALL)

if(_build_shared_libs_bak)
  set_target_properties(kissfft
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
      C_VISIBILITY_PRESET hidden
      CXX_VISIBILITY_PRESET hidden
  )
  set(BUILD_SHARED_LIBS ON)
endif()

set(kissfft_SOURCE_DIR ${kissfft_SOURCE_DIR} PARENT_SCOPE)

include_directories(${kissfft_SOURCE_DIR})
