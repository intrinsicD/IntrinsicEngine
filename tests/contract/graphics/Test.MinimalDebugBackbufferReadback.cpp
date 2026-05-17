// GRAPHICS-033D — CPU-mock contract for the MinimalDebug backbuffer-to-host
// readback seam. The renderer-integrated `gpu;vulkan` smoke fixture in
// tests/integration/graphics/Test.MinimalDebugSurfaceGpuSmoke.cpp drives the
// live four-sample pixel assertion on Vulkan-capable hosts; this file locks in
// the CPU-observable wiring contract so the seam cannot silently regress on
// the default CPU gate (where the MockDevice never reaches operational
// IsOperational() / Vulkan command recording).
//
// Specifically:
//   1. Default state of the renderer: GetMinimalDebugBackbufferReadbackBuffer()
//      returns an invalid handle and the readback copy count stays at zero
//      after a frame.
//   2. After SetMinimalDebugBackbufferReadbackBuffer(handle) with a valid
//      handle and the MinimalDebug recipe selected, the readback triplet
//      records on the MockDevice's CommandContext (Present -> TransferSrc,
//      CopyTextureToBuffer, TransferSrc -> Present) and the
//      MinimalDebugBackbufferReadbackCopyCount counter increments by 1.
//   3. With the Default recipe (or a non-operational device), the wiring is
//      a no-op regardless of whether a handle was set.

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    [[nodiscard]] bool HasBackbufferBarrier(const MockDevice& device,
                                            const RHI::TextureLayout before,
                                            const RHI::TextureLayout after) noexcept
    {
        for (const auto& barrier : device.CommandContext.TextureBarrierCalls)
        {
            if (barrier.Texture == device.BackbufferHandle &&
                barrier.Before == before && barrier.After == after)
            {
                return true;
            }
        }
        return false;
    }
}

TEST(MinimalDebugBackbufferReadbackContract, DefaultStateIsDisabledAndNoCopyRecords)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{421u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

    EXPECT_FALSE(renderer->GetMinimalDebugBackbufferReadbackBuffer().IsValid())
        << "Default post-Initialize readback wiring must be disabled.";

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 128},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u);
    EXPECT_FALSE(HasBackbufferBarrier(device, RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc));

    renderer->Shutdown();
}

TEST(MinimalDebugBackbufferReadbackContract, ConfiguredHandleRecordsReadbackTripletOnce)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{521u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

    const RHI::BufferHandle readback{4242u, 7u};
    renderer->SetMinimalDebugBackbufferReadbackBuffer(readback);
    EXPECT_EQ(renderer->GetMinimalDebugBackbufferReadbackBuffer(), readback);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 128},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 1u)
        << "Readback triplet must record exactly once per operational frame "
           "when a readback handle is configured.";

    EXPECT_TRUE(HasBackbufferBarrier(device, RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc))
        << "Present -> TransferSrc transition must precede the copy.";
    EXPECT_TRUE(HasBackbufferBarrier(device, RHI::TextureLayout::TransferSrc, RHI::TextureLayout::Present))
        << "TransferSrc -> Present transition must restore the layout the device's "
           "Present() expects.";

    renderer->Shutdown();
}

TEST(MinimalDebugBackbufferReadbackContract, DefaultRecipeIgnoresReadbackHandle)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{621u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    // Default recipe selected (no SetFrameRecipe call).

    renderer->SetMinimalDebugBackbufferReadbackBuffer(RHI::BufferHandle{9999u, 1u});

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u)
        << "Default recipe must not record the MinimalDebug readback triplet even "
           "when a handle was configured.";

    renderer->Shutdown();
}

TEST(MinimalDebugBackbufferReadbackContract, NonOperationalDeviceSkipsCopy)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{721u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);
    renderer->SetMinimalDebugBackbufferReadbackBuffer(RHI::BufferHandle{9988u, 1u});

    device.Operational = false;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u)
        << "Non-operational device must skip the readback triplet so the "
           "fallback-counter stability contract is preserved.";

    renderer->Shutdown();
}
