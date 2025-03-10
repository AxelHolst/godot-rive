#ifndef _RIVEEXTENSION_RIVE_RENDER_CONTEXT_HPP_
#define _RIVEEXTENSION_RIVE_RENDER_CONTEXT_HPP_

// rive-cpp
#include <rive/renderer.hpp>
#include <rive/renderer/render_context_helper_impl.hpp>
#include <rive/renderer/texture.hpp>
#include <utils/factory_utils.hpp>

// extension
#include "utils/types.hpp"

using namespace rive;
using namespace rive::gpu;

class BufferRingNULL : public BufferRing {
   public:
    BufferRingNULL(size_t capacityInBytes) : BufferRing(capacityInBytes) {}

   private:
    void* onMapBuffer(int bufferIdx, size_t bytesWritten) {
        return shadowBuffer();
    }

    void onUnmapAndSubmitBuffer(int bufferIdx, size_t bytesWritten) {}
};

class RenderContextNULL : public RenderContextHelperImpl {
   public:
    static Ptr<RenderContext> MakeContext() {
        return std::make_unique<RenderContext>(std::make_unique<RenderContextNULL>());
    }

    RenderContextNULL() {
        m_platformFeatures.supportsRasterOrdering = true;
        m_platformFeatures.supportsFragmentShaderAtomics = true;
        m_platformFeatures.supportsClockwiseAtomicRendering = true;
    }

    rcp<RenderTarget> makeRenderTarget(uint32_t width, uint32_t height) {
        class RenderTargetNULL : public RenderTarget {
           public:
            RenderTargetNULL(uint32_t width, uint32_t height) : RenderTarget(width, height) {}
        };

        return make_rcp<RenderTargetNULL>(width, height);
    }

   private:
    rcp<RenderBuffer> makeRenderBuffer(RenderBufferType type, rive::RenderBufferFlags flags, size_t sizeInBytes)
        override {
        return make_rcp<DataRenderBuffer>(type, flags, sizeInBytes);
    }

    rcp<rive::gpu::Texture> makeImageTexture(
        uint32_t width, uint32_t height, uint32_t mipLevelCount, const uint8_t imageDataRGBA[]
    ) override {
        return make_rcp<rive::gpu::Texture>(width, height);
    }

    Ptr<BufferRing> makeUniformBufferRing(size_t capacityInBytes) override {
        return std::make_unique<BufferRingNULL>(capacityInBytes);
    }

    Ptr<BufferRing> makeStorageBufferRing(size_t capacityInBytes, StorageBufferStructure) override {
        return std::make_unique<BufferRingNULL>(capacityInBytes);
    }

    Ptr<BufferRing> makeVertexBufferRing(size_t capacityInBytes) override {
        return std::make_unique<BufferRingNULL>(capacityInBytes);
    }

    void resizeGradientTexture(uint32_t width, uint32_t height) override {}

    void resizeTessellationTexture(uint32_t width, uint32_t height) override {}

    void resizeAtlasTexture(uint32_t width, uint32_t height) override {}

    void resizeCoverageBuffer(size_t) override {}

    void flush(const FlushDescriptor&) override {}
};

#endif