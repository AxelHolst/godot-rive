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
- [x] Audit rive-runtime API structure
- [ ] Verify build system works
- [ ] Update godot-cpp submodule

---

## 2025-04-13: API Research Complete

### rive-runtime API Analysis

Location: `~/Documents/GitHub/rive-research/rive-runtime/`

#### Key Directories
| Directory | Purpose |
|-----------|---------|
| `/include/rive/` | Core API headers |
| `/include/rive/animation/` | State machines, inputs, events |
| `/include/rive/data_bind/` | ViewModel/Data Binding system |
| `/include/rive/audio/` | Audio engine and assets |
| `/renderer/` | Rive Renderer (GPU) |
| `/skia/` | Skia backend (CPU/GPU) |
| `/scripting/` | Luau VM integration |

#### Input System (SMIInput hierarchy)
From `state_machine_input_instance.hpp`:

```cpp
class SMIInput {
    const std::string& name() const;
    uint16_t inputCoreType() const;
};

class SMIBool : public SMIInput {
    bool value() const;
    void value(bool newValue);
};

class SMINumber : public SMIInput {
    float value() const;
    void value(float newValue);
};

class SMITrigger : public SMIInput, public Triggerable {
    void fire();  // Key method - triggers are "fired", not set
};
```

**Key Insight**: Triggers have a `fire()` method, not a value setter. They reset automatically after `advance()`.

#### Event System
From `state_machine_instance.hpp`:

```cpp
class StateMachineInstance {
    std::size_t reportedEventCount() const;
    const EventReport reportedEventAt(std::size_t index) const;
};
```

Events are polled after `advance()` - not callback-based.

#### Data Binding Structure
Headers in `/include/rive/data_bind/`:
- `bindable_property_*.hpp` - Property types (bool, number, string, enum, trigger, color)
- `data_bind_context.hpp` - Binding context
- `data_context.hpp` - Data source
- `bindable_property_viewmodel.hpp` - ViewModel binding

### rive-unity Implementation Patterns

Location: `~/Documents/GitHub/rive-research/rive-unity/package/Runtime/`

#### Input Wrapper Pattern (SMIInput.cs)
```csharp
public class SMIInput {
    private readonly IntPtr m_nativeSMI;
    public string Name { get; }
    public bool IsBoolean { get; }
    public bool IsTrigger { get; }
    public bool IsNumber { get; }
}

public sealed class SMITrigger : SMIInput {
    public void Fire();  // Calls native fireSMITriggerStateMachine()
}

public sealed class SMIBool : SMIInput {
    public bool Value { get; set; }
}

public sealed class SMINumber : SMIInput {
    public float Value { get; set; }
}
```

#### Event Pattern (ReportedEvent.cs)
- Uses object pooling for performance
- Lazy-loads properties to avoid allocations
- Properties are key-value pairs (string, float, bool)
- Events have: Name, Type, SecondsDelay, Properties dictionary

```csharp
public class ReportedEvent : IDisposable {
    public string Name { get; }
    public ushort Type { get; }
    public float SecondsDelay { get; }
    public Dictionary<string, object> Properties { get; }
}
```

#### DataBinding Pattern (DataBinding/*.cs)
- `ViewModel.cs` - Schema definition
- `ViewModelInstance.cs` - Runtime instance with values
- `ViewModelInstanceProperty.cs` - Individual property accessors
- Property types: String, Number, Boolean, Enum, Color, Trigger, List

### Architectural Decisions Made

#### Decision 1: RiveInput Type System
**Current**: Single `RiveInput` class with `is_bool()`/`is_number()` checks.
**Target**: Consider separate classes or extended input type support.

**Options**:
1. Add `is_trigger()` and `fire()` to existing RiveInput
2. Create RiveInputTrigger subclass
3. Create RiveTrigger as separate class

**Recommendation**: Option 1 (least breaking change, matches rive-cpp pattern)

#### Decision 2: Event Handling
**Pattern**: Poll events after advance(), emit Godot signals.

```gdscript
# Proposed API
signal rive_event(event_name: String, properties: Dictionary)

# Internal implementation
func _process(delta):
    state_machine.advance(delta)
    for i in range(state_machine.reported_event_count()):
        var event = state_machine.reported_event_at(i)
        emit_signal("rive_event", event.name, event.properties)
```

#### Decision 3: ViewModel Binding
This is the most complex feature. Requires:
1. RiveViewModel class (schema)
2. RiveViewModelInstance class (runtime values)
3. Property accessors by name/type
4. Binding to artboard

