#include <gtest/gtest.h>

import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    RHI::BufferDesc AnyBufferDesc()
    {
        return RHI::BufferDesc{
            .SizeBytes  = 1024,
            .Usage      = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName  = "test-buffer",
        };
    }
}

// -----------------------------------------------------------------------------
// F3: error-code propagation on Create()
// -----------------------------------------------------------------------------

TEST(RHIBufferManager, CreateReturnsLeaseOnSuccess)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};

    auto leaseOr = mgr.Create(AnyBufferDesc());
    ASSERT_TRUE(leaseOr.has_value());
    EXPECT_TRUE(leaseOr->IsValid());
    EXPECT_EQ(dev.CreateBufferCount, 1);
}

TEST(RHIBufferManager, CreateReturnsOutOfDeviceMemoryOnDeviceFailure)
{
    MockDevice dev;
    dev.FailNextBufferCreate = true;
    RHI::BufferManager mgr{dev};

    auto leaseOr = mgr.Create(AnyBufferDesc());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::OutOfDeviceMemory);
    EXPECT_EQ(dev.CreateBufferCount, 1);
    EXPECT_EQ(dev.DestroyBufferCount, 0);
}

// -----------------------------------------------------------------------------
// F14: DeviceNotOperational short-circuit
// -----------------------------------------------------------------------------

TEST(RHIBufferManager, CreateShortCircuitsOnNonOperationalDevice)
{
    MockDevice dev;
    dev.Operational = false;
    RHI::BufferManager mgr{dev};

    auto leaseOr = mgr.Create(AnyBufferDesc());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::DeviceNotOperational);
    // Manager must not touch the device when IsOperational() is false.
    EXPECT_EQ(dev.CreateBufferCount, 0);
}

// -----------------------------------------------------------------------------
// Refcount lifecycle (Create -> Share -> drop, drop)
// -----------------------------------------------------------------------------

TEST(RHIBufferManager, LastLeaseDroppedDestroysGpuResource)
{
    MockDevice dev;
    {
        RHI::BufferManager mgr{dev};
        {
            auto lease = *mgr.Create(AnyBufferDesc());
            EXPECT_EQ(dev.DestroyBufferCount, 0);
        } // lease drops here — refcount 1 -> 0 -> DestroyBuffer
        EXPECT_EQ(dev.DestroyBufferCount, 1);
    }
}

TEST(RHIBufferManager, SharedLeasesPostponeDestroyUntilLastDrop)
{
    MockDevice dev;
    {
        RHI::BufferManager mgr{dev};
        auto lease1 = *mgr.Create(AnyBufferDesc());
        auto lease2 = lease1.Share();          // refcount 1 -> 2
        ASSERT_TRUE(lease2.IsValid());
        EXPECT_EQ(dev.DestroyBufferCount, 0);

        {
            auto tmp = std::move(lease1);      // move, not drop
            EXPECT_EQ(dev.DestroyBufferCount, 0);
        } // tmp goes out of scope, refcount 2 -> 1
        EXPECT_EQ(dev.DestroyBufferCount, 0);
    } // lease2 drops, refcount 1 -> 0, DestroyBuffer fires
    EXPECT_EQ(dev.DestroyBufferCount, 1);
}

// -----------------------------------------------------------------------------
// F2: live-lease bookkeeping is balanced after typical use
//
// The manager's destructor asserts LiveLeaseCount == 0. These positive tests
// prove the counting mechanism is balanced under normal Create / Share / drop
// flows — if Retain or Release ever missed a counter update, the scope below
// would trip the destructor assertion.
// -----------------------------------------------------------------------------

TEST(RHIBufferManager, DestructorIsCleanAfterAllLeasesDropped)
{
    MockDevice dev;
    // Deliberately exercise multiple lease sources: Create, Share, move-assign.
    {
        RHI::BufferManager mgr{dev};
        auto a = *mgr.Create(AnyBufferDesc());
        auto b = *mgr.Create(AnyBufferDesc());
        auto c = a.Share();
        auto d = b.Share();
        {
            auto e = c.Share();
        }
        // All leases drop before manager destructor runs.
    } // if counting were off, the destructor's assert would fire.
    EXPECT_EQ(dev.CreateBufferCount,  2);
    EXPECT_EQ(dev.DestroyBufferCount, 2);
}
