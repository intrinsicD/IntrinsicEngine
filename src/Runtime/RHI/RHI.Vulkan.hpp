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
#include <vk_mem_alloc.h>

// Wraps Vulkan calls and logs errors without exceptions
#ifndef NDEBUG
    #define VK_CHECK(x)                                                              \
        do {                                                                         \
            VkResult result = x;                                                     \
            if (result != VK_SUCCESS) {                                              \
                fprintf(stderr, "Vulkan Error: %s failed with result %d at %s:%d\n", \
                       #x, result, __FILE__, __LINE__);                              \
            }                                                                        \
        } while(0)
#else
    // In release, still check but don't print detailed info
    #define VK_CHECK(x) x
#endif