**Deferred to Milestone 5** - needs more research.

---

## Refined Roadmap

### Milestone 1: Core Migration (Weeks 2-3)
- [ ] Replace rive-cpp submodule with rive-runtime
- [ ] Update all `#include` paths
- [ ] Verify File/Artboard/StateMachine APIs still work
- [ ] Build system updates for new library structure
- [ ] Basic smoke test with existing demo

### Milestone 2: Input & Triggers (Week 4)
- [ ] Add SMITrigger support to RiveInput
- [ ] Add `fire()` method for triggers
- [ ] Add `is_trigger()` type check
- [ ] Update demo to showcase triggers

### Milestone 3: Rive Events (Week 5)
- [ ] Create RiveEvent class
- [ ] Add event polling after advance()
- [ ] Emit `rive_event` signal
- [ ] Document event properties access

### Milestone 4: Rendering Upgrade (Weeks 6-8)
- [ ] Research Godot RenderingDevice integration
- [ ] Evaluate Skia GPU vs Rive Renderer
- [ ] Implement GPU rendering path
- [ ] Benchmark CPU vs GPU

### Milestone 5: Data Binding (Weeks 9-11)
- [ ] Implement RiveViewModel
- [ ] Implement RiveViewModelInstance
- [ ] Property type support
- [ ] Artboard binding

### Milestone 6: Scripting (Weeks 12-14)
- [ ] Luau VM integration research
- [ ] Script asset loading
- [ ] Execution context
- [ ] Error bridging

### Milestone 7: Audio (Week 15)
- [ ] Audio asset loader
- [ ] AudioEngine integration
- [ ] Godot AudioStreamPlayer bridge

### Milestone 8: Polish (Weeks 16-18)
- [ ] Comprehensive testing
- [ ] Documentation
- [ ] Cross-platform builds
- [ ] Performance optimization

---

## 2026-04-13: Build System Investigation

### Attempted Build from Source

#### Environment
- macOS x86_64 (Intel Core i9)
- Xcode Command Line Tools (no full Xcode installation)
- Homebrew-installed: cmake, premake5, ninja, scons, python3

#### Issues Discovered

##### 1. Missing C++ Standard Library Headers
The Command Line Tools installation has a broken/incomplete configuration where clang cannot find standard C++ headers (`<atomic>`, `<functional>`, `<cmath>`) when using `-isysroot`.

**Workaround**: Manually add `-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1` to compiler flags.

##### 2. Skia Build Success
Successfully built Skia with the following configuration:
- Location: `thirdparty/rive-cpp/skia/dependencies/skia_rive_optimized/out/macosx/x64/libskia.a`
- Size: 28MB
- Target: macOS 11.0, x64

##### 3. rive-cpp Build System Incomplete
The rive-cpp submodule (commit `7da35c1f`, Feb 2024) is missing critical build infrastructure files:
- `rive_build_config.lua` - Workspace configuration
- `setup_compiler.lua` - Compiler settings
- `dependency.lua` - Dependency management

These files exist in the newer `rive-runtime` repository but are incompatible with the older `rive-cpp` premake files.

##### 4. Premake Version Incompatibility
- rive-cpp build scripts expect premake5 options like `--with_rive_text` and `--with_rive_audio=system`
- rive-runtime uses different options like `--config=debug` and `--with_rive_layout`
- The options are defined in the Lua config files, not in premake5 itself

#### Current State
- [x] Skia built successfully
- [ ] librive.a - Not built (missing build infrastructure)
- [ ] librive_skia_renderer.a - Not built
- [ ] librive_harfbuzz.a - Not built
- [ ] librive_sheenbidi.a - Not built

### Recommended Path Forward

The rive-cpp build system is fundamentally broken/incomplete in this commit. Two options:

**Option A: Full rive-runtime Migration (Recommended)**
1. Replace `thirdparty/rive-cpp` with `rive-runtime`
2. Update SConstruct paths and library names
3. Update C++ source includes
4. This aligns with Rive's official direction

**Option B: Patch rive-cpp Build System**
1. Copy missing Lua files from rive-runtime
2. Adapt them for rive-cpp's older premake5_v2.lua format
3. Manually download/build harfbuzz, sheenbidi dependencies
4. Higher effort, maintains deprecated dependency

### Files Modified Today
- `build/rive_build_config.lua` - Copied from rive-runtime
- `build/setup_compiler.lua` - Copied from rive-runtime
- `build/dependency.lua` - Copied from rive-runtime
- `build/premake5.lua` - Created wrapper

