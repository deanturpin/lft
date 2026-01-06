cmake_minimum_required(VERSION 3.25)
project(lft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

FetchContent_MakeAvailable(httplib json)

# Include directories
include_directories(include)

# Shared library for Alpaca API client
add_library(alpaca_client STATIC
    src/shared/alpaca_client.cxx
)
target_link_libraries(alpaca_client PUBLIC httplib::httplib nlohmann_json::nlohmann_json)

# Strategies library
add_library(strategies STATIC
    src/shared/strategies.cxx
)
target_link_libraries(strategies PUBLIC alpaca_client)

# Manual ticker executable
add_executable(ticker src/manual/ticker.cxx)
target_link_libraries(ticker PRIVATE alpaca_client strategies)

# Positions viewer executable
add_executable(positions src/manual/positions.cxx)
target_link_libraries(positions PRIVATE alpaca_client)

# Backtest executable
add_executable(backtest src/backtest/backtest.cxx)
target_link_libraries(backtest PRIVATE alpaca_client strategies)

# Enable testing
enable_testing()
add_subdirectory(tests)
