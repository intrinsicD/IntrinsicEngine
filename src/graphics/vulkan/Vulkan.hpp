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

namespace Extrinsic::Backends::Vulkan
{
    enum class VkCheckSeverity : unsigned char
    {
        Warning,
        Error,
        Fatal
    };

    void ReportVkCheckFailure(VkCheckSeverity severity,
                              const char* expression,
                              VkResult result,
                              const char* file,
                              int line);
}

// ---------------------------------------------------------------------------
// VK_CHECK macros — callsite-aware error checking (matches src/ policy)
// ---------------------------------------------------------------------------
// Delegates diagnostics to ReportVkCheckFailure so this global-fragment header
// does not import modules while callers still use the project logger.

#define VK_CHECK_FATAL(x)                                                       \
    do {                                                                        \
        VkResult _r = (x);                                                      \
        if (_r != VK_SUCCESS) {                                                 \
            ::Extrinsic::Backends::Vulkan::ReportVkCheckFailure(                \
                ::Extrinsic::Backends::Vulkan::VkCheckSeverity::Fatal,          \
                #x, _r, __FILE__, __LINE__);                                    \
            std::abort();                                                       \
        }                                                                       \
    } while(0)

#define VK_CHECK_RETURN(x, retval)                                              \
    do {                                                                        \
        VkResult _r = (x);                                                      \
        if (_r != VK_SUCCESS) {                                                 \
            ::Extrinsic::Backends::Vulkan::ReportVkCheckFailure(                \
                ::Extrinsic::Backends::Vulkan::VkCheckSeverity::Error,          \
                #x, _r, __FILE__, __LINE__);                                    \
            return retval;                                                      \
        }                                                                       \
    } while(0)

#define VK_CHECK_BOOL(x)   VK_CHECK_RETURN(x, false)

#define VK_CHECK_WARN(x)                                                        \
    do {                                                                        \
        VkResult _r = (x);                                                      \
        if (_r != VK_SUCCESS) {                                                 \
            ::Extrinsic::Backends::Vulkan::ReportVkCheckFailure(                \
                ::Extrinsic::Backends::Vulkan::VkCheckSeverity::Warning,        \
                #x, _r, __FILE__, __LINE__);                                    \
        }                                                                       \
    } while(0)

