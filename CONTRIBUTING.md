# Contributing to NightForge

Thank you for your interest in contributing!

## Development Setup

1. **Prerequisites**:
   - C++17 compatible compiler (GCC 7+, Clang 5+)
   - CMake 3.16+
   - Git

2. **Building**:
   ```bash
   git clone <your-fork>
   cd NightForge
   mkdir build && cd build
   cmake ..
   make
   ```

## Testing

The project uses GitHub Actions for CI/CD. Before submitting a PR:

1. **Build locally**: `make` should complete without errors
2. **Test help commands**: `./nightforge --help` should work
3. **Test basic functionality**: `./nightforge --min-width 10 --min-height 5` should start without crashing

Testing is NOT a suggestion, it is a REQUIREMENT, please do not submit untested PRs.

## Code Style

- Use C++17 features appropriately
- Follow existing naming conventions
- Keep functions focused and small
- Add comments for complex logic
- Ensure no memory leaks

## Submitting Changes

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test locally
5. Submit a pull request

## Architecture (this might be outdated by the time anyone reads it)

- `src/core/` - Engine core and main loop
- `src/rendering/` - TUI renderer and ASCII art conversion
- `tools/` - Asset conversion utilities
- `assets/` - Game content (scenes, scripts, sprites)