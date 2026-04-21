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
import Extrinsic.RHI.SamplerManager;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    RHI::SamplerDesc LinearSampler()
    {
        return RHI::SamplerDesc{
            .MagFilter = RHI::FilterMode::Linear,
            .MinFilter = RHI::FilterMode::Linear,
            .MipFilter = RHI::MipmapMode::Linear,
            .AddressU  = RHI::AddressMode::Repeat,
            .AddressV  = RHI::AddressMode::Repeat,
            .AddressW  = RHI::AddressMode::Repeat,
        };
    }

    RHI::SamplerDesc NearestClampSampler()
    {
        return RHI::SamplerDesc{
            .MagFilter = RHI::FilterMode::Nearest,
            .MinFilter = RHI::FilterMode::Nearest,
            .MipFilter = RHI::MipmapMode::Nearest,
            .AddressU  = RHI::AddressMode::ClampToEdge,
            .AddressV  = RHI::AddressMode::ClampToEdge,
            .AddressW  = RHI::AddressMode::ClampToEdge,
        };
    }
}

// -----------------------------------------------------------------------------
// F3 / F14 coverage
// -----------------------------------------------------------------------------

TEST(RHISamplerManager, GetOrCreateReturnsOutOfDeviceMemoryOnDeviceFailure)
{
    MockDevice dev;
    dev.FailNextSamplerCreate = true;
    RHI::SamplerManager mgr{dev};

    auto leaseOr = mgr.GetOrCreate(LinearSampler());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::OutOfDeviceMemory);
}

TEST(RHISamplerManager, GetOrCreateShortCircuitsOnNonOperationalDevice)
{
    MockDevice dev;
    dev.Operational = false;
    RHI::SamplerManager mgr{dev};

    auto leaseOr = mgr.GetOrCreate(LinearSampler());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::DeviceNotOperational);
    EXPECT_EQ(dev.CreateSamplerCount, 0);
}

// -----------------------------------------------------------------------------
// Deduplication — the headline SamplerManager feature
// -----------------------------------------------------------------------------

TEST(RHISamplerManager, IdenticalDescsReturnSameGpuObject)
{
    MockDevice dev;
    RHI::SamplerManager mgr{dev};

    auto lease1 = *mgr.GetOrCreate(LinearSampler());
    auto lease2 = *mgr.GetOrCreate(LinearSampler());

    EXPECT_EQ(lease1.GetHandle(), lease2.GetHandle());
    EXPECT_EQ(dev.CreateSamplerCount, 1)
        << "Second GetOrCreate with identical desc must hit the dedup cache.";
}

TEST(RHISamplerManager, DifferentDescsReturnDistinctGpuObjects)
{
    MockDevice dev;
    RHI::SamplerManager mgr{dev};

    auto linear  = *mgr.GetOrCreate(LinearSampler());
    auto nearest = *mgr.GetOrCreate(NearestClampSampler());

    EXPECT_NE(linear.GetHandle(), nearest.GetHandle());
    EXPECT_EQ(dev.CreateSamplerCount, 2);
}

// -----------------------------------------------------------------------------
// Refcount + dedup interaction: when all leases for a cached sampler drop, the
// GPU sampler is destroyed and the dedup slot is freed, so a subsequent
// GetOrCreate allocates a fresh GPU sampler.
// -----------------------------------------------------------------------------

TEST(RHISamplerManager, LastLeaseDroppedDestroysGpuSampler)
{
    MockDevice dev;
    {
        RHI::SamplerManager mgr{dev};
        {
            auto a = *mgr.GetOrCreate(LinearSampler());
            auto b = *mgr.GetOrCreate(LinearSampler()); // dedup hit, refcount 2
            EXPECT_EQ(dev.CreateSamplerCount, 1);
            EXPECT_EQ(dev.DestroySamplerCount, 0);
        } // both leases drop, refcount -> 0, DestroySampler fires
        EXPECT_EQ(dev.DestroySamplerCount, 1);

        // After destruction, the dedup entry is gone: same desc allocates a
        // new GPU sampler rather than returning a stale handle.
        auto c = *mgr.GetOrCreate(LinearSampler());
        EXPECT_EQ(dev.CreateSamplerCount, 2);
    }
}

TEST(RHISamplerManager, DestructorIsCleanAfterAllLeasesDropped)
{
    MockDevice dev;
    {
        RHI::SamplerManager mgr{dev};
        auto a = *mgr.GetOrCreate(LinearSampler());
        auto b = *mgr.GetOrCreate(LinearSampler());   // dedup hit
        auto c = *mgr.GetOrCreate(NearestClampSampler());
        auto d = a.Share();
        // All four leases drop before manager destructor runs.
    } // LiveLeaseCount must be zero; assertion would fire otherwise.
}
