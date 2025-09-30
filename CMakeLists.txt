cmake_minimum_required(VERSION 3.16)
project(NightForge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler-specific flags for performance
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# Source files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)

# Remove the converter and tools from main build
list(FILTER SOURCES EXCLUDE REGEX "tools/.*")

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

# Install targets
install(TARGETS nightforge DESTINATION bin)
install(TARGETS webp_to_ascii DESTINATION bin)

# Create assets directory in build
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/scenes)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/scripts)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/sprites)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/saves)