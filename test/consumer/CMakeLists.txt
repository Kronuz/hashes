# Takes only hashes::crc32c, with fetching disabled so any attempt to fetch fails the configure.
cmake_minimum_required(VERSION 3.18)
project(hashes_crc32c_consumer LANGUAGES CXX)

include(FetchContent)
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
FetchContent_Declare(hashes_crc32c SOURCE_DIR "${HASHES_SOURCE_DIR}" SOURCE_SUBDIR crc32c-leaf)
FetchContent_MakeAvailable(hashes_crc32c)

if(TARGET hashes::hashes OR TARGET static_string::static_string OR TARGET char_classify::char_classify)
  message(FATAL_ERROR "hashes::crc32c pulled in the hash families")
endif()

add_executable(consumer consumer.cc)
target_link_libraries(consumer PRIVATE hashes::crc32c)
