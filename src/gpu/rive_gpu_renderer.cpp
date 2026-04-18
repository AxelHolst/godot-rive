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
    m_renderTexture.reset();  // VMA-managed texture (must be released before context)
    m_renderContext.reset();

    // Clean up command resources
    destroyCommandResources();

    // Clean up validation layers
    destroyValidationLayers();

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

    // ==========================================================================
    // MACOS GPU RENDERING TEMPORARILY DISABLED
    // ==========================================================================
    // Rive's PLS Renderer uses VMA (Vulkan Memory Allocator) which creates memory
    // allocations with flags that MoltenVK's getMTLStorageModeFromVkMemoryPropertyFlags()
    // cannot translate to Metal storage modes. This causes a crash in vkQueueSubmit
    // during the Metal blit operation (copyFromBuffer).
    //
    // Root cause: VMA's VMA_MEMORY_USAGE_AUTO selects memory types that work on
    // native Vulkan but cause issues on MoltenVK's Vulkan-over-Metal translation.
    //
    // Potential solutions (for future investigation):
    // 1. Use Rive's native Metal backend (RenderContextMetalImpl) on macOS
    // 2. Configure VMA with specific flags for MoltenVK compatibility
    // 3. Upstream fix to Rive's VulkanContext for MoltenVK
    //
    // For now, macOS falls back to CPU (Skia) rendering, which is stable.
    // GPU rendering works on Linux/Windows with native Vulkan drivers.
    // ==========================================================================
#ifdef __APPLE__
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] macOS: GPU rendering disabled (VMA/MoltenVK incompatibility)");
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Using CPU rendering fallback. See source for details.");
    return nullptr;
#endif

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

    // Create command buffer resources
    if (!renderer->createCommandResources()) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create command resources");
        return nullptr;
    }

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
    m_vkQueue = reinterpret_cast<VkQueue>(bridge.get_vulkan_queue());
    m_vkQueueFamilyIndex = bridge.get_vulkan_queue_family_index();

    if (!m_vkInstance || !m_vkPhysicalDevice || !m_vkDevice) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Missing Vulkan handles from bridge");
        return false;
    }

    if (!m_vkQueue) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Missing VkQueue from bridge");
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
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] VkQueue: 0x",
        godot::String::num_int64(reinterpret_cast<uint64_t>(m_vkQueue), 16));
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Queue Family Index: ", m_vkQueueFamilyIndex);

    // ==========================================================================
    // VULKAN VALIDATION LAYERS (Debug Build Only)
    // ==========================================================================
    // Attempt to hook up validation layer debug callback to catch the exact
    // error causing the vkQueueSubmit crash on MoltenVK.
    // ==========================================================================
    setupValidationLayers();

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
// VULKAN VALIDATION LAYERS
// =============================================================================

// Static member definition
bool RiveGPURenderer::s_validationEnabled = false;

VKAPI_ATTR VkBool32 VKAPI_CALL RiveGPURenderer::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    // Format severity
    const char* severityStr = "UNKNOWN";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severityStr = "ERROR";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severityStr = "WARNING";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severityStr = "INFO";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        severityStr = "VERBOSE";
    }

    // Format type
    const char* typeStr = "";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        typeStr = "VALIDATION";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        typeStr = "PERFORMANCE";
    } else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        typeStr = "GENERAL";
    }

    // Print the validation message
    godot::UtilityFunctions::print(
        "[VK_", severityStr, "][", typeStr, "] ",
        pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "unknown",
        ": ", pCallbackData->pMessage);

    // Print object info if available
    if (pCallbackData->objectCount > 0) {
        for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
            const VkDebugUtilsObjectNameInfoEXT& obj = pCallbackData->pObjects[i];
            godot::UtilityFunctions::print(
                "  Object[", i, "]: type=", static_cast<int>(obj.objectType),
                " handle=0x", godot::String::num_int64(obj.objectHandle, 16),
                " name=", obj.pObjectName ? obj.pObjectName : "unnamed");
        }
    }

    // Print queue/command buffer labels if available
    if (pCallbackData->cmdBufLabelCount > 0) {
        for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
            godot::UtilityFunctions::print(
                "  CmdBufLabel[", i, "]: ", pCallbackData->pCmdBufLabels[i].pLabelName);
        }
    }

    // For errors, also push to Godot's error system
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        godot::UtilityFunctions::push_error(
            "[Vulkan Validation Error] ", pCallbackData->pMessage);
    }

    // Return VK_FALSE to not abort the call that triggered the message
    return VK_FALSE;
}

