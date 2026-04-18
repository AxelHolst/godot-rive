#include "rive_viewer_base.h"

#include <algorithm>

// godot-cpp
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// rive-runtime
#include <rive/animation/linear_animation.hpp>
#include <rive/animation/linear_animation_instance.hpp>
#include <rive/event_report.hpp>
#include <rive/file.hpp>
#include <rive/artboard.hpp>

// skia renderer
#include <skia/renderer/include/skia_renderer.hpp>

// extension
#include "rive_exceptions.hpp"
#include "rive_global.hpp"
#include "utils/godot_macros.hpp"
#include "utils/types.hpp"

const Image::Format IMAGE_FORMAT = Image::Format::FORMAT_RGBA8;

// =============================================================================
// GPU RENDERING (Milestone 5) - Static members
// =============================================================================
// These are static so we only probe the GPU once per process, not per viewer.
// The GPU bridge extracts VkDevice/MTLDevice from Godot's RenderingDevice.
// The GPU renderer (if available) is shared across all viewer instances.
bool RiveViewerBase::gpu_probed = false;
std::unique_ptr<rive_godot::RiveGPUBridge> RiveViewerBase::gpu_bridge = nullptr;
#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)
std::unique_ptr<rive_godot::RiveGPURenderer> RiveViewerBase::gpu_renderer = nullptr;
bool RiveViewerBase::gpu_renderer_failed = false;
Ref<Texture2DRD> RiveViewerBase::gpu_texture;  // Wraps GPU renderer's RID for CanvasItem
#endif

RiveViewerBase::RiveViewerBase(CanvasItem *owner) {
    this->owner = owner;
    inst.set_props(&props, &initialized);  // Pass initialized flag for callback guards
    sk.set_props(&props);
    props.on_artboard_changed([this](int index) { _on_artboard_changed(index); });
    props.on_scene_changed([this](int index) { _on_scene_changed(index); });
    props.on_animation_changed([this](int index) { _on_animation_changed(index); });
    props.on_path_changed([this](String path) { _on_path_changed(path); });
    props.on_size_changed([this](float w, float h) { _on_size_changed(w, h); });
    props.on_transform_changed([this]() { _on_transform_changed(); });
}

void RiveViewerBase::on_input_event(const Ref<InputEvent> &event) {
    auto mouse_event = dynamic_cast<InputEventMouse *>(event.ptr());
    if (!mouse_event || is_editor_hint()) return;

    Vector2 pos = mouse_event->get_position();

    if (auto mouse_button = dynamic_cast<InputEventMouseButton *>(event.ptr())) {
        if (!props.disable_press() && mouse_button->is_pressed()) {
            inst.press_mouse(pos);
            owner->emit_signal("pressed", mouse_event->get_position());
        } else if (!props.disable_press() && mouse_button->is_released()) {
            inst.release_mouse(pos);
            owner->emit_signal("released", mouse_event->get_position());
        }
    }
    if (auto mouse_motion = dynamic_cast<InputEventMouseMotion *>(event.ptr())) {
        if (!props.disable_hover()) inst.move_mouse(pos);
    }
}

void RiveViewerBase::on_draw() {
#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)
    // GPU path: use Texture2DRD if available and valid
    if (gpu_texture.is_valid() && gpu_renderer && gpu_renderer->is_valid() && !gpu_renderer_failed) {
        owner->draw_texture_rect(gpu_texture, Rect2(0, 0, width(), height()), false);
        return;
    }
#endif
    // CPU path: use ImageTexture from Skia
    if (!is_null(texture)) owner->draw_texture_rect(texture, Rect2(0, 0, width(), height()), false);
}

void RiveViewerBase::on_process(float delta) {
    // Deferred initialization - wait until first process frame when bindings are fully ready
    if (!initialized) {
        deferred_init();
    }

    if (owner->is_node_ready() && !props.paused()) {
#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)
        // =======================================================================
        // GPU RENDERING PATH (Milestone 5)
        // =======================================================================
        // Try GPU rendering first. If it succeeds, we're done.
        // If it fails, fall back to CPU (Skia) rendering.
        if (try_gpu_frame(delta)) {
            check_scene_property_changed();
            return;  // GPU frame succeeded
        }
#endif
        // =======================================================================
        // CPU RENDERING PATH (Skia fallback)
        // =======================================================================
        // Recreate image/texture if size changed
        if (is_null(image) || image->get_width() != width() || image->get_height() != height()) {
            image = Image::create(width(), height(), false, IMAGE_FORMAT);
            texture = ImageTexture::create_from_image(image);
        }
        PackedByteArray bytes = frame(delta);
        if (bytes.size()) {
            image->set_data(width(), height(), false, IMAGE_FORMAT, bytes);
            texture->update(image);
            owner->queue_redraw();
        }
        check_scene_property_changed();
    }
}

