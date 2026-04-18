/**
 * @file rive_gpu_renderer.cpp
 * @brief Implementation of the RiveGPURenderer class
 *
 * This file implements the GPU rendering path for Rive animations using
 * the Rive PLS (Pixel Local Storage) Renderer with Vulkan backend.
 */

#include "rive_gpu_renderer.hpp"

#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)

#include <godot_cpp/variant/utility_functions.hpp>

namespace rive_godot {

// =============================================================================
// STATIC HELPER: Load vkGetInstanceProcAddr
// =============================================================================

PFN_vkGetInstanceProcAddr RiveGPURenderer::getVkGetInstanceProcAddr() {
    // Try to find vkGetInstanceProcAddr
    // On macOS, Godot has MoltenVK statically linked, so we need to try multiple approaches

#ifdef __APPLE__
    // Approach 1: Try to find it in the current process (Godot has MoltenVK statically linked)
    auto procAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")
    );
    if (procAddr) {
        godot::UtilityFunctions::print("[RiveGPURenderer] Found vkGetInstanceProcAddr in process");
        return procAddr;
    }

    // Approach 2: Try loading MoltenVK dynamically (for development/SDK installs)
    void* vulkanLib = dlopen("libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!vulkanLib) {
        vulkanLib = dlopen("libvulkan.dylib", RTLD_NOW | RTLD_LOCAL);
    }
    if (!vulkanLib) {
        vulkanLib = dlopen("/usr/local/lib/libvulkan.dylib", RTLD_NOW | RTLD_LOCAL);
    }
    if (!vulkanLib) {
        // Try Vulkan SDK location
        vulkanLib = dlopen("/usr/local/Cellar/molten-vk/1.2.11/lib/libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
    }
#else
    // Linux/Windows - try dynamic loading
    void* vulkanLib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!vulkanLib) {
        vulkanLib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
#endif

#ifndef __APPLE__
    if (!vulkanLib) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] Failed to load Vulkan library");
        return nullptr;
    }
#endif

#ifdef __APPLE__
    if (vulkanLib) {
#endif
        auto procAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(vulkanLib, "vkGetInstanceProcAddr")
        );

        if (procAddr) {
            godot::UtilityFunctions::print("[RiveGPURenderer] Found vkGetInstanceProcAddr in loaded library");
            return procAddr;
        }
#ifdef __APPLE__
    }

    // If we got here on macOS, we couldn't find it anywhere
    godot::UtilityFunctions::push_warning(
        "[RiveGPURenderer] Failed to find vkGetInstanceProcAddr (MoltenVK may be statically linked without exports)");
    return nullptr;
#else
    godot::UtilityFunctions::push_warning(
        "[RiveGPURenderer] Failed to find vkGetInstanceProcAddr");
    return nullptr;
#endif
}

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

RiveGPURenderer::RiveGPURenderer(const GPURendererConfig& config)
    : m_config(config)
    , m_width(config.width)
    , m_height(config.height) {
}

RiveGPURenderer::~RiveGPURenderer() {
    // Clean up Rive resources in reverse order
    m_renderer.reset();
    m_renderTarget.reset();
    m_renderContext.reset();

    // Free Godot texture if we created one
    if (m_godotTextureRID.is_valid() && m_bridge && m_bridge->get_rendering_device()) {
        m_bridge->get_rendering_device()->free_rid(m_godotTextureRID);
    }

    godot::UtilityFunctions::print("[RiveGPURenderer] Destroyed");
}

// =============================================================================
// FACTORY METHOD
// =============================================================================

std::unique_ptr<RiveGPURenderer> RiveGPURenderer::create(
    const RiveGPUBridge& bridge,
    const GPURendererConfig& config
) {
    godot::UtilityFunctions::print("[RiveGPURenderer] Creating GPU renderer...");

    // Validate bridge
    if (!bridge.is_valid()) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] Cannot create: GPU bridge is not valid");
        return nullptr;
    }

    if (bridge.get_backend() != GPUBackend::VULKAN) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] Cannot create: Only Vulkan backend is supported");
        return nullptr;
    }

    // Create renderer instance
    auto renderer = std::unique_ptr<RiveGPURenderer>(new RiveGPURenderer(config));

    // Initialize Vulkan context
    if (!renderer->initVulkan(bridge)) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to initialize Vulkan context");
        return nullptr;
    }

    // Store bridge reference before creating textures (needed for Godot texture creation)
    renderer->m_bridge = &bridge;

    // Create render target
    if (!renderer->createRenderTarget()) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create render target");
        return nullptr;
    }

    // Create Godot texture
    if (!renderer->createGodotTexture()) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create Godot texture");
        return nullptr;
    }

    renderer->m_valid = true;

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Created successfully (",
        config.width, "x", config.height, ")");

    return renderer;
}

