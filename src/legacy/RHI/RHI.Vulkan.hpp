#pragma once

// 1. Enforce the configuration globally
// This ensures no file ever forgets this line.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

// 2. Include Vulkan Headers via Volk
#include <volk.h>
#include <iostream>


// 3. Include VMA Declarations (Optional, but convenient if used everywhere)
// We do NOT define VMA_IMPLEMENTATION here.
#include "RHI.VmaConfig.hpp"
#include <vk_mem_alloc.h>

// ---------------------------------------------------------------------------
// Callsite-aware Vulkan error checking macros (E3a policy)
//
// VK_CHECK_FATAL  — Abort in both debug and release. Use for operations that
//                   invalidate frame safety on failure: object creation, queue
//                   submit, command buffer begin/end/reset.
//
// VK_CHECK_RETURN — Log and return a caller-chosen value. Use for operations
//                   with explicit recovery paths.
//
// VK_CHECK_BOOL   — Log and return false. Syntactic sugar for VK_CHECK_RETURN(x, false).
//
// VK_CHECK_WARN   — Log and continue. Use ONLY for queries where the
//                   zero-initialized output is a safe conservative default
//                   (e.g. timeline semaphore counter reads).
// ---------------------------------------------------------------------------

// Fatal: abort unconditionally on failure (debug and release).
#define VK_CHECK_FATAL(x)                                                            \
        do {                                                                         \
            VkResult result_ = x;                                                    \
            if (result_ != VK_SUCCESS) {                                             \
                Core::Log::Error("Vulkan Fatal: {} failed with VkResult {} at {}:{}", \
                    #x, static_cast<int>(result_), __FILE__, __LINE__);              \
                std::abort();                                                        \
            }                                                                        \
        } while(0)

// Recoverable: log and return a caller-specified value.
#define VK_CHECK_RETURN(x, retval)                                                   \
        do {                                                                         \
            VkResult result_ = x;                                                    \
            if (result_ != VK_SUCCESS) {                                             \
                Core::Log::Error("Vulkan Error: {} failed with VkResult {} at {}:{}", \
                    #x, static_cast<int>(result_), __FILE__, __LINE__);              \
                return retval;                                                       \
            }                                                                        \
        } while(0)

// Recoverable (bool): log and return false.
#define VK_CHECK_BOOL(x)  VK_CHECK_RETURN(x, false)

// Telemetry-only: log and continue. Output variable retains its zero-init value.
#define VK_CHECK_WARN(x)                                                             \
        do {                                                                         \
            VkResult result_ = x;                                                    \
            if (result_ != VK_SUCCESS) {                                             \
                Core::Log::Warn("Vulkan Warning: {} returned VkResult {} at {}:{}",  \
                    #x, static_cast<int>(result_), __FILE__, __LINE__);              \
            }                                                                        \
        } while(0)
