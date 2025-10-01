# Windows Support

I hope this works.

## Building on Windows

### Prerequisites

#### Option 1: MinGW-w64 (Recommended)
1. Install [MSYS2](https://www.msys2.org/)
2. Open MSYS2 terminal and install build tools:
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make
```
3. Add MinGW-w64 to your PATH: `C:\msys64\mingw64\bin`

#### Option 2: Visual Studio (MSVC)
1. Install [Visual Studio 2019+](https://visualstudio.microsoft.com/) with C++ workload
2. Install [CMake](https://cmake.org/download/)

### Compilation

#### MinGW-w64 Build
```bash
# Clone the repository
git clone https://github.com/Lazzzycatwastaken/NightForge.git
cd NightForge

# Configure and build
cmake -G "MinGW Makefiles" -B build
cmake --build build -j4

# Run
build\nightforge.exe assets\scripts\test_functions_simple.ns
```

#### Visual Studio Build
```cmd
# Clone the repository
git clone https://github.com/Lazzzycatwastaken/NightForge.git
cd NightForge

# Configure and build
cmake -B build
cmake --build build --config Release

# Run
build\Release\nightforge.exe assets\scripts\test_functions_simple.ns
```