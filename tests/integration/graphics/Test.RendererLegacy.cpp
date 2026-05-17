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
    EXPECT_TRUE(stats.Compile.Succeeded);
    EXPECT_TRUE(stats.Execute.Succeeded);
    EXPECT_GT(stats.Compile.PassCount, 0u);
    EXPECT_GT(stats.Compile.ResourceCount, 0u);
    EXPECT_GT(stats.Compile.BarrierCount, 0u);
    EXPECT_GT(stats.Compile.TransientMemoryEstimateBytes, 0u);
    EXPECT_GT(device.CommandContext.TextureBarrierCalls.size(), 0u);
    EXPECT_GT(device.CommandContext.BufferBarrierCalls.size(), 0u);
    EXPECT_FALSE(stats.DebugDump.empty());

    renderer->Resize(1920u, 1080u);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
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
    ASSERT_TRUE(stats.Compile.Succeeded);
    ASSERT_FALSE(stats.DebugDump.empty());
    EXPECT_EQ(stats.Compile.CulledPassCount, 0u);

    const auto& dump = stats.DebugDump;
    EXPECT_NE(dump.find("name=\"CullingPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"DepthPrepass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"SurfacePass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"CompositionPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"LinePass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"PointPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"PostProcessPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"ImGuiPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Present\""), std::string::npos);
    EXPECT_EQ(dump.find("name=\"DebugViewPass\""), std::string::npos);
    EXPECT_EQ(dump.find("name=\"PickingPass\""), std::string::npos);

    const std::size_t surfacePos = dump.find("name=\"SurfacePass\"");
    const std::size_t compositionPos = dump.find("name=\"CompositionPass\"");
    const std::size_t linePos = dump.find("name=\"LinePass\"");
    const std::size_t pointPos = dump.find("name=\"PointPass\"");
    const std::size_t postPos = dump.find("name=\"PostProcessPass\"");
    const std::size_t imguiPos = dump.find("name=\"ImGuiPass\"");
    const std::size_t presentPos = dump.find("name=\"Present\"");
    ASSERT_NE(surfacePos, std::string::npos);
    ASSERT_NE(compositionPos, std::string::npos);
    ASSERT_NE(linePos, std::string::npos);
    ASSERT_NE(pointPos, std::string::npos);
    ASSERT_NE(postPos, std::string::npos);
    ASSERT_NE(imguiPos, std::string::npos);
    ASSERT_NE(presentPos, std::string::npos);

    EXPECT_LT(surfacePos, compositionPos);
    EXPECT_LT(compositionPos, linePos);
    EXPECT_LT(linePos, pointPos);
    EXPECT_LT(pointPos, postPos);
    EXPECT_LT(postPos, imguiPos);
    EXPECT_LT(imguiPos, presentPos);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererEnablesDebugChainWhenRequested)
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
        .DebugOverlayEnabled = true,
    };
    auto world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const auto& dump = renderer->GetLastRenderGraphStats().DebugDump;
    ASSERT_FALSE(dump.empty());
    const std::size_t debugPos = dump.find("name=\"DebugViewPass\"");
    const std::size_t imguiPos = dump.find("name=\"ImGuiPass\"");
    const std::size_t presentPos = dump.find("name=\"Present\"");

    ASSERT_NE(debugPos, std::string::npos);
    ASSERT_NE(imguiPos, std::string::npos);
    ASSERT_NE(presentPos, std::string::npos);
    EXPECT_LT(debugPos, imguiPos);
    EXPECT_LT(imguiPos, presentPos);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
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
    ASSERT_TRUE(stats.Compile.Succeeded);
    ASSERT_FALSE(stats.DebugDump.empty());
    EXPECT_NE(stats.DebugDump.find("name=\"PickingPass\""), std::string::npos);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
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
    EXPECT_FALSE(stats.Compile.Succeeded);
    EXPECT_FALSE(stats.Execute.Succeeded);
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
    EXPECT_FALSE(stats.Compile.Succeeded);
    EXPECT_FALSE(stats.Execute.Succeeded);
    EXPECT_FALSE(stats.Diagnostic.empty());

    renderer->Shutdown();
}
