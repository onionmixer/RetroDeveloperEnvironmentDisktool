# How to Compile RDE Disk Tool

This document provides step-by-step instructions for building the RDE Disk Tool on various platforms.

## Prerequisites

### Required Tools

| Tool | Minimum Version | Description |
|------|-----------------|-------------|
| CMake | 3.16+ | Build system generator |
| C++ Compiler | C++17 support | GCC 8+, Clang 7+, or MSVC 2019+ |
| Make/Ninja | Any | Build tool (platform dependent) |

### Platform-Specific Requirements

#### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential cmake git
```

#### Linux (Fedora/RHEL)
```bash
sudo dnf install gcc-c++ cmake git make
```

#### Linux (Arch)
```bash
sudo pacman -S base-devel cmake git
```

#### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install CMake via Homebrew
brew install cmake
```

#### Windows

1. Install Visual Studio 2019 or later with "Desktop development with C++" workload
2. Install CMake from https://cmake.org/download/
3. Or use MSYS2/MinGW:
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make
```

## Building

### Quick Start (Linux/macOS)

```bash
# Clone the repository (if not already done)
cd /path/to/RetroDeveloperEnvironmentDisktool

# Create build directory
mkdir -p build
cd build

# Configure
cmake ..

# Build
cmake --build .

# The executable will be at: ./rdedisktool
```

### Detailed Build Instructions

#### Step 1: Create Build Directory

```bash
mkdir build
cd build
```

#### Step 2: Configure with CMake

**Basic configuration:**
```bash
cmake ..
```

**Release build (optimized):**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

**Debug build (with debug symbols):**
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

**Specify compiler (optional):**
```bash
cmake -DCMAKE_CXX_COMPILER=clang++ ..
```

**Specify install prefix:**
```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
```

#### Step 3: Build

**Using make:**
```bash
make -j$(nproc)
```

**Using CMake (cross-platform):**
```bash
cmake --build . --parallel
```

**Verbose build (shows commands):**
```bash
cmake --build . --verbose
```

#### Step 4: Verify Build

```bash
./rdedisktool --version
```

Expected output:
```
Retro Developer Environment Disk Tool v1.0.0
Supported platforms: Apple II, MSX
```

### Building on Windows

#### Using Visual Studio

```powershell
# Create build directory
mkdir build
cd build

# Configure for Visual Studio 2022
cmake -G "Visual Studio 17 2022" -A x64 ..

# Build Release
cmake --build . --config Release

# Executable location: Release\rdedisktool.exe
```

#### Using MinGW (MSYS2)

```bash
# In MSYS2 MinGW64 shell
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
mingw32-make -j$(nproc)
```

#### Using Ninja (Faster builds)

```bash
cmake -G Ninja ..
ninja
```

### Cross-Compilation

#### Linux to Windows (MinGW)

```bash
# Install cross-compiler
sudo apt install mingw-w64

# Configure
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake ..

# Build
make
```

## Build Options

| CMake Option | Default | Description |
|--------------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Build type: Debug, Release, RelWithDebInfo |
| `CMAKE_INSTALL_PREFIX` | /usr/local | Installation directory |
| `BUILD_TESTING` | ON | Build unit tests |

Example:
```bash
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF ..
```

## Installation

### System-wide Installation

```bash
sudo cmake --install .
```

### Custom Location

```bash
cmake --install . --prefix ~/local
```

### Manual Installation

```bash
# Copy binary
sudo cp rdedisktool /usr/local/bin/

# Verify
rdedisktool --version
```

## Troubleshooting

### Common Issues

#### 1. CMake version too old

Error: `CMake 3.16 or higher is required`

Solution:
```bash
# Ubuntu/Debian - Install from Kitware repository
sudo apt install apt-transport-https ca-certificates gnupg
curl -s https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ focal main'
sudo apt update
sudo apt install cmake
```

#### 2. C++17 not supported

Error: `error: 'filesystem' is not a namespace-name`

Solution: Upgrade compiler or specify C++17 explicitly:
```bash
cmake -DCMAKE_CXX_STANDARD=17 ..
```

#### 3. Missing standard library headers

Error: `fatal error: filesystem: No such file or directory`

Solution (GCC < 9):
```bash
# Link with stdc++fs
cmake -DCMAKE_CXX_FLAGS="-lstdc++fs" ..
```

#### 4. Permission denied during install

Error: `CMake Error: cannot copy file`

Solution:
```bash
sudo cmake --install .
# Or install to user directory
cmake --install . --prefix ~/.local
```

### Build Verification

After building, run these commands to verify:

```bash
# Check version
./rdedisktool --version

# Show help
./rdedisktool --help

# Run a simple test (if you have a test disk image)
./rdedisktool info testdisk.dsk
```

## Development

### Rebuilding After Code Changes

```bash
cd build
cmake --build .
```

### Clean Build

```bash
cd build
rm -rf *
cmake ..
cmake --build .
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

## Project Structure

```
RetroDeveloperEnvironmentDisktool/
├── CMakeLists.txt          # Main CMake configuration
├── README.md               # Project documentation
├── HOWTO_COMPILE.md        # This file
├── include/
│   └── rdedisktool/        # Public headers
│       ├── DiskImage.h
│       ├── DiskImageFactory.h
│       ├── FileSystemHandler.h
│       ├── CLI.h
│       ├── Types.h
│       ├── Exceptions.h
│       ├── CRC.h
│       ├── apple/          # Apple II specific headers
│       ├── msx/            # MSX specific headers
│       └── filesystem/     # File system handler headers
├── src/
│   ├── DiskImage.cpp
│   ├── DiskImageFactory.cpp
│   ├── CRC.cpp
│   ├── cli/               # CLI implementation
│   ├── apple/             # Apple II format implementations
│   ├── msx/               # MSX format implementations
│   ├── filesystem/        # File system implementations
│   └── utils/             # Utility functions
└── build/                  # Build output (created during build)
```

## Contact

For issues or questions, please refer to the project repository.
