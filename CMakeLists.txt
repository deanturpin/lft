cmake_minimum_required(VERSION 3.25)
project(lft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
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

FetchContent_MakeAvailable(httplib json)

# FTXUI for terminal UI
find_package(ftxui QUIET)
if(NOT ftxui_FOUND)
    message(STATUS "FTXUI not found, TUI will be disabled")
    set(HAVE_FTXUI FALSE)
else()
    message(STATUS "FTXUI found, TUI enabled")
    set(HAVE_FTXUI TRUE)
endif()

# Include directories
include_directories(include)

# Alpaca API client library
add_library(alpaca_client STATIC
    src/alpaca_client.cxx
)
target_link_libraries(alpaca_client PUBLIC httplib::httplib nlohmann_json::nlohmann_json)

# Strategies library
add_library(strategies STATIC
    src/strategies.cxx
)
target_link_libraries(strategies PUBLIC alpaca_client)

# TUI library (optional)
if(HAVE_FTXUI)
    add_library(tui STATIC
        src/tui.cxx
    )
    target_link_libraries(tui PUBLIC
        ftxui::screen
        ftxui::dom
        ftxui::component
    )
endif()

# Main executable
add_executable(lft src/lft.cxx)
if(HAVE_FTXUI)
    target_link_libraries(lft PRIVATE strategies tui)
    target_compile_definitions(lft PRIVATE HAVE_FTXUI=1)
else()
    target_link_libraries(lft PRIVATE strategies)
endif()