// =============================================================================
// VULKAN INITIALIZATION
// =============================================================================

bool RiveGPURenderer::initVulkan(const RiveGPUBridge& bridge) {
    godot::UtilityFunctions::print("[RiveGPURenderer] Initializing Vulkan context...");

    // Get vkGetInstanceProcAddr
    PFN_vkGetInstanceProcAddr procAddr = getVkGetInstanceProcAddr();
    if (!procAddr) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Cannot get vkGetInstanceProcAddr");
        return false;
    }

    // Extract Vulkan handles from bridge
    m_vkInstance = reinterpret_cast<VkInstance>(bridge.get_vulkan_instance());
    m_vkPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(bridge.get_vulkan_physical_device());
    m_vkDevice = reinterpret_cast<VkDevice>(bridge.get_vulkan_device());

    if (!m_vkInstance || !m_vkPhysicalDevice || !m_vkDevice) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Missing Vulkan handles from bridge");
        return false;
    }

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] VkInstance: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkInstance), 16));
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] VkPhysicalDevice: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkPhysicalDevice), 16));
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] VkDevice: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkDevice), 16));

    // Configure Vulkan features
    // MoltenVK on macOS is a "portability subset" implementation
    rive::gpu::VulkanFeatures features;
    features.apiVersion = VK_API_VERSION_1_1;

    // These features should be enabled if supported by the device
    // For MoltenVK/Godot, we assume conservative defaults
    features.independentBlend = true;
    features.fillModeNonSolid = false;  // Not always available on MoltenVK
    features.fragmentStoresAndAtomics = true;
    features.shaderClipDistance = true;

    // MoltenVK-specific flag
#ifdef __APPLE__
    features.VK_KHR_portability_subset = true;
#endif

    // Extensions that may not be available everywhere
    features.rasterizationOrderColorAttachmentAccess = false;
    features.fragmentShaderPixelInterlock = false;

    // Configure context options
    rive::gpu::RenderContextVulkanImpl::ContextOptions contextOptions;
    contextOptions.forceAtomicMode = m_config.forceAtomicMode;

    // Create the Rive render context
    try {
        m_renderContext = rive::gpu::RenderContextVulkanImpl::MakeContext(
            m_vkInstance,
            m_vkPhysicalDevice,
            m_vkDevice,
            features,
            procAddr,
            contextOptions
        );
    } catch (const std::exception& e) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Exception creating context: ", e.what());
        return false;
    }

    if (!m_renderContext) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] RenderContext creation returned null");
        return false;
    }

    godot::UtilityFunctions::print("[RiveGPURenderer] Vulkan context initialized");
    return true;
}

// =============================================================================
// RENDER TARGET
// =============================================================================

bool RiveGPURenderer::createRenderTarget() {
    if (!m_renderContext) {
        return false;
    }

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Creating render target (",
        m_width, "x", m_height, ")...");

    // Get the Vulkan implementation
    auto* vulkanImpl = static_cast<rive::gpu::RenderContextVulkanImpl*>(
        m_renderContext->static_impl_cast<rive::gpu::RenderContextVulkanImpl>()
    );

    if (!vulkanImpl) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to cast to RenderContextVulkanImpl");
        return false;
    }

    // Create render target with RGBA8 format (matches Godot's texture format)
    // VK_FORMAT_R8G8B8A8_UNORM = 37
    constexpr VkFormat framebufferFormat = VK_FORMAT_R8G8B8A8_UNORM;

    // Usage flags for the render target
    constexpr VkImageUsageFlags usageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    try {
        m_renderTarget = vulkanImpl->makeRenderTarget(
            m_width,
            m_height,
            framebufferFormat,
            usageFlags
        );
    } catch (const std::exception& e) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Exception creating render target: ", e.what());
        return false;
    }

    if (!m_renderTarget) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create render target");
        return false;
    }

    // Create the RiveRenderer for drawing (note: RiveRenderer is in rive:: namespace)
    m_renderer = std::make_unique<rive::RiveRenderer>(m_renderContext.get());

    godot::UtilityFunctions::print("[RiveGPURenderer] Render target created");
    return true;
}

bool RiveGPURenderer::createGodotTexture() {
    if (!m_bridge || !m_bridge->get_rendering_device()) {
        return false;
    }

    godot::UtilityFunctions::print("[RiveGPURenderer] Creating Godot texture...");

    // For now, create a standard Godot texture
    // In the future, we can use texture_create_from_extension to share VkImage directly
    m_godotTextureRID = m_bridge->create_shared_texture(m_width, m_height);

    if (!m_godotTextureRID.is_valid()) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create Godot texture");
        return false;
    }

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Godot texture created (RID: ",
        m_godotTextureRID.get_id(), ")");

    return true;
}

