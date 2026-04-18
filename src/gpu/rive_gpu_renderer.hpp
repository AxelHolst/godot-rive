/**
 * @file rive_gpu_renderer.hpp
 * @brief GPU Renderer wrapper for Rive PLS (Pixel Local Storage) Renderer
 *
 * This class provides the GPU rendering path for Rive animations in Godot.
 * It wraps the Rive PLS Renderer's RenderContext and integrates with Godot's
 * RenderingDevice API for texture sharing.
 *
 * Architecture:
 *   RiveGPURenderer
 *       |
 *       +-> RenderContextVulkanImpl (Rive's Vulkan backend)
 *       |       |
 *       |       +-> VkDevice (extracted from Godot via RiveGPUBridge)
 *       |       +-> VkPhysicalDevice
 *       |       +-> VkInstance
 *       |
 *       +-> RenderTarget (Vulkan image shared with Godot)
 *       |
 *       +-> RiveRenderer (draws artboard to render target)
 *
 * Frame Flow:
 *   1. beginFrame() - Prepare GPU resources for rendering
 *   2. draw(artboard) - Record draw commands via RiveRenderer
 *   3. endFrame() - Flush to render target, sync with Godot
 *   4. getGodotTextureRID() - Return Godot RID for the rendered frame
 *
 * @note This requires RIVE_GPU_RENDERER and RIVE_VULKAN to be defined.
 * @note Vulkan backend is used on all platforms (macOS via MoltenVK).
 */

#ifndef _RIVEEXTENSION_GPU_RENDERER_HPP_
#define _RIVEEXTENSION_GPU_RENDERER_HPP_

#if defined(RIVE_GPU_RENDERER) && defined(RIVE_VULKAN)

// Godot headers
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// Standard library
#include <memory>
#include <cstdint>
#include <dlfcn.h>

// Vulkan headers (via Rive's setup)
#include <vulkan/vulkan.h>

// Rive headers
#include <rive/artboard.hpp>
#include <rive/animation/state_machine_instance.hpp>

// Rive GPU Renderer headers
#include <rive/renderer/render_context.hpp>
#include <rive/renderer/rive_renderer.hpp>
#include <rive/renderer/vulkan/render_context_vulkan_impl.hpp>
#include <rive/renderer/vulkan/render_target_vulkan.hpp>
#include <rive/renderer/vulkan/vulkan_context.hpp>

// Extension headers
#include "rive_gpu_bridge.hpp"

namespace rive_godot {

/**
 * @brief Configuration options for GPU renderer initialization
 */
struct GPURendererConfig {
    uint32_t width = 256;
    uint32_t height = 256;
    bool enableMSAA = false;        // Multi-sample anti-aliasing
    bool forceAtomicMode = false;   // Force atomic blend mode (slower but more compatible)
};

/**
 * @brief GPU Renderer for Rive animations
 *
 * This class manages the Rive PLS Renderer lifecycle and provides
 * a simple interface for rendering artboards to GPU textures.
 *
 * Usage:
 *   auto renderer = RiveGPURenderer::create(bridge, config);
 *   if (renderer && renderer->is_valid()) {
 *       renderer->beginFrame();
 *       renderer->draw(artboard, stateMachine);
 *       renderer->endFrame();
 *       // Use renderer->getGodotTextureRID() to draw in Godot
 *   }
 */
class RiveGPURenderer {
public:
    /**
     * @brief Create a GPU renderer from an existing GPU bridge
     * @param bridge The RiveGPUBridge with extracted device handles
     * @param config Renderer configuration options
     * @return Unique pointer to renderer, or nullptr on failure
     */
    static std::unique_ptr<RiveGPURenderer> create(
        const RiveGPUBridge& bridge,
        const GPURendererConfig& config = {}
    );

    ~RiveGPURenderer();

    // Non-copyable, non-movable (owns GPU resources)
    RiveGPURenderer(const RiveGPURenderer&) = delete;
    RiveGPURenderer& operator=(const RiveGPURenderer&) = delete;
    RiveGPURenderer(RiveGPURenderer&&) = delete;
    RiveGPURenderer& operator=(RiveGPURenderer&&) = delete;

    /**
     * @brief Check if the renderer was initialized successfully
     */
    bool is_valid() const { return m_valid; }

    /**
     * @brief Get the current render target width
     */
    uint32_t width() const { return m_width; }

