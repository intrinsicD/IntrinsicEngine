// GRAPHICS-038A — contract coverage for the Hi-Z buffer (HZB) resource shape and
// its retained graphics-owned ping-pong lifetime. The HZB build compute shader
// (038B), the two-phase cull-shader extension (038C), the camera-transition
// heuristic (038D), and the opt-in gpu;vulkan conservatism smoke (038E) are
// follow-ups; this slice pins the pure sizing math and the
// allocate/reallocate/retire/no-leak lifetime under the null RHI.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

import Extrinsic.Graphics.HZB;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureManager;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    constexpr std::uint32_t kFramesInFlight = 2u;
}

// Decision 1: pow2 sizing of each render dimension + full mip chain to 1x1 with
// mip count floor(log2(max(W,H)))+1, single-channel float, no normalized format.
TEST(GraphicsHZBResourceLifetime, ComputeDescPow2SizingAndMipCount)
{
    EXPECT_EQ(Graphics::NextPow2(0u), 1u);
    EXPECT_EQ(Graphics::NextPow2(1u), 1u);
    EXPECT_EQ(Graphics::NextPow2(2u), 2u);
    EXPECT_EQ(Graphics::NextPow2(3u), 4u);
    EXPECT_EQ(Graphics::NextPow2(1024u), 1024u);
    EXPECT_EQ(Graphics::NextPow2(1025u), 2048u);

    const Graphics::HZBDesc hd = Graphics::ComputeHZBDesc(1920u, 1080u);
    EXPECT_TRUE(hd.IsValid());
    EXPECT_EQ(hd.Width, 2048u);
    EXPECT_EQ(hd.Height, 2048u);
    EXPECT_EQ(hd.MipLevels, 12u); // floor(log2(2048)) + 1 = 11 + 1
    EXPECT_EQ(hd.Fmt, RHI::Format::R32_FLOAT);

    // Non-square extent: each dimension pow2 independently; mips off the larger.
    const Graphics::HZBDesc wide = Graphics::ComputeHZBDesc(1280u, 720u);
    EXPECT_EQ(wide.Width, 2048u);
    EXPECT_EQ(wide.Height, 1024u);
    EXPECT_EQ(wide.MipLevels, 12u);

    const Graphics::HZBDesc small = Graphics::ComputeHZBDesc(64u, 64u);
    EXPECT_EQ(small.Width, 64u);
    EXPECT_EQ(small.Height, 64u);
    EXPECT_EQ(small.MipLevels, 7u); // floor(log2(64)) + 1 = 6 + 1

    // A zero render extent yields an invalid (all-zero) desc.
    EXPECT_FALSE(Graphics::ComputeHZBDesc(0u, 1080u).IsValid());
    EXPECT_FALSE(Graphics::ComputeHZBDesc(1920u, 0u).IsValid());
}

// Decision 3: two retained textures, swapped per frame so phase 1 reads the
// previous-frame HZB and phase 2 writes the current-frame HZB.
TEST(GraphicsHZBResourceLifetime, PingPongRolesSwapEachFrameOverTwoTextures)
{
    MockDevice device;
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::HZBSystem hzb;
    hzb.Initialize(device, textureMgr);
    EXPECT_TRUE(hzb.IsInitialized());
    EXPECT_FALSE(hzb.IsAllocated());

    ASSERT_TRUE(hzb.EnsureAllocated(1920u, 1080u, /*currentFrame=*/0u));
    EXPECT_TRUE(hzb.IsAllocated());
    EXPECT_EQ(device.CreateTextureCount, 2); // the ping-pong pair

    const RHI::TextureHandle current0 = hzb.CurrentHZB();
    const RHI::TextureHandle previous0 = hzb.PreviousHZB();
    EXPECT_TRUE(current0.IsValid());
    EXPECT_TRUE(previous0.IsValid());
    EXPECT_NE(current0, previous0); // two distinct retained textures

    hzb.AdvanceFrame();
    // The roles swap: this frame's pyramid becomes next frame's history.
    EXPECT_EQ(hzb.CurrentHZB(), previous0);
    EXPECT_EQ(hzb.PreviousHZB(), current0);

    hzb.AdvanceFrame();
    EXPECT_EQ(hzb.CurrentHZB(), current0);
    EXPECT_EQ(hzb.PreviousHZB(), previous0);

    hzb.Shutdown();
}