void RiveViewerBase::on_ready() {
    // DON'T set initialized here - defer to first process frame
    // because _ready() is called during scene loading and bindings may not be fully ready
    elapsed = 0.0;
}

void RiveViewerBase::deferred_init() {
    if (initialized) return;
    initialized = true;  // Now safe to process callbacks that create Ref<> objects

    RIVE_DEBUG_LOG("[RiveViewer] deferred_init() starting...");

    // ========================================================================
    // MILESTONE 5: GPU DEVICE PROBE (Vulkan-first approach)
    // ========================================================================
    // Attempt to extract GPU device handles from Godot's RenderingDevice.
    // This runs once per process (static flag) to avoid redundant probing.
    // The extracted VkDevice will be used for Rive GPU Renderer initialization.
    probe_gpu_device();

    // Deferred file loading - if a file path was set during scene loading,
    // we couldn't load it then because bindings weren't ready. Load it now.
    if (pending_file_load && !props.path().is_empty()) {
        pending_file_load = false;
        RIVE_DEBUG_LOG("[RiveViewer] Loading deferred file: ", props.path());
        load_rive_file(props.path());
    }

    // Extract pending property values (these were saved during scene loading)
    int artboard_idx = pending_property_values.has("artboard") ? (int)pending_property_values["artboard"] : -1;
    int scene_idx = pending_property_values.has("scene") ? (int)pending_property_values["scene"] : -1;
    int animation_idx = pending_property_values.has("animation") ? (int)pending_property_values["animation"] : -1;

    RIVE_DEBUG_LOG("[RiveViewer] Pending indices from .tscn - artboard: ", artboard_idx,
                   ", scene: ", scene_idx, ", animation: ", animation_idx);

    // ========================================================================
    // SAFE DEFAULTS: Auto-select first artboard/scene if file exists but indices are -1
    // This fixes the "pink screen" issue in standalone exports where saved -1 values
    // prevent any content from rendering.
    // ========================================================================
    if (exists(inst.file)) {
        RIVE_DEBUG_LOG("[RiveViewer] File loaded: ", inst.file->get_artboard_count(), " artboard(s)");

        // Auto-select artboard 0 if none selected and artboards exist
        if (artboard_idx == -1 && inst.file->get_artboard_count() > 0) {
            artboard_idx = 0;
            RIVE_DEBUG_LOG("[RiveViewer] SAFE DEFAULT: Auto-selecting artboard 0");
        }
    }

    // Apply artboard index first (so artboard() returns a valid object)
    if (artboard_idx != -1) {
        props.artboard(artboard_idx);
    }

    // Now check for scene/animation defaults after artboard is set
    auto artboard = inst.artboard();
    if (exists(artboard)) {
        RIVE_DEBUG_LOG("[RiveViewer] Artboard '", artboard->get_name(), "' has ",
                       artboard->get_scene_count(), " scene(s), ",
                       artboard->get_animation_count(), " animation(s)");

        // Auto-select scene 0 if no scene AND no animation selected, and scenes exist
        if (scene_idx == -1 && animation_idx == -1 && artboard->get_scene_count() > 0) {
            scene_idx = 0;
            RIVE_DEBUG_LOG("[RiveViewer] SAFE DEFAULT: Auto-selecting scene 0");
        }
    }

    // Apply scene and animation indices
    if (scene_idx != -1) {
        props.scene(scene_idx);
    }
    if (animation_idx != -1) {
        props.animation(animation_idx);
    }

    pending_property_values.clear();

    // Final diagnostic log
    RIVE_DEBUG_LOG("[RiveViewer] FINAL STATE - artboard: ", props.artboard(),
                   ", scene: ", props.scene(), ", animation: ", props.animation());
    RIVE_DEBUG_LOG("[RiveViewer] Objects - File: ", exists(inst.file) ? "OK" : "NULL",
                   ", Artboard: ", exists(inst.artboard()) ? "OK" : "NULL",
                   ", Scene: ", exists(inst.scene()) ? "OK" : "NULL");

    props.size(width(), height());
}

