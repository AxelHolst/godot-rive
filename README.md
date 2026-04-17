# Godot Rive

### An integration of Rive into Godot 4.6+ using GDExtension

> [!NOTE]
> **v0.2.0** - This extension now supports:
> * macOS x86_64 and Linux x86_64
> * Rive Events and Nested Artboard Inputs
> * Full `rive-runtime` integration (migrated from deprecated `rive-cpp`)

This extension adds [Rive](https://rive.app) support to Godot 4.6+.

It makes use of the following third-party libraries:
- [`rive-runtime`](https://github.com/rive-app/rive-runtime) (migrated from `rive-cpp`)
- [`skia`](https://github.com/google/skia) (bundled with rive-runtime)

## Table of Contents

1. [Features](#features)
2. [Building](#building)
3. [Installation](#installation)
4. [Roadmap](#roadmap)
5. [Contributing](#contributing)
6. [Screenshots](#screenshots)

## Features

* Load `.riv` files (artboards, animations, and state machines)
* **Rive Events** - Receive events from Rive files via signals
* **Nested Artboard Inputs** - Control inputs inside nested artboards
* **Trigger inputs** - Fire triggers, not just bool/number inputs
* Listen for input events (hover, pressed, etc.)
* Change state machine properties in-editor and in code
* Robust API for runtime interaction

## Building

> [!IMPORTANT]
> Building locally is tested on macOS (Intel/M1). For Linux, use the GitHub Actions workflow (`.github/workflows/build-linux.yml`).

The following must be installed:
- Python 3
- [git](https://git-scm.com/)
- [scons](https://scons.org/)
- [ninja](https://ninja-build.org/)

To build, run the following commands (from the root directory):
```bash
cd build
python build.py
```

To see the available options, run:
```bash
python build.py --help
```

## Installation

> [!IMPORTANT]
> Pre-built binaries are available for macOS x86_64 and Linux x86_64. For other platforms, you will need to build the extension yourself.

1. Copy `demo/bin/`, `demo/icons/`, and `demo/rive.gdextension` to your project folder
2. Update the paths in `rive.gdextension` to match your project folder structure

## Roadmap
- [x] Load `.riv` files
- [x] Run and play Rive animations
- [x] Raster image support
- [x] Input events (hover, pressed, etc.)
- [x] Alignment & size exported properties
- [x] Multiple scenes/artboards
- [x] Dynamic exported properties based on state machine
- [x] API for interaction during runtime
- [x] Add error handling
- [x] Add signals for event listeners (hover, pressed, etc)
- [x] Disable/enable event listeners (hover, pressed, etc) in API and editor
- [x] Optimization
- [x] Static editor preview
- [x] Animated editor preview
- [x] **Rive Events** (v0.2.0)
- [x] **Nested Artboard Inputs** (v0.2.0)
- [x] **Trigger inputs** (v0.2.0)
- [x] **Linux x86_64 support** (v0.2.0)
- [ ] GPU rendering (RenderingDevice or Rive Renderer)
- [ ] ViewModel/Data Binding
- [ ] Audio support
- [ ] Add reset button
- [ ] `.riv` ResourceLoader (thumbnails)
- [ ] Windows, Android, iOS, Web support

## Contributing

Help would be appreciated testing and/or building for the following platforms:
* Windows
* Android
* iOS
* Web

Feel free to contribute bug fixes (see open issues), documentation, or features as well.

## Screenshots

![In-editor screenshot](screenshots/screenshot_1.png)