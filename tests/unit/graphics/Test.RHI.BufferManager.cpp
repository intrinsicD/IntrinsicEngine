#include <gtest/gtest.h>
#include <cstdint>
#include <string>

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
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Core.Config.Render;

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

// -----------------------------------------------------------------------------
// ARCH-023: cached scratch leases (CreateScratch). Each assertion names the mechanism whose
// removal must fail it.
// -----------------------------------------------------------------------------

namespace
{
    RHI::BufferDesc ScratchDesc(std::uint64_t bytes, const char* name = "scratch")
    {
        return RHI::BufferDesc{
            .SizeBytes = bytes,
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
            .DebugName = name,
        };
    }

    // Stamps the returned buffers now, then advances past every frame that may still use them.
    void CollectAndRetire(MockDevice& dev, RHI::BufferManager& mgr)
    {
        mgr.CollectScratch(RHI::StampRetirement(dev));
        dev.GlobalFrameNumber += dev.FramesInFlight + 1u;
        mgr.CollectScratch(RHI::StampRetirement(dev));
    }
}

TEST(RHIBufferManagerScratch, SizeClassesArePowersOfTwoAboveTheMinimumAndOverflowHasNone)
{
    using M = RHI::BufferManager;
    EXPECT_EQ(M::ScratchClassBytes(0u), 64u);
    EXPECT_EQ(M::ScratchClassBytes(1u), 64u);
    EXPECT_EQ(M::ScratchClassBytes(64u), 64u);
    EXPECT_EQ(M::ScratchClassBytes(65u), 128u);
    EXPECT_EQ(M::ScratchClassBytes(4097u), 8192u);
    EXPECT_EQ(M::ScratchClassBytes(std::uint64_t{1} << 63u), std::uint64_t{1} << 63u);
    EXPECT_EQ(M::ScratchClassBytes((std::uint64_t{1} << 63u) + 1u), 0u);
    EXPECT_EQ(M::ScratchClassBytes(~std::uint64_t{0}), 0u);
}

// The physical allocation is the class; the descriptor and views keep the requested size.
TEST(RHIBufferManagerScratch, ReleasedBufferIsReusedOnlyAfterItsFramesRetire)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    auto lease = *mgr.CreateScratch(ScratchDesc(100u));
    const auto handle = lease.GetHandle();
    ASSERT_EQ(dev.CreatedBufferDescs.size(), 1u);
    EXPECT_EQ(dev.CreatedBufferDescs.back().SizeBytes, 128u);
    EXPECT_EQ(mgr.GetDesc(handle)->SizeBytes, 100u);
    EXPECT_EQ(mgr.View(handle).Size, 100u) << "WholeBuffer must not reach the padded tail";
    EXPECT_EQ(mgr.GetScratchStats().ActiveBytes, 128u);

    lease = {};
    EXPECT_EQ(dev.DestroyBufferCount, 0) << "a released scratch buffer is parked, not destroyed";
    EXPECT_EQ(mgr.GetScratchStats().ActiveBytes, 0u);
    EXPECT_EQ(mgr.GetScratchStats().PendingBytes, 128u);

    mgr.CollectScratch(RHI::StampRetirement(dev));
    auto early = *mgr.CreateScratch(ScratchDesc(120u));
    EXPECT_NE(early.GetHandle(), handle) << "its frames have not retired";
    EXPECT_EQ(mgr.GetScratchStats().Hits, 0u);
    early = {};

    dev.GlobalFrameNumber += dev.FramesInFlight + 1u;
    mgr.CollectScratch(RHI::StampRetirement(dev));
    auto warm = *mgr.CreateScratch(ScratchDesc(120u));
    EXPECT_EQ(warm.GetHandle(), handle);
    EXPECT_EQ(mgr.GetDesc(handle)->SizeBytes, 120u) << "a hit reports the new request's size";
    const auto stats = mgr.GetScratchStats();
    EXPECT_EQ(stats.Hits, 1u);
    EXPECT_EQ(stats.Misses, 2u);
    EXPECT_EQ(dev.CreateBufferCount, 2);
    EXPECT_EQ(stats.ActiveBytes, 128u);
    EXPECT_EQ(stats.PendingBytes, 128u) << "early is returned but stamped after the advance";
    EXPECT_EQ(stats.IdleBytes, 0u);
}

