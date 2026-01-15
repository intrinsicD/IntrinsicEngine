#include <gtest/gtest.h>

import RHI;

TEST(RHIBuffer, ConstructorTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::VulkanBuffer, RHI::VulkanDevice&, size_t, VkBufferUsageFlags, VmaMemoryUsage>);
    static_assert(!std::is_constructible_v<RHI::VulkanBuffer, std::shared_ptr<RHI::VulkanDevice>, size_t, VkBufferUsageFlags, VmaMemoryUsage>);
    SUCCEED();
}

