# Godot Rive

### An integration of Rive into Godot 4.3+ using GDExtension

> [!WARNING]
> This extension is in **alpha**. That means:
>
> - You may encounter some bugs
> - It's untested on many platforms
> - Most features are implemented, but the API may change a little

This extensions adds [Rive](https://rive.app) support to Godot 4.

It makes use of the following third-party libraries:

- [`rive-cpp`](https://github.com/rive-app/rive-cpp)
- [`skia`](https://github.com/google/skia) (included in `rive-cpp`)

## Table of Contents

1. [Features](#features)
2. [Building](#building)
3. [Installation](#installation)
4. [Roadmap](#roadmap)
5. [Contributing](#contributing)
6. [Screenshots](#screenshots)

## Features

- Load `.riv` files (artboards, animations, and state machines)
- Listen for input events
- Change state machine properties in-editor and in code
- Robust API for runtime interaction
- Optimized for Godot

## Building

> [!IMPORTANT]
> These instructions are only tested on MacOS. You may have to modify `build/build.py` or `build/SConstruct` for your system.

The following must be installed:

- Python 3
- [git](https://git-scm.com/)
- [scons](https://scons.org/)
- [ninja](https://ninja-build.org/)
- premake5

1. Follow the build instructions in `thirdparty/rive-cpp/renderer/README.md`. When running `build_rive.sh` make sure to include the `--with-rtti` command, e.g. `build_rive.sh release --with-rtti`.
2. You make have to add the following to `thirdparty/rive-cpp/dependencies/premake5_harfbuzz_v2.lua` (somewhere in the `files` function, after line 17):

```lua
harfbuzz .. '/src/hb-face-builder.cc',
harfbuzz .. '/src/hb-subset-instancer-iup.cc',
harfbuzz .. '/src/hb-subset-instancer-iup.h',
harfbuzz .. '/src/hb-subset-instancer-solver.cc',
harfbuzz .. '/src/hb-subset-instancer-solver.h',
```

4. To build, run the following commands (from the root directory):

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
> If you are not on M1 MacOS, you will need to build the extension yourself. Binaries are only provided for MacOS universal (debug and release).
> Eventually, binaries will be provided for other platforms.

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
- [ ] Add reset button
- [ ] `.riv` ResourceLoader (thumbnails)
- [ ] Other platform support
- [ ] Any missing features

## Contributing

Help would be MUCH appreciated testing and/or building for the following platforms:

- Windows
- Android
- iOS
- Linux
- Web

Feel free to contribute bug fixes (see open issues), documentation, or features as well.

## Screenshots

![In-editor screenshot](screenshots/screenshot_1.png)
