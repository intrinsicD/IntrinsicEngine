module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

export module Extrinsic.Platform.Backend.GlfwVulkanSurface;

import Extrinsic.Platform.Window;

namespace Extrinsic::Platform::Backends::Glfw
{
    export enum class VulkanSurfaceResult
    {
        Success,
        InvalidWindow,
        BackendError,
    };

    export VulkanSurfaceResult CreateVulkanSurface(
        Platform::IWindow& window,
        void* instance,
        const void* allocator,
        void* surfaceOut)
    {
        auto* glfwWindow = static_cast<GLFWwindow*>(window.GetNativeHandle());
        if (!glfwWindow || !instance || !surfaceOut)
        {
            return VulkanSurfaceResult::InvalidWindow;
        }

        const auto result = glfwCreateWindowSurface(
            static_cast<VkInstance>(instance),
            glfwWindow,
            static_cast<const VkAllocationCallbacks*>(allocator),
            static_cast<VkSurfaceKHR*>(surfaceOut));

        return result == VK_SUCCESS
            ? VulkanSurfaceResult::Success
            : VulkanSurfaceResult::BackendError;
    }
}