// =============================================================================
// MILESTONE 5: GPU DEVICE EXTRACTION AND RENDERER INITIALIZATION
// =============================================================================
void RiveViewerBase::probe_gpu_device() {
    // Only probe once per process lifetime
    if (gpu_probed) return;
    gpu_probed = true;

    // Skip GPU probe in editor to avoid potential issues during scene editing
    if (is_editor_hint()) {
        UtilityFunctions::print("[RiveGPU] Skipping GPU probe in editor mode");
        return;
    }

    UtilityFunctions::print("[RiveGPU] === Hardware Handshake ===");
    UtilityFunctions::print("[RiveGPU] Attempting to extract GPU device handles from Godot...");

    // Create the GPU bridge - this extracts VkDevice/MTLDevice from RenderingDevice
    gpu_bridge = rive_godot::RiveGPUBridge::create();

    if (!gpu_bridge) {
        UtilityFunctions::push_warning("[RiveGPU] Failed to create GPU bridge - RenderingDevice unavailable");
        UtilityFunctions::print("[RiveGPU] Falling back to CPU (Skia) rendering");
        return;
    }

    // Print diagnostic information
    gpu_bridge->print_diagnostics();

    if (gpu_bridge->is_valid()) {
        UtilityFunctions::print("[RiveGPU] SUCCESS: GPU device handles extracted!");
        UtilityFunctions::print("[RiveGPU] Backend: ", gpu_bridge->get_backend_name());

        // Log the actual memory addresses
        if (gpu_bridge->get_backend() == rive_godot::GPUBackend::VULKAN) {
            UtilityFunctions::print("[RiveGPU] VkInstance handle: 0x",
                String::num_int64(gpu_bridge->get_vulkan_instance(), 16));
            UtilityFunctions::print("[RiveGPU] VkPhysicalDevice handle: 0x",
                String::num_int64(gpu_bridge->get_vulkan_physical_device(), 16));
            UtilityFunctions::print("[RiveGPU] VkDevice handle: 0x",
                String::num_int64(gpu_bridge->get_vulkan_device(), 16));

            // This is the key output - proves we can talk to Godot's GPU
            UtilityFunctions::print("[RiveGPU] *** HARDWARE HANDSHAKE SUCCESSFUL ***");

#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)
            // Attempt to create the GPU renderer
            UtilityFunctions::print("[RiveGPU] Attempting to initialize GPU renderer...");

            rive_godot::GPURendererConfig config;
            config.width = 512;   // Default size, will be resized per-viewer
            config.height = 512;
            config.forceAtomicMode = false;

            gpu_renderer = rive_godot::RiveGPURenderer::create(*gpu_bridge, config);

            if (gpu_renderer && gpu_renderer->is_valid()) {
                UtilityFunctions::print("[RiveGPU] *** GPU RENDERER INITIALIZED ***");
                gpu_renderer->printDiagnostics();
            } else {
                UtilityFunctions::push_warning("[RiveGPU] GPU renderer initialization failed");
                UtilityFunctions::print("[RiveGPU] Falling back to CPU (Skia) rendering");
                gpu_renderer.reset();
                gpu_renderer_failed = true;
            }
#else
            UtilityFunctions::print("[RiveGPU] GPU renderer not compiled (RIVE_GPU_RENDERER not defined)");
            UtilityFunctions::print("[RiveGPU] Using CPU (Skia) rendering");
#endif
        } else if (gpu_bridge->get_backend() == rive_godot::GPUBackend::METAL) {
            UtilityFunctions::print("[RiveGPU] MTLDevice handle: 0x",
                String::num_int64((uint64_t)gpu_bridge->get_metal_device(), 16));
            UtilityFunctions::print("[RiveGPU] Metal backend not yet implemented, using CPU (Skia) rendering");
        }
    } else {
        if (gpu_bridge->has_partial_vulkan()) {
            UtilityFunctions::push_warning("[RiveGPU] Partial Vulkan handles - missing VkInstance");
        } else {
            UtilityFunctions::push_warning("[RiveGPU] GPU bridge created but no valid handles extracted");
        }
        UtilityFunctions::print("[RiveGPU] Falling back to CPU (Skia) rendering");
    }

    UtilityFunctions::print("[RiveGPU] === End Hardware Handshake ===");
}

// =============================================================================
// MILESTONE 5: GPU RENDERING FRAME
// =============================================================================
#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)