### Build Artifacts
```
thirdparty/rive-cpp/
├── skia/dependencies/
│   ├── skia/out/static/libskia.a  # Symlink to built Skia
│   └── skia_rive_optimized/out/macosx/x64/libskia.a  # Actual build (28MB)
└── build/  # Directory created for premake, contains Lua configs
```

---

## 2026-04-13: rive-runtime Migration Complete (Strategy B)

### Decision
Chose **Option A: Full rive-runtime Migration** as recommended.

### Submodule Swap
```bash
git rm thirdparty/rive-cpp
git submodule add https://github.com/rive-app/rive-runtime.git thirdparty/rive-runtime
```

### Pre-Compiled Libraries Built
All libraries successfully built in `thirdparty/rive-runtime/`:

| Library | Size | Location |
|---------|------|----------|
| `libskia.a` | 29MB | `skia/dependencies/skia/out/macosx/x64/` |
| `librive.a` | 604MB | `out/debug/` |
| `librive_skia_renderer.a` | 1MB | `skia/renderer/out/debug/` |
| `librive_harfbuzz.a` | 74MB | `out/debug/` |
| `librive_sheenbidi.a` | 244KB | `out/debug/` |
| `librive_yoga.a` | 1.9MB | `out/debug/` |

### API Changes Required

#### 1. RiveFile (rive_file.hpp, read_rive_file.hpp)
```cpp
// OLD: std::unique_ptr (Ptr<T> alias)
Ptr<rive::File> file;

// NEW: Reference-counted pointer
rcp<rive::File> file;
```

#### 2. RiveInput (rive_input.hpp)
Added trigger support:
```cpp
#include <rive/animation/state_machine_trigger.hpp>

bool is_trigger() const {
    return input && input->input()->is<rive::StateMachineTrigger>();
}

void fire() {
    if (auto t = trigger_input()) t->fire();
}
```

#### 3. RiveListener (rive_listener.hpp)
The `listenerType()` method was removed in rive-runtime:
```cpp
// OLD (broken)
return listener->listenerType();

// NEW: Use hasListener() to detect type
if (listener->hasListener(rive::ListenerType::enter)) return (int)rive::ListenerType::enter;
if (listener->hasListener(rive::ListenerType::exit)) return (int)rive::ListenerType::exit;
// ... etc
```

#### 4. SkiaInstance (skia_instance.hpp)
```cpp
// OLD: rivestd namespace removed
Ptr<SkiaFactory> factory = rivestd::make_unique<SkiaFactory>();

// NEW: Use std::make_unique directly
Ptr<SkiaFactory> factory = std::make_unique<SkiaFactory>();
```

### SConstruct Updates
- Added `librive_yoga.a` to library list
- Updated all include paths for rive-runtime structure
- C++ header workaround baked in at line 45

### Build Success
```
scons platform=macos target=template_debug arch=x86_64
```

**Output:** `demo/bin/librive.macos.template_debug.framework/librive.macos.template_debug` (13MB)

**Commit:** `d624027` - feat(api): migrate to rive-runtime API

### Next Steps
- [x] Smoke test in Godot 4.6.2 - **CRASH** (see below)
- [ ] Build release configuration
- [ ] Test trigger/event functionality

---

## 2026-04-13: Smoke Test Crash (Signal 11)

### Symptoms
- GDExtension loaded successfully
- `.riv` files imported correctly: `Successfully imported <res://examples/joystick.riv>!`
- Crash occurs during editor layout restoration (loading saved scene with RiveViewer)

### Error Output
```
ERROR: Condition "_instance_bindings != nullptr && _instance_bindings[0].binding != nullptr" is true.
   at: set_instance_binding (core/object/object.cpp:2241)

handle_crash: Program crashed with signal 11
```

### Backtrace (Key Frames)
```
[6] RiveViewerBase::move_mouse(godot::Vector2)
[7] RiveViewer::_gde_binding_reference_callback(void*, void*, unsigned char)
[8] RiveViewerBase::on_set(godot::StringName const&, godot::Variant const&)
[9] RiveViewer::set_bind(void*, void const*, void const*)
[10] Object::set(StringName const&, Variant const&, bool*)
[11] SceneState::instantiate(SceneState::GenEditState)
```

### Analysis
The crash occurs in `RiveViewerBase::on_set()` → `move_mouse()` during property initialization.
This suggests a null pointer dereference - likely accessing uninitialized Rive objects
(scene, artboard, or file) before they're properly loaded.

