# Development Log

This document tracks major decisions, discoveries, and progress for the godot-rive project.

---

## 2025-04-13: Project Inception

### Context
Forked from [kibble-cabal/godot-rive](https://github.com/kibble-cabal/godot-rive) (commit `1540550`) with the goal of creating a production-ready Rive runtime for Godot 4.3+.

### Key Discoveries

#### 1. rive-cpp is Deprecated
The original project uses `rive-cpp` (commit `7da35c1f`, Feb 2024), which Rive has deprecated in favor of `rive-runtime`. The old repository now redirects to `rive-cpp-legacy`.

**Decision**: We will migrate to `rive-runtime` rather than patching the deprecated library.

#### 2. Existing Experimental Branches
The fork contains two experimental branches:
- `rive-renderer` - Partial work toward using Rive Renderer instead of Skia
- `using_skia_rive_optimized` - Earlier Skia optimization attempts

These may provide useful reference but will not be merged directly.

#### 3. Current Architecture
```
Current Rendering Pipeline:
.riv → rive::File → rive::ArtboardInstance → Skia (CPU) → PackedByteArray → Godot ImageTexture

Wrapper Classes:
- RiveFile → rive::File
- RiveArtboard → rive::ArtboardInstance
- RiveScene → rive::StateMachineInstance
- RiveAnimation → rive::LinearAnimationInstance
- RiveInput → rive::SMIInput (Bool/Number only, no Triggers)
- RiveListener → rive::StateMachineListener
```

#### 4. Missing Features vs. rive-unity
| Feature | godot-rive | rive-unity |
|---------|-----------|------------|
| Triggers | No | Yes |
| Rive Events | No | Yes |
| ViewModel/Data Binding | No | Yes |
| Scripting (Luau) | No | Yes |
| Audio | No | Yes |
| GPU Rendering | No | Yes |

### Environment Setup
- Development: macOS x86_64 (Intel Core i9)
- Target: Linux x86_64
- Godot: 4.3 Stable
- License: MIT

### Repository Structure
```
~/Documents/GitHub/
├── godot-rive/              # Main development (this fork)
└── rive-research/           # Reference repositories
    ├── rive-runtime/        # Official C++ runtime
    └── rive-unity/          # Official Unity runtime
```

### Git Configuration
- `origin` → git@github.com:AxelHolst/godot-rive.git (our fork)
- `upstream` → https://github.com/kibble-cabal/godot-rive.git (original)
- Branches: `main` (stable), `develop` (active development)

---

## Milestone Tracking

### Milestone 0: Environment Stabilization
- [x] Fork repository
- [x] Configure remotes (origin/upstream)
- [x] Create branch structure (main/develop)
- [x] Clone research repositories
- [x] Create governance files
- [ ] Audit rive-runtime API structure
- [ ] Verify build system works
- [ ] Update godot-cpp submodule

---

## Research Notes

### rive-runtime Structure (To Be Documented)
Location: `~/Documents/GitHub/rive-research/rive-runtime/`

Key directories to examine:
- `/include/rive/` - Core API headers
- `/renderer/` - Rendering backend
- `/scripting/` - Luau integration
- `/data_binding/` - ViewModel system (if present)

### rive-unity Reference (To Be Documented)
Location: `~/Documents/GitHub/rive-research/rive-unity/`

Key files to study:
- `/package/Runtime/` - C# wrapper implementations
- ViewModel handling patterns
- Event system implementation