TEST(RHIBufferManagerScratch, RetiredFramesDoNotProveComputeLaneCompletion)
{
    MockDevice dev;
    dev.TransferQueue.ComputeFenceToken = RHI::ReadbackToken{(std::uint64_t{1} << 63u) | 3u};
    dev.TransferQueue.ReadbacksComplete = false;
    RHI::BufferManager mgr{dev};
    auto lease = *mgr.CreateScratch(ScratchDesc(256u));
    const auto handle = lease.GetHandle();
    lease = {};
    CollectAndRetire(dev, mgr);
    auto cold = *mgr.CreateScratch(ScratchDesc(256u));
    EXPECT_NE(cold.GetHandle(), handle) << "frames retired, the compute fence has not";
    EXPECT_EQ(mgr.GetScratchStats().IdleBytes, 0u);
    cold = {};
    dev.TransferQueue.ReadbacksComplete = true;
    mgr.CollectScratch(RHI::StampRetirement(dev));
    auto warm = *mgr.CreateScratch(ScratchDesc(256u));
    EXPECT_EQ(warm.GetHandle(), handle);
    EXPECT_EQ(mgr.GetScratchStats().Hits, 1u);
}

TEST(RHIBufferManagerScratch, UsageAndMemoryClassMustMatchForReuse)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    const auto handle = mgr.CreateScratch(ScratchDesc(100u))->GetHandle(); // released at once
    CollectAndRetire(dev, mgr);
    auto storageOnly = ScratchDesc(100u);
    storageOnly.Usage = RHI::BufferUsage::Storage;
    auto host = ScratchDesc(100u);
    host.HostVisible = true;
    auto readback = host;
    readback.HostReadback = true;
    for (const auto& other : {storageOnly, host, readback})
    {
        auto lease = mgr.CreateScratch(other);
        ASSERT_TRUE(lease.has_value());
        EXPECT_NE(lease->GetHandle(), handle);
        // Held until the loop iteration ends, then parked as returned (never idle in this test).
    }
    auto same = *mgr.CreateScratch(ScratchDesc(128u));
    EXPECT_EQ(same.GetHandle(), handle);
    EXPECT_EQ(mgr.GetScratchStats().Hits, 1u);
    EXPECT_EQ(mgr.GetScratchStats().Misses, 4u);
}

TEST(RHIBufferManagerScratch, OverflowingRequestIsRefusedWithoutDeviceCall)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    const auto refused = mgr.CreateScratch(ScratchDesc((std::uint64_t{1} << 63u) + 1u));
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), Core::ErrorCode::OutOfRange);
    EXPECT_EQ(dev.CreateBufferCount, 0);
    EXPECT_EQ(mgr.GetScratchStats().Refusals, 1u);
}

// Active and pending bytes are committed; only idle bytes are evicted to admit a request.
TEST(RHIBufferManagerScratch, ActiveAndPendingBytesCountTowardTheBudgetAndRefusalAllocatesNothing)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    mgr.SetScratchBudget(1024u);
    auto a = *mgr.CreateScratch(ScratchDesc(400u)); // class 512
    auto b = *mgr.CreateScratch(ScratchDesc(200u)); // class 256
    const auto refused = mgr.CreateScratch(ScratchDesc(300u));
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), Core::ErrorCode::ResourceBudgetExceeded);
    EXPECT_EQ(dev.CreateBufferCount, 2);
    EXPECT_EQ(mgr.GetScratchStats().LastRefusedBytes, 512u);
    EXPECT_EQ(mgr.GetScratchStats().LastRefusalCommittedBytes, 768u);

    b = {};
    mgr.CollectScratch(RHI::StampRetirement(dev));
    EXPECT_FALSE(mgr.CreateScratch(ScratchDesc(300u)).has_value()) << "pending bytes stay committed";
    EXPECT_EQ(dev.CreateBufferCount, 2);
    EXPECT_EQ(dev.DestroyBufferCount, 0) << "a refusal evicts nothing";
    EXPECT_EQ(mgr.GetScratchStats().Refusals, 2u);

    dev.GlobalFrameNumber += dev.FramesInFlight + 1u;
    mgr.CollectScratch(RHI::StampRetirement(dev));
    EXPECT_EQ(mgr.GetScratchStats().IdleBytes, 256u);
    auto c = mgr.CreateScratch(ScratchDesc(300u));
    ASSERT_TRUE(c.has_value());
    const auto stats = mgr.GetScratchStats();
    EXPECT_EQ(dev.DestroyBufferCount, 1) << "the idle 256-byte buffer made room";
    EXPECT_EQ(stats.Evictions, 1u);
    EXPECT_EQ(stats.ActiveBytes, 1024u);
    EXPECT_EQ(stats.PendingBytes + stats.IdleBytes, 0u);
    EXPECT_EQ(stats.BudgetBytes, 1024u);
}