void RiveViewerBase::ensure_gpu_texture_size() {
    if (!gpu_renderer || !gpu_renderer->is_valid()) return;

    uint32_t w = static_cast<uint32_t>(width());
    uint32_t h = static_cast<uint32_t>(height());

    // Resize GPU renderer if needed
    if (gpu_renderer->width() != w || gpu_renderer->height() != h) {
        if (!gpu_renderer->resize(w, h)) {
            UtilityFunctions::push_warning("[RiveGPU] Failed to resize GPU renderer");
            gpu_renderer_failed = true;
            return;
        }

        // Update Texture2DRD with new RID
        if (!gpu_texture.is_valid()) {
            gpu_texture.instantiate();
        }
        gpu_texture->set_texture_rd_rid(gpu_renderer->getGodotTextureRID());
    }
}

bool RiveViewerBase::try_gpu_frame(float delta) {
    // Guard: GPU renderer must be available and not failed
    if (!gpu_renderer || !gpu_renderer->is_valid() || gpu_renderer_failed) {
        return false;
    }

    // Guard: Must have an artboard to render
    auto artboard = inst.artboard();
    if (!exists(artboard) || !artboard->artboard) {
        return false;
    }

    // Ensure GPU texture matches viewer size
    ensure_gpu_texture_size();
    if (gpu_renderer_failed) {
        return false;  // Resize failed
    }

    // Create Texture2DRD wrapper if needed
    if (!gpu_texture.is_valid()) {
        gpu_texture.instantiate();
        gpu_texture->set_texture_rd_rid(gpu_renderer->getGodotTextureRID());
        UtilityFunctions::print("[RiveGPU] Created Texture2DRD wrapper");
    }

    // Advance animation state
    advance(delta);

    // Only render if visible
    if (!owner->is_visible()) {
        return true;  // Still "succeeded" - just nothing to draw
    }

    // =======================================================================
    // GPU FRAME RENDERING
    // =======================================================================
    if (!gpu_renderer->beginFrame()) {
        UtilityFunctions::push_warning("[RiveGPU] beginFrame() failed - falling back to CPU");
        gpu_renderer_failed = true;
        return false;
    }

    // Calculate transform to fit artboard in viewer (same logic as Skia path)
    Rect2 bounds = artboard->get_bounds();
    float artboardWidth = bounds.size.x;
    float artboardHeight = bounds.size.y;
    float canvasWidth = static_cast<float>(width());
    float canvasHeight = static_cast<float>(height());

    rive::Mat2D transform;
    if (artboardWidth > 0 && artboardHeight > 0) {
        float scale = std::min(canvasWidth / artboardWidth, canvasHeight / artboardHeight);
        float tx = (canvasWidth - artboardWidth * scale) / 2.0f;
        float ty = (canvasHeight - artboardHeight * scale) / 2.0f;

        // Build transform: translate then scale
        transform = rive::Mat2D(scale, 0, 0, scale, tx, ty);
    }

    // Draw artboard to GPU
    gpu_renderer->draw(artboard->artboard.get(), &transform);

    // End frame and flush to GPU
    if (!gpu_renderer->endFrame()) {
        UtilityFunctions::push_warning("[RiveGPU] endFrame() failed - falling back to CPU");
        gpu_renderer_failed = true;
        return false;
    }

    // Request redraw so on_draw() will display the GPU texture
    owner->queue_redraw();

    return true;  // GPU frame succeeded
}

#else
// Stub implementations when GPU renderer is not compiled
void RiveViewerBase::ensure_gpu_texture_size() {}
bool RiveViewerBase::try_gpu_frame(float delta) { return false; }
#endif

void RiveViewerBase::check_scene_property_changed() {
    if (props.disable_hover() && props.disable_press()) return;  // Don't bother checking if input is disabled
    auto scene = inst.scene();
    if (exists(scene))
        scene->inputs.for_each([this, scene](Ref<RiveInput> input, int _) {
            String prop = input->get_name();
            Variant old_value = cached_scene_property_values.get(prop, input->get_default());
            Variant new_value = input->get_value();
            if (old_value != new_value) owner->emit_signal("scene_property_changed", scene, prop, new_value, old_value);
            cached_scene_property_values[prop] = new_value;
        });
}

int RiveViewerBase::width() const {
    return std::max(get_size().x, (real_t)1);
}

int RiveViewerBase::height() const {
    return std::max(get_size().y, (real_t)1);
}

