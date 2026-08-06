# Offline: debian/vendor/eigen instead of FetchContent.
# (Distro libeigen3-dev is 3.x; upstream kaldi-decoder pins 5.0.1.)

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/eigen")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)

set(eigen_SOURCE_DIR "${_src}")
set(eigen_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/eigen-build")

message(STATUS "debian vendor eigen: ${eigen_SOURCE_DIR}")

add_subdirectory(${eigen_SOURCE_DIR} ${eigen_BINARY_DIR} EXCLUDE_FROM_ALL)
