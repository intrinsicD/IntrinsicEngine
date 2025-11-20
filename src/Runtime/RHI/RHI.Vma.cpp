// RHI.Vma.cpp – *no* `module;` here, this is just a normal TU.

#include <cstdlib>
#include <cstdio>


#define VK_NO_PROTOTYPES

// VMA config – must match everywhere you include vk_mem_alloc.h
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_USE_NULLABILITY_ANNOTATIONS 0

#define VMA_SYSTEM_MALLOC(size) std::malloc(size)
#define VMA_SYSTEM_FREE(ptr) std::free(ptr)
#define VMA_SYSTEM_ALIGNED_MALLOC(size, align) std::aligned_alloc(align, size)
#define VMA_SYSTEM_ALIGNED_FREE(ptr) std::free(ptr)

#include <vk_mem_alloc.h>