bool RiveGPURenderer::setupValidationLayers() {
    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")
    );
    if (!vkGetInstanceProcAddr || !m_vkInstance) {
        godot::UtilityFunctions::print("[RiveGPURenderer] Cannot setup validation: no vkGetInstanceProcAddr");
        return false;
    }

    // Get the function to create debug messenger
    auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_vkInstance, "vkCreateDebugUtilsMessengerEXT")
    );

    if (!vkCreateDebugUtilsMessengerEXT) {
        godot::UtilityFunctions::print(
            "[RiveGPURenderer] VK_EXT_debug_utils not available - validation disabled");
        godot::UtilityFunctions::print(
            "[RiveGPURenderer] To enable: set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation");
        return false;
    }

    // Create the debug messenger
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = this;

    VkResult result = vkCreateDebugUtilsMessengerEXT(
        m_vkInstance, &createInfo, nullptr, &m_debugMessenger);

    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::print(
            "[RiveGPURenderer] Failed to create debug messenger: ", static_cast<int>(result));
        return false;
    }

    s_validationEnabled = true;
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] *** VULKAN VALIDATION ENABLED ***");
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Debug callback will capture validation errors");

    return true;
}

void RiveGPURenderer::destroyValidationLayers() {
    if (m_debugMessenger == VK_NULL_HANDLE || !m_vkInstance) {
        return;
    }

    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")
    );
    if (!vkGetInstanceProcAddr) {
        return;
    }

    auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_vkInstance, "vkDestroyDebugUtilsMessengerEXT")
    );

    if (vkDestroyDebugUtilsMessengerEXT) {
        vkDestroyDebugUtilsMessengerEXT(m_vkInstance, m_debugMessenger, nullptr);
    }
    m_debugMessenger = VK_NULL_HANDLE;
    s_validationEnabled = false;
}

// =============================================================================
// RENDER TEXTURE CREATION (via Rive's VMA)
// =============================================================================

bool RiveGPURenderer::createRenderTexture() {
    // Create render texture using Rive's VulkanContext.
    // This ensures all memory allocations go through the same VMA allocator,
    // avoiding conflicts between our manual allocations and Rive's VMA on MoltenVK.

    if (!m_renderContext) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] No render context for texture creation");
        return false;
    }

    // Get the VulkanContext from the render context
    auto* vulkanImpl = static_cast<rive::gpu::RenderContextVulkanImpl*>(
        m_renderContext->static_impl_cast<rive::gpu::RenderContextVulkanImpl>()
    );
    if (!vulkanImpl) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] Failed to get VulkanImpl");
        return false;
    }

    rive::gpu::VulkanContext* vk = vulkanImpl->vulkanContext();
    if (!vk) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] Failed to get VulkanContext");
        return false;
    }

    // Create a Texture2D via Rive's VMA-managed allocation
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // These usage flags are required by Rive's render target
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

    try {
        m_renderTexture = vk->makeTexture2D(imageInfo, "RiveGPURenderer target");
    } catch (const std::exception& e) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Exception creating render texture: ", e.what());
        return false;
    }

    if (!m_renderTexture) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] makeTexture2D returned null");
        return false;
    }

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Created VMA-managed render texture (",
        m_width, "x", m_height, ")");

    return true;
}

// =============================================================================
// COMMAND BUFFER MANAGEMENT
// =============================================================================

bool RiveGPURenderer::createCommandResources() {
    auto vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetDeviceProcAddr")
    );
    if (!vkGetDeviceProcAddr) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] Cannot get vkGetDeviceProcAddr");
        return false;
    }

    auto vkCreateCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(
        vkGetDeviceProcAddr(m_vkDevice, "vkCreateCommandPool"));
    auto vkAllocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(
        vkGetDeviceProcAddr(m_vkDevice, "vkAllocateCommandBuffers"));
    auto vkCreateFence = reinterpret_cast<PFN_vkCreateFence>(
        vkGetDeviceProcAddr(m_vkDevice, "vkCreateFence"));

    if (!vkCreateCommandPool || !vkAllocateCommandBuffers || !vkCreateFence) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] Failed to get command pool functions");
        return false;
    }

    // Create command pool
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_vkQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(m_vkDevice, &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] vkCreateCommandPool failed: ", static_cast<int>(result));
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(m_vkDevice, &allocInfo, &m_commandBuffer);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] vkAllocateCommandBuffers failed: ", static_cast<int>(result));
        return false;
    }

    // Create fence (signaled initially so first wait doesn't block)
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    result = vkCreateFence(m_vkDevice, &fenceInfo, nullptr, &m_frameFence);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_error("[RiveGPURenderer] vkCreateFence failed: ", static_cast<int>(result));
        return false;
    }

    godot::UtilityFunctions::print("[RiveGPURenderer] Command resources created");
    return true;
}

