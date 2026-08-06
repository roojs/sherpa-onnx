# Use distro nlohmann-json3-dev instead of FetchContent.

find_path(SHERPA_NLOHMANN_JSON_INCLUDE_DIR
  NAMES nlohmann/json.hpp
  PATHS /usr/include
  REQUIRED
)

message(STATUS "debian system nlohmann/json: ${SHERPA_NLOHMANN_JSON_INCLUDE_DIR}")
include_directories(${SHERPA_NLOHMANN_JSON_INCLUDE_DIR})
