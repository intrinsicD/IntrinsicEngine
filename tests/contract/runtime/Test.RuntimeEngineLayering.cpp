#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
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

    std::size_t CountOccurrences(const std::string& content,
                                 const std::string& needle)
    {
        if (needle.empty())
            return 0u;

        std::size_t count = 0u;
        std::size_t offset = 0u;
        while ((offset = content.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

    std::string WithoutWhitespace(const std::string_view content)
    {
        std::string compact;
        compact.reserve(content.size());
        for (const char value : content)
        {
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                compact.push_back(value);
        }
        return compact;
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

TEST(RuntimeEngineLayering, ProductionSourceHasNoApplicationCallbackLifecycle)
{
    const auto sourceRoot = RepoRoot() / "src";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot))
    {
        if (!entry.is_regular_file())
            continue;

        const auto extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".cppm" && extension != ".h" && extension != ".hpp")
        {
            continue;
        }

        const std::string content = ReadFile(entry.path());
        EXPECT_EQ(content.find("IApplication"), std::string::npos) << entry.path();
        EXPECT_EQ(content.find("OnSimTick"), std::string::npos) << entry.path();
        EXPECT_EQ(content.find("OnVariableTick"), std::string::npos) << entry.path();

        if (entry.path().string().find("/src/app/") != std::string::npos)
        {
            EXPECT_EQ(content.find("Runtime::Engine&"), std::string::npos) << entry.path();
            EXPECT_EQ(content.find("Runtime::Engine &"), std::string::npos) << entry.path();
        }
    }
}

TEST(RuntimeEngineLayering, RunFrameDelegatesToPromotedContractsInDocumentedBroadPhaseOrder)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto runFrame = SliceBetween(content,
                                       "void Engine::RunFrame()",
                                       "bool Engine::IsRunning() const noexcept");

    const auto frameContext = runFrame.find("RuntimeFrameContext frameContext{};");
    const auto platformContract = runFrame.find("Core::ExecutePlatformBeginFrameContract(platformHooks");
    const auto fixedStep      = runFrame.find("RunFixedStepSimulationTicks(");
    const auto uiBuild        = runFrame.find("FramePhase::UiBuild");
    const auto renderContract = runFrame.find("Core::ExecuteRenderFrameContract(renderHooks)");
    const auto present = runFrame.find("m_Impl->m_Device->Present(frame);");
    const auto maintenance =
        runFrame.find("Core::ExecuteMaintenanceContract(");
    const auto clockEnd = runFrame.rfind("m_Impl->m_FrameClock.EndFrame();");

    ASSERT_NE(frameContext, std::string::npos);
    ASSERT_NE(platformContract, std::string::npos);
    ASSERT_NE(fixedStep, std::string::npos);
    ASSERT_NE(uiBuild, std::string::npos);
    ASSERT_NE(renderContract, std::string::npos);
    ASSERT_NE(present, std::string::npos);
    ASSERT_NE(maintenance, std::string::npos);
    ASSERT_NE(clockEnd, std::string::npos);

    EXPECT_LT(frameContext, platformContract);
    EXPECT_LT(platformContract, fixedStep);
    EXPECT_LT(fixedStep, uiBuild);
    EXPECT_LT(uiBuild, renderContract);
    EXPECT_LT(renderContract, present);
    EXPECT_LT(present, maintenance);
    EXPECT_LT(maintenance, clockEnd);
}

TEST(RuntimeEngineLayering, RunFrameAppliesJobCompletionsWithBoundedBudget)
{
    const auto content =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto runFrame = SliceBetween(
        content,
        "void Engine::RunFrame()",
        "bool Engine::IsRunning() const noexcept");

    EXPECT_NE(content.find(
                  "constexpr std::uint32_t kJobCompletionApplyBudgetPerFrame = 8u;"),
              std::string::npos);
    EXPECT_EQ(CountOccurrences(
                  runFrame,
                  "m_Impl->m_JobService.DrainCompletions("),
              1u);
    EXPECT_NE(runFrame.find("kJobCompletionApplyBudgetPerFrame"),
              std::string::npos);
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

TEST(RuntimeEngineLayering, EngineDelegatesGpuQueueLifecycleToJobService)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto publicApi = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto bridgeApi =
        ReadFile(RepoRoot() / "src/runtime/Runtime.JobServiceGpuQueueBridge.cppm");
    const auto bridge =
        ReadFile(RepoRoot() / "src/runtime/Runtime.JobServiceGpuQueueBridge.cpp");
    const auto beginShutdown =
        SliceBetween(content, "void Engine::BeginShutdown()", "void Engine::Shutdown()");
    const auto shutdown      = SliceBetween(content, "void Engine::Shutdown()", "// ── Main loop");
    const auto bridgeInstall = SliceBetween(
        bridge,
        "void JobServiceGpuQueueBridge::Install",
        "void JobServiceGpuQueueBridge::Uninstall");
    const auto bridgeShutdown = SliceBetween(
        bridge,
        "std::uint64_t JobServiceGpuQueueBridge::ShutdownParticipants",
        "bool JobServiceGpuQueueBridge::IsInstalled");

    const auto participantShutdown =
        beginShutdown.find("m_Impl->m_JobServiceGpuQueueBridge.ShutdownParticipants(");
    const auto beginShutdownCall = shutdown.find("BeginShutdown();");
    const auto executeShutdown = shutdown.find("Core::ExecuteShutdownContract(hooks)");
    const auto installBridge =
        content.find("m_Impl->m_JobServiceGpuQueueBridge.Install("
                     "*m_Impl->m_Renderer, m_Impl->m_JobService);");
    const auto installDirectHook =
        bridgeInstall.find("renderer.RegisterRuntimeFrameCommandHook(");
    const auto recordCommands =
        bridgeInstall.find("jobs.RecordGpuQueueFrameCommands(commandContext);");
    const auto detachHook =
        bridgeShutdown.find("Uninstall(renderer);");
    const auto serviceShutdown =
        bridgeShutdown.find("jobs.ShutdownGpuQueueParticipants(", detachHook);
    const auto waitIdle = beginShutdown.find(
        "m_Impl->m_Device->WaitIdle();", participantShutdown);

    ASSERT_NE(participantShutdown, std::string::npos);
    ASSERT_NE(beginShutdownCall, std::string::npos);
    ASSERT_NE(executeShutdown, std::string::npos);
    ASSERT_NE(installBridge, std::string::npos);
    ASSERT_NE(installDirectHook, std::string::npos);
    ASSERT_NE(recordCommands, std::string::npos);
    ASSERT_NE(detachHook, std::string::npos);
    ASSERT_NE(serviceShutdown, std::string::npos);
    ASSERT_NE(waitIdle, std::string::npos);

    EXPECT_LT(beginShutdownCall, executeShutdown);
    EXPECT_LT(installDirectHook, recordCommands);
    EXPECT_LT(detachHook, serviceShutdown);
    EXPECT_LT(participantShutdown, waitIdle);
    EXPECT_EQ(publicApi.find("RuntimeFrameCommandHookHandle"), std::string::npos);
    EXPECT_EQ(publicApi.find("m_JobServiceGpuQueueHook"), std::string::npos);
    EXPECT_NE(bridgeApi.find("RuntimeFrameCommandHookHandle"), std::string::npos);
    EXPECT_EQ(content.find("RegisterRuntimeFrameCommandHook"), std::string::npos);
    EXPECT_EQ(content.find("UnregisterRuntimeFrameCommandHook"), std::string::npos);
    EXPECT_EQ(content.find("RecordGpuQueueFrameCommands"), std::string::npos);
    EXPECT_EQ(publicApi.find("RuntimeGpuJobParticipant"), std::string::npos);
    EXPECT_EQ(content.find("RegisterRuntimeGpuJobParticipant"), std::string::npos);
}

