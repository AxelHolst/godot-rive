/**
 * @file rive_gpu_bridge.hpp
 * @brief GPU device extraction bridge for Milestone 5 (GPU Rendering)
 *
 * This file provides utilities to extract native GPU device handles from
 * Godot's RenderingDevice API. These handles are required to initialize
 * the Rive GPU Renderer (rive::gpu::RenderContext).
 *
 * Platform Support:
 * - macOS: Vulkan via MoltenVK (Godot's default)
 * - Linux: Native Vulkan
 * - Windows: Vulkan or D3D12
 *
 * Usage:
 *   auto bridge = RiveGPUBridge::create();
 *   if (bridge && bridge->is_valid()) {
 *       auto renderer = RiveGPURenderer::create(*bridge, config);
 *   }
 */

#ifndef _RIVEEXTENSION_GPU_BRIDGE_HPP_
#define _RIVEEXTENSION_GPU_BRIDGE_HPP_

// Godot headers
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/rid.hpp>

// Standard library
#include <memory>
#include <cstdint>
#include <functional>

namespace rive_godot {

/**
 * @brief GPU backend type detected from Godot's RenderingDevice
 */
enum class GPUBackend {
    UNKNOWN,
    VULKAN,
    METAL,
    D3D12,
    OPENGL,
};

/**
 * @brief Vulkan function loader signature (PFN_vkGetInstanceProcAddr)
 */
using VkGetInstanceProcAddrFn = void* (*)(void* instance, const char* name);

/**
 * @brief Bridge class for extracting native GPU handles from Godot
 *
 * This class uses Godot's RenderingDevice::get_driver_resource() to
 * extract the underlying VkDevice, VkPhysicalDevice, and VkInstance
 * handles that are required by the Rive GPU Renderer.
 */
class RiveGPUBridge {
public:
    /**
     * @brief Create a GPU bridge instance
     * @return Unique pointer to bridge, or nullptr if RenderingDevice unavailable
     */
    static std::unique_ptr<RiveGPUBridge> create() {
        auto* rs = godot::RenderingServer::get_singleton();
        if (!rs) {
            godot::UtilityFunctions::push_warning(
                "[RiveGPUBridge] RenderingServer not available");
            return nullptr;
        }

        auto* rd = rs->get_rendering_device();
        if (!rd) {
            godot::UtilityFunctions::push_warning(
                "[RiveGPUBridge] RenderingDevice not available (software rendering?)");
            return nullptr;
        }

        return std::make_unique<RiveGPUBridge>(rd);
    }

    /**
     * @brief Construct bridge from RenderingDevice
     * @param rd Pointer to Godot's RenderingDevice
     */
    explicit RiveGPUBridge(godot::RenderingDevice* rd)
        : rendering_device(rd), backend(GPUBackend::UNKNOWN) {
        detect_backend();
        extract_device_handles();
    }

    /**
     * @brief Check if GPU device handles were successfully extracted
     */
    bool is_valid() const {
        if (backend == GPUBackend::VULKAN) {
            // For Vulkan, we need at minimum VkDevice and VkPhysicalDevice
            // VkInstance is also required for Rive's RenderContextVulkanImpl
            return vk_device != 0 && vk_physical_device != 0 && vk_instance != 0;
        }
        return false;
    }

    /**
     * @brief Check if we have partial Vulkan handles (missing VkInstance)
     */
    bool has_partial_vulkan() const {
        return backend == GPUBackend::VULKAN && vk_device != 0 && vk_physical_device != 0;
    }

    /**
     * @brief Get the detected GPU backend type
     */
    GPUBackend get_backend() const { return backend; }

    /**
     * @brief Get backend name as string for logging
     */
    const char* get_backend_name() const {
        switch (backend) {
            case GPUBackend::VULKAN: return "Vulkan";
            case GPUBackend::METAL: return "Metal";
            case GPUBackend::D3D12: return "Direct3D 12";
            case GPUBackend::OPENGL: return "OpenGL";
            default: return "Unknown";
        }
    }

    // =========================================================================
    // VULKAN HANDLES
    // =========================================================================

    /**
     * @brief Get Vulkan instance handle (VkInstance)
     * @return VkInstance cast to uint64_t, or 0 if not available
     */
    uint64_t get_vulkan_instance() const { return vk_instance; }

    /**
     * @brief Get Vulkan physical device handle (VkPhysicalDevice)
     * @return VkPhysicalDevice cast to uint64_t, or 0 if not available
     */
    uint64_t get_vulkan_physical_device() const { return vk_physical_device; }

    /**
     * @brief Get Vulkan device handle (VkDevice)
     * @return VkDevice cast to uint64_t, or 0 if not available
     */
    uint64_t get_vulkan_device() const { return vk_device; }

    /**
     * @brief Get Vulkan graphics queue (VkQueue)
     * @return VkQueue cast to uint64_t, or 0 if not available
     */
    uint64_t get_vulkan_queue() const { return vk_queue; }

    /**
     * @brief Get Vulkan queue family index
     * @return Queue family index, or UINT32_MAX if not available
     */
    uint32_t get_vulkan_queue_family_index() const { return vk_queue_family_index; }

    /**
     * @brief Get Metal device handle (MTLDevice*)
     * @return Pointer to MTLDevice, or nullptr if not available
     * @note On macOS with MoltenVK, this would require MVK extension
     */
    void* get_metal_device() const { return mtl_device; }

    /**
     * @brief Get the Godot RenderingDevice for texture operations
     */
    godot::RenderingDevice* get_rendering_device() const { return rendering_device; }

    // =========================================================================
    // TEXTURE OPERATIONS
    // =========================================================================

