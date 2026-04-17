# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned (Roadmap)
- **M5: GPU Rendering** - RenderingDevice or Rive Renderer integration
- **M6: ViewModel/Data Binding** - Unity parity for data-driven animations
- **M7: Luau Scripting** - Script execution inside Rive files
- **M8: Audio** - Rive audio → Godot AudioStreamPlayer bridge

---

## [0.2.0] - 2026-04-17

Production-ready macOS and Linux builds with full rive-runtime integration.

### Added
- **Linux x86_64 Support (M4)** - Full CI/CD pipeline for Linux builds
  - GitHub Actions workflow with caching (~5min builds after initial ~25min)
  - Pre-built artifacts: `librive.linux.template_debug.x86_64.so`
  - Position-independent code (`-fPIC`) for shared library compatibility
- **Rive Events (M3)** - Full event system with `rive_event` signal and `RiveEvent` class
  - `RiveEvent.name` - Event name
  - `RiveEvent.seconds_delay` - Time offset from frame start
  - `RiveEvent.properties` - Dictionary of custom properties (bool, number, string)
  - Signal emitted during `_process()` after state machine advance
- **Nested Artboard Inputs (M3)** - Control inputs inside nested artboards
  - `RiveScene.set_input("NestedArtboard/InputName", value)` - Combined path format
  - `RiveScene.get_input_value("NestedArtboard/InputName")` - Get nested input value
  - `RiveScene.fire("NestedArtboard/TriggerName")` - Fire nested triggers
  - Explicit methods: `set_bool_at_path()`, `set_number_at_path()`, `fire_trigger_at_path()`
- **Trigger input API** - `RiveInput::is_trigger()` and `RiveInput::fire()` methods
- **Debug logging macro** - `RIVE_DEBUG_LOG` conditionally compiled via `RIVE_DEBUG_LOGGING`
- Project governance files (CHANGELOG.md, DEVELOPMENT_LOG.md, ARCHITECTURE.md)
- Comprehensive API migration guide (docs/API_MIGRATION.md)

### Changed
- **BREAKING: Migrated from `rive-cpp` to `rive-runtime`** - Complete rewrite of native bindings
- Forked from [kibble-cabal/godot-rive](https://github.com/kibble-cabal/godot-rive)
- Updated for Godot 4.6.2 compatibility
- `rive::File` now uses `rcp<>` (reference-counted) instead of `unique_ptr`
- `RiveListener::get_type()` now uses `hasListener()` checks instead of removed `listenerType()`
- Replaced macOS-specific `_NOEXCEPT` with standard C++ `noexcept` for cross-platform builds

### Fixed
- **Standalone export "pink screen" (M3)** - Fixed count methods returning cache size instead of actual Rive counts
  - `RiveFile.get_artboard_count()` now calls `file->artboardCount()`
  - `RiveArtboard.get_scene_count()` now calls `artboard->stateMachineCount()`
  - `RiveArtboard.get_animation_count()` now calls `artboard->animationCount()`
  - `RiveScene.get_input_count()` now calls `scene->inputCount()`
  - `RiveScene.get_listener_count()` now calls `scene->stateMachine()->listenerCount()`
- **ABI alignment with librive.a** - SConstruct now matches rive.make build flags exactly
  - Force-include headers for HarfBuzz/Yoga symbol renaming (prevents ODR violations)
  - Preprocessor defines: `WITH_RIVE_TEXT`, `WITH_RIVE_LAYOUT`, platform flags
  - macOS flags: `-fobjc-arc`, `-mmacosx-version-min=11.0`
- **Scene loading crash** - Added deferred initialization to prevent binding callbacks during scene load
- **SkCanvas crash** - Removed `-DRIVE_OPTIMIZED` flag that stubbed out Skia constructors
- **macOS build** - Added C++ header workaround for Command Line Tools
- **Linux build** - Removed Skia LTO (`-flto=full`) for linker compatibility

### Platforms
- macOS x86_64 (local build)
- Linux x86_64 (CI build)

### Dependencies
- Godot 4.6.2
- rive-runtime @ `b11b579c` (April 2026)
- Skia (rive-optimized fork)

---

## Pre-Fork History

The following versions were released by the original maintainers at kibble-cabal.

### [0.1.0-alpha] - 2024-02-01

Initial alpha release (kibble-cabal/godot-rive)

#### Features
- Load `.riv` files (artboards, animations, and state machines)
- Listen for input events (mouse hover, press, release)
- Change state machine properties (Bool/Number) in-editor and in code
- Runtime API for interaction
- RiveViewer (Control-based) and RiveViewer2D (Node2D-based) nodes
- Fit modes and alignment options
- Static and animated editor preview

#### Known Limitations
- macOS only (M1/x86_64 binaries)
- No Trigger input support
- No Rive Events
- No Data Binding / ViewModel
- No Scripting support
- No Audio support
- CPU-based Skia rendering only
