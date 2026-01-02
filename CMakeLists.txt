cmake_minimum_required(VERSION 3.16)
project(rdedisktool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
endif()

# Version information
configure_file(
    "${CMAKE_SOURCE_DIR}/include/rdedisktool/Version.h.in"
    "${CMAKE_SOURCE_DIR}/include/rdedisktool/Version.h"
)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Core library sources
set(CORE_SOURCES
    src/core/DiskImage.cpp
    src/core/DiskImageFactory.cpp
    src/core/FormatDetector.cpp
    src/core/CRC.cpp
)

# Apple II format sources
set(APPLE_SOURCES
    src/apple/AppleDiskImage.cpp
    src/apple/AppleDOImage.cpp
    src/apple/ApplePOImage.cpp
    src/apple/AppleNibImage.cpp
    src/apple/AppleWozImage.cpp
    src/apple/NibbleEncoder.cpp
)

# MSX format sources
set(MSX_SOURCES
    src/msx/MSXDiskImage.cpp
    src/msx/MSXDSKImage.cpp
    src/msx/MSXDMKImage.cpp
    src/msx/MSXXSAImage.cpp
    src/msx/XSAExtractor.cpp
    src/msx/XSACompressor.cpp
    src/msx/XSAHeader.cpp
    src/msx/MFMEncoder.cpp
)

# Filesystem sources
set(FILESYSTEM_SOURCES
    src/filesystem/FileSystemHandler.cpp
    src/filesystem/apple/AppleDOS33Handler.cpp
    src/filesystem/apple/AppleProDOSHandler.cpp
    src/filesystem/msx/MSXDOSHandler.cpp
    src/filesystem/msx/MSXFATUtils.cpp
)

# Utility sources
set(UTILS_SOURCES
    src/utils/BinaryReader.cpp
    src/utils/CommandOptions.cpp
    src/utils/FileUtils.cpp
    src/utils/FilenameConverter.cpp
    src/utils/TimestampUtils.cpp
)

# CLI sources
set(CLI_SOURCES
    src/cli/main.cpp
    src/cli/CLI.cpp
)

# Library target
add_library(rdedisktool_lib STATIC
    ${CORE_SOURCES}
    ${APPLE_SOURCES}
    ${MSX_SOURCES}
    ${FILESYSTEM_SOURCES}
    ${UTILS_SOURCES}
)

target_include_directories(rdedisktool_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Executable target
add_executable(rdedisktool ${CLI_SOURCES})

# Link with whole-archive to preserve static initializers
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_libraries(rdedisktool PRIVATE
        -Wl,--whole-archive rdedisktool_lib -Wl,--no-whole-archive)
else()
    target_link_libraries(rdedisktool PRIVATE rdedisktool_lib)
endif()

# Installation
install(TARGETS rdedisktool DESTINATION bin)
install(DIRECTORY include/rdedisktool DESTINATION include)

# Testing (optional)
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== rdedisktool Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
