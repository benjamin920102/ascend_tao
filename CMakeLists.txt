cmake_minimum_required(VERSION 3.16)
project(ascend_taohash_pool LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Direct detection avoids a hard dependency on pkg-config and .pc files.
# The actual json-c development headers and library are still necessary.
find_path(JSONC_INCLUDE_DIR NAMES json-c/json.h)
find_library(JSONC_LIBRARY NAMES json-c)
if(NOT JSONC_INCLUDE_DIR OR NOT JSONC_LIBRARY)
  message(FATAL_ERROR
    "json-c development files not found. On Ubuntu/Debian: "
    "sudo apt-get install libjson-c-dev")
endif()

# Boost.Multiprecision is header-only (no Boost library to link).
find_path(BOOST_INCLUDE_DIR NAMES boost/multiprecision/cpp_int.hpp)
if(NOT BOOST_INCLUDE_DIR)
  message(FATAL_ERROR
    "Boost headers not found. On Ubuntu/Debian: "
    "sudo apt-get install libboost-dev")
endif()

add_executable(taohash_pool src/miner.cpp src/sha256d.cpp)
target_compile_features(taohash_pool PRIVATE cxx_std_17)
target_include_directories(taohash_pool PRIVATE
  include "${JSONC_INCLUDE_DIR}" "${BOOST_INCLUDE_DIR}")
target_link_libraries(taohash_pool PRIVATE
  Threads::Threads OpenSSL::Crypto "${JSONC_LIBRARY}" ${CMAKE_DL_LIBS})
target_compile_options(taohash_pool PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS taohash_pool RUNTIME DESTINATION bin)

# Original-whitepaper mathematics (educational reference only, NOT Stratum hashing
# or a present-day on-chain Yuma/validator implementation).
add_executable(whitepaper_incentive src/whitepaper_incentive.cpp)
target_compile_features(whitepaper_incentive PRIVATE cxx_std_17)
target_include_directories(whitepaper_incentive PRIVATE "${JSONC_INCLUDE_DIR}")
target_link_libraries(whitepaper_incentive PRIVATE "${JSONC_LIBRARY}")
target_compile_options(whitepaper_incentive PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS whitepaper_incentive RUNTIME DESTINATION bin)
