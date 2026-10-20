cmake_minimum_required(VERSION 3.14)
project(hashes_crc32c LANGUAGES CXX)

# The CRC-32C leaf as its own FetchContent unit (SOURCE_SUBDIR crc32c-leaf): crc32c.hh
# includes only the standard library, so nothing is fetched. Skipped if already defined.
if(NOT TARGET hashes::crc32c)
  add_library(hashes_crc32c INTERFACE)
  target_include_directories(hashes_crc32c INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_compile_features(hashes_crc32c INTERFACE cxx_std_20)
  add_library(hashes::crc32c ALIAS hashes_crc32c)
endif()
