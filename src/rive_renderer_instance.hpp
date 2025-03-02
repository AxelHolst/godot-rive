#ifndef _RIVEEXTENSION_RIVE_RENDERER_INSTANCE_HPP_
#define _RIVEEXTENSION_RIVE_RENDERER_INSTANCE_HPP_

// godot-cpp
#include <godot_cpp/variant/builtin_types.hpp>

// rive-cpp
#include <glad.h>

#include <rive/renderer/gl/render_context_gl_impl.hpp>
#include <rive/renderer/render_context.hpp>
#include <rive/renderer/rive_renderer.hpp>

// extension
#include "utils/types.hpp"
#include "viewer_props.hpp"

using namespace godot;
using namespace rive;
using namespace gpu;

struct RiveRendererInstance {
    ViewerProps* props;

    // Ptr<RenderContext> context = RenderContextGLImpl::MakeContext();
    // Ptr<RiveRenderer> renderer = std::make_unique<RiveRenderer>(context.get());

    void set_props(ViewerProps* props_value) {
        props = props_value;
        if (props) {
            props->on_transform_changed([this]() { on_transform_changed(); });
        }
    }

    RenderContext::FrameDescriptor frame_descriptor() const {
        RenderContext::FrameDescriptor descriptor;
        descriptor.renderTargetWidth = props ? props->width() : 1;
        descriptor.renderTargetHeight = props ? props->height() : 1;
        return descriptor;
    }

    PackedByteArray bytes() const {
        // SkPixmap pixmap;
        PackedByteArray bytes;
        // if (!surface) return bytes;
        // if (!surface->peekPixels(&pixmap)) return bytes;
        // SkImageInfo info = surface->imageInfo();
        // size_t bytes_per_pixel = info.bytesPerPixel(), row_bytes = pixmap.rowBytes();
        // bytes.resize(row_bytes * info.height());
        // for (int y = 0; y < info.height(); y++) {
        //     for (int x = 0; x < info.width(); x++) {
        //         int offset = y * row_bytes + x * bytes_per_pixel;
        //         auto addr = pixmap.addr32(x, y);
        //         bytes.encode_u32(offset, *addr);
        //     }
        // }
        return bytes;
    }

    void clear() {
        // if (renderer) renderer->restore();
    }

   private:
    void on_transform_changed() {
        // context->beginFrame(frame_descriptor());
    }
};

#endif