TEST(RuntimeEngineLayering,
     TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto workflowInterface =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.AssetWorkflowModule.cppm");
    const auto workflowImpl =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.AssetWorkflowModule.cpp");
    const auto textureBakeInterface =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.TextureBakeModule.cppm");
    const auto textureBakeImpl =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.TextureBakeModule.cpp");
    const auto runtimeCMake = ReadFile(RepoRoot() / "src/runtime/CMakeLists.txt");
    const auto graphicsCMake =
        ReadFile(RepoRoot() / "src/graphics/renderer/CMakeLists.txt");
    const auto moduleInventory =
        ReadFile(RepoRoot() / "docs/api/generated/module_inventory.md");

    EXPECT_EQ(engineInterface.find("ObjectSpaceNormalBakeService"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("ObjectSpaceNormalBakeService"),
              std::string::npos);
    EXPECT_EQ(workflowImpl.find(
                  "import Extrinsic.Runtime.ObjectSpaceNormalBakeService;"),
              std::string::npos);
    EXPECT_NE(workflowImpl.find(
                  "import Extrinsic.Runtime.TextureBakeModule;"),
              std::string::npos);
    EXPECT_EQ(workflowImpl.find("TextureBake->ProducerContext()"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find(
                  "import Extrinsic.Runtime.ObjectSpaceNormalBakeService;"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find("ObjectSpaceNormalBakeService Bake{}"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find("state.Bake.SetDependencies("),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find(
                  "state.Bake.RegisterGpuQueueParticipant("),
              std::string::npos);
    EXPECT_EQ(CountOccurrences(
                  workflowImpl,
                  ".ObjectSpaceNormalBakeQueue ="),
              0u);
    EXPECT_EQ(
        CountOccurrences(
            workflowImpl,
            ".TextureBake = TextureBake"),
        2u);
    EXPECT_EQ(workflowImpl.find("&Bake->Queue()"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find("Bake.ClearDependencies()"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find("Bake.Queue().Clear()"),
              std::string::npos);
    EXPECT_EQ(textureBakeImpl.find(
                  "Provide<ObjectSpaceNormalBakeService>"),
              std::string::npos);
    EXPECT_NE(textureBakeImpl.find("Provide<TextureBakeService>"),
              std::string::npos);
    EXPECT_EQ(
        CountOccurrences(
            textureBakeImpl,
            "RegisterGpuQueueParticipant(setup.Jobs())"),
        1u);
    EXPECT_EQ(textureBakeImpl.find("QueueDiagnostics()"),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "export import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RuntimeObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("RuntimeObjectSpaceNormalBakeGpuQueueDependencies"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("MakeGpuQueueParticipantDesc()"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("GetGlobalFrameNumber() + 1u"),
              std::string::npos);

    EXPECT_NE(workflowInterface.find(
                  "export module Extrinsic.Runtime.AssetWorkflowModule"),
              std::string::npos);
    EXPECT_EQ(workflowInterface.find("ObjectSpaceNormalBakeService"),
              std::string::npos);
    EXPECT_NE(textureBakeInterface.find(
                  "export module Extrinsic.Runtime.TextureBakeModule"),
              std::string::npos);
    EXPECT_NE(textureBakeInterface.find("class TextureBakeService"),
              std::string::npos);
    EXPECT_NE(textureBakeInterface.find("class TextureBakeModule final"),
              std::string::npos);
    EXPECT_EQ(
        textureBakeInterface.find(
            "RuntimeObjectSpaceNormalBakeQueue"),
        std::string::npos);
    EXPECT_EQ(
        textureBakeInterface.find(
            "TextureBakeProducerContext"),
        std::string::npos);
    const std::array retiredFiles{
        "src/runtime/Runtime.SelectedMeshTextureBake.cppm",
        "src/runtime/Runtime.SelectedMeshTextureBake.cpp",
        "src/runtime/Runtime.MeshAttributeTextureBake.cppm",
        "src/runtime/Runtime.MeshAttributeTextureBake.cpp",
        "src/runtime/Runtime.ObjectSpaceNormalBakeBinding.cppm",
        "src/runtime/Runtime.ObjectSpaceNormalBakeBinding.cpp",
        "src/runtime/Runtime.ObjectSpaceNormalBakeQueue.cppm",
        "src/runtime/Runtime.ObjectSpaceNormalBakeQueue.cpp",
        "src/runtime/Runtime.ObjectSpaceNormalBakeService.cppm",
        "src/runtime/Runtime.ObjectSpaceNormalBakeService.cpp",
        "src/runtime/Runtime.ObjectSpaceNormalBakeSubmission.cppm",
        "src/runtime/Runtime.ObjectSpaceNormalBakeSubmission.cpp",
        "src/graphics/renderer/Graphics.ObjectSpaceNormalTextureBake.cppm",
        "src/graphics/renderer/Graphics.ObjectSpaceNormalTextureBake.cpp",
    };
    for (const std::string_view retired : retiredFiles)
    {
        const std::string filename =
            std::filesystem::path{retired}.filename().string();
        EXPECT_FALSE(std::filesystem::exists(RepoRoot() / retired))
            << retired;
        EXPECT_EQ(runtimeCMake.find(filename), std::string::npos)
            << retired;
        EXPECT_EQ(graphicsCMake.find(filename), std::string::npos)
            << retired;
    }
    EXPECT_NE(runtimeCMake.find("Runtime.TextureBakeModule.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.TextureBakeModule.cpp"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("ObjectSpaceNormalBake"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("MeshAttributeTextureBake"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("SelectedMeshTextureBake"),
              std::string::npos);
    EXPECT_NE(moduleInventory.find("Extrinsic.Runtime.TextureBakeModule`"),
              std::string::npos);
}

TEST(RuntimeEngineLayering,
     SceneInteractionModuleOwnsGizmoFrameServiceOutOfEngine)
{
    const auto root = RepoRoot();
    const auto engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(
            root /
            "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto interactionInterface =
        ReadFile(
            root /
            "src/runtime/Scene/Runtime.SceneInteractionModule.cppm");
    const auto interactionImpl =
        ReadFile(
            root /
            "src/runtime/Scene/Runtime.SceneInteractionModule.cpp");
    const auto serviceInterface =
        ReadFile(
            root /
            "src/runtime/Gizmos/Runtime.GizmoFrameService.cppm");
    const auto serviceImpl =
        ReadFile(
            root /
            "src/runtime/Gizmos/Runtime.GizmoFrameService.cpp");

    EXPECT_EQ(engineInterface.find("GizmoFrameService"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("GizmoFrameService"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.GizmoInteraction"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GizmoInteraction"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GizmoUndoStack"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("TransformGizmoRenderPacketBuilder"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_GizmoSelectedEntities"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.GizmoInteraction"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("DriveGizmoAndSelectionInputForFrame"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_GizmoPacketBuilder.Build("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_GizmoInteraction.Mode()"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_GizmoSelectedEntities"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.Runtime.GizmoInteraction"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("DriveGizmoInteractionForFrame"),
              std::string::npos);

    EXPECT_NE(
        interactionInterface.find(
            "class SceneInteractionModule final : public IRuntimeModule"),
        std::string::npos);
    EXPECT_NE(interactionInterface.find("struct Impl;"),
              std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "import Extrinsic.Runtime.GizmoFrameService;"),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find("GizmoFrameService Gizmo{}"),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "Gizmo.DriveInputForFrame("),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "Gizmo.BuildRenderPackets("),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "Gizmo.ClearSceneState(BoundRegistry)"),
        std::string::npos);

    const auto unifiedClear =
        SliceBetween(
            interactionImpl,
            "void ClearWorldBoundState()",
            "void BindTo(");
    const auto gizmoClear =
        unifiedClear.find(
            "Gizmo.ClearSceneState(BoundRegistry)");
    const auto selectionClear =
        unifiedClear.find(
            "Selection.ClearSceneState(*BoundRegistry)");
    const auto readbackClear =
        unifiedClear.find("Readback.ClearSceneState()");
    const auto lookupDetach =
        unifiedClear.find(
            "Selection.SetStableEntityLookup(nullptr)");
    const auto lookupDisconnect =
        unifiedClear.find("LookupBinding.Disconnect()");
    const auto lookupClear =
        unifiedClear.find("Lookup.Clear()");
    const auto snapshotClear =
        unifiedClear.find("PublishEmpty(BoundWorld)");
    ASSERT_NE(gizmoClear, std::string::npos);
    ASSERT_NE(selectionClear, std::string::npos);
    ASSERT_NE(readbackClear, std::string::npos);
    ASSERT_NE(lookupDetach, std::string::npos);
    ASSERT_NE(lookupDisconnect, std::string::npos);
    ASSERT_NE(lookupClear, std::string::npos);
    ASSERT_NE(snapshotClear, std::string::npos);
    EXPECT_LT(gizmoClear, selectionClear);
    EXPECT_LT(selectionClear, readbackClear);
    EXPECT_LT(readbackClear, lookupDetach);
    EXPECT_LT(lookupDetach, lookupDisconnect);
    EXPECT_LT(lookupDisconnect, lookupClear);
    EXPECT_LT(lookupClear, snapshotClear);

    EXPECT_NE(serviceInterface.find("export module Extrinsic.Runtime.GizmoFrameService"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("export import Extrinsic.Runtime.GizmoInteraction"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("GizmoInteraction m_Interaction"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("GizmoUndoStack m_UndoStack"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("TransformGizmoRenderPacketBuilder m_PacketBuilder"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::vector<ECS::EntityHandle> m_SelectedEntities"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("DriveGizmoInteractionForFrame"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("SubmitViewportSelectionClickForFrame"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("BuildRenderPackets"),
              std::string::npos);
}

TEST(RuntimeEngineLayering, RunFrameCarriesDataOnlyFrameContext)
{
    const auto content = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");

    EXPECT_NE(frameLoop.find("struct RuntimeFrameContext"), std::string::npos);
    EXPECT_NE(frameLoop.find("double FrameDeltaSeconds"), std::string::npos);
    EXPECT_NE(frameLoop.find("double FixedStepAlpha"), std::string::npos);
    EXPECT_NE(frameLoop.find("std::uint64_t FrameIndex"), std::string::npos);
    EXPECT_NE(frameLoop.find("Graphics::RenderFrameInput RenderInput"), std::string::npos);
    EXPECT_NE(frameLoop.find("RuntimeRenderExtractionStats ExtractionStats"), std::string::npos);
    EXPECT_NE(frameLoop.find("std::uint32_t PooledFrontSlot"), std::string::npos);
    EXPECT_NE(content.find("frameContext.FrameDeltaSeconds = frameDt;"), std::string::npos);
    EXPECT_NE(content.find("frameContext.FixedStepAlpha = alpha;"), std::string::npos);
    EXPECT_NE(content.find("frameContext.FrameIndex = "
                           "m_Impl->m_RenderExtractionService.ConsumeFrameIndex();"),
              std::string::npos);
    EXPECT_NE(content.find("frameContext.ExtractionStats"), std::string::npos);
    EXPECT_NE(content.find("frameContext.PooledFrontSlot"), std::string::npos);
}

TEST(RuntimeEngineLayering,
     GpuProfilingConfigIsSampledOnceAfterUiCaptureBeforeExtraction)
{
    const auto content =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto runFrame = SliceBetween(
        content,
        "void Engine::RunFrame()",
        "bool Engine::IsRunning() const noexcept");

    const auto uiEndCapture =
        runFrame.find("FramePhase::UiEndCapture");
    const auto sample =
        runFrame.find(".EnableGpuProfiling =");
    const auto beforeExtraction =
        runFrame.find("FramePhase::BeforeExtraction");
    const auto renderContract =
        runFrame.find("Core::ExecuteRenderFrameContract(renderHooks)");

    ASSERT_NE(uiEndCapture, std::string::npos);
    ASSERT_NE(sample, std::string::npos);
    ASSERT_NE(beforeExtraction, std::string::npos);
    ASSERT_NE(renderContract, std::string::npos);
    EXPECT_LT(uiEndCapture, sample);
    EXPECT_LT(sample, beforeExtraction);
    EXPECT_LT(sample, renderContract);
    EXPECT_EQ(
        CountOccurrences(runFrame, ".EnableGpuProfiling ="),
        1u);
    EXPECT_NE(
        runFrame.find("m_Impl->m_Config.Render.EnableGpuProfiling", sample),
        std::string::npos);
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
    const auto assets = content.find("assets.TickAssets()");

    ASSERT_NE(transfers, std::string::npos);
    ASSERT_NE(assets, std::string::npos);
    EXPECT_LT(transfers, assets);
    EXPECT_EQ(content.find("streaming."), std::string::npos);
}

TEST(RuntimeEngineLayering, RunFrameRegistersPromotedEcsSystemBundleBeforeCompile)
{
    const auto content =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");

    const auto bundleRegistration = content.find(
        "RegisterPromotedEcsSystemBundle(frameGraph, scene)");
    const auto compile = content.find("frameGraph.Compile()");
    const auto execute = content.find("frameGraph.Execute()");
    const auto resetForReplay =
        content.find("frameGraph.ResetForReplay()");
    const auto bundleImport =
        engineImpl.find("import Extrinsic.Runtime.EcsSystemBundle");

    ASSERT_NE(bundleRegistration, std::string::npos);
    ASSERT_NE(compile, std::string::npos);
    ASSERT_NE(execute, std::string::npos);
    ASSERT_NE(resetForReplay, std::string::npos);
    ASSERT_NE(bundleImport, std::string::npos);

    // The promoted baseline is the complete fixed-step system bundle. BUG-069.
    EXPECT_LT(bundleRegistration, compile);
    EXPECT_LT(compile, execute);
    EXPECT_LT(execute, resetForReplay);
    EXPECT_EQ(content.find("registerModuleSystems"), std::string::npos);
    EXPECT_EQ(content.find("frameGraph.Reset();"), std::string::npos);
}

TEST(RuntimeEngineLayering, SupersededWorkSurfacesAreRetiredFromBuild)
{
    const auto runtimeCMake =
        ReadFile(RepoRoot() / "src/runtime/CMakeLists.txt");
    const auto testsCMake = ReadFile(RepoRoot() / "tests/CMakeLists.txt");

    EXPECT_FALSE(std::filesystem::exists(
        RepoRoot() / "src/runtime/Runtime.StreamingExecutor.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        RepoRoot() / "src/runtime/Runtime.StreamingExecutor.cpp"));
    EXPECT_FALSE(std::filesystem::exists(
        RepoRoot() / "src/runtime/Runtime.DerivedJobGraph.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        RepoRoot() / "src/runtime/Runtime.DerivedJobGraph.cpp"));
    EXPECT_EQ(runtimeCMake.find("Runtime.StreamingExecutor"), std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.DerivedJobGraph"), std::string::npos);
    EXPECT_EQ(testsCMake.find("Test.RuntimeStreamingExecutor.cpp"),
              std::string::npos);
    EXPECT_EQ(testsCMake.find("Test.DerivedJobGraph.cpp"), std::string::npos);
}

TEST(RuntimeEngineLayering,
     SupersededGpuResultReadbackSurfacesAreRetiredFromBuild)
{
    const auto root = RepoRoot();
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");
    const auto testsCMake = ReadFile(root / "tests/CMakeLists.txt");

    const std::array retiredPaths{
        root / "src/runtime/Runtime.AsyncBufferReadback.cppm",
        root / "src/runtime/Runtime.AsyncBufferReadback.cpp",
        root / "src/runtime/Runtime.GpuReadbackJob.cppm",
        root / "src/runtime/Runtime.GpuReadbackJob.cpp",
        root / "tests/contract/runtime/Test.AsyncBufferReadback.cpp",
        root / "tests/contract/runtime/Test.GpuReadbackJob.cpp",
        root / "tests/integration/runtime/Test.GpuReadbackJobGpuSmoke.cpp",
    };
    for (const auto& path : retiredPaths)
        EXPECT_FALSE(std::filesystem::exists(path)) << path;

    EXPECT_EQ(runtimeCMake.find("Runtime.AsyncBufferReadback"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.GpuReadbackJob"),
              std::string::npos);
    EXPECT_EQ(testsCMake.find("Test.AsyncBufferReadback.cpp"),
              std::string::npos);
    EXPECT_EQ(testsCMake.find("Test.GpuReadbackJob.cpp"),
              std::string::npos);
    EXPECT_NE(testsCMake.find("Test.GpuResultReadbackJob.cpp"),
              std::string::npos);
    EXPECT_NE(testsCMake.find("Test.GpuResultReadbackGpuSmoke.cpp"),
              std::string::npos);

    const std::array sourceRoots{
        root / "src/runtime",
        root / "methods",
    };
    for (const auto& sourceRoot : sourceRoots)
    {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(sourceRoot))
        {
            if (!entry.is_regular_file())
                continue;
            const auto extension = entry.path().extension().string();
            if (extension != ".cpp" && extension != ".cppm")
                continue;

            const std::string content = ReadFile(entry.path());
            EXPECT_EQ(content.find("ReadBuffer("), std::string::npos)
                << entry.path();
            EXPECT_EQ(
                content.find(
                    "import Extrinsic.Runtime.AsyncBufferReadback"),
                std::string::npos)
                << entry.path();
            EXPECT_EQ(
                content.find("import Extrinsic.Runtime.GpuReadbackJob"),
                std::string::npos)
                << entry.path();
        }
    }
}

TEST(RuntimeEngineLayering, AsyncWorkModulePublishesOnlyKernelJobService)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop = ReadFile(RepoRoot() / "src/core/Core.FrameLoop.cppm");
    const auto moduleInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AsyncWorkModule.cppm");
    const auto moduleImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AsyncWorkModule.cpp");
    const auto sandboxMain = ReadFile(RepoRoot() / "src/app/Sandbox/main.cpp");

    // The generic Engine composition root may borrow capabilities by service
    // type, but must not know, own, construct, or facade the concrete module.
    EXPECT_EQ(engineInterface.find("AsyncWorkModule"), std::string::npos);
    EXPECT_EQ(engineImpl.find("AsyncWorkModule"), std::string::npos);
    EXPECT_NE(engineImpl.find("JobService m_JobService{}"), std::string::npos);
    EXPECT_EQ(engineImpl.find("Provide<JobService>"), std::string::npos);
    EXPECT_EQ(frameLoop.find("IStreamingFrameHooks"), std::string::npos);

    EXPECT_NE(moduleInterface.find(
                  "export module Extrinsic.Runtime.AsyncWorkModule"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find("public IRuntimeModule"),
              std::string::npos);
    EXPECT_NE(moduleInterface.find("bool m_Registered{false}"),
              std::string::npos);
    EXPECT_EQ(moduleInterface.find("IStreamingFrameHooks"), std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "Provide<JobService>(setup.Jobs(), Name())"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find(
                  "Withdraw<JobService>(context.Jobs)"),
              std::string::npos);
    EXPECT_NE(moduleImpl.find("jobs.SnapshotAll()"), std::string::npos);
    EXPECT_NE(moduleImpl.find("jobs.Cancel(survivor.Token)"),
              std::string::npos);
    EXPECT_EQ(moduleImpl.find("StreamingExecutor"), std::string::npos);
    EXPECT_EQ(moduleImpl.find("DerivedJobRegistry"), std::string::npos);

    EXPECT_NE(sandboxMain.find("import Extrinsic.Runtime.AsyncWorkModule"), std::string::npos);
    EXPECT_NE(sandboxMain.find("EmplaceModule<Extrinsic::Runtime::AsyncWorkModule>()"),
              std::string::npos);
}

TEST(RuntimeEngineLayering,
     EngineConfigControlIsAnAppComposedModuleOutsideEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto controlInterface =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.EngineConfigControl.cppm");
    const auto controlImpl =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.EngineConfigControl.cpp");
    const auto activationInterface =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.RenderRecipeActivation.cppm");
    const auto sandbox =
        ReadFile(RepoRoot() / "src/app/Sandbox/main.cpp");
    const auto runtimeCMake =
        ReadFile(RepoRoot() / "src/runtime/CMakeLists.txt");

    EXPECT_EQ(
        engineInterface.find("EngineConfigControl"),
        std::string::npos);
    EXPECT_EQ(
        engineImpl.find("EngineConfigControl"),
        std::string::npos);
    EXPECT_EQ(
        engineInterface.find("EngineConfigSectionRegistry"),
        std::string::npos);
    EXPECT_EQ(
        engineImpl.find("EngineConfigSectionRegistry"),
        std::string::npos);
    EXPECT_EQ(
        engineInterface.find("GetConfigControl"),
        std::string::npos);

    EXPECT_NE(
        engineImpl.find(
            "LoadAndApplyRuntimeRenderRecipeConfigFile("),
        std::string::npos);
    EXPECT_NE(
        engineImpl.find(
            "ResetRuntimeRenderRecipeActivation(recipeActivation)"),
        std::string::npos);
    EXPECT_NE(
        controlInterface.find(
            "EngineConfigControl final : public IRuntimeModule"),
        std::string::npos);
    EXPECT_NE(
        controlInterface.find(
            "RuntimeEngineConfigSectionRegistry m_SectionRegistry"),
        std::string::npos);
    EXPECT_NE(
        controlImpl.find(
            "Provide<EngineConfigControl>"),
        std::string::npos);
    EXPECT_NE(
        controlImpl.find(
            "Withdraw<EngineConfigControl>(*this)"),
        std::string::npos);
    EXPECT_EQ(
        controlInterface.find("EngineConfigControlDependencies"),
        std::string::npos);
    EXPECT_EQ(
        controlInterface.find("IWindow"),
        std::string::npos);
    EXPECT_EQ(
        controlInterface.find("IRenderer"),
        std::string::npos);
    EXPECT_NE(
        activationInterface.find(
            "struct RuntimeRenderRecipeActivationKernel"),
        std::string::npos);

    EXPECT_NE(
        sandbox.find(
            "std::make_unique<Extrinsic::Runtime::EngineConfigControl>"),
        std::string::npos);
    EXPECT_NE(
        sandbox.find("configControl->SectionRegistry()"),
        std::string::npos);
    EXPECT_NE(
        sandbox.find("engine.AddModule(std::move(configControl))"),
        std::string::npos);
    EXPECT_NE(
        runtimeCMake.find(
            "Runtime.RenderRecipeActivation.cppm"),
        std::string::npos);
    EXPECT_NE(
        runtimeCMake.find(
            "Runtime.RenderRecipeActivation.cpp"),
        std::string::npos);
}

TEST(RuntimeEngineLayering, MaintenanceHasNoOptionalStreamingFacade)
{
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/core/Core.FrameLoop.cpp");
    const auto runFrame = SliceBetween(
        engineImpl,
        "void Engine::RunFrame()",
        "bool Engine::IsRunning() const noexcept");
    const auto maintenanceContract = SliceBetween(
        frameLoop,
        "void ExecuteMaintenanceContract(",
        "bool ExecuteOperationalTransitionContract(");

    EXPECT_EQ(runFrame.find("IStreamingFrameHooks"), std::string::npos);
    EXPECT_NE(runFrame.find("Core::ExecuteMaintenanceContract("),
              std::string::npos);

    const auto transfer =
        maintenanceContract.find("transfer.CollectCompletedTransfers()");
    const auto asset = maintenanceContract.find("assets.TickAssets()");
    ASSERT_NE(transfer, std::string::npos);
    ASSERT_NE(asset, std::string::npos);
    EXPECT_LT(transfer, asset);
    EXPECT_EQ(maintenanceContract.find("streaming"), std::string::npos);
}

TEST(RuntimeEngineLayering, ProductionAsyncSubmissionsCarryOwningWorldScope)
{
    const auto assetImport =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AssetImportPipeline.cpp");
    const auto sceneDocument =
        ReadFile(
            RepoRoot() /
            "src/runtime/Scene/Runtime.SceneDocumentModule.cpp");
    const auto sandboxPolicies =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxDefaultPolicies.cpp");
    const auto visualization =
        ReadFile(RepoRoot() /
                 "src/runtime/Visualization/Runtime.VisualizationRecipes.cpp");
    const auto modelHandoff =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AssetModelSceneHandoff.cpp");
    const auto methodFacade =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxMethodFacade.cpp");
    const auto clusteringModule = ReadFile(
        RepoRoot() /
        "src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp");
    const auto editorFacades =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxEditorFacades.cpp");
    const auto assetWorkflow =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AssetWorkflowModule.cpp");

    EXPECT_EQ(CountOccurrences(assetImport, "JobDesc{"), 2u);
    EXPECT_EQ(
        CountOccurrences(assetImport, ".Scope = submissionWorld"),
        2u);
    // RUNTIME-194 Slice B migrates these submissions to JobService lane by lane.
    // The invariant under test is unchanged — every production async submission
    // carries its owning world scope — so migrated lanes keep the `.Scope =`
    // designator rather than MakeCpuJobDesc's positional scope argument.
    EXPECT_EQ(CountOccurrences(sceneDocument, "JobDesc{"), 2u);
    EXPECT_EQ(CountOccurrences(sceneDocument, ".Scope = world"), 2u);
    EXPECT_EQ(CountOccurrences(sandboxPolicies, "JobDesc{"), 2u);
    EXPECT_EQ(CountOccurrences(sandboxPolicies, ".Scope = world"), 2u);
    EXPECT_EQ(CountOccurrences(visualization, "JobDesc{"), 1u);
    EXPECT_EQ(CountOccurrences(visualization, ".Scope = request.World"), 1u);

    EXPECT_EQ(CountOccurrences(modelHandoff, "JobDesc "), 3u);
    EXPECT_EQ(CountOccurrences(modelHandoff, ".Scope = "), 3u);
    EXPECT_EQ(CountOccurrences(methodFacade, "JobDesc desc{"), 1u);
    EXPECT_EQ(CountOccurrences(methodFacade, ".Scope = context.World"), 1u);
    EXPECT_EQ(
        CountOccurrences(
            clusteringModule,
            "MakeCpuJobDesc<KMeansJobResult>("),
        1u);
    EXPECT_EQ(
        CountOccurrences(
            clusteringModule,
            "const WorldHandle world = snapshot.World"),
        1u);
    EXPECT_NE(
        WithoutWhitespace(clusteringModule).find(
            "returnKMeansSnapshot{.Command=command,.World=world"),
        std::string::npos);
    // RUNTIME-194 Slice B5d moved all five desc factories to JobService. The
    // invariant under test is unchanged — every production async submission
    // carries its owning world scope.
    EXPECT_EQ(CountOccurrences(editorFacades, "return DerivedJobDesc{"), 0u);
    EXPECT_EQ(CountOccurrences(editorFacades, "return JobDesc{"), 5u);
    EXPECT_EQ(CountOccurrences(editorFacades, ".Scope = context.World"), 5u);
    EXPECT_EQ(CountOccurrences(
                  editorFacades,
                  "desc.Scope = m_Worlds->ActiveWorld()"),
              1u);
    EXPECT_NE(WithoutWhitespace(editorFacades).find(".World=activeWorld"),
              std::string::npos);
    EXPECT_EQ(CountOccurrences(assetWorkflow, ".World = BoundWorld"), 2u);
    EXPECT_EQ(CountOccurrences(assetWorkflow, ".Worlds = Worlds"), 1u);
}

TEST(RuntimeEngineLayering, SandboxEditorJobsUseSingleJobServiceSurface)
{
    const auto editorInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxEditorFacades.cppm");
    const auto editorImplementation =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxEditorFacades.cpp");
    const auto methodImplementation =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxMethodFacade.cpp");

    EXPECT_EQ(editorInterface.find(
                  "import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_EQ(editorImplementation.find(
                  "import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_EQ(methodImplementation.find(
                  "import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_EQ(methodImplementation.find(
                  "import Extrinsic.Runtime.StreamingExecutor"),
              std::string::npos);

    EXPECT_NE(editorInterface.find("struct SandboxEditorJobCommandSurface"),
              std::string::npos);
    EXPECT_NE(editorInterface.find("SandboxEditorJobCommandSurface JobCommands"),
              std::string::npos);
    EXPECT_NE(editorInterface.find("FindActive{}"), std::string::npos);
    EXPECT_NE(editorInterface.find("SnapshotEntity{}"), std::string::npos);
    EXPECT_EQ(editorInterface.find("SandboxEditorDerivedJobCommandSurface"),
              std::string::npos);
    EXPECT_EQ(editorInterface.find("DerivedJobCommands"), std::string::npos);
    EXPECT_EQ(editorInterface.find("ToSandboxEditorJobQueueSnapshot"),
              std::string::npos);
    EXPECT_EQ(editorImplementation.find("DerivedJobRegistry"),
              std::string::npos);
    EXPECT_EQ(editorImplementation.find("m_DerivedJobSnapshot"),
              std::string::npos);
    EXPECT_EQ(methodImplementation.find("FindActiveEditorDerivedJob"),
              std::string::npos);
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

TEST(RuntimeEngineLayering, DeviceBootstrapKeepsBackendAndFallbackPolicyOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto workflowImpl =
        ReadFile(
            RepoRoot() /
            "src/runtime/Runtime.AssetWorkflowModule.cpp");
    const auto bootstrap =
        ReadFile(RepoRoot() / "src/runtime/Runtime.DeviceBootstrap.cpp");
    const auto workflowRegistration = SliceBetween(
        workflowImpl,
        "Core::Result AssetWorkflowModule::OnRegister(",
        "Core::Result AssetWorkflowModule::OnResolve(");
    const auto engineInitialize = SliceBetween(
        engineImpl,
        "void Engine::Initialize()",
        "void Engine::Shutdown()");

    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.DeviceBootstrap"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("CreateRuntimeDevice(m_Impl->m_Config.Render)"),
              std::string::npos);
    EXPECT_EQ(engineInitialize.find("InitializeRuntimeGpuAssetFallbackTexture("),
              std::string::npos);
    EXPECT_NE(workflowRegistration.find(
                  "InitializeRuntimeGpuAssetFallbackTexture("),
              std::string::npos);
    EXPECT_NE(workflowRegistration.find(
                  "std::make_unique<Graphics::GpuAssetCache>"),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("RuntimeDeviceSelection"), std::string::npos);
    EXPECT_EQ(engineInterface.find("SelectRuntimeDeviceBackend"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "ShouldEmitVulkanRequestedButNotOperationalBreadcrumb"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("MakeFallbackTextureBytes"), std::string::npos);
    EXPECT_EQ(engineImpl.find("BuildFallbackTextureDesc"), std::string::npos);
    EXPECT_EQ(engineImpl.find("CreateVulkanDevice"), std::string::npos);
    EXPECT_EQ(engineImpl.find("CreateNullDevice"), std::string::npos);
    EXPECT_EQ(engineImpl.find("std::make_unique<Graphics::GpuAssetCache>"),
              std::string::npos);

    EXPECT_NE(bootstrap.find("MakeFallbackTextureBytes"), std::string::npos);
    EXPECT_NE(bootstrap.find("BuildFallbackTextureDesc"), std::string::npos);
    EXPECT_NE(bootstrap.find("CreateRuntimeDevice"), std::string::npos);
}

TEST(RuntimeEngineLayering, ObsoleteMeshPrimitiveViewControlsAreDeleted)
{
    const auto root = RepoRoot();
    const auto engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto extractionInterface =
        ReadFile(root / "src/runtime/Runtime.RenderExtraction.cppm");
    const auto extractionInternal =
        ReadFile(
            root /
            "src/runtime/Runtime.RenderExtraction.Internal.cpp");

    EXPECT_FALSE(std::filesystem::exists(
        root /
        "src/runtime/Runtime.MeshPrimitiveViewControls.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root /
        "src/runtime/Runtime.MeshPrimitiveViewControls.cpp"));
    EXPECT_EQ(engineInterface.find("MeshPrimitiveViewSettings"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("MeshPrimitiveViewSettings"),
              std::string::npos);
    EXPECT_EQ(
        extractionInterface.find("MeshPrimitiveViewSettings"),
        std::string::npos);
    EXPECT_EQ(
        extractionInternal.find("MeshPrimitiveViewSettings"),
        std::string::npos);

    EXPECT_EQ(engineImpl.find("import Extrinsic.Graphics.Component.RenderGeometry"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("RenderEdges"), std::string::npos);
    EXPECT_EQ(engineImpl.find("RenderPoints"), std::string::npos);
    EXPECT_EQ(engineImpl.find("ToRenderPointType"), std::string::npos);
    EXPECT_EQ(engineImpl.find("ToMeshVertexViewRenderMode"), std::string::npos);
    EXPECT_EQ(engineImpl.find("entt::registry"), std::string::npos);

}

TEST(RuntimeEngineLayering,
     CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine)
{
    const auto root = RepoRoot();
    const auto engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto cameraModuleInterface = ReadFile(
        root / "src/runtime/Cameras/Runtime.CameraModule.cppm");
    const auto cameraModuleImpl = ReadFile(
        root / "src/runtime/Cameras/Runtime.CameraModule.cpp");
    const auto referenceInterface =
        ReadFile(root / "src/runtime/Runtime.ReferenceScene.cppm");
    const auto referenceImpl =
        ReadFile(root / "src/runtime/Runtime.ReferenceScene.cpp");
    const auto sandbox =
        ReadFile(root / "src/app/Sandbox/Sandbox.cpp");
    const auto sandboxMain  = ReadFile(root / "src/app/Sandbox/main.cpp");
    const auto runtimeCMake = ReadFile(root / "src/runtime/CMakeLists.txt");

    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ReferenceSceneControl.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ReferenceSceneControl.cpp"));
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Runtime.ReferenceScene"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "import Extrinsic.Runtime.ReferenceScene"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Runtime.CameraControllers"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "import Extrinsic.Runtime.CameraControllers"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "import Extrinsic.Graphics.CameraSnapshots"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find(
                  "import Extrinsic.Graphics.CameraSnapshots"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("ReferenceScenePopulation"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("CameraControllerRegistry"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetReferenceSceneRegistry"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetReferenceCameraSeed"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("IsReferenceSceneInstalled"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetCameraControllerRegistry"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("ReferenceScenePopulation"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("CameraControllerRegistry"),
              std::string::npos);

    EXPECT_NE(cameraModuleInterface.find(
                  "class CameraModule final : public IRuntimeModule"),
              std::string::npos);
    EXPECT_NE(cameraModuleInterface.find(
                  "CameraControllerRegistry m_Registry"),
              std::string::npos);
    EXPECT_NE(cameraModuleImpl.find(
                  "Provide<CameraControllerRegistry>("),
              std::string::npos);
    EXPECT_NE(cameraModuleImpl.find(
                  "Withdraw<CameraControllerRegistry>("),
              std::string::npos);
    EXPECT_NE(cameraModuleImpl.find(
                  "setup.Subscribe<ActiveWorldChanged>"),
              std::string::npos);
    EXPECT_NE(cameraModuleImpl.find(
                  "setup.Subscribe<WorldWillBeDestroyed>"),
              std::string::npos);
    EXPECT_NE(cameraModuleImpl.find(
                  "setup.RegisterViewportInputHook("),
              std::string::npos);
    EXPECT_NE(cameraModuleImpl.find(
                  "context.ActiveWorldHandle != m_Registry.BoundWorld()"),
              std::string::npos);

    EXPECT_NE(referenceInterface.find(
                  "BootstrapReferenceScene("),
              std::string::npos);
    EXPECT_NE(referenceInterface.find(
                  "TeardownReferenceScene("),
              std::string::npos);
    EXPECT_EQ(referenceInterface.find("IReferenceSceneProvider"),
              std::string::npos);
    EXPECT_EQ(referenceInterface.find("ReferenceSceneRegistry"),
              std::string::npos);
    EXPECT_EQ(referenceInterface.find("TriangleProvider"),
              std::string::npos);
    EXPECT_EQ(referenceImpl.find("IReferenceSceneProvider"),
              std::string::npos);
    EXPECT_EQ(referenceImpl.find("ReferenceSceneRegistry"),
              std::string::npos);

    EXPECT_NE(sandboxMain.find("engine.EmplaceModule<Extrinsic::Runtime::CameraModule>()"),
              std::string::npos);
    EXPECT_NE(sandbox.find(
                  "Runtime::BootstrapReferenceScene("),
              std::string::npos);
    EXPECT_NE(sandbox.find(
                  "Runtime::TeardownReferenceScene("),
              std::string::npos);
    EXPECT_NE(sandbox.find(
                  "Runtime::WorldHandle World"),
              std::string::npos);
    EXPECT_NE(sandbox.find("services.Find<Runtime::CameraControllerRegistry>()"),
              std::string::npos);

    EXPECT_NE(runtimeCMake.find(
                  "Cameras/Runtime.CameraModule.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find(
                  "Cameras/Runtime.CameraModule.cpp"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.ReferenceSceneControl"),
              std::string::npos);
}

TEST(RuntimeEngineLayering, InputActionsKeepRegistryAndDispatchOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto inputInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.InputActions.cppm");
    const auto inputImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.InputActions.cpp");

    EXPECT_EQ(engineInterface.find("export import Extrinsic.Runtime.InputActions"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.InputActions;"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.InputActions;"),
              std::string::npos);
    EXPECT_NE(WithoutWhitespace(engineImpl).find(
                  "RuntimeInputActionRegistrym_InputActions"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_InputActions.Register(std::move(desc))"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RegisterInputAction("),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("UnregisterInputAction("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_InputActions.DispatchForFrame("),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("struct RuntimeInputActionRecord"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_NextInputActionHandle"), std::string::npos);
    EXPECT_EQ(engineImpl.find("RuntimeInputActionRecord"), std::string::npos);
    EXPECT_EQ(engineImpl.find("m_NextInputActionHandle"), std::string::npos);
    EXPECT_EQ(engineImpl.find("m_InputActions.push_back"), std::string::npos);
    EXPECT_EQ(engineImpl.find("std::erase_if(\n            m_InputActions"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("RuntimeInputActionTriggered"), std::string::npos);
    EXPECT_EQ(frameLoop.find("DispatchRuntimeInputActionsForFrame"),
              std::string::npos);

    EXPECT_NE(inputInterface.find("export module Extrinsic.Runtime.InputActions"),
              std::string::npos);
    EXPECT_NE(inputInterface.find("struct RuntimeInputActionRecord"),
              std::string::npos);
    EXPECT_NE(inputInterface.find("std::vector<RuntimeInputActionRecord> m_Actions"),
              std::string::npos);
    EXPECT_NE(inputInterface.find("std::uint64_t m_NextHandle"),
              std::string::npos);
    EXPECT_NE(inputImpl.find("RuntimeInputActionTriggered"),
              std::string::npos);
    EXPECT_NE(inputImpl.find("std::erase_if("), std::string::npos);
    EXPECT_NE(inputImpl.find("Core::Log::Warn"), std::string::npos);
}

TEST(RuntimeEngineLayering, RuntimeModuleScheduleKeepsRetainedHookPolicyOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto scheduleInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ModuleSchedule.cppm");
    const auto scheduleImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ModuleSchedule.cpp");

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ModuleSchedule"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.ModuleSchedule"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_RuntimeModuleSchedule"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("RuntimeModuleSchedule m_RuntimeModuleSchedule"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RuntimeModuleSchedule.Clear()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RuntimeModuleSchedule.FinalizeForBoot("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("RegisterSimSystemsForTick"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_Impl->m_RuntimeModuleSchedule.RunFrameHooks("),
              std::string::npos);
    EXPECT_NE(engineImpl.find(
                  "m_Impl->m_RuntimeModuleSchedule.RunViewportInputHooks("),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("RuntimeModuleSimSystemRecord"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RuntimeModuleFrameHookRecord"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find(
                  "RuntimeModuleViewportInputHookRecord"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_RuntimeModuleSimSystems"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_RuntimeModuleFrameHooks"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_NextRuntimeModuleRegistrationSequence"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("std::vector<std::vector<std::size_t>> edges"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("waits for unprovided signal"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("Sim system dependency cycle detected"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("record.Desc.Options"), std::string::npos);
    EXPECT_EQ(engineImpl.find("RuntimeFrameHookContext context{"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("for (const RuntimeModuleFrameHookRecord& hook"),
              std::string::npos);

    EXPECT_NE(scheduleInterface.find("export module Extrinsic.Runtime.ModuleSchedule"),
              std::string::npos);
    EXPECT_EQ(scheduleInterface.find("RuntimeModuleSimSystemRecord"),
              std::string::npos);
    EXPECT_NE(scheduleInterface.find("RuntimeModuleFrameHookRecord"),
              std::string::npos);
    EXPECT_NE(scheduleInterface.find(
                  "RuntimeModuleViewportInputHookRecord"),
              std::string::npos);
    EXPECT_EQ(scheduleImpl.find("std::vector<std::vector<std::size_t>> edges"),
              std::string::npos);
    EXPECT_EQ(scheduleImpl.find("waits for unprovided signal"),
              std::string::npos);
    EXPECT_EQ(scheduleImpl.find("Sim system dependency cycle detected"),
              std::string::npos);
    EXPECT_EQ(scheduleImpl.find("context.Graph.AddPass("),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("RuntimeFrameHookContext hookContext"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("for (const RuntimeModuleFrameHookRecord& hook"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find(
                  "RuntimeModuleSchedule::RunViewportInputHooks("),
              std::string::npos);
}

TEST(RuntimeEngineLayering, SelectionReadbackKeepsPickCorrelationCacheOutOfEngine)
{
    const auto root = RepoRoot();
    const auto engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(
            root /
            "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto interactionInterface =
        ReadFile(
            root /
            "src/runtime/Scene/Runtime.SceneInteractionModule.cppm");
    const auto interactionImpl =
        ReadFile(
            root /
            "src/runtime/Scene/Runtime.SceneInteractionModule.cpp");
    const auto sceneDocumentInterface =
        ReadFile(
            root /
            "src/runtime/Scene/Runtime.SceneDocumentModule.cppm");
    const auto sceneDocumentImpl =
        ReadFile(
            root /
            "src/runtime/Scene/Runtime.SceneDocumentModule.cpp");
    const auto readbackInterface =
        ReadFile(root / "src/runtime/Runtime.SelectionReadback.cppm");
    const auto readbackImpl =
        ReadFile(root / "src/runtime/Runtime.SelectionReadback.cpp");

    EXPECT_EQ(engineInterface.find("SelectionReadback"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("SelectionReadback"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GetLastRefinedPrimitive"),
              std::string::npos);
    EXPECT_EQ(sceneDocumentInterface.find("SelectionReadback"),
              std::string::npos);
    EXPECT_EQ(sceneDocumentImpl.find("SelectionReadback"),
              std::string::npos);
    EXPECT_EQ(
        engineImpl.find(
            "RUNTIME-188.EngineInteractionTransition"),
        std::string::npos);

    EXPECT_NE(
        interactionInterface.find(
            "class SceneInteractionModule final : public IRuntimeModule"),
        std::string::npos);
    EXPECT_NE(interactionInterface.find("struct Impl;"),
              std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "SelectionReadbackState Readback{}"),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "Readback.DrainPendingPickForFrame("),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "Readback.DrainCompletedReadbacksForFrame("),
        std::string::npos);
    EXPECT_NE(
        interactionImpl.find(
            "RegisterReplacementParticipant("),
        std::string::npos);

    EXPECT_EQ(engineInterface.find("struct InFlightPickContext"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_InFlightPickContexts"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_LastRefinedPrimitive"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_LastRefinedPrimitiveGeneration"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("BuildPickReadbackContextForFrame"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("RememberPickReadbackContextForFrame"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("DrainPendingSelectionPickForFrame"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("ApplySelectionReadbackToController"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("RefineSelectionReadbackForFrame"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("DrainCompletedSelectionReadbacksForFrame"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("RefinePickReadbackResult(scene"),
              std::string::npos);
    EXPECT_EQ(sceneDocumentInterface.find("LastRefinedPrimitive{}"),
              std::string::npos);
    EXPECT_EQ(sceneDocumentInterface.find("LastRefinedPrimitiveGeneration{}"),
              std::string::npos);

    EXPECT_NE(readbackInterface.find("export module Extrinsic.Runtime.SelectionReadback"),
              std::string::npos);
    EXPECT_NE(readbackInterface.find("struct InFlightPickContext"),
              std::string::npos);
    EXPECT_NE(readbackInterface.find("std::vector<InFlightPickContext> m_InFlightPickContexts"),
              std::string::npos);
    EXPECT_NE(readbackInterface.find("std::optional<PrimitiveSelectionResult> m_LastRefinedPrimitive"),
              std::string::npos);
    EXPECT_NE(readbackImpl.find("BuildPickReadbackContextForFrame"),
              std::string::npos);
    EXPECT_NE(readbackImpl.find("selectionSystem.PopPickResult()"),
              std::string::npos);
    EXPECT_NE(readbackImpl.find("result->Sequence == 0u"),
              std::string::npos);
    EXPECT_NE(readbackImpl.find("context.World != world"),
              std::string::npos);
    EXPECT_NE(
        readbackImpl.find(
            "context.InteractionEpoch != interactionEpoch"),
        std::string::npos);
    EXPECT_NE(readbackImpl.find("RefinePickReadbackResult("),
              std::string::npos);
}

TEST(RuntimeEngineLayering, EditorUiModulePrivatelyMirrorsImGuiFramePacingDiagnostics)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto editorUiModuleInterface =
        ReadFile(RepoRoot() / "src/runtime/Editor/Runtime.EditorUiModule.cppm");
    const auto editorUiModuleImpl =
        ReadFile(RepoRoot() / "src/runtime/Editor/Runtime.EditorUiModule.cpp");
    const auto diagnosticsInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.FramePacingDiagnostics.cppm");
    const auto diagnosticsImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.FramePacingDiagnostics.cpp");

    EXPECT_EQ(engineInterface.find("export import Extrinsic.Runtime.FramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.FramePacingDiagnostics;"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.FramePacingDiagnostics"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("MirrorImGuiFramePacingDiagnostics("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("MirrorRenderGraphFramePacingDiagnostics("),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("export struct RuntimeFramePacingDiagnostics"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("pacing.ImGuiEditorCallbackMicros ="),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("pacing.ImGuiDrawDataCopyMicros ="),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("pacing.RenderGraphCompileMicros ="),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("pacing.RenderGraphExecuteMicros ="),
              std::string::npos);

    EXPECT_NE(diagnosticsInterface.find("export module Extrinsic.Runtime.FramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(diagnosticsInterface.find("export struct RuntimeFramePacingDiagnostics"),
              std::string::npos);
    EXPECT_EQ(diagnosticsInterface.find("import Extrinsic.Runtime.ImGuiAdapter"),
              std::string::npos);
    EXPECT_EQ(diagnosticsInterface.find("MirrorImGuiFramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(diagnosticsInterface.find("MirrorRenderGraphFramePacingDiagnostics"),
              std::string::npos);
    EXPECT_EQ(diagnosticsImpl.find("ImGuiAdapterDiagnostics"),
              std::string::npos);
    EXPECT_EQ(diagnosticsImpl.find("pacing.ImGuiEditorCallbackMicros ="),
              std::string::npos);
    EXPECT_NE(diagnosticsImpl.find("pacing.RenderGraphCompileMicros = stats.Compile.TimeMicros"),
              std::string::npos);

    EXPECT_EQ(editorUiModuleInterface.find("ImGuiAdapter"),
              std::string::npos);
    EXPECT_EQ(editorUiModuleInterface.find("MirrorImGuiDiagnostics"),
              std::string::npos);
    EXPECT_NE(editorUiModuleImpl.find(
                  "import Extrinsic.Runtime.FramePacingDiagnostics;"),
              std::string::npos);
    EXPECT_NE(editorUiModuleImpl.find(
                  "import Extrinsic.Runtime.ImGuiAdapter;"),
              std::string::npos);
    EXPECT_NE(editorUiModuleImpl.find("void MirrorImGuiDiagnostics("),
              std::string::npos);
    EXPECT_NE(editorUiModuleImpl.find(
                  "pacing.ImGuiEditorCallbackMicros ="),
              std::string::npos);
    EXPECT_NE(editorUiModuleImpl.find(
                  "pacing.ImGuiDrawDataCopyMicros ="),
              std::string::npos);
    EXPECT_NE(editorUiModuleImpl.find(
                  "MirrorImGuiDiagnostics(context.Pacing, diagnostics);"),
              std::string::npos);
}

TEST(RuntimeEngineLayering,
     AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence)
{
    const auto root = RepoRoot();
    const auto engineInterface =
        ReadFile(root / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(root / "src/runtime/Runtime.Engine.cpp");
    const auto workflowImpl =
        ReadFile(
            root /
            "src/runtime/Runtime.AssetWorkflowModule.cpp");
    const auto coreFrameLoop =
        ReadFile(root / "src/core/Core.FrameLoop.cpp");
    const auto announcement = SliceBetween(
        engineImpl,
        "void Engine::AnnounceRuntimeShutdown()",
        "void Engine::ShutdownRuntimeModules()");
    const auto moduleShutdown = SliceBetween(
        engineImpl,
        "void Engine::ShutdownRuntimeModules()",
        "void Engine::RefreshActiveWorldScenePointer()");
    const auto beginShutdown =
        SliceBetween(engineImpl, "void Engine::BeginShutdown()", "void Engine::Shutdown()");
    const auto shutdown = SliceBetween(engineImpl, "void Engine::Shutdown()", "// ── Main loop");
    const auto workflowAnnouncement = SliceBetween(
        workflowImpl,
        "void AnnounceShutdown()",
        "AssetImportPipeline Pipeline{}");
    const auto workflowResolve = SliceBetween(
        workflowImpl,
        "Core::Result AssetWorkflowModule::OnResolve(",
        "void AssetWorkflowModule::OnShutdown(");

    constexpr std::string_view removedEngineTokens[] = {
        "EngineSceneReplacementTransitions",
        "BindActiveSceneAssetHandoffs",
        "RegisterSceneReplacementParticipants",
        "CancelActiveAssetImportsForShutdown",
        "AnnounceAndShutdownRuntimeModules",
        "m_SelectionController",
    };
    for (const auto token : removedEngineTokens)
    {
        EXPECT_EQ(engineInterface.find(token), std::string::npos)
            << token;
        EXPECT_EQ(engineImpl.find(token), std::string::npos)
            << token;
    }

    EXPECT_NE(workflowResolve.find(
                  "Require<SceneDocumentModule>"),
              std::string::npos);
    EXPECT_NE(workflowResolve.find(
                  "Require<EditorCommandHistory>"),
              std::string::npos);
    EXPECT_NE(workflowResolve.find(
                  "Find<SelectionController>()"),
              std::string::npos);
    EXPECT_NE(workflowResolve.find(
                  "RegisterReplacementParticipant("),
              std::string::npos);

    const auto cancelImports = workflowAnnouncement.find(
        "Pipeline->CancelActiveAssetImportsForShutdown()");
    const auto detachPipeline =
        workflowAnnouncement.find("DetachPipeline();");
    const auto stopCallbacks =
        workflowAnnouncement.find("AcceptingCallbacks = false;");
    const auto detachScene =
        workflowAnnouncement.find("SceneHandoff.reset();");
    const auto releaseDocument =
        workflowAnnouncement.find("ReleaseDocumentParticipant();");
    const auto clearSelection =
        workflowAnnouncement.find("Selection = nullptr;");
    ASSERT_NE(cancelImports, std::string::npos);
    ASSERT_NE(detachPipeline, std::string::npos);
    ASSERT_NE(stopCallbacks, std::string::npos);
    ASSERT_NE(detachScene, std::string::npos);
    ASSERT_NE(releaseDocument, std::string::npos);
    ASSERT_NE(clearSelection, std::string::npos);
    EXPECT_LT(cancelImports, detachPipeline);
    EXPECT_LT(detachPipeline, stopCallbacks);
    EXPECT_LT(stopCallbacks, detachScene);
    EXPECT_LT(detachScene, releaseDocument);
    EXPECT_LT(releaseDocument, clearSelection);

    const auto markUninitialized =
        announcement.find("m_Impl->m_Initialized = false;");
    const auto publishAnnouncement = announcement.find(
        "m_Impl->m_KernelEvents.Publish(RuntimeShutdownAnnounced{});");
    const auto pumpAnnouncement =
        announcement.find("(void)m_Impl->m_KernelEvents.Pump();");
    ASSERT_NE(markUninitialized, std::string::npos);
    ASSERT_NE(publishAnnouncement, std::string::npos);
    ASSERT_NE(pumpAnnouncement, std::string::npos);
    EXPECT_LT(markUninitialized, publishAnnouncement);
    EXPECT_LT(publishAnnouncement, pumpAnnouncement);

    const auto discardCommands =
        beginShutdown.find("m_Impl->m_CommandBus.DiscardPending();");
    const auto invokeAnnouncement = beginShutdown.find("AnnounceRuntimeShutdown();");
    const auto quiesceParticipants =
        beginShutdown.find("m_Impl->m_JobServiceGpuQueueBridge.ShutdownParticipants(");
    const auto beginShutdownCall = shutdown.find("BeginShutdown();");
    const auto executeShutdown =
        shutdown.find("Core::ExecuteShutdownContract(hooks)");
    ASSERT_NE(discardCommands, std::string::npos);
    ASSERT_NE(invokeAnnouncement, std::string::npos);
    ASSERT_NE(quiesceParticipants, std::string::npos);
    ASSERT_NE(beginShutdownCall, std::string::npos);
    ASSERT_NE(executeShutdown, std::string::npos);
    EXPECT_LT(discardCommands, invokeAnnouncement);
    EXPECT_LT(invokeAnnouncement, quiesceParticipants);
    EXPECT_LT(beginShutdownCall, executeShutdown);
    const auto moduleHook =
        shutdown.find("void ShutdownRuntimeModules() override");
    const auto invokeModuleShutdown =
        shutdown.find("Owner.ShutdownRuntimeModules();", moduleHook);
    ASSERT_NE(moduleHook, std::string::npos);
    ASSERT_NE(invokeModuleShutdown, std::string::npos);
    EXPECT_EQ(shutdown.find("ShutdownApplication"), std::string::npos);
    EXPECT_EQ(coreFrameLoop.find("ShutdownApplication"), std::string::npos);

    const auto reverseModules =
        moduleShutdown.find("m_Impl->m_RuntimeModules.rbegin()");
    const auto resetServices =
        moduleShutdown.find("m_Impl->m_ServiceRegistry.Reset();");
    ASSERT_NE(reverseModules, std::string::npos);
    ASSERT_NE(resetServices, std::string::npos);
    EXPECT_LT(reverseModules, resetServices);

    EXPECT_NE(workflowImpl.find(
                  "setup.Subscribe<RuntimeShutdownAnnounced>"),
              std::string::npos);
    EXPECT_NE(workflowImpl.find(
                  "state->AnnounceShutdown();"),
              std::string::npos);
    EXPECT_NE(workflowImpl.find(
                  "m_Impl->Shared->AnnounceShutdown();"),
              std::string::npos);
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

// RUNTIME-199 — the spatial-debug adapter registry was retired because no
// production workflow ever registered an adapter. This pins the deletion so a
// future change cannot silently resurrect the interface, the opaque ECS
// binding, or the dormant closest-face module. The graphics-side
// `SpatialDebugVisualizers` packet contract is deliberately NOT covered here:
// it survives untouched and its spans stay declared-but-empty until a real
// producer exists.
TEST(RuntimeEngineLayering, RetiredSpatialDebugRegistryHasNoProductionSurface)
{
    const auto moduleInventory =
        ReadFile(RepoRoot() / "docs/api/generated/module_inventory.md");
    const auto runtimeCMake = ReadFile(RepoRoot() / "src/runtime/CMakeLists.txt");
    const auto ecsComponentsCMake =
        ReadFile(RepoRoot() / "src/ecs/Components/CMakeLists.txt");
    const auto extractionInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.RenderExtraction.cppm");
    const auto editorHistoryInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.EditorCommandHistory.cppm");
    const auto sandboxFacadeInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SandboxEditorFacades.cppm");

    // No retired module is published.
    EXPECT_EQ(moduleInventory.find("Extrinsic.Runtime.SpatialDebugAdapters"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("Extrinsic.Runtime.SpatialDebugClosestFace"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("Extrinsic.ECS.Component.SpatialDebugBinding"),
              std::string::npos);

    // No retired translation unit is built.
    EXPECT_EQ(runtimeCMake.find("SpatialDebug"), std::string::npos);
    EXPECT_EQ(ecsComponentsCMake.find("SpatialDebugBinding"), std::string::npos);

    // No adapter interface, opaque key registry, or registration surface.
    EXPECT_EQ(extractionInterface.find("ISpatialDebugAdapter"), std::string::npos);
    EXPECT_EQ(extractionInterface.find("SpatialDebugAdapterRegistry"),
              std::string::npos);
    EXPECT_EQ(extractionInterface.find("RegisterSpatialDebugAdapter"),
              std::string::npos);
    EXPECT_EQ(extractionInterface.find("SpatialDebugBindingsObserved"),
              std::string::npos);

    // No opaque binding survives in the editor command/model surfaces.
    EXPECT_EQ(editorHistoryInterface.find("SpatialDebug"), std::string::npos);
    EXPECT_EQ(sandboxFacadeInterface.find("SpatialDebug"), std::string::npos);

    // The graphics packet contract that RUNTIME-199 preserved is still present.
    const auto rendererInterface =
        ReadFile(RepoRoot() / "src/graphics/renderer/Graphics.Renderer.cppm");
    EXPECT_NE(rendererInterface.find("SpatialDebugBounds"), std::string::npos);
}

// RUNTIME-193 — one general geometry-presentation recipe and snapshot path.
TEST(RuntimeEngineLayering,
     RetiredProgressivePresentationModulesAndSymbolsStayDeleted)
{
    const std::filesystem::path root = RepoRoot();
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ProgressiveRenderData.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ProgressiveRenderData.cpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ProgressivePresentationExtraction.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "src/runtime/Runtime.ProgressivePresentationExtraction.cpp"));

    const std::array<std::string_view, 5u> retiredSymbols{
        "Extrinsic.Runtime.ProgressiveRenderData",
        "Extrinsic.Runtime.ProgressivePresentationExtraction",
        "ProgressivePresentationBindings",
        "ProgressivePresentationSnapshot",
        "BuildProgressivePresentationSnapshot",
    };
    const std::filesystem::path sourceRoot = root / "src";
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(sourceRoot))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".cppm" &&
            extension != ".h" && extension != ".hpp")
        {
            continue;
        }

        const std::string content = ReadFile(entry.path());
        for (const std::string_view retired : retiredSymbols)
        {
            EXPECT_EQ(content.find(retired), std::string::npos)
                << entry.path().string() << " still names " << retired;
        }
    }

    const std::string runtimeCMake =
        ReadFile(root / "src/runtime/CMakeLists.txt");
    EXPECT_EQ(runtimeCMake.find("Runtime.ProgressiveRenderData"),
              std::string::npos);
    EXPECT_EQ(runtimeCMake.find("Runtime.ProgressivePresentationExtraction"),
              std::string::npos);

    const std::string presentation =
        ReadFile(root / "src/runtime/Runtime.GeometryPresentation.cppm");
    EXPECT_NE(presentation.find("struct GeometryPresentationRecipe"),
              std::string::npos);
    EXPECT_NE(presentation.find("struct GeometryPresentationRuntimeState"),
              std::string::npos);
    EXPECT_NE(presentation.find("struct GeometryPresentationSnapshot"),
              std::string::npos);
}

// RUNTIME-192 — one canonical geometry-property vocabulary.
//
// Bake, presentation, visualization, and selected-analysis each used to mirror
// a subset of GeometryElementDomain + Geometry::PropertyValueKind behind its own
// enum, forcing conversion switches and letting otherwise identical property
// identity drift between consumers. This pins the retirement so a new duplicate
// cannot reappear.
TEST(RuntimeEngineLayering, NoDuplicateGeometryPropertyVocabularyRemains)
{
    const std::array<std::filesystem::path, 5> sources{
        RepoRoot() / "src/runtime/Runtime.GeometryPresentation.cppm",
        RepoRoot() / "src/runtime/Runtime.GeometryPresentation.cpp",
        RepoRoot() / "src/runtime/Runtime.SandboxEditorFacades.cppm",
        RepoRoot() / "src/runtime/Runtime.SandboxEditorFacades.cpp",
        RepoRoot() / "src/runtime/Runtime.TextureBakeModule.cpp",
    };

    for (const auto& path : sources)
    {
        const std::string content = ReadFile(path);
        EXPECT_EQ(content.find("ProgressiveGeometryDomain"), std::string::npos)
            << path.string();
        EXPECT_EQ(content.find("ProgressivePropertyValueKind"), std::string::npos)
            << path.string();
        EXPECT_EQ(content.find("SandboxEditorVisualizationPropertyValueKind"),
                  std::string::npos)
            << path.string();
        EXPECT_EQ(content.find("SandboxEditorPropertyCatalogValueKind"),
                  std::string::npos)
            << path.string();
    }

    // The canonical vocabulary and its constraint form live in one place.
    const std::string availability =
        ReadFile(RepoRoot() / "src/runtime/Runtime.GeometryAvailability.cppm");
    EXPECT_NE(availability.find("struct GeometryPropertyRef"), std::string::npos);
    EXPECT_NE(availability.find("GeometryPropertyCatalogSnapshot"),
              std::string::npos);
    EXPECT_NE(availability.find("using GeometryPropertyValueKindFilter"),
              std::string::npos);

    // The retired derived-job-only MeshSurface scope was not promoted into the
    // canonical property-domain vocabulary.
    EXPECT_EQ(availability.find("MeshSurface"), std::string::npos);

    // The persisted wire format keeps its legacy spellings, and they are owned
    // by the serializer rather than derived from the canonical debug names.
    const std::string serialization =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SceneSerialization.cpp");
    EXPECT_NE(serialization.find("\"ScalarFloat\""), std::string::npos);
    EXPECT_NE(serialization.find("\"ScalarDouble\""), std::string::npos);
    EXPECT_NE(serialization.find("\"GraphVertex\""), std::string::npos);
    EXPECT_NE(serialization.find("kPropertyValueKindWire"), std::string::npos);
    EXPECT_NE(serialization.find("kPropertyDomainWire"), std::string::npos);
}

// RUNTIME-197 — one graphics-owned upload-plan/residency lifecycle. Runtime
// keeps typed topology adapters private and exposes only truthful topology and
// authoring value modules.
TEST(RuntimeEngineLayering,
     RetiredGeometryPackerAndLifecycleSurfacesStayDeleted)
{
    const std::filesystem::path root = RepoRoot();
    const std::array<std::filesystem::path, 17u> retiredFiles{
        root / "src/runtime/Runtime.MeshGeometryPacker.cppm",
        root / "src/runtime/Runtime.MeshGeometryPacker.cpp",
        root / "src/runtime/Runtime.GraphGeometryPacker.cppm",
        root / "src/runtime/Runtime.GraphGeometryPacker.cpp",
        root / "src/runtime/Runtime.PointCloudGeometryPacker.cppm",
        root / "src/runtime/Runtime.PointCloudGeometryPacker.cpp",
        root / "src/runtime/Runtime.ProceduralGeometryPacker.cppm",
        root / "src/runtime/Runtime.ProceduralGeometryPacker.cpp",
        root / "src/runtime/Runtime.MeshPrimitiveViewPacker.cppm",
        root / "src/runtime/Runtime.MeshPrimitiveViewPacker.cpp",
        root / "src/runtime/Runtime.ProceduralGeometry.cppm",
        root / "src/runtime/Runtime.ProceduralGeometry.cpp",
        root / "tests/contract/runtime/Test.MeshGeometryPacker.cpp",
        root / "tests/contract/runtime/Test.GraphGeometryPacker.cpp",
        root / "tests/contract/runtime/Test.PointCloudGeometryPacker.cpp",
        root / "tests/contract/runtime/Test.MeshPrimitiveViewPacker.cpp",
        root / "tests/contract/runtime/Test.ProceduralGeometryCache.cpp",
    };
    for (const auto& path : retiredFiles)
        EXPECT_FALSE(std::filesystem::exists(path)) << path.string();

    const std::array<std::string_view, 12u> retiredSymbols{
        "Extrinsic.Runtime.MeshGeometryPacker",
        "Extrinsic.Runtime.GraphGeometryPacker",
        "Extrinsic.Runtime.PointCloudGeometryPacker",
        "Extrinsic.Runtime.ProceduralGeometryPacker",
        "Extrinsic.Runtime.MeshPrimitiveViewPacker",
        "ProceduralGeometryCache",
        "GetProceduralGeometryCacheForTest",
        "TickProceduralGeometry",
        "TickMeshGeometry",
        "TickGraphGeometry",
        "TickPointCloudGeometry",
        "TickMeshPrimitiveViewGeometry",
    };
    const std::filesystem::path runtimeRoot = root / "src/runtime";
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(runtimeRoot))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".cppm")
            continue;

        const std::string content = ReadFile(entry.path());
        for (const std::string_view retired : retiredSymbols)
        {
            EXPECT_EQ(content.find(retired), std::string::npos)
                << entry.path().string() << " still names " << retired;
        }
        EXPECT_EQ(content.find(".UploadGeometry("), std::string::npos)
            << entry.path().string();
        EXPECT_EQ(content.find(".UpdateGeometryChannels("), std::string::npos)
            << entry.path().string();
        EXPECT_EQ(content.find(".FreeGeometry("), std::string::npos)
            << entry.path().string();
    }

    const std::string runtimeCMake =
        ReadFile(root / "src/runtime/CMakeLists.txt");
    const std::size_t privateModules = runtimeCMake.find(
        "FILE_SET render_extraction_impl TYPE CXX_MODULES FILES");
    const std::size_t privateBuilders =
        runtimeCMake.find("Runtime.GeometryPlanBuilders.cppm");
    ASSERT_NE(privateModules, std::string::npos);
    ASSERT_NE(privateBuilders, std::string::npos);
    EXPECT_LT(privateModules, privateBuilders);
    EXPECT_NE(runtimeCMake.find("Runtime.MeshSurfaceTopology.cppm"),
              std::string::npos);
    EXPECT_NE(runtimeCMake.find("Runtime.MeshPrimitiveView.cppm"),
              std::string::npos);

    const std::string extraction =
        ReadFile(root / "src/runtime/Runtime.RenderExtraction.cppm");
    EXPECT_EQ(CountOccurrences(extraction, "void TickGeometryResidency("), 1u);

    const std::string channelBindings =
        ReadFile(root / "src/runtime/Runtime.VertexChannelBindings.cppm");
    EXPECT_NE(channelBindings.find("GeometryPropertyRef Property"),
              std::string::npos);
    EXPECT_EQ(channelBindings.find("AttributeSourceType SourceType"),
              std::string::npos);
    EXPECT_EQ(channelBindings.find("std::string SourceProperty"),
              std::string::npos);
}