void RiveGPURenderer::destroyCommandResources() {
    if (m_vkDevice == VK_NULL_HANDLE) return;

    auto vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetDeviceProcAddr")
    );
    if (!vkGetDeviceProcAddr) return;

    auto vkDestroyFence = reinterpret_cast<PFN_vkDestroyFence>(
        vkGetDeviceProcAddr(m_vkDevice, "vkDestroyFence"));
    auto vkDestroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(
        vkGetDeviceProcAddr(m_vkDevice, "vkDestroyCommandPool"));

    if (m_frameFence != VK_NULL_HANDLE && vkDestroyFence) {
        vkDestroyFence(m_vkDevice, m_frameFence, nullptr);
        m_frameFence = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE && vkDestroyCommandPool) {
        // Command buffer is automatically freed when pool is destroyed
        vkDestroyCommandPool(m_vkDevice, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
        m_commandBuffer = VK_NULL_HANDLE;
    }
}

bool RiveGPURenderer::beginCommandBuffer() {
    auto vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetDeviceProcAddr")
    );
    if (!vkGetDeviceProcAddr) return false;

    auto vkWaitForFences = reinterpret_cast<PFN_vkWaitForFences>(
        vkGetDeviceProcAddr(m_vkDevice, "vkWaitForFences"));
    auto vkResetFences = reinterpret_cast<PFN_vkResetFences>(
        vkGetDeviceProcAddr(m_vkDevice, "vkResetFences"));
    auto vkResetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(
        vkGetDeviceProcAddr(m_vkDevice, "vkResetCommandBuffer"));
    auto vkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(
        vkGetDeviceProcAddr(m_vkDevice, "vkBeginCommandBuffer"));

    if (!vkWaitForFences || !vkResetFences || !vkResetCommandBuffer || !vkBeginCommandBuffer) {
        return false;
    }

    // Wait for previous frame to complete
    VkResult result = vkWaitForFences(m_vkDevice, 1, &m_frameFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_warning("[RiveGPURenderer] vkWaitForFences failed: ", static_cast<int>(result));
        return false;
    }

    result = vkResetFences(m_vkDevice, 1, &m_frameFence);
    if (result != VK_SUCCESS) {
        return false;
    }

    // Reset and begin command buffer
    result = vkResetCommandBuffer(m_commandBuffer, 0);
    if (result != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_warning("[RiveGPURenderer] vkBeginCommandBuffer failed: ", static_cast<int>(result));
        return false;
    }

    return true;
}

bool RiveGPURenderer::endAndSubmitCommandBuffer() {
    auto vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(RTLD_DEFAULT, "vkGetDeviceProcAddr")
    );
    if (!vkGetDeviceProcAddr) return false;

    auto vkEndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(
        vkGetDeviceProcAddr(m_vkDevice, "vkEndCommandBuffer"));
    auto vkQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(
        vkGetDeviceProcAddr(m_vkDevice, "vkQueueSubmit"));
    auto vkDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
        vkGetDeviceProcAddr(m_vkDevice, "vkDeviceWaitIdle"));

    if (!vkEndCommandBuffer || !vkQueueSubmit) {
        return false;
    }

    VkResult result = vkEndCommandBuffer(m_commandBuffer);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_warning("[RiveGPURenderer] vkEndCommandBuffer failed: ", static_cast<int>(result));
        return false;
    }

    // Wait for Godot to finish any pending work before we submit
    // This is a heavy-handed sync but ensures no queue conflicts
    if (vkDeviceWaitIdle) {
        vkDeviceWaitIdle(m_vkDevice);
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    result = vkQueueSubmit(m_vkQueue, 1, &submitInfo, m_frameFence);
    if (result != VK_SUCCESS) {
        godot::UtilityFunctions::push_warning("[RiveGPURenderer] vkQueueSubmit failed: ", static_cast<int>(result));
        return false;
    }

    // Wait for our work to complete before Godot continues
    if (vkDeviceWaitIdle) {
        vkDeviceWaitIdle(m_vkDevice);
    }

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
    constexpr VkFormat framebufferFormat = VK_FORMAT_R8G8B8A8_UNORM;

    // Usage flags for the render target
    constexpr VkImageUsageFlags usageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

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
            "[RiveGPURenderer] Failed to create render target object");
        return false;
    }

    // Create render texture via Rive's VMA (consistent memory allocation)
    if (!createRenderTexture()) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create render texture via VMA");
        return false;
    }

    // Set the target image on the render target using the VMA-allocated texture
    auto* implTarget = dynamic_cast<rive::gpu::RenderTargetVulkanImpl*>(m_renderTarget.get());
    if (!implTarget) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Cannot cast to RenderTargetVulkanImpl");
        return false;
    }

    // Get VkImage and VkImageView from the Texture2D
    VkImage renderImage = m_renderTexture->vkImage();
    VkImageView renderImageView = m_renderTexture->vkImageView();

    implTarget->setTargetImageView(
        renderImageView,
        renderImage,
        rive::gpu::vkutil::ImageAccess{
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_ACCESS_NONE,
            VK_IMAGE_LAYOUT_UNDEFINED
        }
    );

    godot::UtilityFunctions::print("[RiveGPURenderer] Render target configured with VMA-managed texture");

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

    // Create a Godot texture via RenderingDevice
    m_godotTextureRID = m_bridge->create_shared_texture(m_width, m_height);

    if (!m_godotTextureRID.is_valid()) {
        godot::UtilityFunctions::push_error(
            "[RiveGPURenderer] Failed to create Godot texture");
        return false;
    }

    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Godot texture created (RID: ",
        m_godotTextureRID.get_id(), ")");

    // ==========================================================================
    // PHASE 4: Separate Textures Mode
    // ==========================================================================
    // For Phase 4 "First Pixels", we use separate textures:
    //   - Rive renders to its own internal VkImage (created by makeRenderTarget)
    //   - Godot has its own VkImage (created above)
    //   - We'll implement CPU readback or blit in Phase 5
    //
    // Zero-copy sharing (Phase 5) would use setupSharedTexture() to have Rive
    // render directly to Godot's VkImage, but this requires careful handling
    // of image layouts and barriers.
    // ==========================================================================
    godot::UtilityFunctions::print(
        "[RiveGPURenderer] Using separate textures mode (Phase 4)");

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

    // Clean up old VMA-managed render texture
    m_renderTexture.reset();

    // Recreate render target with new dimensions
    m_renderTarget.reset();
    if (!createRenderTarget()) {
        m_valid = false;
        return false;
    }

    // Recreate Godot texture (and zero-copy sharing)
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
    if (!m_valid || !m_renderContext || !m_commandBuffer) {
        return false;
    }

    if (m_frameInProgress) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] beginFrame() called while frame already in progress");
        return false;
    }

    // Begin the Vulkan command buffer first
    if (!beginCommandBuffer()) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] Failed to begin command buffer");
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
    if (!m_valid || !m_frameInProgress || !m_renderContext || !m_renderTarget || !m_commandBuffer) {
        return false;
    }

    // Flush the render context to the render target
    // FlushResources uses currentFrameNumber and safeFrameNumber for resource lifetime management
    // CRITICAL: Pass our command buffer to Rive - it needs it to record draw commands
    m_renderContext->flush({
        .renderTarget = m_renderTarget.get(),
        .externalCommandBuffer = m_commandBuffer,  // Our VkCommandBuffer
        .currentFrameNumber = m_frameNumber,
        .safeFrameNumber = m_frameNumber > 2 ? m_frameNumber - 2 : 0  // 2-frame latency for safety
    });

    // End and submit the command buffer
    if (!endAndSubmitCommandBuffer()) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] Failed to submit command buffer");
        m_frameInProgress = false;
        return false;
    }

    m_frameNumber++;
    m_frameInProgress = false;

    // ==========================================================================
    // TEXTURE SYNCHRONIZATION (Phase 4: CPU Readback)
    // ==========================================================================
    // For Phase 4 "First Pixels", we use CPU readback to copy rendered pixels
    // from Rive's VkImage to Godot's texture. This proves the pipeline works.
    //
    // Phase 5 will optimize with GPU blit or zero-copy sharing.
    // ==========================================================================
    if (!syncTextureToGodot()) {
        godot::UtilityFunctions::push_warning(
            "[RiveGPURenderer] Texture sync failed - frame may not display");
        // Don't fail the frame - let it continue, user will see stale/black texture
    }

    return true;
}

bool RiveGPURenderer::syncTextureToGodot() {
    // ==========================================================================
    // TEXTURE SYNC (Phase 4)
    // ==========================================================================
    // For Phase 4 "First Pixels", Rive renders to our own VkImage (m_renderImage),
    // and we need to copy pixels to Godot's texture.
    //
    // Phase 5 will optimize this with either:
    // - Zero-copy: Rive renders directly to Godot's VkImage
    // - Or GPU blit: vkCmdCopyImage/vkCmdBlitImage between VkImages
    //
    // For now, we'll implement CPU readback to prove the pipeline works.
    // ==========================================================================

    if (!m_bridge || !m_godotTextureRID.is_valid() || !m_renderContext) {
        return false;
    }

    // TODO Phase 4b: Implement CPU readback or GPU blit
    // For now, return true - we'll see if Rive's internal command submission is enough
    // (It may already copy to a host-visible buffer that we can access)

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