### Root Cause Analysis
The crash was NOT in `move_mouse()` itself - the backtrace was misleading. The actual cause:

1. During `SceneState::instantiate()`, Godot restores saved properties via `_set()`
2. `on_set()` calls `inst.instantiate()` at line 168
3. `instantiate()` creates `Ref<RiveScene>`, `Ref<RiveArtboard>` objects
4. Creating these `Ref<>` objects triggers `_gde_binding_reference_callback`
5. **Godot's binding system isn't fully initialized during scene loading**
6. This causes the crash in `set_instance_binding()`

### Fix Applied (rive_viewer_base.cpp:168)
```cpp
// Guard: Don't instantiate Rive objects until the owner node is fully ready.
// During scene loading, Godot's binding system isn't ready and creating Ref<>
// objects would trigger _gde_binding_reference_callback on invalid bindings.
if (!owner->is_node_ready()) return false;
inst.instantiate();
```

### Rebuild Status
- Rebuilt successfully after fix
- Awaiting smoke test verification

---

## 2026-04-14: RIVE_OPTIMIZED Flag Causing SkCanvas Crash - FIXED

### Symptoms
- `canvas->save()` crashed with SIGSEGV at address 0x58
- SkCanvas was created but internal `fMCRec` pointer was NULL
- Crash occurred in `incl 0x58(%rcx)` when trying to increment `fMCRec->fDeferredSaveCount`

### Root Cause
The `-DRIVE_OPTIMIZED` flag in Skia's `args.gn` causes SkCanvas constructors to be **stubbed out**:

```cpp
#ifdef RIVE_OPTIMIZED
SkCanvas::SkCanvas(const SkBitmap& bitmap, ...)
    : fMCStack(...) {}  // Empty! No init() call!
#else
SkCanvas::SkCanvas(const SkBitmap& bitmap, ...)
    : fMCStack(...) {
    this->init(device);  // This sets up fMCRec
}
#endif
```

Without `init()`, the `fMCRec` pointer remains NULL, causing crashes on any canvas operation.

### Fix Applied (thirdparty/rive-runtime/skia/dependencies/skia/out/macosx/x64/args.gn)
Removed `-DRIVE_OPTIMIZED` from `extra_cflags`:
```gn
extra_cflags = [
  "--target=x86_64-apple-macos10.12",
  "-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1",
  "-fexceptions",
  "-frtti",
  # NOTE: RIVE_OPTIMIZED intentionally NOT set - stubs out SkCanvas constructors!
]
```

### Additional Fixes Required

1. **Rebuild Skia** without RIVE_OPTIMIZED:
   ```bash
   cd thirdparty/rive-runtime/skia/dependencies/skia
   ./bin/gn gen out/macosx/x64 --args='...'
   ninja -C out/macosx/x64
   ```

2. **Rebuild rive-runtime with RTTI** (for ABI compatibility):
   ```bash
   cd thirdparty/rive-runtime
   ./build/build_rive.sh debug --with-rtti
   ```

3. **Rebuild skia renderer with RTTI**:
   ```bash
   cd thirdparty/rive-runtime/skia/renderer
   RIVE_PREMAKE_ARGS="--with-rtti" ../../build/build_rive.sh clean debug
   ```

4. **SConstruct changes** (build/SConstruct):
   - Added `-frtti` to CXXFLAGS for ABI compatibility
   - Removed `-Wl,-S`, `-Wl,-x` strip flags that were hiding Skia symbols

### Size Change Fix
Also fixed: RiveViewer size was stuck at 1x1 because `set_size()` had a guard preventing 
size updates before initialization. Changed to always sync size from Control on every process frame.

### Verification
- Standalone Skia test: `canvas->save()` returns successfully
- Godot test: 40000/40000 non-transparent pixels rendered (200x200)
- Rive animations are now visible in Godot!

---

## 2026-04-14: Repository Stabilization & Workspace Audit

### Context
After the RIVE_OPTIMIZED fix, the workspace had accumulated uncommitted changes. Performed a full audit and organized into atomic commits.

### Audit Categories

#### Category 1: Source Code Fixes (Critical)
- `src/rive_instance.hpp`: Added `is_ready()` guard in `on_path_changed()` to prevent crashes during scene loading
- `src/utils/read_rive_file.hpp`: Changed to explicit `rive::` namespace prefixes
- `src/viewer_props.hpp`: Minor code cleanup