void RiveViewerBase::_on_path_changed(String path) {
    // Guard: File loading creates Ref<RiveFile> which triggers binding callbacks.
    // During scene loading, bindings aren't ready. Defer loading to on_ready().
    if (!initialized) {
        pending_file_load = true;
        return;
    }
    load_rive_file(path);
}

void RiveViewerBase::load_rive_file(String path) {
    try {
        inst.file = RiveFile::Load(path, RiveGlobal::get_factory());
    } catch (RiveException error) {
        error.report();
    }
    if (exists(inst.file)) {
        _on_size_changed(props.width(), props.height());
        if (is_editor_hint()) owner->notify_property_list_changed();
    }
}

void RiveViewerBase::get_property_list(List<PropertyInfo> *list) const {
    // Guard: Only enumerate dynamic properties after full initialization
    // During scene loading, accessing Rive objects creates Ref<> which triggers binding callbacks
    if (!initialized) return;

    inst.instantiate();
    if (exists(inst.file)) {
        String artboard_hint = inst.file->_get_artboard_property_hint();
        list->push_back(PropertyInfo(Variant::INT, "artboard", PROPERTY_HINT_ENUM, artboard_hint));
    }
    auto artboard = inst.artboard();
    if (exists(artboard)) {
        String scene_hint = artboard->_get_scene_property_hint();
        list->push_back(PropertyInfo(Variant::INT, "scene", PROPERTY_HINT_ENUM, scene_hint));
        String anim_hint = artboard->_get_animation_property_hint();
        list->push_back(PropertyInfo(Variant::INT, "animation", PROPERTY_HINT_ENUM, anim_hint));
    }
    auto scene = inst.scene();
    if (exists(scene)) {
        list->push_back(PropertyInfo(Variant::NIL, "Scene", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CATEGORY));
        scene->_get_input_property_list(list);
    }
}

void RiveViewerBase::_on_artboard_changed(int _index) {
    owner->notify_property_list_changed();
}

void RiveViewerBase::_on_scene_changed(int _index) {
    cached_scene_property_values.clear();
    owner->notify_property_list_changed();
}

void RiveViewerBase::_on_animation_changed(int _index) {
    try {
        if (props.scene() != -1)
            throw RiveException("Animation will not play because a scene is selected.")
                .from(owner, "set_animation")
                .warning();
    } catch (RiveException error) {
        error.report();
    }
}

bool RiveViewerBase::on_set(const StringName &prop, const Variant &value) {
    // CRITICAL GUARD: During scene loading, setting properties that trigger callbacks
    // can cause crashes because Godot's binding system isn't ready. We store the values
    // and apply them later in on_ready().
    if (!initialized) {
        String name = prop;
        if (name == "artboard" || name == "scene" || name == "animation") {
            pending_property_values[name] = value;
            return true;
        }
        // For other properties (like scene inputs), ignore during loading
        return false;
    }

    String name = prop;
    if (name == "artboard") {
        props.artboard((int)value);
        return true;
    }
    if (name == "scene") {
        props.scene((int)value);
        return true;
    }
    if (name == "animation") {
        props.animation((int)value);
        return true;
    }
    // Scene property handling - only after full initialization
    if (!initialized) return false;
    inst.instantiate();
    if (exists(inst.scene()) && inst.scene()->get_input_names().has(name)) {
        props.scene_property(name, value);
        return true;
    }
    return false;
}

bool RiveViewerBase::on_get(const StringName &prop, Variant &return_value) const {
    String name = prop;
    if (name == "artboard") {
        return_value = props.artboard();
        return true;
    }
    if (name == "scene") {
        return_value = props.scene();
        return true;
    }
    if (name == "animation") {
        return_value = props.animation();
        return true;
    }
    if (props.has_scene_property(name)) {
        return_value = props.scene_property(name);
        return true;
    }
    return false;
}

void RiveViewerBase::_on_size_changed(float w, float h) {
    // Guard: Creating Image/ImageTexture during scene loading might be unsafe
    if (!initialized) return;
    if (!is_null(image)) unref(image);
    if (!is_null(texture)) unref(texture);
    image = Image::create(width(), height(), false, IMAGE_FORMAT);
    texture = ImageTexture::create_from_image(image);
}

void RiveViewerBase::_on_transform_changed() {
    // Skip redraw during transform changes - the regular on_process frame processing
    // will handle the draw. This prevents crashes in Yoga layout when the artboard
    // is being set up for the first time.
    // TODO: Consider enabling this for interactive resizing scenarios
}

