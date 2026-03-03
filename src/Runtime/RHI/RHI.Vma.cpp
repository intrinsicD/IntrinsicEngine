// RHI.Vma.cpp – *no* `module;` here, this is just a normal TU.

// VMA uses snprintf in its stats-string helpers (enabled by default).
// Include <cstdio> here so the implementation TU is self-sufficient across toolchains.
#include <cstdio>

// VMA config – must match everywhere you include vk_mem_alloc.h
#define VMA_IMPLEMENTATION

// Suppress all warnings originating from VMA's implementation body.
// This is a third-party header-only library; we do not control its code quality.
// Our engine flags (-Wall -Wextra -Wpedantic) must not be applied to it.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wnullability-extension"
#endif

#include "RHI.Vulkan.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif
