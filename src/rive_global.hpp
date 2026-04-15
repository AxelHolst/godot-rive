#ifndef _RIVEEXTENSION_GLOBAL_HPP_
#define _RIVEEXTENSION_GLOBAL_HPP_

#include <skia/renderer/include/skia_factory.hpp>

// Global singleton for rive::SkiaFactory
// This ensures all rive Files and resources use the same factory instance,
// which is required because rive::File binds to the Factory used during import().
// Using different factory instances causes memory corruption when accessing
// resources created by a different factory.
class RiveGlobal {
public:
    static rive::SkiaFactory* get_factory() {
        static rive::SkiaFactory instance;
        return &instance;
    }

    // Delete copy/move constructors
    RiveGlobal(const RiveGlobal&) = delete;
    RiveGlobal& operator=(const RiveGlobal&) = delete;

private:
    RiveGlobal() = default;
};

#endif