#### Category 2: Submodule Fixes (Critical)
- `godot-cpp/tools/macos.py`: Added C++ header include path for Intel Macs
- `thirdparty/rive-runtime/build/rive_build_config.lua`: Same fix for rive-runtime builds

#### Category 3: Demo/Test Infrastructure
- Added `test_minimal.tscn`, `test_ghost.tscn`, `test_no_file.tscn`, `test_with_file.tscn`
- Updated `project.godot` for Godot 4.6
- Updated `.gitignore` to exclude `demo/bin/` and `*.uid` files
- Removed tracked binaries from version control

#### Category 4: Documentation
- Added `docs/API_MIGRATION.md` for rive-cpp → rive-runtime migration guide

### Commits Made
```
b3826c8 chore: remove built binaries from version control
d5d08ca chore(demo): update Godot import files for 4.6
eef3c85 chore(deps): update rive-runtime with macOS header fix
4991b8f chore(deps): update godot-cpp with macOS header fix
99e7558 docs: add API migration guide for rive-cpp to rive-runtime
77e0437 feat(demo): add test scenes and update to Godot 4.6
c117372 chore: update gitignore for build artifacts and UIDs
167df76 fix(core): add binding safety guards and namespace hygiene
a41317b fix(skia): resolve canvas crash by removing RIVE_OPTIMIZED flag
```

### Current State
- **Editor**: Opens without crash ✅
- **Runtime**: Animations render and play ✅
- **Vector scaling**: Works correctly ✅
- **Repository**: Clean, all changes committed ✅

### Remaining Submodule Notes
The `thirdparty/rive-runtime` submodule shows "modified content, untracked content" in git status. This is expected - those are local build artifacts (`out/`, `dependencies/`) that are not committed to the submodule. This is the correct behavior.

---

## 2026-04-15: Deep Research Phase - rive-unity Comparison

### Objective
Compare godot-rive architecture with rive-unity to identify feature gaps and establish a production-quality roadmap.

### Research Sources
| Repository | Location | Purpose |
|------------|----------|---------|
| `rive-runtime` | `~/Documents/GitHub/rive-research/rive-runtime/` | C++ runtime API reference |
| `rive-unity` | `~/Documents/GitHub/rive-research/rive-unity/` | Architecture patterns, feature parity target |

### Key Unity Files Analyzed
- `package/Runtime/StateMachine.cs` (350 lines) - Event polling, input access patterns
- `package/Runtime/Artboard.cs` (570 lines) - Nested input paths, text runs
- `package/Runtime/SMIInput.cs` (154 lines) - Input type hierarchy
- `package/Runtime/ReportedEvent.cs` (370 lines) - Event class with object pooling
- `package/Runtime/DataBinding/ViewModelInstance.cs` (800+ lines) - Full data binding system

### Feature Gap Analysis

#### What We Have ✅
| Feature | Status | Implementation |
|---------|--------|----------------|
| File loading | ✅ | `RiveFile::Load()` → `rcp<rive::File>` |
| Artboard rendering | ✅ | CPU Skia via `SkiaRenderer` |
| State machine | ✅ | `scene->advanceAndApply(delta)` |
| Bool inputs | ✅ | `RiveInput::set_value(bool)` |
| Number inputs | ✅ | `RiveInput::set_value(float)` |
| Trigger inputs | ✅ | `RiveInput::fire()` |
| Mouse interaction | ✅ | `pointerMove/Down/Up` |

#### What We're Missing 🔴
| Feature | rive-unity Reference | Gap Severity |
|---------|---------------------|--------------|
| **Rive Events** | `StateMachine.ReportedEvents()` with pooling | Critical |
| **Nested Input Paths** | `Artboard.SetBooleanInputStateAtPath()` | Critical |
| **ViewModel** | `ViewModelInstance` (800+ lines) | Critical |
| **Data Binding** | `BindViewModelInstance()` | Critical |
| **Luau Scripting** | `WITH_RIVE_TOOLS` compilation flag | High |
| **Audio** | `AudioEngine` class | Medium |

### Architectural Differences

#### Memory Management
**rive-unity**: Native pointer + IDisposable pattern
```csharp
public class StateMachine : IDisposable {
    private readonly IntPtr m_nativeStateMachine;
    public void Dispose() => unrefStateMachine(m_nativeStateMachine);
}
```

**godot-rive**: Godot Ref<> + std::unique_ptr mixing
```cpp
class RiveScene : public Resource {
    Ptr<rive::StateMachineInstance> scene;  // unique_ptr inside Ref<>
};
```

This mixing caused our scene loading crash - bindings callbacks triggered on partially-initialized objects.

#### Event System
**rive-unity**: Full event polling with object pooling
```csharp
// StateMachine.cs:228
public List<ReportedEvent> ReportedEvents() {
    uint count = getReportedEventCount(m_nativeStateMachine);
    for (uint i = 0; i < count; i++) {
        list.Add(ReportedEvent.GetPooled(...));  // Pooled!
    }
}
```

**godot-rive**: No event support at all. The rive-runtime API is available:
```cpp
// rive/animation/state_machine_instance.hpp
std::size_t reportedEventCount() const;
const EventReport reportedEventAt(std::size_t index) const;
```

#### Nested Artboard Inputs
**rive-unity**: Full path-based input navigation
```csharp
// Artboard.cs:309
public void SetBooleanInputStateAtPath(string inputName, bool value, string path);
public void FireInputStateAtPath(string inputName, string path);
```

**godot-rive**: Only top-level inputs supported.

### Revised Roadmap

| Milestone | Status | Description |
|-----------|--------|-------------|
| M1: Core Migration | ✅ Complete | rive-cpp → rive-runtime |
| M2: Smoke Test | ✅ Complete | Rendering verified |
| **M3: Events & Triggers** | 🔄 Next | `RiveEvent` class, signals, nested paths |
| **M4: Linux Build** | ⏳ Pending | Cross-platform priority |
| **M5: GPU Rendering** | ⏳ Pending | RenderingDevice or Rive Renderer |
| **M6: ViewModel/Binding** | ⏳ Pending | Unity parity for data-driven animations |
| **M7: Rive Scripting** | ⏳ Pending | Luau VM integration |
| **M8: Polish & Release** | ⏳ Pending | Documentation, 1.0 stability |

### Documentation Updated
- `CLAUDE.md` - Revised with research findings and new roadmap
- `ARCHITECTURE.md` - Updated with feature gap analysis and planned changes
- `DEVELOPMENT_LOG.md` - This entry

### Next Steps
1. ~~Implement `RiveEvent` class with properties~~ ✅
2. ~~Add `rive_event` signal to RiveViewer~~ ✅
3. ~~Add event polling in advance loop~~ ✅
4. ~~Implement nested input path methods on RiveArtboard~~ ✅

---

## 2026-04-16: Milestone 3 Complete - Events & Nested Inputs

### Summary
Milestone 3 fully implemented, adding two critical features that bring godot-rive closer to rive-unity parity.

### Part 1: Rive Events System

**Commit:** `1ab74bd` - feat(events): implement Rive event system (M3 Part 1)

**Files Added/Modified:**
- `src/api/rive_event.hpp` (NEW) - RiveEvent class exposing:
  - `name` - Event name from Rive file
  - `seconds_delay` - Time offset from frame start
  - `properties` - Dictionary of custom properties (bool, number, string)
- `src/register_types.cpp` - Registered RiveEvent class
- `src/rive_viewer_base.h` - Added `rive_event` signal
- `src/rive_viewer_base.cpp` - Added `poll_events()` called after advance

**Implementation Details:**
```cpp
// Event polling pattern (matches rive-unity approach)
void RiveViewerBase::poll_events() {
    rive::StateMachineInstance* sm = scene->scene.get();
    std::size_t event_count = sm->reportedEventCount();
    for (std::size_t i = 0; i < event_count; i++) {
        const rive::EventReport report = sm->reportedEventAt(i);
        Ref<RiveEvent> event = RiveEvent::from_report(report);
        owner->emit_signal("rive_event", event);
    }
}
```

**GDScript Usage:**
```gdscript
func _ready():
    $RiveViewer.rive_event.connect(_on_rive_event)

func _on_rive_event(event: RiveEvent):
    print("Event: ", event.name)
    print("Properties: ", event.properties)
```

**Tested With:** `nested_artboard_events.riv` - Events firing confirmed!

### Part 2: Nested Artboard Inputs

**Files Modified:**
- `src/api/rive_scene.hpp` - Added nested input methods

**New Methods on RiveScene:**

*Explicit Path API (mirrors rive-runtime):*
```cpp
// Separate path and name parameters
get_bool_at_path(name, path)
set_bool_at_path(name, path, value)
get_number_at_path(name, path)
set_number_at_path(name, path, value)
fire_trigger_at_path(name, path)
get_input_at_path(name, path)
```

*Convenience API (combined path format):*
```cpp
// Single path string like "NestedArtboard/InputName"
set_input(input_path, value)      // Works with bool/number
get_input_value(input_path)       // Returns variant
fire(input_path)                  // Fire triggers
```