// An unchanged render extent keeps the retained pair (no reallocation); a changed
// extent reallocates and retires the old pair through the framesInFlight window so
// an in-flight frame never samples a freed texture, with no net leak.
TEST(GraphicsHZBResourceLifetime, ReallocationRetiresOldPairThroughRetireWindow)
{
    MockDevice device;
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::HZBSystem hzb;
    hzb.Initialize(device, textureMgr);

    ASSERT_TRUE(hzb.EnsureAllocated(1920u, 1080u, /*currentFrame=*/0u));
    EXPECT_EQ(device.CreateTextureCount, 2);
    EXPECT_EQ(hzb.GetDiagnostics().AllocationCount, 1u);

    // Same pow2-resolved desc -> no realloc.
    ASSERT_TRUE(hzb.EnsureAllocated(1900u, 1070u, /*currentFrame=*/1u));
    EXPECT_EQ(device.CreateTextureCount, 2);
    EXPECT_EQ(hzb.GetDiagnostics().ReallocationCount, 0u);

    // Changed desc -> reallocate; the previous pair is retired (not yet freed).
    ASSERT_TRUE(hzb.EnsureAllocated(1024u, 1024u, /*currentFrame=*/5u));
    EXPECT_EQ(device.CreateTextureCount, 4);
    EXPECT_EQ(device.DestroyTextureCount, 0);
    EXPECT_EQ(hzb.GetDiagnostics().ReallocationCount, 1u);
    EXPECT_EQ(hzb.GetDiagnostics().AllocationCount, 2u);

    // Deadline not yet elapsed: the old pair survives.
    hzb.Tick(/*currentFrame=*/5u + kFramesInFlight - 1u, kFramesInFlight);
    EXPECT_EQ(device.DestroyTextureCount, 0);
    EXPECT_EQ(hzb.GetDiagnostics().PendingRetireCount, 2u);

    // Deadline elapsed: the old pair is freed exactly once.
    hzb.Tick(/*currentFrame=*/5u + kFramesInFlight, kFramesInFlight);
    EXPECT_EQ(device.DestroyTextureCount, 2);
    EXPECT_EQ(hzb.GetDiagnostics().RetiredTextureCount, 2u);
    EXPECT_EQ(hzb.GetDiagnostics().PendingRetireCount, 0u);

    // Shutdown frees the live pair; creates == destroys, no leak.
    hzb.Shutdown();
    EXPECT_EQ(device.DestroyTextureCount, 4);
    EXPECT_EQ(device.CreateTextureCount, 4);
    EXPECT_FALSE(hzb.IsAllocated());
    EXPECT_FALSE(hzb.IsInitialized());
    EXPECT_FALSE(hzb.CurrentHZB().IsValid());
}

// A failed texture create leaves the system unallocated and reports failure
// rather than exposing a half-allocated pair.
TEST(GraphicsHZBResourceLifetime, FailedTextureCreateLeavesSystemUnallocated)
{
    MockDevice device;
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::HZBSystem hzb;
    hzb.Initialize(device, textureMgr);

    device.FailNextTextureCreate = true; // first texture of the pair fails
    EXPECT_FALSE(hzb.EnsureAllocated(1920u, 1080u, /*currentFrame=*/0u));
    EXPECT_FALSE(hzb.IsAllocated());
    EXPECT_FALSE(hzb.CurrentHZB().IsValid());
    EXPECT_EQ(hzb.GetDiagnostics().AllocationCount, 0u);

    hzb.Shutdown();
}

