// RHI.Vma.cpp – *no* `module;` here, this is just a normal TU.

// VMA uses snprintf in its stats-string helpers (enabled by default).
// Include <cstdio> here so the implementation TU is self-sufficient across toolchains.
#include <cstdio>

// VMA config – must match everywhere you include vk_mem_alloc.h
#define VMA_IMPLEMENTATION

#include "RHI.Vulkan.hpp"