    /**
     * @brief Create a Godot texture that can be shared with Rive
     * @param width Texture width in pixels
     * @param height Texture height in pixels
     * @return RID of created texture, or invalid RID on failure
     */
    godot::RID create_shared_texture(uint32_t width, uint32_t height) const {
        if (!rendering_device) return godot::RID();

        // Create texture format - RGBA8 UNORM for compatibility
        godot::Ref<godot::RDTextureFormat> format;
        format.instantiate();
        format->set_format(godot::RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
        format->set_width(width);
        format->set_height(height);
        format->set_depth(1);
        format->set_array_layers(1);
        format->set_mipmaps(1);
        format->set_texture_type(godot::RenderingDevice::TEXTURE_TYPE_2D);
        format->set_samples(godot::RenderingDevice::TEXTURE_SAMPLES_1);
        format->set_usage_bits(
            godot::RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
            godot::RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
            godot::RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
            godot::RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT
        );

        // Create texture view
        godot::Ref<godot::RDTextureView> view;
        view.instantiate();

        // Create the texture
        return rendering_device->texture_create(format, view);
    }

    /**
     * @brief Get the underlying VkImage from a Godot texture RID
     * @param texture_rid RID of the Godot texture
     * @return VkImage cast to uint64_t, or 0 if not available
     */
    uint64_t get_texture_vulkan_image(godot::RID texture_rid) const {
        if (!rendering_device || !texture_rid.is_valid()) return 0;

        return rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_IMAGE,
            texture_rid,
            0
        );
    }

    /**
     * @brief Print diagnostic information about the GPU bridge
     */
    void print_diagnostics() const {
        godot::UtilityFunctions::print(
            "[RiveGPUBridge] Backend: ", get_backend_name());

        if (backend == GPUBackend::VULKAN) {
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkInstance: 0x",
                godot::String::num_int64(vk_instance, 16));
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkPhysicalDevice: 0x",
                godot::String::num_int64(vk_physical_device, 16));
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkDevice: 0x",
                godot::String::num_int64(vk_device, 16));
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkQueue: 0x",
                godot::String::num_int64(vk_queue, 16));
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] Queue Family Index: ", vk_queue_family_index);
        } else if (backend == GPUBackend::METAL) {
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] MTLDevice: 0x",
                godot::String::num_int64((uint64_t)mtl_device, 16));
        }

        godot::UtilityFunctions::print(
            "[RiveGPUBridge] Valid: ", is_valid() ? "yes" : "no");

        if (has_partial_vulkan() && !is_valid()) {
            godot::UtilityFunctions::push_warning(
                "[RiveGPUBridge] Partial Vulkan handles - missing VkInstance");
        }
    }

private:
    godot::RenderingDevice* rendering_device;
    GPUBackend backend;

    // Vulkan handles
    uint64_t vk_instance = 0;
    uint64_t vk_physical_device = 0;
    uint64_t vk_device = 0;
    uint64_t vk_queue = 0;
    uint32_t vk_queue_family_index = UINT32_MAX;

    // Metal handles (for future use)
    void* mtl_device = nullptr;

    /**
     * @brief Detect which GPU backend Godot is using
     */
    void detect_backend() {
        if (!rendering_device) return;

        // Try to get VkDevice to detect Vulkan backend
        uint64_t device = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_DEVICE,
            godot::RID(),
            0
        );

        if (device != 0) {
            backend = GPUBackend::VULKAN;
            return;
        }

        // Fallback: try generic LOGICAL_DEVICE
        device = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
            godot::RID(),
            0
        );

        if (device != 0) {
            // Assume Vulkan on desktop platforms
            backend = GPUBackend::VULKAN;
            return;
        }

        backend = GPUBackend::UNKNOWN;
    }

    /**
     * @brief Extract native device handles using RenderingDevice API
     */
    void extract_device_handles() {
        if (!rendering_device || backend != GPUBackend::VULKAN) return;

        // Extract VkInstance (DRIVER_RESOURCE_VULKAN_INSTANCE = 2)
        // This maps to DRIVER_RESOURCE_TOPMOST_OBJECT in the generic enum
        vk_instance = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE,
            godot::RID(),
            0
        );

        // Extract VkPhysicalDevice
        vk_physical_device = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE,
            godot::RID(),
            0
        );

        // Extract VkDevice
        vk_device = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_DEVICE,
            godot::RID(),
            0
        );

        // Extract VkQueue
        vk_queue = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_QUEUE,
            godot::RID(),
            0
        );

        // Extract queue family index
        vk_queue_family_index = static_cast<uint32_t>(
            rendering_device->get_driver_resource(
                godot::RenderingDevice::DRIVER_RESOURCE_VULKAN_QUEUE_FAMILY_INDEX,
                godot::RID(),
                0
            )
        );

        // Fallback: if VULKAN-specific enums don't work, try generic ones
        if (vk_device == 0) {
            vk_device = rendering_device->get_driver_resource(
                godot::RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                godot::RID(),
                0
            );
        }

        if (vk_physical_device == 0) {
            vk_physical_device = rendering_device->get_driver_resource(
                godot::RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE,
                godot::RID(),
                0
            );
        }

        if (vk_instance == 0) {
            // Try TOPMOST_OBJECT which maps to VkInstance in Vulkan
            vk_instance = rendering_device->get_driver_resource(
                godot::RenderingDevice::DRIVER_RESOURCE_TOPMOST_OBJECT,
                godot::RID(),
                0
            );
        }
    }
};

} // namespace rive_godot

#endif // _RIVEEXTENSION_GPU_BRIDGE_HPP_
