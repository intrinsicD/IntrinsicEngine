#include <gtest/gtest.h>

import RHI;

TEST(RHIStagingBelt, TakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::StagingBelt, RHI::VulkanDevice&, size_t>);
    static_assert(!std::is_constructible_v<RHI::StagingBelt, std::shared_ptr<RHI::VulkanDevice>, size_t>);
    SUCCEED();
}

