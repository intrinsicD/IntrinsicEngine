#include <gtest/gtest.h>

import RHI;

TEST(RHIDescriptors, LayoutAndAllocatorTakeDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::DescriptorLayout, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::DescriptorLayout, std::shared_ptr<RHI::VulkanDevice>>);

    static_assert(std::is_constructible_v<RHI::DescriptorAllocator, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::DescriptorAllocator, std::shared_ptr<RHI::VulkanDevice>>);

    SUCCEED();
}

