#ifndef _RIVEEXTENSION_TYPES_
#define _RIVEEXTENSION_TYPES_

#include <functional>
#include <memory>

// rive-runtime reference-counted pointer
#include <rive/refcnt.hpp>

template <typename T>
using Ptr = std::unique_ptr<T>;

// Import rive's rcp into global namespace for convenience
using rive::rcp;

template <typename R, typename... Args>
using Fn = std::function<R(Args...)>;

template <typename... Args>
using Callback = Fn<void, Args...>;

#endif