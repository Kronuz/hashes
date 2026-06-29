cmake_minimum_required(VERSION 3.14)
project(hashes LANGUAGES CXX)

include(FetchContent)

# --- Dependencies ---
# hashes.hh does #include "static_string.hh" (so the hash families accept a
# static_string argument) and #include "chars.hh" (for chars::tolower, used by
# the case-insensitive FNV). Both are already-extracted standalone libraries;
# FetchContent pulls them in at the SHAs hashes was extracted against and puts
# their headers on the include path so those includes resolve.
FetchContent_Declare(
  static_string
  GIT_REPOSITORY https://github.com/Kronuz/static-string.git
  GIT_TAG        43a2e67a31a10031eb9e0d8bef61c8b6c14cd958
)
FetchContent_Declare(
  char_classify
  GIT_REPOSITORY https://github.com/Kronuz/char-classify.git
  GIT_TAG        8094f0f1e209cd5ca1c4b6ce9a788139f77cdf8e
)
FetchContent_MakeAvailable(static_string char_classify)

# --- Library target ---
# hashes is header-only: one constexpr hashing header (hashes.hh). The target is
# a pure INTERFACE library that puts the source dir on the consumer's include
# path, requests cxx_std_20, and links the two deps so static_string.hh / chars.hh
# resolve and ride onto the consumer's include path too.
#
# hashes.hh has one optional seam: its *runtime* xxh64/xxh32 fast paths defer to
# XXH64()/XXH32() when an xxHash header is reachable. The library auto-detects
# "xxhash.h" / <xxhash.h> via __has_include and stays fully usable (constexpr
# xxh64, FNV, djb2, ...) when it is absent. Consumers that want the fast path put
# an xxHash on the include path; nothing here pulls one in.
add_library(hashes INTERFACE)
target_include_directories(hashes INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(hashes INTERFACE cxx_std_20)
target_link_libraries(hashes INTERFACE static_string::static_string char_classify::char_classify)
# Alias so consumers can link `hashes::hashes` whether they add this via
# add_subdirectory or FetchContent.
add_library(hashes::hashes ALIAS hashes)

# Only build the test when this repo is the top-level project, so consumers
# (e.g. via FetchContent) don't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(hashes_test test/test.cc)
  target_link_libraries(hashes_test PRIVATE hashes)
  add_test(NAME hashes COMMAND hashes_test)

  # A runnable tour of the library (not a test): build & run ./build/hashes_demo.
  # Links the same `hashes` target as the test, so static_string.hh / chars.hh
  # resolve and the constexpr hash families are on the include path.
  add_executable(hashes_demo examples/demo.cc)
  target_link_libraries(hashes_demo PRIVATE hashes)
endif()
