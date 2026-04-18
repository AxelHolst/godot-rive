/**
 * @file rive_gpu_bridge.hpp
 * @brief GPU device extraction bridge for Milestone 5 (GPU Rendering)
 *
 * This file provides utilities to extract native GPU device handles from
 * Godot's RenderingDevice API. These handles are required to initialize
 * the Rive GPU Renderer (rive::gpu::RenderContext).
 *
 * Platform Support:
 * - macOS: MTLDevice extraction (Metal)
 * - Linux: VkDevice extraction (Vulkan via MoltenVK or native)
 *
 * Usage:
 *   auto bridge = RiveGPUBridge::create();
 *   if (bridge && bridge->is_valid()) {
 *       // Use bridge->get_metal_device() or bridge->get_vulkan_device()
 *   }
 *
 * @note This is infrastructure for Milestone 5. The actual GPU renderer
 *       integration will be implemented once Metal shader compilation
 *       is available (requires full Xcode installation).
 */

#ifndef _RIVEEXTENSION_GPU_BRIDGE_HPP_
#define _RIVEEXTENSION_GPU_BRIDGE_HPP_

// Godot headers
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// Standard library
#include <memory>
#include <cstdint>

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
 * @brief Bridge class for extracting native GPU handles from Godot
 *
 * This class uses Godot's RenderingDevice::get_driver_resource() to
 * extract the underlying VkDevice (Vulkan) or MTLDevice (Metal) handles
 * that are required by the Rive GPU Renderer.
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
        return backend != GPUBackend::UNKNOWN &&
               (vk_device != 0 || mtl_device != nullptr);
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

    /**
     * @brief Get Vulkan device handle (VkDevice)
     * @return VkDevice cast to uint64_t, or 0 if not available
     */
    uint64_t get_vulkan_device() const { return vk_device; }

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
     * @brief Get Metal device handle (MTLDevice*)
     * @return Pointer to MTLDevice, or nullptr if not available
     * @note On macOS, this is an Objective-C object pointer
     */
    void* get_metal_device() const { return mtl_device; }

    /**
     * @brief Print diagnostic information about the GPU bridge
     */
    void print_diagnostics() const {
        godot::UtilityFunctions::print(
            "[RiveGPUBridge] Backend: ", get_backend_name());

        if (backend == GPUBackend::VULKAN) {
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkInstance: ", vk_instance);
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkPhysicalDevice: ", vk_physical_device);
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] VkDevice: ", vk_device);
        } else if (backend == GPUBackend::METAL) {
            godot::UtilityFunctions::print(
                "[RiveGPUBridge] MTLDevice: ", (uint64_t)mtl_device);
        }

        godot::UtilityFunctions::print(
            "[RiveGPUBridge] Valid: ", is_valid() ? "yes" : "no");
    }

private:
    godot::RenderingDevice* rendering_device;
    GPUBackend backend;

    // Vulkan handles
    uint64_t vk_instance = 0;
    uint64_t vk_physical_device = 0;
    uint64_t vk_device = 0;

    // Metal handles
    void* mtl_device = nullptr;

    /**
     * @brief Detect which GPU backend Godot is using
     */
    void detect_backend() {
        if (!rendering_device) return;

        // Try to get device name to detect backend
        // Godot 4.x uses Vulkan by default on all platforms except web
        // macOS uses MoltenVK (Vulkan over Metal) or native Metal

        // Try Vulkan first (most common)
        uint64_t device = rendering_device->get_driver_resource(
            godot::RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
            godot::RID(),
            0
        );

        if (device != 0) {
            // Check if this is MoltenVK on macOS or native Vulkan
#ifdef __APPLE__
            // On macOS, Godot uses Vulkan via MoltenVK
            // We could also use Metal directly for better performance
            backend = GPUBackend::VULKAN;
#else
            backend = GPUBackend::VULKAN;
#endif
            return;
        }

        // Fallback: assume unknown
        backend = GPUBackend::UNKNOWN;
    }

    /**
     * @brief Extract native device handles using RenderingDevice API
     */
    void extract_device_handles() {
        if (!rendering_device) return;

        // Extract based on detected backend
        if (backend == GPUBackend::VULKAN) {
            // DRIVER_RESOURCE_VULKAN_DEVICE = VkDevice
            // DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE = VkPhysicalDevice
            // DRIVER_RESOURCE_VULKAN_INSTANCE = VkInstance

            // Note: These enum values map to Godot's DriverResource enum
            // DRIVER_RESOURCE_LOGICAL_DEVICE is the VkDevice
            vk_device = rendering_device->get_driver_resource(
                godot::RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                godot::RID(),
                0
            );

            // Physical device
            vk_physical_device = rendering_device->get_driver_resource(
                godot::RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE,
                godot::RID(),
                0
            );

            // Instance
            // Note: DRIVER_RESOURCE_VULKAN_INSTANCE might not be directly exposed
            // May need to use different approach for VkInstance
        }

#ifdef __APPLE__
        // On macOS, we might also want to extract the underlying MTLDevice
        // from MoltenVK for potential future Metal-native rendering
        // This would require MoltenVK's vkGetMTLDeviceMVK() extension
#endif
    }
};

} // namespace rive_godot

#endif // _RIVEEXTENSION_GPU_BRIDGE_HPP_
