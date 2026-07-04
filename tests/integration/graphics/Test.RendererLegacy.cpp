#include <gtest/gtest.h>

#include <string>

import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.FrameHandle;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    void ExecutePreparedFrame(Graphics::IRenderer& renderer,
                              const RHI::FrameHandle& frame,
                              const Graphics::RenderFrameInput& input)
    {
        auto world = renderer.ExtractRenderWorld(input);
        renderer.PrepareFrame(world);
        renderer.ExecuteFrame(frame, world);
    }
}

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
    ExecutePreparedFrame(*renderer, frame, input);
    const auto& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded);
    EXPECT_EQ(stats.Compile.AttemptCount, 1u);
    EXPECT_EQ(stats.Compile.CacheMissCount, 1u);
    EXPECT_EQ(stats.Compile.CacheHitCount, 0u);
    EXPECT_FALSE(stats.Compile.ReusedCachedGraph);
    EXPECT_TRUE(stats.Execute.Succeeded);
    EXPECT_GT(stats.Compile.PassCount, 0u);
    EXPECT_GT(stats.Compile.ResourceCount, 0u);
    EXPECT_GT(stats.Compile.BarrierCount, 0u);
    EXPECT_GT(stats.Compile.TransientMemoryEstimateBytes, 0u);
    EXPECT_GT(device.CommandContext.TextureBarrierCalls.size(), 0u);
    EXPECT_GT(device.CommandContext.BufferBarrierCalls.size(), 0u);
    EXPECT_FALSE(stats.Compile.DebugDumpGenerated);
    EXPECT_TRUE(stats.DebugDump.empty());

    renderer->Resize(1920u, 1080u);
    ExecutePreparedFrame(*renderer, frame, input);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererReusesCompiledGraphForSteadyStateFrames)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    const Graphics::RenderFrameInput input{
        .Alpha = 0.5,
        .Viewport = {.Width = 1280u, .Height = 720u},
    };

    device.NextFrame = {.FrameIndex = 0u, .SwapchainImageIndex = 0u};
    RHI::FrameHandle firstFrame{};
    ASSERT_TRUE(renderer->BeginFrame(firstFrame));
    ExecutePreparedFrame(*renderer, firstFrame, input);
    const Graphics::RenderGraphFrameStats firstStats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(firstStats.Compile.Succeeded);
    ASSERT_TRUE(firstStats.Execute.Succeeded);
    EXPECT_EQ(firstStats.Compile.AttemptCount, 1u);
    EXPECT_EQ(firstStats.Compile.CacheMissCount, 1u);
    EXPECT_EQ(firstStats.Compile.CacheHitCount, 0u);
    EXPECT_FALSE(firstStats.Compile.ReusedCachedGraph);
    EXPECT_EQ(renderer->EndFrame(firstFrame), 1u);

    device.NextFrame = {.FrameIndex = 1u, .SwapchainImageIndex = 1u};
    RHI::FrameHandle secondFrame{};
    ASSERT_TRUE(renderer->BeginFrame(secondFrame));
    ExecutePreparedFrame(*renderer, secondFrame, input);
    const Graphics::RenderGraphFrameStats secondStats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(secondStats.Compile.Succeeded);
    ASSERT_TRUE(secondStats.Execute.Succeeded);
    EXPECT_EQ(secondStats.Compile.AttemptCount, 0u);
    EXPECT_EQ(secondStats.Compile.CacheMissCount, 0u);
    EXPECT_EQ(secondStats.Compile.CacheHitCount, 1u);
    EXPECT_TRUE(secondStats.Compile.ReusedCachedGraph);
    EXPECT_EQ(secondStats.Compile.PassCount, firstStats.Compile.PassCount);
    EXPECT_EQ(secondStats.Compile.ResourceCount, firstStats.Compile.ResourceCount);
    EXPECT_EQ(secondStats.Compile.BarrierCount, firstStats.Compile.BarrierCount);
    EXPECT_EQ(renderer->EndFrame(secondFrame), 2u);

    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererRecompilesOnFeatureAndSizingChangesAndRebindsImports)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);

    const Graphics::RenderFrameInput baselineInput{
        .Alpha = 0.5,
        .Viewport = {.Width = 1280u, .Height = 720u},
    };

    device.NextFrame = {.FrameIndex = 0u, .SwapchainImageIndex = 0u};
    RHI::FrameHandle firstFrame{};
    ASSERT_TRUE(renderer->BeginFrame(firstFrame));
    ExecutePreparedFrame(*renderer, firstFrame, baselineInput);
    ASSERT_TRUE(renderer->GetLastRenderGraphStats().Compile.Succeeded);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheMissCount, 1u);
    EXPECT_EQ(renderer->EndFrame(firstFrame), 1u);

    Graphics::RenderFrameInput debugInput = baselineInput;
    debugInput.DebugOverlayEnabled = true;
    device.NextFrame = {.FrameIndex = 1u, .SwapchainImageIndex = 1u};
    RHI::FrameHandle featureFrame{};
    ASSERT_TRUE(renderer->BeginFrame(featureFrame));
    ExecutePreparedFrame(*renderer, featureFrame, debugInput);
    ASSERT_TRUE(renderer->GetLastRenderGraphStats().Compile.Succeeded);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.AttemptCount, 1u);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheMissCount, 1u);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheHitCount, 0u);
    EXPECT_EQ(renderer->EndFrame(featureFrame), 2u);

    device.NextFrame = {.FrameIndex = 2u, .SwapchainImageIndex = 0u};
    RHI::FrameHandle cachedDebugFrame{};
    ASSERT_TRUE(renderer->BeginFrame(cachedDebugFrame));
    ExecutePreparedFrame(*renderer, cachedDebugFrame, debugInput);
    ASSERT_TRUE(renderer->GetLastRenderGraphStats().Compile.Succeeded);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.AttemptCount, 0u);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheHitCount, 1u);
    EXPECT_EQ(renderer->EndFrame(cachedDebugFrame), 3u);

    Graphics::RenderFrameInput resizedInput = debugInput;
    resizedInput.Viewport = {.Width = 800u, .Height = 600u};
    device.NextFrame = {.FrameIndex = 3u, .SwapchainImageIndex = 1u};
    RHI::FrameHandle resizedFrame{};
    ASSERT_TRUE(renderer->BeginFrame(resizedFrame));
    ExecutePreparedFrame(*renderer, resizedFrame, resizedInput);
    ASSERT_TRUE(renderer->GetLastRenderGraphStats().Compile.Succeeded);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.AttemptCount, 1u);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheMissCount, 1u);
    EXPECT_EQ(renderer->EndFrame(resizedFrame), 4u);

    device.BackbufferHandle = RHI::TextureHandle{101u, 1u};
    device.CommandContext.TextureBarrierCalls.clear();
    device.NextFrame = {.FrameIndex = 4u, .SwapchainImageIndex = 0u};
    RHI::FrameHandle importFrame{};
    ASSERT_TRUE(renderer->BeginFrame(importFrame));
    ExecutePreparedFrame(*renderer, importFrame, resizedInput);
    ASSERT_TRUE(renderer->GetLastRenderGraphStats().Compile.Succeeded);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.AttemptCount, 0u);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheMissCount, 0u);
    EXPECT_EQ(renderer->GetLastRenderGraphStats().Compile.CacheHitCount, 1u);
    bool sawReboundBackbufferBarrier = false;
    for (const auto& barrier : device.CommandContext.TextureBarrierCalls)
    {
        if (barrier.Texture == device.BackbufferHandle)
        {
            sawReboundBackbufferBarrier = true;
            break;
        }
    }
    EXPECT_TRUE(sawReboundBackbufferBarrier);
    EXPECT_EQ(renderer->EndFrame(importFrame), 5u);

    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererCachedDebugDumpMatchesInitialCompileWhenEnabled)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);
    renderer->SetRenderGraphDebugDumpEnabled(true);
    EXPECT_TRUE(renderer->GetRenderGraphDebugDumpEnabled());

    const Graphics::RenderFrameInput input{
        .Alpha = 0.5,
        .Viewport = {.Width = 1280u, .Height = 720u},
    };

    device.NextFrame = {.FrameIndex = 0u, .SwapchainImageIndex = 0u};
    RHI::FrameHandle firstFrame{};
    ASSERT_TRUE(renderer->BeginFrame(firstFrame));
    ExecutePreparedFrame(*renderer, firstFrame, input);
    const Graphics::RenderGraphFrameStats firstStats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(firstStats.Compile.Succeeded);
    ASSERT_TRUE(firstStats.Compile.DebugDumpGenerated);
    ASSERT_FALSE(firstStats.DebugDump.empty());
    EXPECT_EQ(firstStats.Compile.CacheMissCount, 1u);
    EXPECT_EQ(renderer->EndFrame(firstFrame), 1u);

    device.NextFrame = {.FrameIndex = 1u, .SwapchainImageIndex = 1u};
    RHI::FrameHandle secondFrame{};
    ASSERT_TRUE(renderer->BeginFrame(secondFrame));
    ExecutePreparedFrame(*renderer, secondFrame, input);
    const Graphics::RenderGraphFrameStats secondStats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(secondStats.Compile.Succeeded);
    ASSERT_TRUE(secondStats.Compile.DebugDumpGenerated);
    EXPECT_TRUE(secondStats.Compile.ReusedCachedGraph);
    EXPECT_EQ(secondStats.Compile.CacheHitCount, 1u);
    EXPECT_EQ(secondStats.DebugDump, firstStats.DebugDump);
    EXPECT_EQ(renderer->EndFrame(secondFrame), 2u);

    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererForwardDebugDumpContainsCanonicalPassesAndDataflowOrder)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);
    renderer->SetRenderGraphDebugDumpEnabled(true);

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
    EXPECT_EQ(dump.find("name=\"CompositionPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"LinePass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"PointPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"PostProcessPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"ImGuiPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Present\""), std::string::npos);
    EXPECT_EQ(dump.find("name=\"DebugViewPass\""), std::string::npos);
    EXPECT_EQ(dump.find("name=\"PickingPass\""), std::string::npos);

    const std::size_t surfacePos = dump.find("name=\"SurfacePass\"");
    const std::size_t linePos = dump.find("name=\"LinePass\"");
    const std::size_t pointPos = dump.find("name=\"PointPass\"");
    const std::size_t postPos = dump.find("name=\"PostProcessPass\"");
    const std::size_t imguiPos = dump.find("name=\"ImGuiPass\"");
    const std::size_t presentPos = dump.find("name=\"Present\"");
    ASSERT_NE(surfacePos, std::string::npos);
    ASSERT_NE(linePos, std::string::npos);
    ASSERT_NE(pointPos, std::string::npos);
    ASSERT_NE(postPos, std::string::npos);
    ASSERT_NE(imguiPos, std::string::npos);
    ASSERT_NE(presentPos, std::string::npos);

    EXPECT_LT(surfacePos, linePos);
    EXPECT_LT(linePos, pointPos);
    EXPECT_LT(pointPos, postPos);
    EXPECT_LT(postPos, imguiPos);
    EXPECT_LT(imguiPos, presentPos);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererDeferredDebugDumpPlacesCompositionBetweenSurfaceAndLine)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);
    renderer->SetRenderGraphDebugDumpEnabled(true);
    renderer->SetLightingPath(Graphics::FrameRecipeLightingPath::Deferred);

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

    const auto& dump = stats.DebugDump;
    const std::size_t surfacePos = dump.find("name=\"SurfacePass\"");
    const std::size_t compositionPos = dump.find("name=\"CompositionPass\"");
    const std::size_t linePos = dump.find("name=\"LinePass\"");
    ASSERT_NE(surfacePos, std::string::npos);
    ASSERT_NE(compositionPos, std::string::npos);
    ASSERT_NE(linePos, std::string::npos);

    EXPECT_LT(surfacePos, compositionPos);
    EXPECT_LT(compositionPos, linePos);

    EXPECT_EQ(renderer->EndFrame(frame), 1u);
    renderer->Shutdown();
}

TEST(GraphicsRenderer, NullRendererEnablesDebugChainWhenRequested)
{
    MockDevice device;
    auto renderer = Graphics::CreateRenderer();
    ASSERT_NE(renderer, nullptr);

    renderer->Initialize(device);
    renderer->SetRenderGraphDebugDumpEnabled(true);

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
    renderer->SetRenderGraphDebugDumpEnabled(true);

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
