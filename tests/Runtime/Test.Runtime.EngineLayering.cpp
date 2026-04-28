#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
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
    const auto content = ReadFile("src_new/Runtime/Runtime.Engine.cpp");

    // Runtime must orchestrate renderer phases, not GPU barriers/resources.
    EXPECT_NE(content.find("m_Renderer->BeginFrame"), std::string::npos);
    EXPECT_NE(content.find("m_Renderer->ExtractRenderWorld"), std::string::npos);
    EXPECT_NE(content.find("m_Renderer->PrepareFrame"), std::string::npos);
    EXPECT_NE(content.find("m_Renderer->ExecuteFrame"), std::string::npos);
    EXPECT_NE(content.find("m_Renderer->EndFrame"), std::string::npos);

    EXPECT_EQ(content.find("TextureBarrier"), std::string::npos);
    EXPECT_EQ(content.find("BufferBarrier"), std::string::npos);
    EXPECT_EQ(content.find("TextureUsage::"), std::string::npos);
    EXPECT_EQ(content.find("BufferUsage::"), std::string::npos);
    EXPECT_EQ(content.find("VkImage"), std::string::npos);
    EXPECT_EQ(content.find("VkBuffer"), std::string::npos);
    EXPECT_EQ(content.find("vkCmd"), std::string::npos);
}

TEST(RuntimeEngineLayering, RunFramePreservesDocumentedBroadPhaseOrdering)
{
    const auto content = ReadFile("src_new/Runtime/Runtime.Engine.cpp");

    const auto pollEvents = content.find("m_Window->PollEvents();");
    const auto simTick = content.find("m_Application->OnSimTick(*this, m_FixedDt);");
    const auto variableTick = content.find("m_Application->OnVariableTick(*this, alpha, frameDt);");
    const auto beginFrame = content.find("m_Renderer->BeginFrame(frame)");
    const auto extract = content.find("m_Renderer->ExtractRenderWorld(renderInput)");
    const auto prepare = content.find("m_Renderer->PrepareFrame(renderWorld);");
    const auto execute = content.find("m_Renderer->ExecuteFrame(frame, renderWorld);");
    const auto endFrame = content.find("m_Renderer->EndFrame(frame);");
    const auto present = content.find("m_Device->Present(frame);");
    const auto maintenance = content.find("m_Device->GetTransferQueue().CollectCompleted();");
    const auto streamingDrain = content.find("m_StreamingExecutor->DrainCompletions();");
    const auto clockEnd = content.rfind("m_FrameClock.EndFrame();");

    ASSERT_NE(pollEvents, std::string::npos);
    ASSERT_NE(simTick, std::string::npos);
    ASSERT_NE(variableTick, std::string::npos);
    ASSERT_NE(beginFrame, std::string::npos);
    ASSERT_NE(extract, std::string::npos);
    ASSERT_NE(prepare, std::string::npos);
    ASSERT_NE(execute, std::string::npos);
    ASSERT_NE(endFrame, std::string::npos);
    ASSERT_NE(present, std::string::npos);
    ASSERT_NE(maintenance, std::string::npos);
    ASSERT_NE(streamingDrain, std::string::npos);
    ASSERT_NE(clockEnd, std::string::npos);

    EXPECT_LT(pollEvents, simTick);
    EXPECT_LT(simTick, variableTick);
    EXPECT_LT(variableTick, beginFrame);
    EXPECT_LT(beginFrame, extract);
    EXPECT_LT(extract, prepare);
    EXPECT_LT(prepare, execute);
    EXPECT_LT(execute, endFrame);
    EXPECT_LT(endFrame, present);
    EXPECT_LT(present, maintenance);
    EXPECT_LT(maintenance, streamingDrain);
    EXPECT_LT(streamingDrain, clockEnd);
}

TEST(RuntimeEngineLayering, StreamingExecutorApiStaysCpuOnly)
{
    const auto publicApi = ReadFile("src_new/Runtime/Runtime.StreamingExecutor.cppm");
    EXPECT_EQ(publicApi.find("import Extrinsic.ECS"), std::string::npos);
    EXPECT_EQ(publicApi.find("import Extrinsic.RHI"), std::string::npos);
    EXPECT_EQ(publicApi.find("Vk"), std::string::npos);
    EXPECT_EQ(publicApi.find("GpuWorld"), std::string::npos);
}

TEST(RuntimeEngineLayering, RenderGraphStaysOutOfECSAndCoreStaysOutOfGpuBarriers)
{
    const std::vector<std::filesystem::path> renderGraphFiles{
        "src_new/Graphics/Graphics.RenderGraph.cppm",
        "src_new/Graphics/Graphics.RenderGraph.Resources.cppm",
        "src_new/Graphics/Graphics.RenderGraph.Pass.cppm",
        "src_new/Graphics/Graphics.RenderGraph.Compiler.cppm",
        "src_new/Graphics/Graphics.RenderGraph.Barriers.cppm",
        "src_new/Graphics/Graphics.RenderGraph.TransientAllocator.cppm",
        "src_new/Graphics/Graphics.RenderGraph.Executor.cppm",
        "src_new/Graphics/Graphics.RenderGraph.cpp",
        "src_new/Graphics/Graphics.RenderGraph.Compiler.cpp",
        "src_new/Graphics/Graphics.RenderGraph.Executor.cpp",
        "src_new/Graphics/Graphics.RenderGraph.TransientAllocator.cpp",
    };

    for (const auto& path : renderGraphFiles)
    {
        const auto content = ReadFile(path);
        EXPECT_EQ(content.find("import Extrinsic.ECS"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("import ECS"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("Vk"), std::string::npos) << path.string();
    }

    const std::vector<std::filesystem::path> coreGraphFiles{
        "src_new/Core/Core.Dag.Scheduler.cppm",
        "src_new/Core/Core.Dag.Scheduler.Types.cppm",
        "src_new/Core/Core.Dag.Scheduler.Compiler.cppm",
        "src_new/Core/Core.Dag.Scheduler.Hazards.cppm",
        "src_new/Core/Core.Dag.TaskGraph.cppm",
        "src_new/Core/Core.Dag.TaskGraph.cpp",
        "src_new/Core/Core.FrameGraph.cppm",
        "src_new/Core/Core.FrameGraph.cpp",
    };

    for (const auto& path : coreGraphFiles)
    {
        const auto content = ReadFile(path);
        EXPECT_EQ(content.find("TextureBarrier"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("BufferBarrier"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("TextureUsage"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("BufferUsage"), std::string::npos) << path.string();
        EXPECT_EQ(content.find("Vk"), std::string::npos) << path.string();
    }
}
