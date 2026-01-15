#include <gtest/gtest.h>

import RHI;

TEST(RHITexture, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::Texture, RHI::VulkanDevice&, uint32_t, uint32_t, VkFormat>);

    // Must not accept shared_ptr device anymore.
    static_assert(!std::is_constructible_v<RHI::Texture, std::shared_ptr<RHI::VulkanDevice>, uint32_t, uint32_t, VkFormat>);

    SUCCEED();
}

