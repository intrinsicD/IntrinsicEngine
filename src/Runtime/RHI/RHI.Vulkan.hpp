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

import Core;

// Wraps Vulkan calls and logs errors without exceptions
#ifndef NDEBUG
#define VK_CHECK(x)                                                                  \
        do {                                                                         \
            VkResult result = x;                                                     \
            if (result != VK_SUCCESS) {                                              \
                Core::Log::Error("Vulkan Error: {} failed with result {} at {}:{}",  \
                    #x, (int)result, __FILE__, __LINE__);                            \
                std::abort();                                                        \
            }                                                                        \
        } while(0)
#else
// In release, still check but don't print detailed info
#define VK_CHECK(x) x
#endif
