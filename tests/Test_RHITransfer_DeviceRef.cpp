#include <gtest/gtest.h>

import RHI;

TEST(RHITransfer, ManagerTakesDeviceByRef)
{
    static_assert(std::is_constructible_v<RHI::TransferManager, RHI::VulkanDevice&>);
    static_assert(!std::is_constructible_v<RHI::TransferManager, std::shared_ptr<RHI::VulkanDevice>>);
    SUCCEED();
}

