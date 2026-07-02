#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path RepoRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        EXPECT_TRUE(in.good()) << "Unable to open: " << path.string();
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    std::string SliceBetween(const std::string& content,
                             const std::string& beginMarker,
                             const std::string& endMarker)
    {
        const std::size_t begin = content.find(beginMarker);
        EXPECT_NE(begin, std::string::npos);
        const std::size_t end = content.find(endMarker, begin + beginMarker.size());
        EXPECT_NE(end, std::string::npos);
        if (begin == std::string::npos || end == std::string::npos)
            return {};
        return content.substr(begin, end - begin);
    }
}

TEST(RuntimeEngineLayering, RunFrameDoesNotUseGpuResourceOrPassLevelDetails)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop = ReadFile(RepoRoot() / "src/core/Core.FrameLoop.cpp");
    const auto runFrame = SliceBetween(content,
                                       "void Engine::RunFrame()",
                                       "bool Engine::IsRunning() const noexcept");

    // Runtime must orchestrate renderer phases, not GPU barriers/resources.
    EXPECT_NE(content.find("import Extrinsic.Core.FrameLoop"), std::string::npos);
    EXPECT_NE(content.find("Core::ExecuteRenderFrameContract(renderHooks)"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.BeginFrame()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.ExtractRenderWorld()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.PrepareFrame()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.ExecuteFrame()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.EndFrame()"), std::string::npos);

    EXPECT_EQ(runFrame.find("TextureBarrier"), std::string::npos);
    EXPECT_EQ(runFrame.find("BufferBarrier"), std::string::npos);
    EXPECT_EQ(runFrame.find("TextureUsage::"), std::string::npos);
    EXPECT_EQ(runFrame.find("BufferUsage::"), std::string::npos);
    EXPECT_EQ(runFrame.find("VkImage"), std::string::npos);
    EXPECT_EQ(runFrame.find("VkBuffer"), std::string::npos);
    EXPECT_EQ(runFrame.find("vkCmd"), std::string::npos);
}

TEST(RuntimeEngineLayering, RunFrameDelegatesToPromotedContractsInDocumentedBroadPhaseOrder)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto runFrame = SliceBetween(content,
                                       "void Engine::RunFrame()",
                                       "bool Engine::IsRunning() const noexcept");

    const auto frameContext = runFrame.find("RuntimeFrameContext frameContext{};");
    const auto platformContract = runFrame.find("Core::ExecutePlatformBeginFrameContract(platformHooks");
    const auto fixedStep = runFrame.find("RunFixedStepSimulationTicks(*this");
    const auto variableTick = runFrame.find("m_Application->OnVariableTick(*this, alpha, frameDt);");
    const auto renderContract = runFrame.find("Core::ExecuteRenderFrameContract(renderHooks)");
    const auto present = runFrame.find("m_Device->Present(frame);");
    const auto maintenance = runFrame.find("Core::ExecuteMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);");
    const auto clockEnd = runFrame.rfind("m_FrameClock.EndFrame();");

    ASSERT_NE(frameContext, std::string::npos);
    ASSERT_NE(platformContract, std::string::npos);
    ASSERT_NE(fixedStep, std::string::npos);
    ASSERT_NE(variableTick, std::string::npos);
    ASSERT_NE(renderContract, std::string::npos);
    ASSERT_NE(present, std::string::npos);
    ASSERT_NE(maintenance, std::string::npos);
    ASSERT_NE(clockEnd, std::string::npos);

    EXPECT_LT(frameContext, platformContract);
    EXPECT_LT(platformContract, fixedStep);
    EXPECT_LT(fixedStep, variableTick);
    EXPECT_LT(variableTick, renderContract);
    EXPECT_LT(renderContract, present);
    EXPECT_LT(present, maintenance);
    EXPECT_LT(maintenance, clockEnd);
}

TEST(RuntimeEngineLayering, RunFrameStopsAfterPlatformCloseBeforeRendererContract)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto runFrame = SliceBetween(content,
                                       "void Engine::RunFrame()",
                                       "bool Engine::IsRunning() const noexcept");

    const auto platformContract = runFrame.find("Core::ExecutePlatformBeginFrameContract(platformHooks");
    const auto closeBranch = runFrame.find("platformResult.ShouldClose");
    const auto requestExit = runFrame.find("RequestExitFromWindowClose(", closeBranch);
    const auto returnFromClose = runFrame.find("return;", requestExit);
    const auto renderContract = runFrame.find("Core::ExecuteRenderFrameContract(renderHooks)");

    ASSERT_NE(platformContract, std::string::npos);
    ASSERT_NE(closeBranch, std::string::npos);
    ASSERT_NE(requestExit, std::string::npos);
    ASSERT_NE(returnFromClose, std::string::npos);
    ASSERT_NE(renderContract, std::string::npos);

    EXPECT_LT(platformContract, closeBranch);
    EXPECT_LT(closeBranch, requestExit);
    EXPECT_LT(requestExit, returnFromClose);
    EXPECT_LT(returnFromClose, renderContract);
}

