# Populates the leaf (name hashes_crc32c) and the families (name hashes) in the order FIRST
# says, then builds one program that uses both targets. Fetching is disabled; the family dependencies
# come from the parent's sources.
cmake_minimum_required(VERSION 3.18)
project(hashes_order LANGUAGES CXX)

include(FetchContent)
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
FetchContent_Declare(hashes_crc32c SOURCE_DIR "${HASHES_SOURCE_DIR}" SOURCE_SUBDIR crc32c-leaf)
FetchContent_Declare(hashes SOURCE_DIR "${FAMILIES_DIR}")

if(FIRST STREQUAL "leaf")
  FetchContent_MakeAvailable(hashes_crc32c hashes)
else()
  FetchContent_MakeAvailable(hashes hashes_crc32c)
endif()

foreach(t hashes::crc32c hashes::hashes)
  if(NOT TARGET ${t})
    message(FATAL_ERROR "${t} was not defined")
  endif()
endforeach()

add_executable(order order.cc)
target_link_libraries(order PRIVATE hashes::crc32c hashes::hashes)
