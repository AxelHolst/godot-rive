# rive-cpp to rive-runtime API Migration Guide

This document maps the existing godot-rive wrapper classes to the new rive-runtime API.

## Summary

The rive-runtime API is largely compatible with rive-cpp, with these key changes:
1. `rive::File` now uses `rcp<File>` (reference-counted pointer) instead of `std::unique_ptr`
2. Header paths remain the same: `<rive/file.hpp>`, `<rive/artboard.hpp>`, etc.
3. New ViewModel/DataBinding APIs are available
4. SMITrigger class now officially available with `fire()` method

---

## Class-by-Class Migration

### RiveFile (rive_file.hpp)

| Current (rive-cpp) | New (rive-runtime) | Status |
|-------------------|-------------------|--------|
| `#include <rive/file.hpp>` | `#include <rive/file.hpp>` | No Change |
| `Ptr<rive::File>` (unique_ptr) | `rcp<rive::File>` | **CHANGE** - use refcounted |
| `file->artboardCount()` | `file->artboardCount()` | No Change |
| `file->artboardAt(index)` | `file->artboardAt(index)` | No Change |
| `file->artboardNameAt(index)` | `file->artboardNameAt(index)` | No Change |

**Required Changes:**
```cpp
// OLD
Ptr<rive::File> file;  // std::unique_ptr

// NEW
rcp<rive::File> file;  // rive reference-counted pointer
```

**Import Function:**
```cpp
// OLD
rive::File::import(span, factory)

// NEW
rive::File::import(span, factory, &result, assetLoader, scriptingVM)
```

---

### RiveArtboard (rive_artboard.hpp)

| Current | New | Status |
|---------|-----|--------|
| `#include <rive/artboard.hpp>` | `#include <rive/artboard.hpp>` | No Change |
| `Ptr<rive::ArtboardInstance>` | `std::unique_ptr<rive::ArtboardInstance>` | No Change |
| `artboard->stateMachineCount()` | `artboard->stateMachineCount()` | No Change |
| `artboard->stateMachineAt(index)` | `artboard->stateMachineAt(index)` | No Change |
| `artboard->stateMachineNameAt(index)` | `artboard->stateMachineNameAt(index)` | No Change |
| `artboard->animationCount()` | `artboard->animationCount()` | No Change |
| `artboard->animationAt(index)` | `artboard->animationAt(index)` | No Change |
| `artboard->animationNameAt(index)` | `artboard->animationNameAt(index)` | No Change |
| `artboard->bounds()` | `artboard->bounds()` | No Change |
| `artboard->worldTransform()` | `artboard->worldTransform()` | No Change |

**No significant changes required.**

---

### RiveScene (rive_scene.hpp)

| Current | New | Status |
|---------|-----|--------|
| `#include <rive/animation/state_machine_instance.hpp>` | Same | No Change |
| `Ptr<rive::StateMachineInstance>` | `std::unique_ptr<rive::StateMachineInstance>` | No Change |
| `scene->inputCount()` | `scene->inputCount()` | No Change |
| `scene->input(index)` | `scene->input(index)` | No Change |
| `scene->bounds()` | `scene->bounds()` | No Change |
| `scene->durationSeconds()` | `scene->durationSeconds()` | No Change |
| `scene->isTranslucent()` | `scene->isTranslucent()` | No Change |
| `scene->pointerMove()` | `scene->pointerMove()` | No Change |
| `scene->pointerDown()` | `scene->pointerDown()` | No Change |
| `scene->pointerUp()` | `scene->pointerUp()` | No Change |
| `scene->loop()` | `scene->loop()` | No Change |

**New APIs available:**
- `scene->reportedEventCount()` - Get number of events fired
- `scene->reportedEventAt(index)` - Get event details

---

### RiveInput (rive_input.hpp)