TEST(RuntimeEngineLayering, ShutdownWaitsIdleBeforeDestroyingRuntimeGpuJobQueue)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto shutdown = SliceBetween(content,
                                       "void Engine::Shutdown()",
                                       "// ── Main loop");

    const auto detachHook = shutdown.find("SetRuntimeFrameCommandHook({})");
    const auto waitIdle = shutdown.find("m_Device->WaitIdle();", detachHook);
    const auto destroyKMeans = shutdown.find("m_KMeansGpuJobs.reset();", waitIdle);
    const auto executeShutdown = shutdown.find("Core::ExecuteShutdownContract(hooks)");

    ASSERT_NE(detachHook, std::string::npos);
    ASSERT_NE(waitIdle, std::string::npos);
    ASSERT_NE(destroyKMeans, std::string::npos);
    ASSERT_NE(executeShutdown, std::string::npos);

    EXPECT_LT(detachHook, waitIdle);
    EXPECT_LT(waitIdle, destroyKMeans);
    EXPECT_LT(destroyKMeans, executeShutdown);
}

TEST(RuntimeEngineLayering, RunFrameCarriesDataOnlyFrameContext)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");

    EXPECT_NE(content.find("struct RuntimeFrameContext"), std::string::npos);
    EXPECT_NE(content.find("double FrameDeltaSeconds"), std::string::npos);
    EXPECT_NE(content.find("double FixedStepAlpha"), std::string::npos);
    EXPECT_NE(content.find("std::uint64_t FrameIndex"), std::string::npos);
    EXPECT_NE(content.find("Graphics::RenderFrameInput RenderInput"), std::string::npos);
    EXPECT_NE(content.find("RuntimeRenderExtractionStats ExtractionStats"), std::string::npos);
    EXPECT_NE(content.find("std::uint32_t PooledFrontSlot"), std::string::npos);
    EXPECT_NE(content.find("frameContext.FrameDeltaSeconds = frameDt;"), std::string::npos);
    EXPECT_NE(content.find("frameContext.FixedStepAlpha = alpha;"), std::string::npos);
    EXPECT_NE(content.find("frameContext.FrameIndex = m_FrameIndex++;"), std::string::npos);
    EXPECT_NE(content.find("frameContext.ExtractionStats"), std::string::npos);
    EXPECT_NE(content.find("frameContext.PooledFrontSlot"), std::string::npos);
}

TEST(RuntimeEngineLayering, PromotedFrameLoopContractPreservesRendererAndMaintenanceOrder)
{
    const auto content = ReadFile(RepoRoot() / "src/core/Core.FrameLoop.cpp");

    const auto beginFrame = content.find("hooks.BeginFrame()");
    const auto extract = content.find("hooks.ExtractRenderWorld()");
    const auto prepare = content.find("hooks.PrepareFrame()");
    const auto execute = content.find("hooks.ExecuteFrame()");
    const auto endFrame = content.find("hooks.EndFrame()");

    ASSERT_NE(beginFrame, std::string::npos);
    ASSERT_NE(extract, std::string::npos);
    ASSERT_NE(prepare, std::string::npos);
    ASSERT_NE(execute, std::string::npos);
    ASSERT_NE(endFrame, std::string::npos);

    EXPECT_LT(beginFrame, extract);
    EXPECT_LT(extract, prepare);
    EXPECT_LT(prepare, execute);
    EXPECT_LT(execute, endFrame);

    const auto transfers = content.find("transfer.CollectCompletedTransfers()");
    const auto drain = content.find("streaming.DrainCompletions()");
    const auto apply = content.find("streaming.ApplyMainThreadResults()");
    const auto assets = content.find("assets.TickAssets()");
    const auto submit = content.find("streaming.SubmitFrameWork()");
    const auto pump = content.find("streaming.PumpBackground(maxStreamingLaunches)");

    ASSERT_NE(transfers, std::string::npos);
    ASSERT_NE(drain, std::string::npos);
    ASSERT_NE(apply, std::string::npos);
    ASSERT_NE(assets, std::string::npos);
    ASSERT_NE(submit, std::string::npos);
    ASSERT_NE(pump, std::string::npos);

    EXPECT_LT(transfers, drain);
    EXPECT_LT(drain, apply);
    EXPECT_LT(apply, assets);
    EXPECT_LT(assets, submit);
    EXPECT_LT(submit, pump);
}

