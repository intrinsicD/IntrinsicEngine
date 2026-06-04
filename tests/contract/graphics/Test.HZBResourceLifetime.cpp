// GRAPHICS-038A — contract coverage for the Hi-Z buffer (HZB) resource shape and
// its retained graphics-owned ping-pong lifetime. The HZB build compute shader
// (038B), the two-phase cull-shader extension (038C), the camera-transition
// heuristic (038D), and the opt-in gpu;vulkan conservatism smoke (038E) are
// follow-ups; this slice pins the pure sizing math and the
// allocate/reallocate/retire/no-leak lifetime under the null RHI.

#include <cstdint>

#include <gtest/gtest.h>

import Extrinsic.Graphics.HZB;
import Extrinsic.RHI.Descriptors;
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
