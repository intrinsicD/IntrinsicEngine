#include <gtest/gtest.h>

#include <utility>

import Extrinsic.Core.HandleLease;
import Extrinsic.Core.StrongHandle;

namespace
{
    struct LeaseTag;
    using Handle = Extrinsic::Core::StrongHandle<LeaseTag>;

    struct MockManager
    {
        using LeaseType = Extrinsic::Core::Lease<Handle, MockManager>;

        int RetainCalls = 0;
        int ReleaseCalls = 0;
        int AcquireCalls = 0;

        void Retain(Handle)
        {
            ++RetainCalls;
        }

        void Release(Handle)
        {
            ++ReleaseCalls;
        }

        [[nodiscard]] LeaseType AcquireLease(Handle handle)
        {
            ++AcquireCalls;
            return LeaseType::RetainNew(*this, handle);
        }
    };

    [[nodiscard]] Handle ValidHandle()
    {
        return Handle{7u, 3u};
    }
}

TEST(CoreHandleLease, DefaultAndInvalidShareAreNoOps)
{
    MockManager manager;
    MockManager::LeaseType lease;

    EXPECT_FALSE(lease.IsValid());
    EXPECT_FALSE(static_cast<bool>(lease));

    auto shared = lease.Share();
    EXPECT_FALSE(shared.IsValid());

    lease.Reset();
    EXPECT_EQ(manager.RetainCalls, 0);
    EXPECT_EQ(manager.ReleaseCalls, 0);
    EXPECT_EQ(manager.AcquireCalls, 0);
}

TEST(CoreHandleLease, AdoptDoesNotRetainButReleasesOnDestruction)
{
    MockManager manager;
    {
        auto lease = MockManager::LeaseType::Adopt(manager, ValidHandle());
        ASSERT_TRUE(lease.IsValid());
        EXPECT_EQ(lease.GetHandle(), ValidHandle());
        EXPECT_EQ(manager.RetainCalls, 0);
        EXPECT_EQ(manager.ReleaseCalls, 0);
    }

    EXPECT_EQ(manager.RetainCalls, 0);
    EXPECT_EQ(manager.ReleaseCalls, 1);
}

TEST(CoreHandleLease, RetainNewRetainsAndReleases)
{
    MockManager manager;
    {
        auto lease = MockManager::LeaseType::RetainNew(manager, ValidHandle());
        EXPECT_EQ(manager.RetainCalls, 1);
        EXPECT_TRUE(lease.IsValid());
    }

    EXPECT_EQ(manager.ReleaseCalls, 1);
}

TEST(CoreHandleLease, ShareDelegatesToManagerAcquireLease)
{
    MockManager manager;
    auto lease = MockManager::LeaseType::Adopt(manager, ValidHandle());

    {
        auto shared = lease.Share();
        EXPECT_TRUE(shared.IsValid());
        EXPECT_EQ(manager.AcquireCalls, 1);
        EXPECT_EQ(manager.RetainCalls, 1);
    }

    EXPECT_EQ(manager.ReleaseCalls, 1);
}

TEST(CoreHandleLease, MoveConstructionTransfersOwnership)
{
    MockManager manager;
    auto source = MockManager::LeaseType::Adopt(manager, ValidHandle());

    auto moved = std::move(source);
    EXPECT_FALSE(source.IsValid());
    EXPECT_TRUE(moved.IsValid());

    moved.Reset();
    EXPECT_EQ(manager.ReleaseCalls, 1);
}

TEST(CoreHandleLease, MoveAssignmentReleasesPreviousHandle)
{
    MockManager manager;
    auto lhs = MockManager::LeaseType::Adopt(manager, ValidHandle());
    auto rhs = MockManager::LeaseType::Adopt(manager, ValidHandle());

    lhs = std::move(rhs);
    EXPECT_EQ(manager.ReleaseCalls, 1);
    EXPECT_TRUE(lhs.IsValid());
    EXPECT_FALSE(rhs.IsValid());

    lhs.Reset();
    EXPECT_EQ(manager.ReleaseCalls, 2);
}

TEST(CoreHandleLease, SelfMoveAssignmentIsSafeNoExtraRelease)
{
    MockManager manager;
    auto lease = MockManager::LeaseType::Adopt(manager, ValidHandle());

    lease = std::move(lease);
    EXPECT_TRUE(lease.IsValid());
    EXPECT_EQ(manager.ReleaseCalls, 0);

    lease.Reset();
    EXPECT_EQ(manager.ReleaseCalls, 1);
}
