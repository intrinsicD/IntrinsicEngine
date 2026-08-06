#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    std::filesystem::path RepoRoot()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path()
            .parent_path().parent_path();
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        EXPECT_TRUE(input.good()) << "Unable to open: " << path.string();
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    std::string SliceBetween(const std::string& content,
                             const std::string_view beginMarker,
                             const std::string_view endMarker)
    {
        const std::size_t begin = content.find(beginMarker);
        EXPECT_NE(begin, std::string::npos);
        const std::size_t end = content.find(endMarker, begin);
        EXPECT_NE(end, std::string::npos);
        if (begin == std::string::npos || end == std::string::npos)
            return {};
        return content.substr(begin, end - begin);
    }

    std::string EngineRunFrame()
    {
        return SliceBetween(
            ReadFile(RepoRoot() / "src/runtime/Kernel/Runtime.Engine.cpp"),
            "void Engine::RunFrame()",
            "bool Engine::IsRunning() const noexcept");
    }
}

TEST(RuntimeFrameLoopContract, PlatformBeginFramePollsBeforeMinimizedWait)
{
    const std::string runFrame = EngineRunFrame();
    const auto poll = runFrame.find("m_Impl->m_Window->PollEvents()");
    const auto close = runFrame.find("m_Impl->m_Window->ShouldClose()", poll);
    const auto minimized = runFrame.find("m_Impl->m_Window->IsMinimized()", close);
    const auto wait = runFrame.find(
        "m_Impl->m_Window->WaitForEventsTimeout(kIdleSleepSeconds)", minimized);

    ASSERT_NE(poll, std::string::npos);
    ASSERT_NE(close, std::string::npos);
    ASSERT_NE(minimized, std::string::npos);
    ASSERT_NE(wait, std::string::npos);
    EXPECT_LT(poll, close);
    EXPECT_LT(close, minimized);
    EXPECT_LT(minimized, wait);
}

TEST(RuntimeFrameLoopContract, PlatformBeginFrameStopsWhenPollRequestsClose)
{
    const std::string runFrame = EngineRunFrame();
    const auto closeBranch = runFrame.find("if (platformShouldClose)");
    const auto requestExit =
        runFrame.find("RequestExitFromWindowClose(\"platform-poll\")", closeBranch);
    const auto closeReturn = runFrame.find("return;", requestExit);
    const auto rendererBegin =
        runFrame.find("m_Impl->m_Renderer->BeginFrame(frame)");

    ASSERT_NE(closeBranch, std::string::npos);
    ASSERT_NE(requestExit, std::string::npos);
    ASSERT_NE(closeReturn, std::string::npos);
    ASSERT_NE(rendererBegin, std::string::npos);
    EXPECT_LT(closeBranch, requestExit);
    EXPECT_LT(requestExit, closeReturn);
    EXPECT_LT(closeReturn, rendererBegin);
}

TEST(RuntimeFrameLoopContract, RenderFrameOrdersPromotedRendererPhases)
{
    const std::string runFrame = EngineRunFrame();
    const auto begin = runFrame.find("m_Impl->m_Renderer->BeginFrame(frame)");
    const auto submit = runFrame.find(
        "m_Impl->m_RenderExtractionCache.ExtractAndSubmit(", begin);
    const auto extract = runFrame.find(
        "m_Impl->m_Renderer->ExtractRenderWorld(renderInput, extractSlot)", submit);
    const auto prepare = runFrame.find(
        "m_Impl->m_Renderer->PrepareFrame(renderWorld)", extract);
    const auto execute = runFrame.find(
        "m_Impl->m_Renderer->ExecuteFrame(frame, renderWorld)", prepare);
    const auto end = runFrame.find("m_Impl->m_Renderer->EndFrame(frame)", execute);

    ASSERT_NE(begin, std::string::npos);
    ASSERT_NE(submit, std::string::npos);
    ASSERT_NE(extract, std::string::npos);
    ASSERT_NE(prepare, std::string::npos);
    ASSERT_NE(execute, std::string::npos);
    ASSERT_NE(end, std::string::npos);
    EXPECT_LT(begin, submit);
    EXPECT_LT(submit, extract);
    EXPECT_LT(extract, prepare);
    EXPECT_LT(prepare, execute);
    EXPECT_LT(execute, end);
}

TEST(RuntimeFrameLoopContract, RenderFrameSkipsExtractionWhenBeginFrameFails)
{
    const std::string runFrame = EngineRunFrame();
    const auto begin = runFrame.find("m_Impl->m_Renderer->BeginFrame(frame)");
    const auto failure = runFrame.find("if (!pacing.RendererBeganFrame)", begin);
    const auto failureReturn = runFrame.find("return;", failure);
    const auto extraction = runFrame.find(
        "m_Impl->m_RenderExtractionCache.ExtractAndSubmit(", failure);

    ASSERT_NE(begin, std::string::npos);
    ASSERT_NE(failure, std::string::npos);
    ASSERT_NE(failureReturn, std::string::npos);
    ASSERT_NE(extraction, std::string::npos);
    EXPECT_LT(begin, failure);
    EXPECT_LT(failure, failureReturn);
    EXPECT_LT(failureReturn, extraction);
}

TEST(RuntimeFrameLoopContract,
     MaintenanceOrdersTransferBeforeAssetAndGeometryMaintenance)
{
    const std::string runFrame = EngineRunFrame();
    const auto transfer = runFrame.find(
        "m_Impl->m_Device->GetTransferQueue().CollectCompleted()");
    const auto asset = runFrame.find("assetWorkflow->RunFrameMaintenance()", transfer);
    const auto geometry = runFrame.find(
        "m_Impl->m_RenderExtractionCache.TickGeometryResidency(", asset);

    ASSERT_NE(transfer, std::string::npos);
    ASSERT_NE(asset, std::string::npos);
    ASSERT_NE(geometry, std::string::npos);
    EXPECT_LT(transfer, asset);
    EXPECT_LT(asset, geometry);
}

