// RHI.Vma.cpp – *no* `module;` here, this is just a normal TU.

/*#include <cstdlib>
#include <cstdio>*/

// VMA config – must match everywhere you include vk_mem_alloc.h
#define VMA_IMPLEMENTATION

/*#define VMA_SYSTEM_MALLOC(size) std::malloc(size)
#define VMA_SYSTEM_FREE(ptr) std::free(ptr)
#define VMA_SYSTEM_ALIGNED_MALLOC(size, align) std::aligned_alloc(align, size)
#define VMA_SYSTEM_ALIGNED_FREE(ptr) std::free(ptr)*/

#include <RHI/RHI.Vulkan.hpp>