TEST(GraphicsHZBBuildPlan, PerMipFallbackCoversEveryMipInOrder)
{
    const Graphics::HZBDesc desc = Graphics::ComputeHZBDesc(64u, 32u);
    ASSERT_TRUE(desc.IsValid());
    ASSERT_EQ(desc.MipLevels, 7u);

    const Graphics::HZBBuildDispatchPlan plan =
        Graphics::ComputeHZBBuildDispatchPlan(
            desc,
            Graphics::HZBBuildCapabilities{.SupportsSinglePassMipChain = false});

    EXPECT_EQ(plan.Mode, Graphics::HZBBuildMode::PerMipDispatch);
    ASSERT_TRUE(plan.IsValid());
    ASSERT_EQ(plan.Dispatches.size(), desc.MipLevels);

    constexpr std::uint32_t kExpectedWidths[] = {64u, 32u, 16u, 8u, 4u, 2u, 1u};
    constexpr std::uint32_t kExpectedHeights[] = {32u, 16u, 8u, 4u, 2u, 1u, 1u};
    constexpr std::uint32_t kExpectedGroupsX[] = {4u, 2u, 1u, 1u, 1u, 1u, 1u};
    constexpr std::uint32_t kExpectedGroupsY[] = {2u, 1u, 1u, 1u, 1u, 1u, 1u};

    for (std::uint32_t mip = 0u; mip < desc.MipLevels; ++mip)
    {
        const Graphics::HZBBuildDispatchDesc& dispatch = plan.Dispatches[mip];
        EXPECT_EQ(dispatch.TargetMip, mip);
        EXPECT_EQ(dispatch.SourceMip, mip == 0u ? 0u : mip - 1u);
        EXPECT_EQ(dispatch.ReadsDepthSource, mip == 0u);
        EXPECT_EQ(dispatch.TargetWidth, kExpectedWidths[mip]);
        EXPECT_EQ(dispatch.TargetHeight, kExpectedHeights[mip]);
        EXPECT_EQ(dispatch.GroupCountX, kExpectedGroupsX[mip]);
        EXPECT_EQ(dispatch.GroupCountY, kExpectedGroupsY[mip]);
        EXPECT_EQ(dispatch.GroupCountZ, 1u);
    }
}

TEST(GraphicsHZBBuildPlan, SinglePassMipChainUsesOneBaseDispatch)
{
    const Graphics::HZBDesc desc = Graphics::ComputeHZBDesc(64u, 32u);
    ASSERT_TRUE(desc.IsValid());

    const Graphics::HZBBuildDispatchPlan plan =
        Graphics::ComputeHZBBuildDispatchPlan(
            desc,
            Graphics::HZBBuildCapabilities{.SupportsSinglePassMipChain = true},
            /*tileSize=*/8u);

    EXPECT_EQ(plan.Mode, Graphics::HZBBuildMode::SinglePassMipChain);
    ASSERT_TRUE(plan.IsValid());
    ASSERT_EQ(plan.Dispatches.size(), 1u);

    const Graphics::HZBBuildDispatchDesc& dispatch = plan.Dispatches.front();
    EXPECT_EQ(dispatch.TargetMip, 0u);
    EXPECT_EQ(dispatch.SourceMip, 0u);
    EXPECT_TRUE(dispatch.ReadsDepthSource);
    EXPECT_EQ(dispatch.TargetWidth, desc.Width);
    EXPECT_EQ(dispatch.TargetHeight, desc.Height);
    EXPECT_EQ(dispatch.GroupCountX, 8u);
    EXPECT_EQ(dispatch.GroupCountY, 4u);
    EXPECT_EQ(dispatch.GroupCountZ, 1u);
}