TEST(RuntimeFrameLoopContract, OperationalTransitionWaitsIdleThenRebuildsRendererOnce)
{
    const std::string runFrame = EngineRunFrame();
    const auto device = runFrame.find("m_Impl->m_Device->IsOperational()");
    const auto renderer = runFrame.find("!m_Impl->m_RendererOperational", device);
    const auto wait = runFrame.find("m_Impl->m_Device->WaitIdle()", renderer);
    const auto rebuild = runFrame.find(
        "m_Impl->m_Renderer->RebuildOperationalResources(", wait);
    const auto mark = runFrame.find(
        "m_Impl->m_RendererOperational = true", rebuild);

    ASSERT_NE(device, std::string::npos);
    ASSERT_NE(renderer, std::string::npos);
    ASSERT_NE(wait, std::string::npos);
    ASSERT_NE(rebuild, std::string::npos);
    ASSERT_NE(mark, std::string::npos);
    EXPECT_LT(device, renderer);
    EXPECT_LT(renderer, wait);
    EXPECT_LT(wait, rebuild);
    EXPECT_LT(rebuild, mark);
}

TEST(RuntimeFrameLoopContract, OperationalTransitionNoOpsUntilDeviceBecomesOperational)
{
    const std::string runFrame = EngineRunFrame();
    const std::string transition = SliceBetween(
        runFrame,
        "const auto operationalBegin",
        "pacing.OperationalTransitionMicros");

    EXPECT_NE(transition.find(
                  "if (m_Impl->m_Device->IsOperational() &&"),
              std::string::npos);
    EXPECT_EQ(
        transition.find("m_Impl->m_Device->WaitIdle()"),
        transition.rfind("m_Impl->m_Device->WaitIdle()"));
}

TEST(RuntimeFrameLoopContract, OperationalTransitionDoesNotMarkRendererWhenRebuildFails)
{
    const std::string runFrame = EngineRunFrame();
    const std::string transition = SliceBetween(
        runFrame,
        "const auto operationalBegin",
        "pacing.OperationalTransitionMicros");
    const auto rebuildGuard = transition.find(
        "if (m_Impl->m_Renderer->RebuildOperationalResources(");
    const auto mark = transition.find(
        "m_Impl->m_RendererOperational = true", rebuildGuard);

    ASSERT_NE(rebuildGuard, std::string::npos);
    ASSERT_NE(mark, std::string::npos);
    EXPECT_LT(rebuildGuard, mark);
}

TEST(RuntimeFrameLoopContract, ShutdownOrdersRuntimeModulesAndSubsystemTeardown)
{
    const auto root = RepoRoot();
    const std::string engine =
        ReadFile(root / "src/runtime/Kernel/Runtime.Engine.cpp");
    const std::string shutdown = SliceBetween(
        engine, "void Engine::Shutdown()", "// ── Main loop");
    const std::array<std::string_view, 8u> ordered{
        "ShutdownRuntimeModules();",
        "m_Impl->m_WorldRegistry.Clear();",
        "m_Impl->m_FrameGraph.reset();",
        "m_Impl->m_Renderer->Shutdown();",
        "m_Impl->m_Device->Shutdown();",
        "m_Impl->m_Window.reset();",
        "Core::Tasks::Scheduler::Shutdown();",
        "m_Impl->m_Initialized = false;",
    };
    std::size_t previous = 0u;
    for (const std::string_view token : ordered)
    {
        const std::size_t position = shutdown.find(token, previous);
        ASSERT_NE(position, std::string::npos) << token;
        previous = position + token.size();
    }

    EXPECT_FALSE(std::filesystem::exists(root / "src/core/Core.FrameLoop.cppm"));
    EXPECT_FALSE(std::filesystem::exists(root / "src/core/Core.FrameLoop.cpp"));

    const std::array<std::string, 17u> retiredTokens{
        std::string{"Extrinsic.Core."} + "FrameLoop",
        std::string{"I"} + "RenderFrameHooks",
        std::string{"I"} + "PlatformFrameHooks",
        std::string{"I"} + "TransferFrameHooks",
        std::string{"I"} + "AssetFrameHooks",
        std::string{"I"} + "OperationalTransitionHooks",
        std::string{"I"} + "ShutdownHooks",
        std::string{"RenderFrame"} + "Phase",
        std::string{"RenderFrame"} + "Result",
        std::string{"PlatformFrame"} + "Result",
        std::string{"PlatformFrame"} + "Hooks",
        std::string{"OperationalTransition"} + "Hooks",
        std::string{"RuntimeRenderFrame"} + "Hooks",
        std::string{"Transfer"} + "Hooks",
        std::string{"Asset"} + "Hooks",
        std::string{"Shutdown"} + "Hooks",
        std::string{"FrameLoop"} + "Hooks",
    };
    const auto ratchet = root /
        "tests/contract/runtime/Test.RuntimeFrameLoopContract.cpp";
    for (const auto relativeRoot : {"src", "tests"})
    {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root / relativeRoot))
        {
            if (!entry.is_regular_file() || entry.path() == ratchet)
                continue;
            const auto extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".cppm" &&
                extension != ".h" && extension != ".hpp")
            {
                continue;
            }
            const std::string content = ReadFile(entry.path());
            for (const std::string& token : retiredTokens)
                EXPECT_EQ(content.find(token), std::string::npos)
                    << token << " in " << entry.path();
        }
    }
}
