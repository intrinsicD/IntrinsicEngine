// GRAPHICS-076E — CPU-mock contract for the canonical default-recipe
// backbuffer-to-host readback seam. The opt-in gpu;vulkan smoke in
// Test.DefaultRecipeSurfaceGpuSmoke.cpp drives the four-sample pixel assertion;
// this file locks the CPU-observable API, recipe gating, and diagnostic counter.

#include <cstdint>
#include <memory>

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

    void DriveOneDefaultFrame(Graphics::IRenderer& renderer)
    {
        RHI::FrameHandle frame{};
        ASSERT_TRUE(renderer.BeginFrame(frame));

        const Graphics::RenderFrameInput input{
            .Viewport = {.Width = 128, .Height = 128},
        };
        Graphics::RenderWorld world = renderer.ExtractRenderWorld(input);
        renderer.PrepareFrame(world);
        renderer.ExecuteFrame(frame, world);
    }
}

TEST(DefaultRecipeBackbufferReadbackContract, DefaultStateIsDisabledAndNoCopyRecords)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{1421u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    EXPECT_FALSE(renderer->GetDefaultRecipeBackbufferReadbackBuffer().IsValid())
        << "Default post-Initialize readback wiring must be disabled.";

    DriveOneDefaultFrame(*renderer);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 0u);
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u);
    EXPECT_FALSE(HasBackbufferBarrier(device, RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc));

    renderer->Shutdown();
}

TEST(DefaultRecipeBackbufferReadbackContract, ConfiguredHandleRecordsReadbackTripletOnce)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{1521u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    const RHI::BufferHandle readback{5242u, 7u};
    renderer->SetDefaultRecipeBackbufferReadbackBuffer(readback);
    EXPECT_EQ(renderer->GetDefaultRecipeBackbufferReadbackBuffer(), readback);

    DriveOneDefaultFrame(*renderer);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Readback triplet must record exactly once per operational default-recipe frame "
           "when a readback handle is configured.";
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u)
        << "Default-recipe readback must not increment the MinimalDebug counter.";

    EXPECT_TRUE(HasBackbufferBarrier(device, RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc))
        << "Present -> TransferSrc transition must precede the copy.";
    EXPECT_TRUE(HasBackbufferBarrier(device, RHI::TextureLayout::TransferSrc, RHI::TextureLayout::Present))
        << "TransferSrc -> Present transition must restore the layout the device's Present() expects.";

    renderer->Shutdown();
}

TEST(DefaultRecipeBackbufferReadbackContract, MinimalDebugRecipeIgnoresDefaultRecipeReadbackHandle)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{1621u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);
    renderer->SetDefaultRecipeBackbufferReadbackBuffer(RHI::BufferHandle{5999u, 1u});

    DriveOneDefaultFrame(*renderer);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 0u)
        << "MinimalDebug must not record the default-recipe readback triplet even "
           "when a default-recipe handle was configured.";
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u)
        << "The MinimalDebug readback hook was not armed.";

    renderer->Shutdown();
}


TEST(DefaultRecipeBackbufferReadbackContract, SkipsCopyWhenDeviceIsNonOperational)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{1721u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetDefaultRecipeBackbufferReadbackBuffer(RHI::BufferHandle{5988u, 1u});

    device.Operational = false;

    DriveOneDefaultFrame(*renderer);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 0u)
        << "Non-operational device must skip the default-recipe readback triplet.";

    renderer->Shutdown();
}