TEST(GraphicsHZBBuildPlan, RecordFallbackDispatchesAndMipStitchBarriers)
{
    const Graphics::HZBDesc desc = Graphics::ComputeHZBDesc(64u, 32u);
    const Graphics::HZBBuildDispatchPlan plan =
        Graphics::ComputeHZBBuildDispatchPlan(
            desc,
            Graphics::HZBBuildCapabilities{.SupportsSinglePassMipChain = false});

    Tests::MockCommandContext cmd;
    const RHI::PipelineHandle pipeline{41u, 1u};
    const RHI::TextureHandle hzb{42u, 1u};

    ASSERT_TRUE(Graphics::RecordHZBBuild(cmd, pipeline, hzb, plan));

    EXPECT_EQ(cmd.BindPipelineCalls, 1);
    ASSERT_EQ(cmd.DispatchRecords.size(), plan.Dispatches.size());
    ASSERT_EQ(cmd.PushConstantPayloads.size(), plan.Dispatches.size());
    ASSERT_EQ(cmd.TextureBarrierCalls.size(), plan.Dispatches.size() - 1u);

    for (std::size_t i = 0u; i < plan.Dispatches.size(); ++i)
    {
        const Graphics::HZBBuildDispatchDesc& dispatch = plan.Dispatches[i];
        const Tests::MockCommandContext::DispatchRecord& recorded = cmd.DispatchRecords[i];
        EXPECT_EQ(recorded.X, dispatch.GroupCountX);
        EXPECT_EQ(recorded.Y, dispatch.GroupCountY);
        EXPECT_EQ(recorded.Z, dispatch.GroupCountZ);

        ASSERT_EQ(cmd.PushConstantPayloads[i].size(), sizeof(Graphics::HZBBuildPushConstants));
        Graphics::HZBBuildPushConstants pc{};
        std::memcpy(&pc, cmd.PushConstantPayloads[i].data(), sizeof(pc));
        EXPECT_EQ(pc.RenderWidth, desc.Width);
        EXPECT_EQ(pc.RenderHeight, desc.Height);
        EXPECT_EQ(pc.TargetMip, dispatch.TargetMip);
        EXPECT_EQ(pc.SourceMip, dispatch.SourceMip);
        EXPECT_EQ(pc.MipCount, desc.MipLevels);
        EXPECT_EQ(pc.BuildMode, static_cast<std::uint32_t>(Graphics::HZBBuildMode::PerMipDispatch));
        EXPECT_EQ(pc.TargetWidth, dispatch.TargetWidth);
        EXPECT_EQ(pc.TargetHeight, dispatch.TargetHeight);
    }

    for (const Tests::MockCommandContext::TextureBarrierRecord& barrier : cmd.TextureBarrierCalls)
    {
        EXPECT_EQ(barrier.Texture, hzb);
        EXPECT_EQ(barrier.Before, RHI::TextureLayout::General);
        EXPECT_EQ(barrier.After, RHI::TextureLayout::General);
        EXPECT_EQ(barrier.BeforeAccess, RHI::MemoryAccess::ShaderWrite);
        EXPECT_EQ(barrier.AfterAccess,
                  RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
    }
}

TEST(GraphicsHZBBuildPlan, RecordRejectsInvalidInputs)
{
    Tests::MockCommandContext cmd;
    const Graphics::HZBBuildDispatchPlan plan =
        Graphics::ComputeHZBBuildDispatchPlan(
            Graphics::ComputeHZBDesc(64u, 32u),
            Graphics::HZBBuildCapabilities{});

    EXPECT_FALSE(Graphics::RecordHZBBuild(cmd, RHI::PipelineHandle{}, RHI::TextureHandle{42u, 1u}, plan));
    EXPECT_FALSE(Graphics::RecordHZBBuild(cmd, RHI::PipelineHandle{41u, 1u}, RHI::TextureHandle{}, plan));
    EXPECT_FALSE(Graphics::RecordHZBBuild(cmd, RHI::PipelineHandle{41u, 1u}, RHI::TextureHandle{42u, 1u}, {}));
    EXPECT_EQ(cmd.BindPipelineCalls, 0);
    EXPECT_EQ(cmd.DispatchCalls, 0);
}
