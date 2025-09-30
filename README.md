# NightForge

![Build Status](https://github.com/Lazzzycatwastaken/NightForge/workflows/Build%20and%20Test/badge.svg)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square&logo=cplusplus&logoColor=white)
![NightScript](https://img.shields.io/badge/NightScript-Language-8b00ef?style=flat-square&logo=data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+Cjxzdmcgd2lkdGg9IjEzLjg0IiBoZWlnaHQ9IjEzLjg0IiB2aWV3Qm94PSIwIDAgMy42NiAzLjY2IiB2ZXJzaW9uPSIxLjEiIGlkPSJzdmcxIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciPgo8ZGVmcyBpZD0iZGVmczEiPgo8bGluZWFyR3JhZGllbnQgaWQ9ImxpbmVhckdyYWRpZW50OSI+CjxzdG9wIHN0eWxlPSJzdG9wLWNvbG9yOiM4YjAwZWY7c3RvcC1vcGFjaXR5OjE7IiBvZmZzZXQ9IjAiIGlkPSJzdG9wOSIvPgo8c3RvcCBzdHlsZT0ic3RvcC1jb2xvcjojMmMwMDQwO3N0b3Atb3BhY2l0eToxOyIgb2Zmc2V0PSIxIiBpZD0ic3RvcDgiLz4KPC9saW5lYXJHcmFkaWVudD4KPC9kZWZzPgo8ZyBpZD0ibGF5ZXIxIj4KPGNpcmNsZSBzdHlsZT0iZmlsbDojMjgyODI4O2ZpbGwtb3BhY2l0eToxO3N0cm9rZS13aWR0aDowLjI2NDU4MyIgaWQ9InBhdGgzIiBjeD0iMS44MyIgY3k9IjEuODMiIHI9IjEuODMiLz4KPC9nPgo8L3N2Zz4=)
![CMake](https://img.shields.io/badge/CMake-Build-green?style=flat-square&logo=cmake&logoColor=white)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

A high-performance terminal-based game engine written in C++ with NightScript language support.

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
# Running the engine
./nightforge

# Test the NightScript interpreter
./nightforge ../assets/scripts/demo.ns

# Converting images to ASCII
./webp_to_ascii image.webp clean yes 80

# Run with custom terminal requirements
./nightforge --min-width 100 --min-height 30
```

## Terminal Requirements

- Monospace font required for proper layout
- Minimum terminal size: 80x24 (configurable)
- ANSI escape sequence support

## Development

The project uses GitHub Actions for continuous integration:
- **Build and Test**: Testing on Linux and macOS with multiple compilers
- **Quick Build**: Fast build verification on every push
- **ASCII Converter Test**: Validates image conversion functionality

All tests run automatically on push and pull requests.
## License

MIT License
