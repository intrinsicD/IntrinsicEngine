#include <gtest/gtest.h>

#include <atomic>

import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    RHI::PipelineDesc AnyGraphicsPipeline()
    {
        RHI::PipelineDesc d{};
        d.VertexShaderPath   = "shaders/test.vert.spv";
        d.FragmentShaderPath = "shaders/test.frag.spv";
        return d;
    }
}

// -----------------------------------------------------------------------------
// F3 / F14 coverage
// -----------------------------------------------------------------------------

TEST(RHIPipelineManager, CreateReturnsLeaseOnSuccess)
{
    MockDevice dev;
    RHI::PipelineManager mgr{dev};

    auto leaseOr = mgr.Create(AnyGraphicsPipeline());
    ASSERT_TRUE(leaseOr.has_value());
    EXPECT_TRUE(leaseOr->IsValid());
    EXPECT_EQ(dev.CreatePipelineCount, 1);
    EXPECT_TRUE(mgr.IsReady(leaseOr->GetHandle()));
}

TEST(RHIPipelineManager, CreateReturnsPipelineCreationFailedOnDeviceFailure)
{
    MockDevice dev;
    dev.FailNextPipelineCreate = true;
    RHI::PipelineManager mgr{dev};

    auto leaseOr = mgr.Create(AnyGraphicsPipeline());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::PipelineCreationFailed);
    EXPECT_EQ(dev.DestroyPipelineCount, 0);
}

TEST(RHIPipelineManager, CreateShortCircuitsOnNonOperationalDevice)
{
    MockDevice dev;
    dev.Operational = false;
    RHI::PipelineManager mgr{dev};

    auto leaseOr = mgr.Create(AnyGraphicsPipeline());
    ASSERT_FALSE(leaseOr.has_value());
    EXPECT_EQ(leaseOr.error(), Core::ErrorCode::DeviceNotOperational);
    EXPECT_EQ(dev.CreatePipelineCount, 0);
}

// -----------------------------------------------------------------------------
// Create callback — fires at construction time on the calling thread
// -----------------------------------------------------------------------------

TEST(RHIPipelineManager, OnCompiledCallbackFiresAtCreate)
{
    MockDevice dev;
    RHI::PipelineManager mgr{dev};
    std::atomic<int> callbackHits{0};

    auto lease = *mgr.Create(AnyGraphicsPipeline(),
                             [&](RHI::PipelineHandle h)
                             {
                                 EXPECT_TRUE(h.IsValid());
                                 ++callbackHits;
                             });

    EXPECT_EQ(callbackHits.load(), 1) << "Initial compile must fire OnCompiled once.";
}

// -----------------------------------------------------------------------------
// Hot-reload lifecycle: Recompile stages + CommitPending promotes
//
// Recompile() on any thread must not invalidate the pool handle the caller
// already holds. Before CommitPending(), GetDeviceHandle still returns the
// PREVIOUS device pipeline (so rendering continues). CommitPending destroys
// the old device pipeline and promotes the new one.
// -----------------------------------------------------------------------------

TEST(RHIPipelineManager, RecompileStagesNewPipelineWithoutInvalidatingHandle)
{
    MockDevice dev;
    RHI::PipelineManager mgr{dev};
    auto lease = *mgr.Create(AnyGraphicsPipeline());
    const auto handle = lease.GetHandle();
    const auto deviceBefore = mgr.GetDeviceHandle(handle);

    RHI::PipelineDesc newDesc = AnyGraphicsPipeline();
    newDesc.FragmentShaderPath = "shaders/test-v2.frag.spv";
    mgr.Recompile(handle, newDesc);

    // Handle still valid; pool generation unchanged.
    EXPECT_TRUE(mgr.IsReady(handle));
    EXPECT_EQ(mgr.GetDeviceHandle(handle), deviceBefore)
        << "GetDeviceHandle must still return the OLD device pipeline until CommitPending.";

    // Device recorded the new CreatePipeline call but the old pipeline is
    // still alive — no DestroyPipeline yet.
    EXPECT_EQ(dev.CreatePipelineCount, 2);
    EXPECT_EQ(dev.DestroyPipelineCount, 0);
}

TEST(RHIPipelineManager, CommitPendingPromotesNewPipelineAndDestroysOld)
{
    MockDevice dev;
    RHI::PipelineManager mgr{dev};
    std::atomic<int> callbackHits{0};

    auto lease = *mgr.Create(AnyGraphicsPipeline(),
                             [&](RHI::PipelineHandle) { ++callbackHits; });
    const auto handle = lease.GetHandle();
    const auto deviceBefore = mgr.GetDeviceHandle(handle);

    callbackHits = 0; // ignore the initial-compile callback

    mgr.Recompile(handle, AnyGraphicsPipeline());
    mgr.CommitPending();

    // Old pipeline destroyed, new one active, callback fired.
    EXPECT_EQ(dev.DestroyPipelineCount, 1);
    EXPECT_NE(mgr.GetDeviceHandle(handle), deviceBefore);
    EXPECT_EQ(callbackHits.load(), 1)
        << "CommitPending must fire the OnCompiled callback for each promoted pipeline.";
}

// -----------------------------------------------------------------------------
// Lifetime + F2 balance
// -----------------------------------------------------------------------------

TEST(RHIPipelineManager, LastLeaseDroppedDestroysGpuPipeline)
{
    MockDevice dev;
    {
        RHI::PipelineManager mgr{dev};
        {
            auto lease = *mgr.Create(AnyGraphicsPipeline());
            EXPECT_EQ(dev.DestroyPipelineCount, 0);
        } // lease drops -> refcount -> 0 -> DestroyPipeline
        EXPECT_EQ(dev.DestroyPipelineCount, 1);
    }
}

TEST(RHIPipelineManager, DestructorIsCleanAfterAllLeasesDropped)
{
    MockDevice dev;
    {
        RHI::PipelineManager mgr{dev};
        auto a = *mgr.Create(AnyGraphicsPipeline());
        auto b = *mgr.Create(AnyGraphicsPipeline());
        auto c = a.Share();
    } // LiveLeaseCount must balance to zero.
    EXPECT_EQ(dev.CreatePipelineCount,  2);
    EXPECT_EQ(dev.DestroyPipelineCount, 2);
}
