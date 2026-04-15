# Architecture Document

This document describes the technical architecture of the godot-rive GDExtension.

---

## Overview

godot-rive is a GDExtension that integrates the [Rive](https://rive.app) animation runtime into Godot 4.3+. It provides Godot nodes and resources that wrap the native `rive-runtime` C++ library.

## Current Architecture (Pre-Migration)

> **Note**: This section describes the architecture inherited from kibble-cabal/godot-rive.
> It will be updated as we migrate to rive-runtime.

### Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     GDScript / Godot Editor                  │
├─────────────────────────────────────────────────────────────┤
│                    Godot Wrapper Classes                     │
│  RiveViewer, RiveViewer2D, RiveFile, RiveArtboard, etc.     │
├─────────────────────────────────────────────────────────────┤
│                    GDExtension Bindings                      │
│              godot-cpp (GDCLASS, ClassDB)                   │
├─────────────────────────────────────────────────────────────┤
│                     rive-runtime (C++)                       │
│     rive::File, rive::ArtboardInstance, StateMachine        │
├─────────────────────────────────────────────────────────────┤
│                    Skia Renderer (CPU)                       │
│              SkSurface, SkCanvas, SkiaRenderer              │
└─────────────────────────────────────────────────────────────┘
```

### Class Hierarchy

#### Node Classes (Scene Tree Integration)

```
Control
└── RiveViewer          # For UI/overlay use cases
    └── RiveViewerBase  # Shared implementation (composition)

Node2D
└── RiveViewer2D        # For 2D game scenes
    └── RiveViewerBase  # Shared implementation (composition)
```

#### Resource Classes (Data Wrappers)

```
Resource
├── RiveFile            # Wraps rive::File
├── RiveArtboard        # Wraps rive::ArtboardInstance
├── RiveScene           # Wraps rive::StateMachineInstance
├── RiveAnimation       # Wraps rive::LinearAnimationInstance
├── RiveInput           # Wraps rive::SMIInput (Bool/Number)
└── RiveListener        # Wraps rive::StateMachineListener
```

### Rendering Pipeline

```
┌──────────────────┐
│   .riv File      │
└────────┬─────────┘
         │ FileAccess::get_file_as_bytes()
         ▼
┌──────────────────┐
│  rive::File      │
│    ::import()    │
└────────┬─────────┘
         │ artboardAt(index)
         ▼
┌──────────────────┐
│ ArtboardInstance │
│   ::advance()    │
│   ::draw()       │
└────────┬─────────┘
         │ draw(SkiaRenderer)
         ▼
┌──────────────────┐
│  Skia Surface    │
│   (CPU Raster)   │
└────────┬─────────┘
         │ peekPixels() → PackedByteArray
         ▼
┌──────────────────┐
│  Godot Image     │
│  ImageTexture    │
└────────┬─────────┘
         │ draw_texture_rect()
         ▼
┌──────────────────┐
│  Godot Canvas    │
│  (RenderingServer)│
└──────────────────┘
```

### File Structure

```
godot-rive/
├── src/
│   ├── register_types.cpp/h     # GDExtension entry point
│   ├── rive_viewer.hpp          # RiveViewer (Control)
│   ├── rive_viewer_2d.hpp       # RiveViewer2D (Node2D)
│   ├── rive_viewer_base.cpp/h   # Shared viewer implementation
│   ├── rive_instance.hpp        # Rive content manager
│   ├── skia_instance.hpp        # Skia renderer wrapper
│   ├── viewer_props.hpp         # Configuration properties
│   ├── rive_exceptions.hpp      # Error handling
│   ├── api/
│   │   ├── rive_file.hpp        # RiveFile wrapper
│   │   ├── rive_artboard.hpp    # RiveArtboard wrapper
│   │   ├── rive_scene.hpp       # RiveScene wrapper
│   │   ├── rive_animation.hpp   # RiveAnimation wrapper
│   │   ├── rive_input.hpp       # RiveInput wrapper
│   │   ├── rive_listener.hpp    # RiveListener wrapper
│   │   └── instances.hpp        # Instance management template
│   └── utils/
│       ├── godot_macros.hpp     # Utility macros
│       ├── memory.hpp           # Smart pointer aliases
│       ├── read_rive_file.hpp   # File I/O helpers
│       ├── out_redirect.hpp     # Output redirection
│       └── types.hpp            # Type conversions
├── build/
│   ├── build.py                 # Build orchestrator
│   ├── SConstruct               # SCons configuration
│   └── SConscript.common        # Common build utilities
├── thirdparty/
│   └── rive-cpp/                # (To be replaced with rive-runtime)
├── godot-cpp/                   # Godot C++ bindings submodule
└── demo/                        # Example Godot project
```

---

## Target Architecture (Post-Migration)

### Goals

1. **Full Feature Parity with rive-unity**
   - ViewModel / Data Binding
   - Rive Events
   - Trigger inputs
   - Scripting (Luau)
   - Audio

2. **GPU-Accelerated Rendering**
   - Option 1: Skia GPU backend
   - Option 2: Rive Renderer → Godot RenderingDevice
   - Fallback: CPU rendering for compatibility

3. **Cross-Platform Support**
   - Linux x86_64 (primary)
   - macOS x86_64 / ARM64
   - Windows x86_64

### Proposed Class Additions

```
Resource (new classes)
├── RiveViewModel       # Data binding context
├── RiveEvent           # Rive event data
└── RiveTrigger         # Trigger input (separate from RiveInput?)

OR extend RiveInput to support:
├── RiveInputBool
├── RiveInputNumber
└── RiveInputTrigger
```

### Proposed Signals

```gdscript
# RiveViewer / RiveViewer2D
signal rive_event_received(event: RiveEvent)
signal state_changed(state_machine: RiveScene, state_name: String)
signal viewmodel_property_changed(property: String, value: Variant)
```

---

## Build System

### Dependencies

| Dependency | Purpose | Source |
|------------|---------|--------|
| godot-cpp | GDExtension bindings | Git submodule |
| rive-runtime | Rive C++ runtime | Git submodule |
| Skia | 2D graphics (bundled with rive-runtime) | Built by rive-runtime |

### Build Commands

```bash
cd build
python build.py --platform=linux --target=debug    # Linux debug
python build.py --platform=macosx --target=release # macOS release
```

### Output Locations

- macOS: `demo/bin/librive.macos.template_*.framework/`
- Linux: `demo/bin/librive.linux.template_*`
- Windows: `demo/bin/librive.windows.template_*.dll`

---

## API Design Principles

1. **Resource-based**: All Rive data types extend Godot `Resource` for editor integration
2. **Reference-counted**: Use `Ref<T>` for automatic memory management
3. **Signal-driven**: Events communicated via Godot signals
4. **Property-reactive**: Changes to exported properties trigger appropriate callbacks
5. **Null-safe**: All wrapper methods check for null internal pointers

---

## Future Considerations

### GPU Rendering Strategy

The CPU-based Skia rendering creates a performance ceiling. Options:

| Strategy | Complexity | Performance |
|----------|------------|-------------|
| Keep CPU Skia | Low | Baseline |
| Skia GPU → Texture upload | Medium | Better |
| Rive Renderer → RenderingDevice | High | Best |

**Recommendation**: Implement GPU rendering as Milestone 4, after API migration is stable.

### Scripting Integration

Luau scripting requires:
1. Luau VM embedded in rive-runtime
2. Script asset loading
3. Execution context management
4. Error handling bridge to Godot

This is the most complex feature and should be tackled last.
