cmake_minimum_required(VERSION 3.25)

# Prefer GCC 15 if available (better C++23/26 support than Clang on macOS)
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(GCC15_CXX NAMES g++-15)
    if(GCC15_CXX)
        set(CMAKE_CXX_COMPILER ${GCC15_CXX})
        find_program(GCC15_CC NAMES gcc-15)
        if(GCC15_CC)
            set(CMAKE_C_COMPILER ${GCC15_CC})
        endif()
    endif()
endif()

project(lft VERSION 0.1.0 LANGUAGES CXX)

# Use C++26 with GCC 15, fallback to C++23 otherwise
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15.0")
    set(CMAKE_CXX_STANDARD 26)
else()
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Debug build for development (enables assertions)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Dependencies
include(FetchContent)

# cpp-httplib for HTTP client
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)

# nlohmann/json for JSON parsing
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# Catch2 for unit testing
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.1
)

FetchContent_MakeAvailable(httplib json Catch2)

# Include directories
include_directories(include)

# Alpaca API client library
add_library(alpaca_client STATIC
    src/alpaca_client.cxx
)
target_link_libraries(alpaca_client PUBLIC httplib::httplib nlohmann_json::nlohmann_json)

# Main executable (entry point: main.cxx)
add_executable(lft
    src/main.cxx
    src/lft.cxx
    src/calibrate.cxx
    src/evaluate_entries.cxx
    src/check_exits.cxx
    src/liquidate.cxx
    src/strategies.cxx
)
target_link_libraries(lft PRIVATE alpaca_client)

# Test executables
add_executable(lft_tests
    tests/test_phases.cxx
    src/lft.cxx
    src/calibrate.cxx
    src/evaluate_entries.cxx
    src/check_exits.cxx
    src/liquidate.cxx
    src/strategies.cxx
)
target_link_libraries(lft_tests PRIVATE alpaca_client Catch2::Catch2WithMain)

add_executable(lft_trading_tests
    tests/test_trading_logic.cxx
    src/lft.cxx
    src/calibrate.cxx
    src/evaluate_entries.cxx
    src/check_exits.cxx
    src/liquidate.cxx
    src/strategies.cxx
)
target_link_libraries(lft_trading_tests PRIVATE alpaca_client Catch2::Catch2WithMain)

# Clock API test (manual test executable)
add_executable(test_clock test_clock.cxx)
target_link_libraries(test_clock PRIVATE alpaca_client)

# Account and market status test
add_executable(test_account test_account.cxx)
target_link_libraries(test_account PRIVATE alpaca_client)

# Enable CTest
enable_testing()
add_test(NAME lft_tests COMMAND lft_tests)
add_test(NAME lft_trading_tests COMMAND lft_trading_tests)
