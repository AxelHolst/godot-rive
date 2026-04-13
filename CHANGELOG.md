# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Project governance files (CHANGELOG.md, DEVELOPMENT_LOG.md, ARCHITECTURE.md)
- Established `develop` branch for ongoing development

### Changed
- Forked from [kibble-cabal/godot-rive](https://github.com/kibble-cabal/godot-rive)
- Repository now targets production-ready status with full rive-runtime feature support

### Planned
- Migration from deprecated `rive-cpp` to modern `rive-runtime`
- Data Binding / ViewModel support
- Rive Events system
- Trigger inputs
- Luau scripting integration
- Audio system
- GPU-accelerated rendering pipeline

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
