#include <gtest/gtest.h>

import RHI;

TEST(RHIBindless, ConstructorTakesDeviceByRef)
{
    // Compile-time API contract: bindless system must be device-owned, not shared-owned.
    static_assert(std::is_constructible_v<RHI::BindlessDescriptorSystem, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::BindlessDescriptorSystem, std::shared_ptr<RHI::VulkanDevice>>);

    SUCCEED();
}

