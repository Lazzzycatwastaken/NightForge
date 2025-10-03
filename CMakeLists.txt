cmake_minimum_required(VERSION 3.16)
project(NightForge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check compiler version and adjust settings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "7.0")
        message(WARNING "GCC version ${CMAKE_CXX_COMPILER_VERSION} has limited C++17 support. Consider upgrading to GCC 7+ for full compatibility.")
        # Fall back to C++14 for older GCC
        set(CMAKE_CXX_STANDARD 14)
    endif()
endif()

# Compiler-specific flags for performance
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
    # Remove -march=native for better Windows compatibility
    if(NOT WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

string(REPLACE "-DDEBUG_TRACE_EXECUTION" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-DDEBUG_TRACE_EXECUTION" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
string(REPLACE "-DDEBUG_TRACE_EXECUTION" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

# Source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)

# Remove the converter and tools from main build
list(FILTER SOURCES EXCLUDE REGEX "tools/.*")

# Platform-specific terminal implementation
if(WIN32)
    # Remove POSIX terminal implementation on Windows
    list(FILTER SOURCES EXCLUDE REGEX ".*terminal_posix\\.(cpp|h)")
else()
    # Remove Windows terminal implementation on non-Windows
    list(FILTER SOURCES EXCLUDE REGEX ".*terminal_win\\.(cpp|h)")
endif()

# Create main executable
add_executable(nightforge ${SOURCES})

# Include directories
target_include_directories(nightforge PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
)

# Converter tool
add_executable(webp_to_ascii 
    tools/converter.cpp
    src/rendering/ascii_art.cpp
    src/rendering/ascii_art.h
    src/rendering/stb_image.h
)

target_include_directories(webp_to_ascii PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
)

# Test runner
file(GLOB_RECURSE TEST_SOURCES 
    "src/*.cpp"
    "src/*.h"
)

# Remove main.cpp from test sources and add test_runner.cpp
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*main\\.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX "tools/.*")

# Platform-specific terminal implementation for tests
if(WIN32)
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*terminal_posix\\.(cpp|h)")
else()
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*terminal_win\\.(cpp|h)")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_runner.cpp")
    add_executable(test_runner 
        tests/test_runner.cpp
        ${TEST_SOURCES}
    )

    target_include_directories(test_runner PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
else()
    message(STATUS "tests/test_runner.cpp not found; skipping test_runner target")
endif()

# Install targets
install(TARGETS nightforge DESTINATION bin)
install(TARGETS webp_to_ascii DESTINATION bin)
if(TARGET test_runner)
    install(TARGETS test_runner DESTINATION bin)
endif()

# Create assets directory in build
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/scenes)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/scripts)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/sprites)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/saves)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests)