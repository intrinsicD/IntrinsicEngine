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
        void* surfaceOut);
}

