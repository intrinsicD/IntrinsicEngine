#pragma once

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

// VMA v3.x unconditionally defines _Nullable/_Nonnull when compiled with Clang,
// emitting -Wnullability-extension noise for every function declaration.
// VMA_USE_NULLABILITY_ANNOTATIONS does not exist in v3.x; the correct fix is to
// pre-define the macros as empty before vk_mem_alloc.h's own #ifndef guards run.
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_NULLABLE_NON_DISPATCHABLE
#define VMA_NOT_NULL_NON_DISPATCHABLE