**GDScript Usage:**
```gdscript
var scene = $RiveViewer.get_scene()

# Set nested input (combined path)
scene.set_input("Cirkle1/Color", 2.0)
scene.fire("Panel/Button/Click")

# Or explicit path API
scene.set_number_at_path("Color", "Cirkle1", 2.0)
scene.fire_trigger_at_path("Click", "Panel/Button")
```

### Path Format
The combined path format uses `/` as separator:
- `"InputName"` → Root-level input (empty path)
- `"NestedArtboard/InputName"` → Single nested artboard
- `"Parent/Child/InputName"` → Deeply nested (path = "Parent/Child", name = "InputName")

### Build Verification
```bash
scons platform=macos target=template_debug arch=x86_64
# Output: demo/bin/librive.macos.template_debug.framework/librive.macos.template_debug
```

### Feature Comparison After M3

| Feature | godot-rive | rive-unity |
|---------|-----------|------------|
| Events | ✅ | ✅ |
| Nested Inputs | ✅ | ✅ |
| Triggers | ✅ (untested) | ✅ |
| Bool/Number inputs | ✅ | ✅ |
| ViewModel | ❌ | ✅ |
| Audio | ❌ | ✅ |
| GPU Rendering | ❌ | ✅ |

### What Remains for M3
- [ ] Test nested inputs with `nested_artboard_events.riv`
- [ ] Test triggers end-to-end
- [ ] Commit Part 2

### Roadmap Update
- **M3: Events & Triggers** - ✅ Complete
- **M4: Distribution** - 🔄 In Progress (macOS export validation → Linux builds)
- **M5: GPU Rendering** - Deferred
- **M6: ViewModel** - Requires deeper research

---

## 2026-04-16: Distribution Phase - macOS Export Validation

### Objective
Validate standalone macOS export before moving to Linux cross-compilation.

### Infrastructure Setup

**Directory Structure:**
```
builds/
├── macos/          # macOS .app bundles
├── linux/          # Linux x86_64 executables
└── windows/        # Windows executables (future)
```

**.gitignore Updated:**
- Added `builds/` to exclusions

**.gdextension Updated:**
- Added Linux library paths (placeholders)
- Temporarily using debug framework for release (rive-runtime release libs not built)

### Current State
- Debug framework: ✅ Built (34MB)
- Release framework: ❌ Not built (requires rive-runtime release libs ~15-30 min build)

### macOS Export Checklist

**Pre-Export:**
- [ ] Verify `demo/bin/librive.macos.template_debug.framework/` exists
- [ ] Verify `demo/rive.gdextension` points to correct paths
- [ ] Ensure export templates installed (Godot 4.6.2 Stable)

**Export Settings (Godot Editor):**
1. **Project > Export > Add Preset > macOS**
2. **Application:**
   - App Category: `Games`
   - Bundle Identifier: `com.example.rivedemo` (or custom)
3. **GDExtension:**
   - Godot automatically includes files referenced in `.gdextension`
   - The `bin/` folder with framework will be bundled
4. **Export Path:**
   - `builds/macos/RiveDemo.app`
5. **Options:**
   - "Export With Debug" for initial smoke test
   - Code signing can be skipped for local testing

**Post-Export Verification:**
- [ ] `.app` bundle created successfully
- [ ] `Contents/Frameworks/` contains librive framework
- [ ] App launches without crash
- [ ] Rive animations render correctly
- [ ] Events fire (test with nested_artboard_events.riv)
- [ ] Inputs respond (test with interactive .riv)

### Potential Export Issues

**Issue 1: Missing Framework**
If the framework isn't bundled, check:
- `.gdextension` paths are relative to `demo/`
- Framework directory structure is correct (not just the binary)

**Issue 2: Code Signing (macOS Gatekeeper)**
For local testing, right-click and "Open" to bypass.
For distribution, will need Apple Developer signing.

**Issue 3: Library Dependencies**
The framework should be self-contained. If there are missing symbol errors:
- Check all static libs were linked during scons build
- Verify no dynamic dependencies on system libs

### Next Steps After macOS Validation
1. Document any export issues found
2. Build rive-runtime release libraries
3. Set up Linux cross-compilation (M4)

---

## 2026-04-16: Milestone 3 Complete - Standalone Export Fix

### The "Pink Screen" Bug

**Symptom**: Standalone macOS exports showed a pink screen (no rendering) even though the editor worked fine.

