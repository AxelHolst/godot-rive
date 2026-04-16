# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
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
- **Trigger input API** - `RiveInput::is_trigger()` and `RiveInput::fire()` methods (untested)
- Project governance files (CHANGELOG.md, DEVELOPMENT_LOG.md, ARCHITECTURE.md)
- Established `develop` branch for ongoing development
- Test scenes for debugging (test_minimal.tscn, test_ghost.tscn, etc.)
- Comprehensive API migration guide (docs/API_MIGRATION.md)

### Changed
- **BREAKING: Migrated from `rive-cpp` to `rive-runtime`** - Complete rewrite of native bindings
- Forked from [kibble-cabal/godot-rive](https://github.com/kibble-cabal/godot-rive)
- Updated for Godot 4.6.2 compatibility
- `rive::File` now uses `rcp<>` (reference-counted) instead of `unique_ptr`
- `RiveListener::get_type()` now uses `hasListener()` checks instead of removed `listenerType()`

### Fixed
- **Scene loading crash** - Added deferred initialization to prevent binding callbacks during scene load
- **SkCanvas crash** - Removed `-DRIVE_OPTIMIZED` flag that stubbed out Skia constructors
- **macOS build** - Added C++ header workaround for Command Line Tools

### Needs Testing
- [x] Rive Events (tested with nested_artboard_events.riv)
- [ ] Nested artboard inputs
- [ ] Trigger inputs (`RiveInput::fire()`)
- [ ] State machine input changes via inspector
- [ ] Mouse interactions (joystick.riv)

### Planned (Roadmap)
- **M4: Linux Build** - Cross-platform deployment
- **M5: GPU Rendering** - RenderingDevice or Rive Renderer integration
- **M6: ViewModel/Data Binding** - Unity parity for data-driven animations
- **M7: Luau Scripting** - Script execution inside Rive files
- **M8: Audio** - Rive audio → Godot AudioStreamPlayer bridge

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
