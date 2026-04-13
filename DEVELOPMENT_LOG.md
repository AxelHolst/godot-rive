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
