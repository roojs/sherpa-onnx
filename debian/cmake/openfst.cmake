# Offline: debian/vendor/openfst instead of FetchContent.
# Matches pins / behaviour of top-level cmake/openfst.cmake.

if(NOT SHERPA_DEBIAN_VENDOR_DIR)
  set(SHERPA_DEBIAN_VENDOR_DIR "${CMAKE_SOURCE_DIR}/debian/vendor")
endif()

set(_src "${SHERPA_DEBIAN_VENDOR_DIR}/openfst")
if(NOT EXISTS "${_src}/CMakeLists.txt")
  message(FATAL_ERROR
    "Missing ${_src} — run debian/vendor-fetch.sh before configuring")
endif()

set(HAVE_BIN OFF CACHE BOOL "" FORCE)
set(HAVE_SCRIPT OFF CACHE BOOL "" FORCE)
set(HAVE_COMPACT OFF CACHE BOOL "" FORCE)
set(HAVE_COMPRESS OFF CACHE BOOL "" FORCE)
set(HAVE_CONST OFF CACHE BOOL "" FORCE)
set(HAVE_FAR ON CACHE BOOL "" FORCE)
set(HAVE_GRM OFF CACHE BOOL "" FORCE)
set(HAVE_PDT OFF CACHE BOOL "" FORCE)
set(HAVE_MPDT OFF CACHE BOOL "" FORCE)
set(HAVE_LINEAR OFF CACHE BOOL "" FORCE)
set(HAVE_LOOKAHEAD OFF CACHE BOOL "" FORCE)
set(HAVE_NGRAM OFF CACHE BOOL "" FORCE)
set(HAVE_PYTHON OFF CACHE BOOL "" FORCE)
set(HAVE_SPECIAL OFF CACHE BOOL "" FORCE)

set(openfst_SOURCE_DIR "${_src}")
set(openfst_BINARY_DIR "${CMAKE_BINARY_DIR}/debian-vendor/openfst-build")

message(STATUS "debian vendor openfst: ${openfst_SOURCE_DIR}")

set(_build_shared_libs_bak ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${openfst_SOURCE_DIR} ${openfst_BINARY_DIR} EXCLUDE_FROM_ALL)

set_target_properties(fst fstfar
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(_build_shared_libs_bak)
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS ON)
endif()

set(openfst_SOURCE_DIR ${openfst_SOURCE_DIR} PARENT_SCOPE)

set_target_properties(fst PROPERTIES OUTPUT_NAME "sherpa-onnx-fst")
set_target_properties(fstfar PROPERTIES OUTPUT_NAME "sherpa-onnx-fstfar")
