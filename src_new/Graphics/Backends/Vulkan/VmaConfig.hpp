#pragma once

#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

// Suppress Clang nullability-extension noise from VMA headers.
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_NULLABLE_NON_DISPATCHABLE
#define VMA_NOT_NULL_NON_DISPATCHABLE