    /**
     * @brief Get the current render target height
     */
    uint32_t height() const { return m_height; }

    /**
     * @brief Resize the render target
     * @param width New width in pixels
     * @param height New height in pixels
     * @return true if resize succeeded
     */
    bool resize(uint32_t width, uint32_t height);

    // =========================================================================
    // FRAME RENDERING API
    // =========================================================================

    /**
     * @brief Begin a new frame
     * @return true if frame was started successfully
     *
     * Call this before any draw operations. Must be paired with endFrame().
     */
    bool beginFrame();

    /**
     * @brief Draw an artboard to the render target
     * @param artboard The artboard to render
     * @param transform Optional 2D transform matrix (nullptr = identity)
     *
     * The artboard is drawn using the current state machine state.
     * Multiple artboards can be drawn in a single frame.
     */
    void draw(rive::Artboard* artboard, const rive::Mat2D* transform = nullptr);

    /**
     * @brief End the current frame and flush to GPU
     * @return true if frame was completed successfully
     *
     * This submits all draw commands to the GPU and synchronizes
     * with Godot's rendering pipeline.
     */
    bool endFrame();

    // =========================================================================
    // GODOT INTEGRATION
    // =========================================================================

    /**
     * @brief Get the Godot RID for the rendered texture
     * @return RID that can be used with CanvasItem.draw_texture_rect_region()
     *
     * The texture is updated after each endFrame() call.
     * Returns an invalid RID if the renderer is not initialized.
     */
    godot::RID getGodotTextureRID() const { return m_godotTextureRID; }

    /**
     * @brief Print diagnostic information about the renderer
     */
    void printDiagnostics() const;

private:
    explicit RiveGPURenderer(const GPURendererConfig& config);

    /**
     * @brief Initialize the Vulkan render context
     * @param bridge GPU bridge with device handles
     * @return true if initialization succeeded
     */
    bool initVulkan(const RiveGPUBridge& bridge);

    /**
     * @brief Create or recreate the render target
     * @return true if render target was created successfully
     */
    bool createRenderTarget();

    /**
     * @brief Create Godot texture from Vulkan image
     * @return true if texture was created successfully
     */
    bool createGodotTexture();

    /**
     * @brief Get vkGetInstanceProcAddr function pointer
     * @return Function pointer, or nullptr if not available
     */
    static PFN_vkGetInstanceProcAddr getVkGetInstanceProcAddr();

    // State
    bool m_valid = false;
    bool m_frameInProgress = false;
    uint64_t m_frameNumber = 0;

    // Dimensions
    uint32_t m_width;
    uint32_t m_height;

    // Configuration
    GPURendererConfig m_config;

    // Rive GPU Renderer (Vulkan backend)
    std::unique_ptr<rive::gpu::RenderContext> m_renderContext;
    rive::rcp<rive::gpu::RenderTargetVulkan> m_renderTarget;
    std::unique_ptr<rive::RiveRenderer> m_renderer;  // Note: RiveRenderer is in rive:: namespace

    // Vulkan handles (owned by Godot, not us)
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkQueue m_vkQueue = VK_NULL_HANDLE;
    uint32_t m_vkQueueFamilyIndex = 0;

    // Vulkan validation/debug (for diagnosing MoltenVK issues)
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    static bool s_validationEnabled;
    bool setupValidationLayers();
    void destroyValidationLayers();
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    // Command buffer management (owned by us)
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_frameFence = VK_NULL_HANDLE;  // Fence for frame synchronization

    // Command buffer helpers
    bool createCommandResources();
    void destroyCommandResources();
    bool beginCommandBuffer();
    bool endAndSubmitCommandBuffer();

    // Godot texture integration
    godot::RID m_godotTextureRID;
    const RiveGPUBridge* m_bridge = nullptr;

    // Render target texture (owned by Rive's VulkanContext via VMA)
    // This ensures consistent memory allocation with Rive's internal buffers
    rive::rcp<rive::gpu::vkutil::Texture2D> m_renderTexture;

    // Texture sync helper
    bool syncTextureToGodot();   // Sync/copy pixels to Godot texture
    bool createRenderTexture();  // Create render texture via Rive's VMA
};

} // namespace rive_godot

#endif // RIVE_GPU_RENDERER && RIVE_VULKAN

#endif // _RIVEEXTENSION_GPU_RENDERER_HPP_
