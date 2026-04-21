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
import Extrinsic.RHI.TextureManager;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    RHI::TextureDesc AnyTextureDesc()
    {
        return RHI::TextureDesc{
            .Width     = 64,
            .Height    = 64,
            .MipLevels = 1,
            .Fmt       = RHI::Format::RGBA8_UNORM,
            .Usage     = RHI::TextureUsage::Sampled,
            .DebugName = "test-texture",
        };
    }

    RHI::SamplerHandle AnySamplerHandle()
    {
        // The sampler is passed through to IBindlessHeap::AllocateTextureSlot.
        // The MockBindlessHeap doesn't inspect it; any valid-looking handle
        // triggers the Allocate path.
        return RHI::SamplerHandle{42u, 1u};
    }
}

// -----------------------------------------------------------------------------
// F3 / F14 coverage
// -----------------------------------------------------------------------------

TEST(RHITextureManager, CreateReturnsLeaseOnSuccess)
{
    MockDevice dev;
    RHI::TextureManager mgr{dev, dev.Bindless};

    auto leaseOr = mgr.Create(AnyTextureDesc(), AnySamplerHandle());
    ASSERT_TRUE(leaseOr.has_value());
    EXPECT_TRUE(leaseOr->IsValid());
    EXPECT_EQ(dev.CreateTextureCount, 1);
    EXPECT_EQ(dev.Bindless.AllocateCalls, 1)
        << "Creating a texture with a valid sampler must register a bindless slot.";
}

TEST(RHITextureManager, CreateReturnsOutOfDeviceMemoryOnDeviceFailure)
{
    MockDevice dev;
    dev.FailNextTextureCreate = true;
    RHI::TextureManager mgr{dev, dev.Bindless};

    auto leaseOr = mgr.Create(AnyTextureDesc(), AnySamplerHandle());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::OutOfDeviceMemory);
    EXPECT_EQ(dev.Bindless.AllocateCalls, 0)
        << "Bindless slot must not be allocated when the device Create failed.";
}

TEST(RHITextureManager, CreateShortCircuitsOnNonOperationalDevice)
{
    MockDevice dev;
    dev.Operational = false;
    RHI::TextureManager mgr{dev, dev.Bindless};

    auto leaseOr = mgr.Create(AnyTextureDesc(), AnySamplerHandle());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::DeviceNotOperational);
    EXPECT_EQ(dev.CreateTextureCount, 0);
    EXPECT_EQ(dev.Bindless.AllocateCalls, 0);
}

// -----------------------------------------------------------------------------
// Lifecycle: bindless slot lifetime is coupled to the texture lease
// -----------------------------------------------------------------------------

TEST(RHITextureManager, LastLeaseDroppedDestroysTextureAndFreesBindlessSlot)
{
    MockDevice dev;
    {
        RHI::TextureManager mgr{dev, dev.Bindless};
        {
            auto lease = *mgr.Create(AnyTextureDesc(), AnySamplerHandle());
            EXPECT_EQ(dev.Bindless.FreeCalls, 0);
            EXPECT_EQ(dev.DestroyTextureCount, 0);
        } // lease drops -> refcount 1 -> 0 -> bindless free, then texture destroy
        EXPECT_EQ(dev.Bindless.FreeCalls, 1);
        EXPECT_EQ(dev.DestroyTextureCount, 1);
    }
}

TEST(RHITextureManager, CreateWithoutSamplerSkipsBindlessRegistration)
{
    MockDevice dev;
    RHI::TextureManager mgr{dev, dev.Bindless};

    // Invalid/default sampler handle -> no bindless registration.
    auto lease = *mgr.Create(AnyTextureDesc(), RHI::SamplerHandle{});
    EXPECT_TRUE(lease.IsValid());
    EXPECT_EQ(dev.Bindless.AllocateCalls, 0);
}

TEST(RHITextureManager, DestructorIsCleanAfterAllLeasesDropped)
{
    MockDevice dev;
    {
        RHI::TextureManager mgr{dev, dev.Bindless};
        auto a = *mgr.Create(AnyTextureDesc(), AnySamplerHandle());
        auto b = *mgr.Create(AnyTextureDesc(), AnySamplerHandle());
        auto c = a.Share();
    } // LiveLeaseCount must balance to zero.
    EXPECT_EQ(dev.CreateTextureCount, 2);
    EXPECT_EQ(dev.DestroyTextureCount, 2);
    EXPECT_EQ(dev.Bindless.AllocateCalls, 2);
    EXPECT_EQ(dev.Bindless.FreeCalls, 2);
}
