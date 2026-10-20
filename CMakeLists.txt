cmake_minimum_required(VERSION 3.14)
project(hashes LANGUAGES CXX)

include(FetchContent)

# --- hashes::crc32c: the leaf target ---
# crc32c.hh includes only the standard library, so a consumer that links this fetches
# nothing else.
add_library(hashes_crc32c INTERFACE)
target_include_directories(hashes_crc32c INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(hashes_crc32c INTERFACE cxx_std_20)
add_library(hashes::crc32c ALIAS hashes_crc32c)

# --- hashes::hashes: the hash families (hashes.hh) ---
# hashes.hh does #include "static_string.hh" (so the hash families accept a
# static_string argument) and #include "chars.hh" (for chars::tolower, used by
# the case-insensitive FNV). Both are already-extracted standalone libraries;
# FetchContent pulls them in at the SHAs hashes was extracted against and puts
# their headers on the include path so those includes resolve. EXCLUDE_FROM_ALL would
# still fetch them at configure time, so a CRC-32C-only consumer sets HASHES_FAMILIES
# OFF before making hashes available; hashes::hashes is then not defined.
option(HASHES_FAMILIES "Define hashes::hashes (fetches static-string and char-classify)" ON)
if(HASHES_FAMILIES)
  FetchContent_Declare(
    static_string
    GIT_REPOSITORY https://github.com/Kronuz/static-string.git
    GIT_TAG        eb1efcb39f4d8bb4fd20a9ea91498a288d25aca2
  )
  FetchContent_Declare(
    char_classify
    GIT_REPOSITORY https://github.com/Kronuz/char-classify.git
    GIT_TAG        306ecd96d55d47d9bd3167eaeb7877e40d0631ac
  )
  FetchContent_MakeAvailable(static_string char_classify)

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
  target_link_libraries(hashes INTERFACE hashes::crc32c static_string::static_string char_classify::char_classify)
  # Alias so consumers can link `hashes::hashes` whether they add this via
  # add_subdirectory or FetchContent.
  add_library(hashes::hashes ALIAS hashes)
endif()

# Only build the test when this repo is the top-level project, so consumers
# (e.g. via FetchContent) don't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(crc32c_test test/crc32c_test.cc)
  target_link_libraries(crc32c_test PRIVATE hashes::crc32c)
  add_test(NAME crc32c COMMAND crc32c_test)
  add_executable(crc32c_bench bench/crc32c.cc)
  target_link_libraries(crc32c_bench PRIVATE hashes::crc32c)

  # A consumer that takes only the leaf: with fetching disabled, configure must not need
  # static-string or char-classify, and the built program must run.
  add_test(NAME crc32c_consumer
    COMMAND ${CMAKE_CTEST_COMMAND} --build-and-test
      "${CMAKE_CURRENT_SOURCE_DIR}/test/consumer" "${CMAKE_CURRENT_BINARY_DIR}/consumer"
      --build-generator "${CMAKE_GENERATOR}"
      --build-options "-DHASHES_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      --test-command "${CMAKE_CURRENT_BINARY_DIR}/consumer/consumer")

  if(HASHES_FAMILIES)
    add_executable(hashes_test test/test.cc)
    target_link_libraries(hashes_test PRIVATE hashes)
    add_test(NAME hashes COMMAND hashes_test)

    # A runnable tour of the library (not a test): build & run ./build/hashes_demo.
    # Links the same `hashes` target as the test, so static_string.hh / chars.hh
    # resolve and the constexpr hash families are on the include path.
    add_executable(hashes_demo examples/demo.cc)
    target_link_libraries(hashes_demo PRIVATE hashes)

    # Benchmark (not a test): which hash to pick for perfect-hash string dispatch, on real
    # key sets. Pulls the constexpr-phf engine as a bench-only contender; constexpr-phf is
    # integer-only and does not depend on hashes, so this is a clean one-way bench dependency.
    FetchContent_Declare(constexpr_phf
      GIT_REPOSITORY https://github.com/Kronuz/constexpr-phf.git
      GIT_TAG        1a035230c03982a822637f7c741656bd688bd9e3)
    FetchContent_MakeAvailable(constexpr_phf)
    add_executable(hashes_bench_dispatch bench/dispatch.cc)
    target_link_libraries(hashes_bench_dispatch PRIVATE hashes constexpr_phf)
    target_include_directories(hashes_bench_dispatch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bench)
    target_compile_options(hashes_bench_dispatch PRIVATE -O3)
  endif()
endif()
