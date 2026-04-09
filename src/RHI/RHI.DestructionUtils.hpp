#pragma once

// RHI.DestructionUtils.hpp — Typed SafeDestroy helpers for common Vulkan
// resource destruction patterns.  Every RHI wrapper that defers destruction
// through VulkanDevice::SafeDestroy() follows the same recipe:
//
//   1. Copy the raw Vulkan handle(s) into locals.
//   2. Copy the device / allocator pointer.
//   3. Capture those locals in a noexcept-move-constructible lambda.
//   4. Call SafeDestroy() with the lambda.
//
// The helpers below eliminate that boilerplate while keeping the lambda
// capture list trivially move-constructible (required by InplaceFunction).
//
// Usage:
//   SafeDestroyVk(device, pipeline, vkDestroyPipeline);
//   SafeDestroyVma(device, buffer, allocation, vmaDestroyBuffer);

#include "RHI.Vulkan.hpp"
#include <vector>

namespace RHI::DestructionUtils
{
    // Defer destruction of a single VkHandle via a vkDestroy* function.
    // Signature: void Fn(VkDevice, Handle, const VkAllocationCallbacks*)
    template <typename Handle, typename DestroyFn>
    inline void SafeDestroyVk(auto& device, Handle& handle, DestroyFn destroyFn)
    {
        if (handle == VK_NULL_HANDLE)
            return;

        VkDevice logicalDevice = device.GetLogicalDevice();
        Handle   captured      = handle;
        handle = VK_NULL_HANDLE;

        device.SafeDestroy([logicalDevice, captured, destroyFn]()
        {
            destroyFn(logicalDevice, captured, nullptr);
        });
    }

    // Defer destruction of a VMA-allocated resource (buffer or image).
    // Signature: void Fn(VmaAllocator, VkResource, VmaAllocation)
    template <typename VkResource, typename DestroyFn>
    inline void SafeDestroyVma(auto& device, VkResource& resource, VmaAllocation& allocation, DestroyFn destroyFn)
    {
        if (resource == VK_NULL_HANDLE)
            return;

        VmaAllocator  allocator      = device.GetAllocator();
        VkResource    capturedRes    = resource;
        VmaAllocation capturedAlloc  = allocation;
        resource   = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;

        device.SafeDestroy([allocator, capturedRes, capturedAlloc, destroyFn]()
        {
            destroyFn(allocator, capturedRes, capturedAlloc);
        });
    }

    // Defer destruction of a vector of VkHandles (e.g. image views, pools).
    // The vector is move-captured to satisfy InplaceFunction's nothrow-move
    // requirement.
    template <typename Handle, typename DestroyFn>
    inline void SafeDestroyBatch(auto& device, std::vector<Handle>& handles, DestroyFn destroyFn)
    {
        if (handles.empty())
            return;

        VkDevice logicalDevice = device.GetLogicalDevice();
        std::vector<Handle> captured = std::move(handles);
        handles.clear();

        device.SafeDestroy([logicalDevice, captured = std::move(captured), destroyFn]()
        {
            for (auto h : captured)
                if (h != VK_NULL_HANDLE)
                    destroyFn(logicalDevice, h, nullptr);
        });
    }
}