| Current | New | Status |
|---------|-----|--------|
| `rive::SMIInput*` | `rive::SMIInput*` | No Change |
| `rive::SMIBool` | `rive::SMIBool` | No Change |
| `rive::SMINumber` | `rive::SMINumber` | No Change |
| N/A | `rive::SMITrigger` | **NEW** |
| `input->name()` | `input->name()` | No Change |
| `input->input()->is<T>()` | `input->input()->is<T>()` | No Change |

**Required Changes for Trigger Support:**
```cpp
// Add to RiveInput class:
bool is_trigger() const {
    return input && input->input()->is<rive::StateMachineTrigger>();
}

rive::SMITrigger* trigger_input() const {
    if (is_trigger()) return static_cast<rive::SMITrigger*>(input);
    return nullptr;
}

void fire() {
    if (auto t = trigger_input()) t->fire();
}
```

**New include required:**
```cpp
#include <rive/animation/state_machine_trigger.hpp>
```

---

### RiveAnimation (rive_animation.hpp)

| Current | New | Status |
|---------|-----|--------|
| `#include <rive/animation/linear_animation_instance.hpp>` | Same | No Change |
| `Ptr<rive::LinearAnimationInstance>` | `std::unique_ptr<rive::LinearAnimationInstance>` | No Change |
| `animation->durationSeconds()` | `animation->durationSeconds()` | No Change |
| `animation->time()` | `animation->time()` | No Change |
| `animation->direction()` | `animation->direction()` | No Change |
| `animation->reset()` | `animation->reset()` | No Change |

**No changes required.**

---

## New Features Available

### 1. Triggers (SMITrigger)

Triggers are now fully supported in rive-runtime.

```cpp
// Check if input is a trigger
if (smiInput->input()->is<rive::StateMachineTrigger>()) {
    auto trigger = static_cast<rive::SMITrigger*>(smiInput);
    trigger->fire();  // Fire the trigger
}
```

### 2. Rive Events

Events can be polled after `advance()`:

```cpp
scene->advance(deltaTime);

// Check for events
size_t eventCount = scene->reportedEventCount();
for (size_t i = 0; i < eventCount; i++) {
    const rive::EventReport& event = scene->reportedEventAt(i);
    // event.event() - the Event object
    // event.secondsDelay() - delay from start of frame
}
```

### 3. ViewModel / Data Binding

New in rive-runtime:
```cpp
// Create ViewModel instance
rcp<rive::ViewModelInstance> vm = file->createViewModelInstance("MyViewModel");

// Access properties
auto prop = vm->propertyValue("myProperty");
```

---

## Include Path Changes

All includes remain in the same location:
- `<rive/file.hpp>`
- `<rive/artboard.hpp>`
- `<rive/scene.hpp>`
- `<rive/animation/state_machine_instance.hpp>`
- `<rive/animation/state_machine_input_instance.hpp>`
- `<rive/animation/linear_animation_instance.hpp>`

**New includes for new features:**
- `<rive/animation/state_machine_trigger.hpp>` - For SMITrigger
- `<rive/event_report.hpp>` - For event handling
- `<rive/viewmodel/viewmodel_instance.hpp>` - For data binding

---

## Build System Changes

The library names and paths have changed:

| Old (rive-cpp) | New (rive-runtime) |
|---------------|-------------------|
| `thirdparty/rive-cpp/` | `thirdparty/rive-runtime/` |
| `build/{platform}/bin/{target}/librive.a` | TBD - check build output |
| `skia/renderer/build/...` | `skia/renderer/build/...` |
| `dependencies/{platform}/cache/bin/{target}/librive_harfbuzz.a` | TBD |
| `dependencies/{platform}/cache/bin/{target}/librive_sheenbidi.a` | TBD |

---

## Migration Priority

1. **Phase 1**: Update include paths and `Ptr<rive::File>` → `rcp<rive::File>`
2. **Phase 2**: Add SMITrigger support to RiveInput
3. **Phase 3**: Add Rive Events support
4. **Phase 4**: Add ViewModel/Data Binding support