bool RiveViewerBase::advance(float delta) {
    elapsed += delta;
    bool result = inst.advance(delta);

    // Poll and emit Rive events after advancing
    poll_events();

    return result;
}

void RiveViewerBase::poll_events() {
    auto scene = inst.scene();
    if (!exists(scene) || !scene->scene) return;

    // Get the underlying StateMachineInstance
    rive::StateMachineInstance* sm = scene->scene.get();

    // Poll all reported events
    std::size_t event_count = sm->reportedEventCount();
    for (std::size_t i = 0; i < event_count; i++) {
        const rive::EventReport report = sm->reportedEventAt(i);
        Ref<RiveEvent> event = RiveEvent::from_report(report);
        owner->emit_signal("rive_event", event);
    }
}

PackedByteArray RiveViewerBase::redraw() {
    auto* canvas = sk.getCanvas();
    if (!canvas) return PackedByteArray();

    // Clear to transparent
    canvas->clear(SK_ColorTRANSPARENT);

    // Get the artboard and draw it
    auto artboard = inst.artboard();
    if (exists(artboard)) {
        // Create renderer using the global factory
        rive::SkiaRenderer renderer(canvas);

        canvas->save();

        // Scale and center artboard in the canvas
        Rect2 bounds = artboard->get_bounds();
        float artboardWidth = bounds.size.x;
        float artboardHeight = bounds.size.y;
        float canvasWidth = props.width();
        float canvasHeight = props.height();

        if (artboardWidth > 0 && artboardHeight > 0) {
            float scale = std::min(canvasWidth / artboardWidth, canvasHeight / artboardHeight);
            float tx = (canvasWidth - artboardWidth * scale) / 2.0f;
            float ty = (canvasHeight - artboardHeight * scale) / 2.0f;

            canvas->translate(tx, ty);
            canvas->scale(scale, scale);
        }

        // Draw the artboard - access the raw rive::ArtboardInstance pointer
        artboard->artboard->draw(&renderer);

        canvas->restore();
    } else {
        // No artboard - draw a test pattern (magenta for visibility)
        canvas->drawColor(SkColorSetRGB(255, 0, 255));
    }

    return sk.bytes();
}

PackedByteArray RiveViewerBase::frame(float delta) {
    if (!sk.getCanvas()) return PackedByteArray();

    // Advance the animation
    advance(delta);

    // Redraw if visible
    if (owner->is_visible()) {
        return redraw();
    }
    return PackedByteArray();
}

float RiveViewerBase::get_elapsed_time() const {
    return elapsed;
}

Ref<RiveFile> RiveViewerBase::get_file() const {
    return inst.file;
}

Ref<RiveArtboard> RiveViewerBase::get_artboard() const {
    return inst.artboard();
}

Ref<RiveScene> RiveViewerBase::get_scene() const {
    return inst.scene();
}

Ref<RiveAnimation> RiveViewerBase::get_animation() const {
    return inst.animation();
}

void RiveViewerBase::go_to_artboard(Ref<RiveArtboard> artboard_value) {
    try {
        if (is_null(artboard_value))
            throw RiveException("Attempted to go to null artboard").from(owner, "go_to_artboard").warning();
        props.artboard(artboard_value->get_index());
    } catch (RiveException error) {
        error.report();
    }
}

void RiveViewerBase::go_to_scene(Ref<RiveScene> scene_value) {
    try {
        if (is_null(scene_value))
            throw RiveException("Attempted to go to null scene").from(owner, "go_to_scene").warning();
        props.scene(scene_value->get_index());
    } catch (RiveException error) {
        error.report();
    }
}

void RiveViewerBase::go_to_animation(Ref<RiveAnimation> animation_value) {
    try {
        if (is_null(animation_value))
            throw RiveException("Attempted to go to null animation").from(owner, "go_to_animation").warning();
        props.animation(animation_value->get_index());
        if (props.scene() != -1)
            throw RiveException("Went to animation, but it won't play because a scene is currently playing.")
                .from(owner, "go_to_animation")
                .warning();
    } catch (RiveException error) {
        error.report();
    }
}

void RiveViewerBase::press_mouse(Vector2 position) {
    inst.press_mouse(position);
}

void RiveViewerBase::release_mouse(Vector2 position) {
    inst.release_mouse(position);
}

void RiveViewerBase::move_mouse(Vector2 position) {
    inst.move_mouse(position);
}