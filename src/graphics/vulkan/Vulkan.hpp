#pragma once

// All Vulkan headers for the Extrinsic Vulkan backend.
// Include ONLY from .cpp files inside Backends/Vulkan — never from .cppm interfaces.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <volk.h>

#include "VmaConfig.hpp"
#include <vk_mem_alloc.h>

#include <cassert>
#include <cstdlib>

// ---------------------------------------------------------------------------
// VK_CHECK macros — callsite-aware error checking (matches src/ policy)
// ---------------------------------------------------------------------------
// Uses fprintf + abort instead of Core::Log to avoid a module dependency
// inside the global module fragment.

#define VK_CHECK_FATAL(x)                                                       \
    do {                                                                        \
        VkResult _r = (x);                                                      \
        if (_r != VK_SUCCESS) {                                                 \
            fprintf(stderr, "[Vulkan FATAL] %s = %d  at %s:%d\n",              \
                    #x, static_cast<int>(_r), __FILE__, __LINE__);             \
            std::abort();                                                       \
        }                                                                       \
    } while(0)

#define VK_CHECK_RETURN(x, retval)                                              \
    do {                                                                        \
        VkResult _r = (x);                                                      \
        if (_r != VK_SUCCESS) {                                                 \
            fprintf(stderr, "[Vulkan ERROR] %s = %d  at %s:%d\n",              \
                    #x, static_cast<int>(_r), __FILE__, __LINE__);             \
            return retval;                                                      \
        }                                                                       \
    } while(0)

#define VK_CHECK_BOOL(x)   VK_CHECK_RETURN(x, false)

#define VK_CHECK_WARN(x)                                                        \
    do {                                                                        \
        VkResult _r = (x);                                                      \
        if (_r != VK_SUCCESS) {                                                 \
            fprintf(stderr, "[Vulkan WARN] %s = %d  at %s:%d\n",               \
                    #x, static_cast<int>(_r), __FILE__, __LINE__);             \
        }                                                                       \
    } while(0)

