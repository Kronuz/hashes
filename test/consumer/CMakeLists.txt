# Takes only hashes::crc32c, with fetching disabled so any attempt to fetch fails the configure.
cmake_minimum_required(VERSION 3.14)
project(hashes_crc32c_consumer LANGUAGES CXX)

include(FetchContent)
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
set(HASHES_FAMILIES OFF)
FetchContent_Declare(hashes SOURCE_DIR "${HASHES_SOURCE_DIR}")
FetchContent_MakeAvailable(hashes)

if(TARGET hashes::hashes OR TARGET static_string::static_string OR TARGET char_classify::char_classify)
  message(FATAL_ERROR "hashes::crc32c pulled in the hash families")
endif()

add_executable(consumer consumer.cc)
target_link_libraries(consumer PRIVATE hashes::crc32c)
