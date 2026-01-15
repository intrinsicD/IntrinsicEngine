#include <gtest/gtest.h>

import RHI;

TEST(RHIImage, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::VulkanImage,
        RHI::VulkanDevice&,
        uint32_t, uint32_t, uint32_t,
        VkFormat, VkImageUsageFlags, VkImageAspectFlags, VkSharingMode>);

    static_assert(!std::is_constructible_v<RHI::VulkanImage,
        std::shared_ptr<RHI::VulkanDevice>,
        uint32_t, uint32_t, uint32_t,
        VkFormat, VkImageUsageFlags, VkImageAspectFlags, VkSharingMode>);

    SUCCEED();
}