TEST(RHIBufferManagerScratch, EvictionDestroysTheLeastRecentlyUsedIdleBufferFirst)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    mgr.SetScratchBudget(384u);
    auto x = *mgr.CreateScratch(ScratchDesc(128u));
    auto y = *mgr.CreateScratch(ScratchDesc(128u));
    const auto newer = y.GetHandle();
    x = {};
    CollectAndRetire(dev, mgr); // x idle first
    y = {};
    CollectAndRetire(dev, mgr); // then y
    auto other = ScratchDesc(256u);
    other.Usage = RHI::BufferUsage::Storage; // a miss that needs one 128-byte buffer evicted
    auto z = mgr.CreateScratch(other);
    ASSERT_TRUE(z.has_value());
    EXPECT_EQ(mgr.GetScratchStats().Evictions, 1u);
    auto again = *mgr.CreateScratch(ScratchDesc(128u));
    EXPECT_EQ(again.GetHandle(), newer) << "the older idle buffer was evicted";
}

TEST(RHIBufferManagerScratch, ParkedBufferKeepsNoCallerDebugName)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    std::string pluginName = "Plugin.Scratch";
    auto lease = *mgr.CreateScratch(ScratchDesc(100u, pluginName.c_str()));
    const auto handle = lease.GetHandle();
    EXPECT_EQ(mgr.GetDesc(handle)->DebugName, pluginName.c_str());
    lease = {};
    pluginName = std::string(64, 'x'); // the plugin's storage is gone
    CollectAndRetire(dev, mgr);
    static constexpr const char* kHostName = "Host.Scratch";
    auto warm = *mgr.CreateScratch(ScratchDesc(100u, kHostName));
    ASSERT_EQ(warm.GetHandle(), handle);
    EXPECT_EQ(mgr.GetDesc(handle)->DebugName, kHostName);
}

TEST(RHIBufferManagerScratch, DrainDestroysParkedBuffersAndLaterReleasesDestroyDirectly)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    auto held = *mgr.CreateScratch(ScratchDesc(64u));
    (void)mgr.CreateScratch(ScratchDesc(64u)); // returned
    auto idle = *mgr.CreateScratch(ScratchDesc(64u));
    idle = {};
    CollectAndRetire(dev, mgr);
    (void)mgr.CreateScratch(ScratchDesc(1024u)); // returned after the collect
    mgr.DrainScratch();
    EXPECT_EQ(dev.DestroyBufferCount, 3);
    EXPECT_EQ(mgr.GetScratchStats().PendingBytes + mgr.GetScratchStats().IdleBytes, 0u);
    held = {};
    EXPECT_EQ(dev.DestroyBufferCount, 4);
    EXPECT_EQ(mgr.GetScratchStats().ActiveBytes, 0u);
}

TEST(RHIBufferManagerScratch, DestructorDestroysParkedBuffers)
{
    MockDevice dev;
    {
        RHI::BufferManager mgr{dev};
        (void)mgr.CreateScratch(ScratchDesc(64u));
        EXPECT_EQ(dev.DestroyBufferCount, 0);
    }
    EXPECT_EQ(dev.DestroyBufferCount, 1);
}

TEST(RHIBufferManagerScratch, EnsureKeepsACoveringLeaseAndReplacesAnUndersizedOne)
{
    MockDevice dev;
    RHI::BufferManager mgr{dev};
    RHI::BufferManager::BufferLease lease;
    ASSERT_TRUE(mgr.EnsureScratch(lease, ScratchDesc(100u)));
    const auto first = lease.GetHandle();
    ASSERT_TRUE(mgr.EnsureScratch(lease, ScratchDesc(80u)));
    EXPECT_EQ(lease.GetHandle(), first);
    ASSERT_TRUE(mgr.EnsureScratch(lease, ScratchDesc(120u))) << "same class, larger request: a new buffer";
    EXPECT_NE(lease.GetHandle(), first);
    EXPECT_EQ(mgr.GetDesc(lease.GetHandle())->SizeBytes, 120u);
    EXPECT_EQ(mgr.GetScratchStats().PendingBytes, 128u) << "the replaced buffer is returned";
    mgr.SetScratchBudget(256u);
    EXPECT_FALSE(mgr.EnsureScratch(lease, ScratchDesc(200u)));
    EXPECT_FALSE(lease.IsValid());
}
