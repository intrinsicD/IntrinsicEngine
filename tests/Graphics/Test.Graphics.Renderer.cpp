#include <gtest/gtest.h>

#include <string>

import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.FrameHandle;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

TEST(GraphicsRenderer, NullRendererExecutesRenderGraphPath)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Alpha = 0.25,
        .Viewport = {.Width = 1280u, .Height = 720u},
    };
    auto world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);

    renderer->ExecuteFrame(frame, world);
    const auto& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.CompileSucceeded);
    EXPECT_TRUE(stats.ExecuteSucceeded);
    EXPECT_GT(stats.PassCount, 0u);
    EXPECT_GT(stats.ResourceCount, 0u);
    EXPECT_GT(stats.BarrierCount, 0u);
    EXPECT_GT(stats.TransientMemoryEstimateBytes, 0u);
    EXPECT_GT(device.CommandContext.TextureBarrierCalls.size(), 0u);
    EXPECT_GT(device.CommandContext.BufferBarrierCalls.size(), 0u);
    EXPECT_FALSE(stats.DebugDump.empty());

    renderer->Resize(1920u, 1080u);
    renderer->ExecuteFrame(frame, world);

    EXPECT_EQ(renderer->EndFrame(frame), 0u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererDebugDumpContainsCanonicalPassesAndDataflowOrder)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Alpha = 0.5,
        .Viewport = {.Width = 1920u, .Height = 1080u},
    };
    auto world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const auto& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.CompileSucceeded);
    ASSERT_FALSE(stats.DebugDump.empty());
    EXPECT_GT(stats.CulledPassCount, 0u);

    const auto& dump = stats.DebugDump;
    EXPECT_NE(dump.find("name=\"Null.Compute.Prologue\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.Culling\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.GBuffer\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.DeferredLighting\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.Bloom\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.ToneMap\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.FXAA\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.SelectionOutline\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Null.Present\""), std::string::npos);
    EXPECT_EQ(dump.find("name=\"Null.OptionalDebugView\""), std::string::npos);
    EXPECT_EQ(dump.find("name=\"Null.Picking\""), std::string::npos);

    const std::size_t gbufferPos = dump.find("name=\"Null.GBuffer\"");
    const std::size_t deferredPos = dump.find("name=\"Null.DeferredLighting\"");
    const std::size_t bloomPos = dump.find("name=\"Null.Bloom\"");
    const std::size_t toneMapPos = dump.find("name=\"Null.ToneMap\"");
    const std::size_t presentPos = dump.find("name=\"Null.Present\"");
    ASSERT_NE(gbufferPos, std::string::npos);
    ASSERT_NE(deferredPos, std::string::npos);
    ASSERT_NE(bloomPos, std::string::npos);
    ASSERT_NE(toneMapPos, std::string::npos);
    ASSERT_NE(presentPos, std::string::npos);

    EXPECT_LT(gbufferPos, deferredPos);
    EXPECT_LT(deferredPos, bloomPos);
    EXPECT_LT(bloomPos, toneMapPos);
    EXPECT_LT(toneMapPos, presentPos);

    EXPECT_EQ(renderer->EndFrame(frame), 0u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererAddsPickingPassWhenPickIsPending)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Alpha = 0.5,
        .Viewport = {.Width = 1920u, .Height = 1080u},
        .HasPendingPick = true,
    };
    auto world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const auto& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.CompileSucceeded);
    ASSERT_FALSE(stats.DebugDump.empty());
    EXPECT_NE(stats.DebugDump.find("name=\"Null.Picking\""), std::string::npos);

    EXPECT_EQ(renderer->EndFrame(frame), 0u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererExecuteFrameRequiresPreparePhase)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Alpha = 0.5,
        .Viewport = {.Width = 800u, .Height = 600u},
    };
    auto world = renderer->ExtractRenderWorld(input);
    renderer->ExecuteFrame(frame, world);

    const auto& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.CompileSucceeded);
    EXPECT_FALSE(stats.ExecuteSucceeded);
    EXPECT_FALSE(stats.Diagnostic.empty());

    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererPrepareBeforeExtractPreventsExecute)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    Graphics::RenderWorld world{};
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const auto& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.CompileSucceeded);
    EXPECT_FALSE(stats.ExecuteSucceeded);
    EXPECT_FALSE(stats.Diagnostic.empty());

    renderer->Shutdown();
}
