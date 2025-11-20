#pragma once

// 1. Enforce the configuration globally
// This ensures no file ever forgets this line.
#ifndef VK_NO_PROTOTYPES
    #define VK_NO_PROTOTYPES
#endif

// 2. Include Vulkan Headers via Volk
#include <volk.h>

// 3. Include VMA Declarations (Optional, but convenient if used everywhere)
// We do NOT define VMA_IMPLEMENTATION here.
#include <vk_mem_alloc.h>