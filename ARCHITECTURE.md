# Architecture Document

This document describes the technical architecture of the godot-rive GDExtension.

**Last Updated**: April 18, 2026 (M5 GPU Infrastructure Complete)

---

## Overview

godot-rive is a GDExtension that integrates the [Rive](https://rive.app) animation runtime into Godot 4.6+. It provides Godot nodes and resources that wrap the native `rive-runtime` C++ library.

## Current Architecture (Post-Migration)

> **Status**: Migration from `rive-cpp` to `rive-runtime` is complete.
> The extension compiles and renders Rive animations in Godot.

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
├────────────────────────┬────────────────────────────────────┤
│   Skia Renderer (CPU)  │   Rive PLS Renderer (GPU) [M5]     │
│   SkSurface, SkCanvas  │   RenderContextVulkanImpl          │
│       (macOS)          │       (Linux, Windows)             │
└────────────────────────┴────────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────────┐
│                 src/gpu/ Layer (M5)                          │
│  RiveGPUBridge: VkDevice extraction from Godot              │
│  RiveGPURenderer: Command buffers, render targets           │
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
│   ├── gpu/                     # GPU Rendering (M5)
│   │   ├── rive_gpu_bridge.hpp/cpp    # VkDevice extraction from Godot
│   │   └── rive_gpu_renderer.hpp/cpp  # Rive PLS Renderer wrapper
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
│   └── rive-runtime/            # Rive C++ runtime (migrated from rive-cpp)
├── godot-cpp/                   # Godot C++ bindings submodule
└── demo/                        # Example Godot project
```

---

## Feature Gap vs rive-unity (Research April 2026)

Based on detailed comparison with `rive-unity`, the following gaps were identified:

### Currently Implemented ✅

| Feature | Implementation |
|---------|---------------|
| File loading | `RiveFile::Load()` → `rcp<rive::File>` |
| Artboard rendering | CPU Skia via `SkiaRenderer` |
| State machine advance | `scene->advanceAndApply(delta)` |
| Bool/Number inputs | `RiveInput` with `set_value(Variant)` |
| Trigger inputs | `RiveInput::fire()` method |
| Mouse interaction | `pointerMove/Down/Up` via `RiveScene` |
| Fit/Alignment | `rive::computeAlignment()` |
| Rive Events | `rive_event` signal with `RiveEvent` resource |
| Nested artboard inputs | `set_input("Path/Name", value)` path syntax |
| Cross-platform builds | macOS local, Linux CI |

### Missing Features (Prioritized) 🔴

| Feature | rive-unity Reference | rive-runtime API | Priority |
|---------|---------------------|------------------|----------|
| **GPU Rendering** | Metal/Vulkan backends | RenderingDevice integration | M5 |
| **ViewModel** | `ViewModelInstance` (800+ LOC) | `ViewModelInstance::propertyValue()` | M6 |
| **Data Binding** | `BindViewModelInstance()` | `bindViewModelInstanceToStateMachine()` | M6 |
| **Luau Scripting** | `WITH_RIVE_TOOLS` flag | `rive_luau.hpp` | M7 |
| **Audio** | `AudioEngine` class | `rive/audio/` headers | M8 |

---

## Planned Architecture Changes

### M3: Event System

```
New class: RiveEvent (Resource)
├── name: String                    # event->name()
├── type: int                       # event type enum
├── seconds_delay: float            # EventReport::secondsDelay()
└── properties: Dictionary          # custom properties

Modified: RiveViewerBase
├── signal rive_event(event: RiveEvent)
└── poll_events() in advance loop:
    for i in range(scene->reportedEventCount()):
        emit_signal("rive_event", RiveEvent::from_report(scene->reportedEventAt(i)))
```

### M3: Nested Artboard Inputs

```
Modified: RiveArtboard
├── set_bool_at_path(name: String, value: bool, path: String)
├── set_number_at_path(name: String, value: float, path: String)
├── fire_trigger_at_path(name: String, path: String)
└── get_input_at_path(name: String, path: String) -> RiveInput
```

### M6: ViewModel System

```
New class: RiveViewModel (Resource)
├── name: String
├── property_names: PackedStringArray
└── create_instance() -> RiveViewModelInstance

New class: RiveViewModelInstance (RefCounted)
├── get_property(path: String) -> Variant
├── set_property(path: String, value: Variant)
├── bind_to_artboard(artboard: RiveArtboard)
└── bind_to_scene(scene: RiveScene)
```

---

## Target Architecture Goals

1. **Full Feature Parity with rive-unity**
   - ✅ Trigger inputs (implemented)
   - ✅ Rive Events (M3 complete)
   - ✅ Nested artboard inputs (M3 complete)
   - 🔲 ViewModel / Data Binding (M6)
   - 🔲 Scripting (Luau) (M7)
   - 🔲 Audio (M8)

2. **GPU-Accelerated Rendering** (M5 Infrastructure Complete)
   - ✅ Vulkan pipeline implemented
   - ✅ VkDevice extraction from Godot
   - ✅ Command buffer management
   - 🔲 macOS Metal backend (M5b - planned)
   - ✅ CPU fallback for compatibility

3. **Cross-Platform Support** (M4 Complete, M5 Partial)
   - ✅ macOS x86_64 (Skia CPU)
   - ✅ Linux x86_64 (Rive GPU)
   - 🔲 macOS ARM64
   - 🔲 Windows x86_64

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
# Local macOS build
cd build && scons platform=macos target=template_debug arch=x86_64

# Linux build (via CI)
# See .github/workflows/build-linux.yml
# Artifacts available at GitHub Actions

# Manual Linux build (requires rive-runtime + Skia pre-built)
cd build && scons platform=linux target=template_debug arch=x86_64
```

### CI/CD Pipeline (Linux)

```
┌─────────────────┐  ┌─────────────────┐
│ build-rive-     │  │ build-skia      │
│ runtime (5min)  │  │ (cached/20min)  │
└────────┬────────┘  └────────┬────────┘
         │                    │
         └────────┬───────────┘
                  ▼
         ┌────────────────┐
         │ build-         │
         │ gdextension    │
         │ (7min)         │
         └────────┬───────┘
                  ▼
         ┌────────────────┐
         │ librive.linux  │
         │ .so artifact   │
         └────────────────┘
```

### Output Locations

- macOS: `demo/bin/librive.macos.template_*.framework/`
- Linux: `demo/bin/librive.linux.template_*.so`
- Windows: `demo/bin/librive.windows.template_*.dll`

---

## API Design Principles

1. **Resource-based**: All Rive data types extend Godot `Resource` for editor integration
2. **Reference-counted**: Use `Ref<T>` for automatic memory management
3. **Signal-driven**: Events communicated via Godot signals
4. **Property-reactive**: Changes to exported properties trigger appropriate callbacks
5. **Null-safe**: All wrapper methods check for null internal pointers

---

## GPU Rendering Architecture (M5)

### Overview

The GPU rendering infrastructure extracts Vulkan handles from Godot's RenderingDevice and creates a bridge to Rive's PLS (Pixel Local Storage) Renderer.

### Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Godot RenderingServer                     │
│                    └── RenderingDevice                       │
│                         ├── VkInstance                       │
│                         ├── VkPhysicalDevice                 │
│                         └── VkDevice                         │
└────────────────────────────┬────────────────────────────────┘
                             │ get_driver_resource()
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                      RiveGPUBridge                           │
│   Extracts: VkInstance, VkPhysicalDevice, VkDevice, VkQueue │
│   Detects: Backend type (Vulkan/OpenGL)                     │
│   Creates: Godot RID textures for sharing                   │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                     RiveGPURenderer                          │
│   ├── RenderContextVulkanImpl (Rive's PLS context)          │
│   ├── RenderTargetVulkanImpl (render target wrapper)        │
│   ├── VMA-managed Texture2D (render image)                  │
│   ├── VkCommandPool / VkCommandBuffer (frame recording)     │
│   └── VkFence (frame synchronization)                       │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                      Frame Flow                              │
│   1. beginFrame() → vkWaitForFences, begin command buffer   │
│   2. draw(artboard) → RiveRenderer draws to render target   │
│   3. endFrame() → flush(), vkQueueSubmit                    │
│   4. syncTextureToGodot() → copy to Godot texture (TODO)    │
└─────────────────────────────────────────────────────────────┘
```

### Platform-Specific Behavior

| Platform | GPU Backend | Renderer | Status |
|----------|-------------|----------|--------|
| Linux | Native Vulkan | RiveGPURenderer | ✅ Experimental |
| Windows | Native Vulkan | RiveGPURenderer | ⏳ Untested |
| macOS | MoltenVK | **Disabled** | ❌ VMA conflict |

### Known Limitation: VMA/MoltenVK Incompatibility

On macOS, Rive's VMA (Vulkan Memory Allocator) allocates memory with property flags that MoltenVK cannot translate to Metal storage modes. This causes crashes in `vkQueueSubmit`.

**Root Cause**:
```
vkQueueSubmit
└── Metal copyFromBuffer:sourceOffset:...
    └── MVKPhysicalDevice::getMTLStorageModeFromVkMemoryPropertyFlags()
        └── CRASH (invalid memory property flags)
```

**Recommended Solution**: Implement Rive's native Metal backend (`RenderContextMetalImpl`) for macOS, bypassing MoltenVK entirely.

### Debug Tools

Vulkan validation layer support is built-in but disabled by default (Godot's VkInstance lacks the required extension). To enable:

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./godot --path project
```

The `RiveGPURenderer::debugCallback()` will print detailed validation errors to the Godot console.

---

## Future Considerations

### Scripting Integration

Luau scripting requires:
1. Luau VM embedded in rive-runtime
2. Script asset loading
3. Execution context management
4. Error handling bridge to Godot

This is the most complex feature and should be tackled last.
