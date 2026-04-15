#ifndef _RIVEEXTENSION_SKIA_INSTANCE_HPP_
#define _RIVEEXTENSION_SKIA_INSTANCE_HPP_

// godot-cpp
#include <godot_cpp/variant/builtin_types.hpp>

// skia
#include <skia/dependencies/skia/include/core/SkBitmap.h>
#include <skia/dependencies/skia/include/core/SkCanvas.h>
#include <skia/dependencies/skia/include/core/SkSurface.h>
#include <memory>

#include <skia/renderer/include/skia_factory.hpp>
#include <skia/renderer/include/skia_renderer.hpp>

// extension
#include "utils/types.hpp"
#include "viewer_props.hpp"

using namespace godot;

struct SkiaInstance {
    ViewerProps *props;
    SkBitmap bitmap;
    std::unique_ptr<SkCanvas> canvas;
    // Note: Factory is now a global singleton (see rive_global.hpp)
    // Do NOT create per-instance factories - this causes memory corruption

    void set_props(ViewerProps *props_value) {
        props = props_value;
        if (props) {
            props->on_transform_changed([this]() { on_transform_changed(); });
        }
    }

    SkImageInfo image_info() const {
        // Use N32Premul format like rive-runtime examples
        return SkImageInfo::MakeN32Premul(
            props ? props->width() : 1,
            props ? props->height() : 1
        );
    }

    SkCanvas* getCanvas() {
        return canvas ? canvas.get() : nullptr;
    }

    PackedByteArray bytes() const {
        PackedByteArray bytes;
        if (!canvas || bitmap.isNull()) return bytes;
        SkImageInfo info = bitmap.info();
        size_t bytes_per_pixel = info.bytesPerPixel();
        size_t row_bytes = bitmap.rowBytes();
        bytes.resize(row_bytes * info.height());
        for (int y = 0; y < info.height(); y++) {
            for (int x = 0; x < info.width(); x++) {
                int offset = y * row_bytes + x * bytes_per_pixel;
                auto addr = bitmap.getAddr32(x, y);
                bytes.encode_u32(offset, *addr);
            }
        }
        return bytes;
    }

   private:
    void on_transform_changed() {
        // Recreate bitmap and canvas when size changes
        int w = props ? props->width() : 1;
        int h = props ? props->height() : 1;

        // Allocate bitmap
        SkImageInfo info = SkImageInfo::MakeN32Premul(w, h);
        if (!bitmap.tryAllocPixels(info)) {
            return;
        }

        // Create canvas directly from bitmap
        canvas = std::make_unique<SkCanvas>(bitmap);
    }
};

#endif