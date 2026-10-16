#include <gtest/gtest.h>
#include <limits>
#include "../../../src/graphics/vulkan/Backends.Vulkan.StagingRing.hpp"

namespace
{
    using Extrinsic::Backends::Vulkan::Detail::StagingRing;
}

TEST(VulkanStagingRing, FullRingDoesNotOverlapOutstandingTicket)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(256, 256), 0u);
    ring.Retire(1);
    ASSERT_EQ(ring.Allocate(512, 256), 256u);
    ring.Retire(2);
    ring.GarbageCollect(1);
    ASSERT_EQ(ring.Allocate(256, 256), 768u);
    ring.Retire(3);
    ASSERT_EQ(ring.Allocate(256, 256), 0u);
    ring.Retire(4);
    ASSERT_FALSE(ring.Allocate(256, 256).has_value());
    ring.GarbageCollect(2);
    EXPECT_EQ(ring.Allocate(256, 256), 256u);
}

TEST(VulkanStagingRing, CollectKeepsPendingReservation)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(256, 256), 0u);
    ring.GarbageCollect(0);
    EXPECT_EQ(ring.Allocate(256, 256), 256u);
    ring.Retire(1);
    ring.GarbageCollect(1);
    EXPECT_EQ(ring.Allocate(1024, 1), 0u);
}

TEST(VulkanStagingRing, InvalidRequestsDoNotMutate)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(4, 1), 0u);
    EXPECT_FALSE(ring.Allocate(0, 1).has_value());
    EXPECT_FALSE(ring.Allocate(std::numeric_limits<std::size_t>::max(), 1).has_value());
    EXPECT_EQ(ring.Allocate(1, 1), 4u);
}

TEST(VulkanStagingRing, OversizedRequestLeavesCapacityAvailable)
{
    StagingRing ring{1024};
    EXPECT_FALSE(ring.Allocate(1025, 1).has_value());
    EXPECT_EQ(ring.Allocate(1024, 1), 0u);
}

TEST(VulkanStagingRing, ExactCapacityIsReusableAfterCompletion)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(1024, 1), 0u);
    ring.Retire(1);
    ring.GarbageCollect(1);
    EXPECT_EQ(ring.Allocate(1024, 1), 0u);
}

TEST(VulkanStagingRing, NonPowerOfTwoAlignment)
{
    StagingRing ring{1024};
    EXPECT_EQ(ring.Allocate(4, 1), 0u);
    EXPECT_EQ(ring.Allocate(4, 12), 12u);
}

TEST(VulkanStagingRing, PaddedWrapPreservesOutstandingRegion)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(256, 1), 0u);
    ring.Retire(1);
    ASSERT_EQ(ring.Allocate(600, 1), 256u);
    ring.Retire(2);
    ring.GarbageCollect(1);
    EXPECT_EQ(ring.Allocate(128, 256), 0u);
    ring.Retire(3);
    EXPECT_EQ(ring.Allocate(64, 1), 128u);
}

TEST(VulkanStagingRing, CancelPendingPreservesEarlierRetirements)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(256, 1), 0u);
    ring.Retire(1);
    ASSERT_EQ(ring.Allocate(640, 1), 256u);
    ring.Retire(2);
    ring.GarbageCollect(1);
    ASSERT_EQ(ring.Allocate(64, 256), 0u);
    ring.CancelPending();
    ASSERT_EQ(ring.Allocate(128, 1), 896u);
    ring.Retire(3);
    ring.GarbageCollect(2);
    EXPECT_EQ(ring.Allocate(256, 256), 0u);
    ring.Retire(4);
    ring.GarbageCollect(4);
    EXPECT_EQ(ring.Allocate(1024, 1), 0u);
}

TEST(VulkanStagingRing, CancelledLastPendingMakesWholeEmptyRingAvailable)
{
    StagingRing ring{1024};
    ASSERT_EQ(ring.Allocate(768, 1), 0u);
    ring.Retire(1);
    ASSERT_EQ(ring.Allocate(128, 1), 768u);
    ring.GarbageCollect(1);
    ring.CancelPending();
    EXPECT_EQ(ring.Allocate(900, 1), 0u);
}
