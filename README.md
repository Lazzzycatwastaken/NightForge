# NightForge

![Build Status](https://github.com/Lazzzycatwastaken/NightForge/workflows/Build%20and%20Test/badge.svg)

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