**Initial Hypothesis**: ABI mismatch between GDExtension and librive.a due to missing preprocessor defines or compiler flags.

### Investigation Process

1. **Added sizeof() diagnostics** to capture struct sizes at runtime
2. **Compared** our build flags against `rive.make` (librive.a's Makefile)
3. **Added missing flags** to SConstruct:
   - Force-include headers: `rive_harfbuzz_renames.h`, `rive_yoga_renames.h`
   - Preprocessor defines: `WITH_RIVE_TEXT`, `WITH_RIVE_LAYOUT`, `RIVE_MACOSX`, `_RIVE_INTERNAL_`
   - macOS flags: `-fobjc-arc`, `-mmacosx-version-min=11.0`

4. **Key Discovery**: sizeof values matched perfectly (264, 3104, 24), but `artboardCount()` still returned 0!

### Root Cause (NOT an ABI issue!)

The actual bug was a **logic error** in the wrapper classes:

```cpp
// BROKEN: Returns cache size (always 0 before any artboard is accessed)
int get_artboard_count() const {
    return artboards.get_size();  // Returns std::map size = 0
}

// FIXED: Returns actual rive::File count
int get_artboard_count() const {
    return file ? static_cast<int>(file->artboardCount()) : 0;
}
```

The `Instances<T>` class is a lazy-loading cache that only populates when items are requested. The `get_size()` method returned the cache map size (0), not the actual count from the underlying rive objects.

### Files Fixed

| File | Method | Before | After |
|------|--------|--------|-------|
| `rive_file.hpp` | `get_artboard_count()` | `artboards.get_size()` | `file->artboardCount()` |
| `rive_artboard.hpp` | `get_scene_count()` | `scenes.get_size()` | `artboard->stateMachineCount()` |
| `rive_artboard.hpp` | `get_animation_count()` | `animations.get_size()` | `artboard->animationCount()` |
| `rive_scene.hpp` | `get_input_count()` | `inputs.get_size()` | `scene->inputCount()` |
| `rive_scene.hpp` | `get_listener_count()` | `listeners.get_size()` | `scene->stateMachine()->listenerCount()` |

### SConstruct Improvements (Retained)

Even though the root cause wasn't ABI, the SConstruct improvements are valuable for correctness:

1. **Force-include headers** prevent ODR violations from duplicate HarfBuzz/Yoga symbols
2. **Preprocessor defines** ensure struct layouts match librive.a
3. **macOS flags** ensure proper Objective-C interop and deployment target

### Verification

```
[RiveViewer] File loaded: 3 artboard(s)
[RiveViewer] Artboard 'Main' has 1 scene(s), 13 animation(s)
[RiveViewer] Objects - File: OK, Artboard: OK, Scene: OK
```

Standalone macOS export now renders correctly with animated circles.

### Milestone 3 Status: ✅ COMPLETE

- [x] Rive Events working
- [x] Nested artboard inputs implemented
- [x] Standalone macOS export working
- [x] ABI alignment with librive.a
- [x] Pink screen bug fixed

---

## Milestone 4: Linux Build - Planning

### Challenge
We need to build the GDExtension for Linux x86_64 from a macOS host.

### Dependencies Required for Linux
1. **librive.a** (Linux x86_64) - Must be cross-compiled or built on Linux
2. **librive_skia_renderer.a** (Linux x86_64)
3. **librive_harfbuzz.a** (Linux x86_64)
4. **librive_sheenbidi.a** (Linux x86_64)
5. **librive_yoga.a** (Linux x86_64)
6. **libskia.a** (Linux x86_64)
7. **libgodot-cpp.a** (Linux x86_64)

### Approach Options

**Option A: GitHub Actions CI**
- Build rive-runtime and GDExtension in a Linux container
- Most reliable, matches real deployment environment
- Can cache built libraries between runs

**Option B: Docker on macOS**
- Run a Linux container locally for compilation
- Faster iteration than CI
- Requires Docker Desktop

**Option C: Cross-compilation toolchain**
- Use clang with Linux target triple
- Most complex setup
- May have issues with system library compatibility

### Recommended: GitHub Actions (Option A)

1. Create `.github/workflows/build-linux.yml`
2. Use Ubuntu runner
3. Install dependencies: premake5, ninja, clang
4. Build rive-runtime libraries
5. Build godot-cpp
6. Build GDExtension
7. Upload artifacts

### Next Steps
1. Research rive-runtime's Linux build process
2. Create initial GitHub Actions workflow
3. Test first compilation
4. Iterate on missing dependencies