TEST(RuntimeEngineLayering, RunFrameRegistersPromotedEcsSystemBundleBetweenSimTickAndCompile)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");

    const auto simTick = content.find("application.OnSimTick(engine, fixedDt);");
    const auto bundleRegistration = content.find(
        "RegisterPromotedEcsSystemBundle(frameGraph, scene)");
    const auto compile = content.find("frameGraph.Compile()");
    const auto bundleImport = content.find("import Extrinsic.Runtime.EcsSystemBundle");

    ASSERT_NE(simTick, std::string::npos);
    ASSERT_NE(bundleRegistration, std::string::npos);
    ASSERT_NE(compile, std::string::npos);
    ASSERT_NE(bundleImport, std::string::npos);

    // Bundle activation must run after the app has had its OnSimTick callback
    // (so app-added passes are in the graph) and before FrameGraph::Compile
    // resolves dependencies. RUNTIME-091.
    EXPECT_LT(simTick, bundleRegistration);
    EXPECT_LT(bundleRegistration, compile);
}

TEST(RuntimeEngineLayering, StreamingExecutorApiStaysCpuOnly)
{
    const auto publicApi = ReadFile(RepoRoot() / "src/runtime/Runtime.StreamingExecutor.cppm");
    EXPECT_EQ(publicApi.find("import Extrinsic.ECS"), std::string::npos);
    EXPECT_EQ(publicApi.find("import Extrinsic.RHI"), std::string::npos);
    EXPECT_EQ(publicApi.find("Vk"), std::string::npos);
    EXPECT_EQ(publicApi.find("GpuWorld"), std::string::npos);
}

TEST(RuntimeEngineLayering, FrameLoopContractDoesNotBecomeCompositionRoot)
{
    const auto frameLoop = ReadFile(RepoRoot() / "src/core/Core.FrameLoop.cppm");

    EXPECT_EQ(frameLoop.find("import Extrinsic.Platform"), std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.Graphics"), std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.Asset"), std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.ECS"), std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.RHI"), std::string::npos);
    EXPECT_EQ(frameLoop.find("CreateWindow"), std::string::npos);
    EXPECT_EQ(frameLoop.find("CreateRenderer"), std::string::npos);
    EXPECT_EQ(frameLoop.find("CreateDevice"), std::string::npos);
    EXPECT_EQ(frameLoop.find("AssetService"), std::string::npos);
    EXPECT_EQ(frameLoop.find("StreamingExecutor"), std::string::npos);
}

TEST(RuntimeEngineLayering, RenderGraphStaysOutOfECSAndCoreStaysOutOfGpuBarriers)
{
    const std::vector<std::filesystem::path> renderGraphFiles{
        "src/graphics/framegraph/Graphics.RenderGraph.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.Resources.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.Pass.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.Compiler.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.Barriers.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.TransientAllocator.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.Executor.cppm",
        "src/graphics/framegraph/Graphics.RenderGraph.cpp",
        "src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp",
        "src/graphics/framegraph/Graphics.RenderGraph.Executor.cpp",
        "src/graphics/framegraph/Graphics.RenderGraph.TransientAllocator.cpp",
    };

    for (const auto& path : renderGraphFiles)
    {
        const auto content = ReadFile(RepoRoot() / path);
        EXPECT_EQ(content.find("import Extrinsic.ECS"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("import ECS"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("Vk"), std::string::npos) << path.string();
    }

    const std::vector<std::filesystem::path> coreGraphFiles{
        "src/core/Core.Dag.Scheduler.cppm",
        "src/core/Core.Dag.Scheduler.Types.cppm",
        "src/core/Core.Dag.Scheduler.Compiler.cppm",
        "src/core/Core.Dag.Scheduler.Hazards.cppm",
        "src/core/Core.Dag.TaskGraph.cppm",
        "src/core/Core.Dag.TaskGraph.cpp",
        "src/core/Core.FrameGraph.cppm",
        "src/core/Core.FrameGraph.cpp",
    };

    for (const auto& path : coreGraphFiles)
    {
        const auto content = ReadFile(RepoRoot() / path);
        EXPECT_EQ(content.find("TextureBarrier"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("BufferBarrier"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("TextureUsage"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("BufferUsage"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("Vk"), std::string::npos) << path.string();
    }
}
