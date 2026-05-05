# C++ Scaffold

This repository provides a minimal modern C++ starter project with:

- CMake and CMake Presets
- Third-party dependencies via `FetchContent`
- `fmt` for formatting
- `spdlog` for logging
- `GoogleTest` for unit tests
- `clang-format` and `.editorconfig`
- GitHub Actions for CI and release artifacts

## Logging

The scaffold includes a baseline `spdlog` setup with:

- colored console output
- file logging under `logs/`
- a shared log pattern
- level selection by build type
- convenience macros such as `LOG_INFO(...)` and `LOG_ERROR(...)`

Main entry points:

- `app::logging::init("app_name")`
- `LOG_TRACE(...)`
- `LOG_DEBUG(...)`
- `LOG_INFO(...)`
- `LOG_WARN(...)`
- `LOG_ERROR(...)`
- `LOG_CRITICAL(...)`

## Requirements

- CMake 3.21+
- A C++20 compiler
- Ninja or another supported CMake generator

## Build

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

On Windows with Visual Studio 2022 installed:

```bash
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

## Demo

```bash
./build/default/hello_world
```

Expected output:

```text
[2026-05-05 08:39:44.502] [hello_world] [info] Hello, World!
```