// =============================================================================
// RESIZE
// =============================================================================

bool RiveGPURenderer::resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) {
        return true;  // No change needed
    }

    if (width == 0 || height == 0) {
        return false;  // Invalid dimensions
    }

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Resizing from ", m_width, "x", m_height,
        " to ", width, "x", height);

    m_width = width;
    m_height = height;

    // Recreate render target with new dimensions
    m_renderTarget.reset();
    if (!createRenderTarget()) {
        m_valid = false;
        return false;
    }

    // Recreate Godot texture
    if (m_godotTextureRID.is_valid() && m_bridge && m_bridge->get_rendering_device()) {
        m_bridge->get_rendering_device()->free_rid(m_godotTextureRID);
    }
    if (!createGodotTexture()) {
        m_valid = false;
        return false;
    }

    return true;
}

// =============================================================================
// FRAME RENDERING
// =============================================================================

bool RiveGPURenderer::beginFrame() {
    if (!m_valid || !m_renderContext) {
        return false;
    }

    if (m_frameInProgress) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] beginFrame() called while frame already in progress");
        return false;
    }

    // Begin a new frame with the Rive render context
    rive::gpu::RenderContext::FrameDescriptor frameDesc;
    frameDesc.renderTargetWidth = m_width;
    frameDesc.renderTargetHeight = m_height;
    frameDesc.clearColor = 0x00000000;  // Transparent black

    m_renderContext->beginFrame(frameDesc);
    m_frameInProgress = true;

    return true;
}

void RiveGPURenderer::draw(rive::Artboard* artboard, const rive::Mat2D* transform) {
    if (!m_valid || !m_frameInProgress || !artboard || !m_renderer) {
        return;
    }

    // Save renderer state
    m_renderer->save();

    // Apply transform if provided
    if (transform) {
        m_renderer->transform(*transform);
    }

    // Draw the artboard
    artboard->draw(m_renderer.get());

    // Restore renderer state
    m_renderer->restore();
}

bool RiveGPURenderer::endFrame() {
    if (!m_valid || !m_frameInProgress || !m_renderContext || !m_renderTarget) {
        return false;
    }

    // Flush the render context to the render target
    // FlushResources uses currentFrameNumber and safeFrameNumber for resource lifetime management
    m_renderContext->flush({
        .renderTarget = m_renderTarget.get(),
        .externalCommandBuffer = nullptr,  // Let Rive manage command buffers
        .currentFrameNumber = m_frameNumber,
        .safeFrameNumber = m_frameNumber > 2 ? m_frameNumber - 2 : 0  // 2-frame latency for safety
    });

    m_frameNumber++;

    m_frameInProgress = false;

    // TODO: Implement texture synchronization with Godot
    // This will involve:
    // 1. Using vkCmdCopyImage to copy from Rive's render target to Godot's texture
    // 2. Or using texture_create_from_extension for zero-copy sharing
    // 3. Proper synchronization with Godot's render thread

    return true;
}

// =============================================================================
// DIAGNOSTICS
// =============================================================================

void RiveGPURenderer::printDiagnostics() const {
    godot::UtilityFunctions::print("=== RiveGPURenderer Diagnostics ===");
    godot::UtilityFunctions::print("  Valid: ", m_valid ? "yes" : "no");
    godot::UtilityFunctions::print("  Dimensions: ", m_width, "x", m_height);
    godot::UtilityFunctions::print("  Frame Number: ", m_frameNumber);
    godot::UtilityFunctions::print("  Frame In Progress: ", m_frameInProgress ? "yes" : "no");

    godot::UtilityFunctions::print("  VkInstance: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkInstance), 16));
    godot::UtilityFunctions::print("  VkPhysicalDevice: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkPhysicalDevice), 16));
    godot::UtilityFunctions::print("  VkDevice: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkDevice), 16));

    godot::UtilityFunctions::print("  RenderContext: ",
        m_renderContext ? "created" : "null");
    godot::UtilityFunctions::print("  RenderTarget: ",
        m_renderTarget ? "created" : "null");
    godot::UtilityFunctions::print("  RiveRenderer: ",
        m_renderer ? "created" : "null");

    godot::UtilityFunctions::print("  Godot Texture RID: ",
        m_godotTextureRID.is_valid() ? "valid" : "invalid");

    godot::UtilityFunctions::print("=== End Diagnostics ===");
}

} // namespace rive_godot

#endif // RIVE_GPU_RENDERER && RIVE_VULKAN
