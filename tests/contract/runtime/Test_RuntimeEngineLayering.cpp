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
}

TEST(RuntimeEngineLayering, RunFrameDoesNotUseGpuResourceOrPassLevelDetails)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop = ReadFile(RepoRoot() / "src/runtime/Runtime.FrameLoop.cppm");

    // Runtime must orchestrate renderer phases, not GPU barriers/resources.
    EXPECT_NE(content.find("import Extrinsic.Runtime.FrameLoop"), std::string::npos);
    EXPECT_NE(content.find("ExecuteRuntimeRenderFrameContract(renderHooks)"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.BeginFrame()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.ExtractRenderWorld()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.PrepareFrame()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.ExecuteFrame()"), std::string::npos);
    EXPECT_NE(frameLoop.find("hooks.EndFrame()"), std::string::npos);

    EXPECT_EQ(content.find("TextureBarrier"), std::string::npos);
    EXPECT_EQ(content.find("BufferBarrier"), std::string::npos);
    EXPECT_EQ(content.find("TextureUsage::"), std::string::npos);
    EXPECT_EQ(content.find("BufferUsage::"), std::string::npos);
    EXPECT_EQ(content.find("VkImage"), std::string::npos);
    EXPECT_EQ(content.find("VkBuffer"), std::string::npos);
    EXPECT_EQ(content.find("vkCmd"), std::string::npos);
}

TEST(RuntimeEngineLayering, RunFrameDelegatesToPromotedContractsInDocumentedBroadPhaseOrder)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");

    const auto pollEvents = content.find("m_Window->PollEvents();");
    const auto simTick = content.find("m_Application->OnSimTick(*this, m_FixedDt);");
    const auto variableTick = content.find("m_Application->OnVariableTick(*this, alpha, frameDt);");
    const auto renderContract = content.find("ExecuteRuntimeRenderFrameContract(renderHooks)");
    const auto present = content.find("m_Device->Present(frame);");
    const auto maintenance = content.find("ExecuteRuntimeMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);");
    const auto clockEnd = content.rfind("m_FrameClock.EndFrame();");

    ASSERT_NE(pollEvents, std::string::npos);
    ASSERT_NE(simTick, std::string::npos);
    ASSERT_NE(variableTick, std::string::npos);
    ASSERT_NE(renderContract, std::string::npos);
    ASSERT_NE(present, std::string::npos);
    ASSERT_NE(maintenance, std::string::npos);
    ASSERT_NE(clockEnd, std::string::npos);

    EXPECT_LT(pollEvents, simTick);
    EXPECT_LT(simTick, variableTick);
    EXPECT_LT(variableTick, renderContract);
    EXPECT_LT(renderContract, present);
    EXPECT_LT(present, maintenance);
    EXPECT_LT(maintenance, clockEnd);
}

TEST(RuntimeEngineLayering, PromotedFrameLoopContractPreservesRendererAndMaintenanceOrder)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.FrameLoop.cppm");

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
    const auto frameLoop = ReadFile(RepoRoot() / "src/runtime/Runtime.FrameLoop.cppm");

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
