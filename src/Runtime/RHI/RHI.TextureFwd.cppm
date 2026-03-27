module;

#include <memory>
#include "RHI.Vulkan.hpp"

export module RHI.TextureFwd;

import Core.Handle;
import RHI.Image;
import RHI.Device;
export import RHI.TextureHandle;

export namespace RHI
{

    // Heavy GPU data (lives in TextureManager pool)
    struct TextureGpuData
    {
        std::unique_ptr<VulkanImage> Image;
        VkSampler Sampler = VK_NULL_HANDLE;

        // Shader-visible stable index into the global bindless texture array.
        // NOTE: This is intentionally NOT TextureHandle::Index because pool indices can be reused.
        uint32_t BindlessSlot = 0;
    };
}