#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
    [[nodiscard]] bool ContainsTextureBarrier(const Extrinsic::Tests::MockCommandContext& context,
                                              const Extrinsic::RHI::TextureHandle handle)
    {
        for (const auto& barrier : context.TextureBarrierCalls)
        {
            if (barrier.Texture == handle)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int FindEventIndex(const Extrinsic::Tests::MockCommandContext& context,
                                     const Extrinsic::Tests::MockCommandContext::EventKind kind,
                                     const int start = 0)
    {
        for (int i = start; i < static_cast<int>(context.Events.size()); ++i)
        {
            if (context.Events[static_cast<std::size_t>(i)] == kind)
            {
                return i;
            }
        }
        return -1;
    }
}

TEST(RendererFrameLifecycle, UsesDeviceFrameLifecycleBackbufferAndCommandContext)
{
    Extrinsic::Tests::MockDevice device;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 5u, .SwapchainImageIndex = 2u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    EXPECT_EQ(device.BeginFrameCount, 1);
    EXPECT_EQ(frame.FrameIndex, 5u);
    EXPECT_EQ(frame.SwapchainImageIndex, 2u);

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.CompileSucceeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.ExecuteSucceeded) << stats.Diagnostic;
    EXPECT_EQ(device.GetBackbufferHandleCount, 1);
    EXPECT_EQ(device.LastBackbufferFrame.FrameIndex, frame.FrameIndex);
    EXPECT_EQ(device.LastBackbufferFrame.SwapchainImageIndex, frame.SwapchainImageIndex);
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);
    EXPECT_TRUE(ContainsTextureBarrier(device.CommandContext, device.BackbufferHandle));
    EXPECT_GE(device.CommandContext.FillBufferCalls, 8);
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 2);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 2);
    ASSERT_EQ(device.CommandContext.PushConstantSizes.size(), 2u);
    EXPECT_EQ(device.CommandContext.PushConstantSizes[0], sizeof(Extrinsic::RHI::GpuCullPushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[1], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.LastDispatch.X, 1563u);
    EXPECT_EQ(device.CommandContext.LastDispatch.Y, 1u);
    EXPECT_EQ(device.CommandContext.LastDispatch.Z, 1u);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 1);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 1);
    EXPECT_EQ(device.CommandContext.LastMaxDrawCount, 100000u);

    const int dispatchEvent = FindEventIndex(device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::Dispatch);
    const int drawEvent = FindEventIndex(device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::DrawIndexedIndirectCount);
    ASSERT_GE(dispatchEvent, 0);
    ASSERT_GE(drawEvent, 0);
    EXPECT_LT(dispatchEvent, drawEvent);

    const std::uint64_t completedFrame = renderer->EndFrame(frame);
    EXPECT_EQ(completedFrame, 1u);
    EXPECT_EQ(device.EndFrameCount, 1);
    EXPECT_EQ(device.PresentCount, 0) << "Runtime remains responsible for presentation.";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, InvalidDeviceBackbufferReportsRecipeDiagnostic)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = {};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.CompileSucceeded);
    EXPECT_FALSE(stats.ExecuteSucceeded);
    EXPECT_NE(stats.Diagnostic.find("Backbuffer"), std::string::npos);
    EXPECT_EQ(device.GetBackbufferHandleCount, 1);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, NonOperationalDeviceSkipsCullingCommandsButExecutesGraph)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{91u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.CompileSucceeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.ExecuteSucceeded) << stats.Diagnostic;
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);
    EXPECT_EQ(device.CommandContext.FillBufferCalls, 0);
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 0);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 0);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 0);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, BeginFrameSkipDoesNotRecordCommands)
{
    Extrinsic::Tests::MockDevice device;
    device.BeginFrameResult = false;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(renderer->BeginFrame(frame));
    EXPECT_EQ(device.BeginFrameCount, 1);
    EXPECT_EQ(device.GetBackbufferHandleCount, 0);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);

    renderer->Shutdown();
}


