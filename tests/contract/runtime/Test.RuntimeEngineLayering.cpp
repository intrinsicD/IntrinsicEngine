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

TEST(RuntimeEngineLayering, StreamingHookAppliesMainThreadResultsWithFrameBudget)
{
    const auto content =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");

    EXPECT_NE(content.find("static constexpr std::uint32_t kApplyBudgetPerFrame = 8u;"),
              std::string::npos);
    EXPECT_NE(content.find("AsyncWork.ApplyMainThreadResults(kApplyBudgetPerFrame)"),
              std::string::npos);
    EXPECT_EQ(content.find("DerivedJobs->ApplyMainThreadResults"), std::string::npos);
    EXPECT_EQ(content.find("Executor.ApplyMainThreadResults"), std::string::npos);
    EXPECT_EQ(content.find("Executor.ApplyMainThreadResults();"), std::string::npos);
    EXPECT_EQ(content.find("DerivedJobs->ApplyMainThreadResults();"), std::string::npos);
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
    const auto shutdown = SliceBetween(content,
                                       "void Engine::Shutdown()",
                                       "// ── Main loop");
    const auto bridgeInstall = SliceBetween(
        bridge,
        "void JobServiceGpuQueueBridge::Install",
        "void JobServiceGpuQueueBridge::Uninstall");
    const auto bridgeShutdown = SliceBetween(
        bridge,
        "std::uint64_t JobServiceGpuQueueBridge::ShutdownParticipants",
        "bool JobServiceGpuQueueBridge::IsInstalled");

    const auto participantShutdown =
        shutdown.find("m_JobServiceGpuQueueBridge.ShutdownParticipants(");
    const auto executeShutdown = shutdown.find("Core::ExecuteShutdownContract(hooks)");
    const auto installBridge =
        content.find("m_JobServiceGpuQueueBridge.Install(*m_Renderer, m_JobService);");
    const auto installDirectHook =
        bridgeInstall.find("renderer.RegisterRuntimeFrameCommandHook(");
    const auto recordCommands =
        bridgeInstall.find("jobs.RecordGpuQueueFrameCommands(commandContext);");
    const auto detachHook =
        bridgeShutdown.find("Uninstall(renderer);");
    const auto serviceShutdown =
        bridgeShutdown.find("jobs.ShutdownGpuQueueParticipants(", detachHook);
    const auto waitIdle = shutdown.find("m_Device->WaitIdle();", participantShutdown);

    ASSERT_NE(participantShutdown, std::string::npos);
    ASSERT_NE(executeShutdown, std::string::npos);
    ASSERT_NE(installBridge, std::string::npos);
    ASSERT_NE(installDirectHook, std::string::npos);
    ASSERT_NE(recordCommands, std::string::npos);
    ASSERT_NE(detachHook, std::string::npos);
    ASSERT_NE(serviceShutdown, std::string::npos);
    ASSERT_NE(waitIdle, std::string::npos);

    EXPECT_LT(participantShutdown, executeShutdown);
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

TEST(RuntimeEngineLayering, ObjectSpaceNormalBakeServiceKeepsGpuQueueCompositionOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto serviceInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ObjectSpaceNormalBakeService.cppm");
    const auto serviceImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ObjectSpaceNormalBakeService.cpp");
    const auto runtimeCMake = ReadFile(RepoRoot() / "src/runtime/CMakeLists.txt");
    const auto moduleInventory =
        ReadFile(RepoRoot() / "docs/api/generated/module_inventory.md");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeService"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("ObjectSpaceNormalBakeService             m_ObjectSpaceNormalBakeService"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ObjectSpaceNormalBakeService.SetDependencies("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ObjectSpaceNormalBakeService.RegisterGpuQueueParticipant("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("&m_ObjectSpaceNormalBakeService.Queue()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ObjectSpaceNormalBakeService.ClearDependencies()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ObjectSpaceNormalBakeService.QueueDiagnostics()"),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RuntimeObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("RuntimeObjectSpaceNormalBakeGpuQueueDependencies"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("MakeGpuQueueParticipantDesc()"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("GetGlobalFrameNumber() + 1u"),
              std::string::npos);

    EXPECT_NE(serviceInterface.find("export module Extrinsic.Runtime.ObjectSpaceNormalBakeService"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("export import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue"),
              std::string::npos);
    EXPECT_EQ(serviceInterface.find("import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(serviceInterface.find("RuntimeObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("struct Impl;"), std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<Impl> m_Impl"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("struct ObjectSpaceNormalBakeService::Impl"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("MakeGpuQueueParticipantDesc()"),
              std::string::npos);
    const auto readyFrameDefinition =
        serviceImpl.find("ObjectSpaceNormalBakeReadyFrame(");
    ASSERT_NE(readyFrameDefinition, std::string::npos);
    EXPECT_NE(serviceImpl.find("ObjectSpaceNormalBakeReadyFrame(",
                               readyFrameDefinition + 1u),
              std::string::npos);
    EXPECT_EQ(serviceImpl.find("device->GetGlobalFrameNumber() + 1u"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("jobs.RegisterGpuQueueParticipant("),
              std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(
        RepoRoot() / "src/runtime/Runtime.ObjectSpaceNormalBakeGpuQueue.cppm"));
    EXPECT_FALSE(std::filesystem::exists(
        RepoRoot() / "src/runtime/Runtime.ObjectSpaceNormalBakeGpuQueue.cpp"));
    EXPECT_EQ(runtimeCMake.find("Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
    EXPECT_EQ(moduleInventory.find("Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue"),
              std::string::npos);
}

TEST(RuntimeEngineLayering, AssetResidencyServiceKeepsGpuCacheAndModelHandoffsOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto serviceInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AssetResidencyService.cppm");
    const auto serviceImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AssetResidencyService.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.AssetResidencyService"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("AssetResidencyService                    m_AssetResidencyService"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService.InitializeGpuCache("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService.InitializeSceneHandoffs("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService.CachePtr()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService.ModelTextureHandoff()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService.ModelSceneHandoff()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AssetResidencyService.Cache()"),
              std::string::npos);
    EXPECT_NE(frameLoop.find("AssetResidency.TickAssets(AssetService"),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.AssetModelSceneHandoff"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.AssetModelTextureHandoff"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Asset.EventBus"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_GpuAssetCache"), std::string::npos);
    EXPECT_EQ(engineInterface.find("m_GpuAssetCacheListener"), std::string::npos);
    EXPECT_EQ(engineInterface.find("m_AssetModelTextureHandoff"), std::string::npos);
    EXPECT_EQ(engineInterface.find("m_AssetModelSceneHandoff"), std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.AssetModelSceneHandoff"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.AssetModelTextureHandoff"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Asset.EventBus"), std::string::npos);
    EXPECT_EQ(engineImpl.find("std::make_unique<Graphics::GpuAssetCache>"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("std::make_unique<AssetModelTextureHandoff>"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("std::make_unique<AssetModelSceneHandoff>"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("SubscribeAll("), std::string::npos);
    EXPECT_EQ(engineImpl.find("NotifyFailed(id)"), std::string::npos);
    EXPECT_EQ(engineImpl.find("InitializeRuntimeGpuAssetFallbackTexture("),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("AssetModelSceneHandoff*"), std::string::npos);

    EXPECT_NE(serviceInterface.find("export module Extrinsic.Runtime.AssetResidencyService"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<Graphics::GpuAssetCache> m_GpuAssetCache"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("m_GpuAssetCacheListener"), std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<AssetModelTextureHandoff> m_AssetModelTextureHandoff"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<AssetModelSceneHandoff> m_AssetModelSceneHandoff"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("std::make_unique<Graphics::GpuAssetCache>"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("InitializeRuntimeGpuAssetFallbackTexture(*m_GpuAssetCache"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("assets.SubscribeAll("), std::string::npos);
    EXPECT_NE(serviceImpl.find("cache->NotifyFailed(id)"), std::string::npos);
    EXPECT_NE(serviceImpl.find("std::make_unique<AssetModelTextureHandoff>"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("std::make_unique<AssetModelSceneHandoff>"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("ResolvePendingMaterialTextureBindings()"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("assets->UnsubscribeAll(m_GpuAssetCacheListener)"),
              std::string::npos);
}

TEST(RuntimeEngineLayering, GizmoFrameServiceKeepsInteractionStateOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto serviceInterface =
        ReadFile(RepoRoot() / "src/runtime/Gizmos/Runtime.GizmoFrameService.cppm");
    const auto serviceImpl =
        ReadFile(RepoRoot() / "src/runtime/Gizmos/Runtime.GizmoFrameService.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.GizmoFrameService"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("GizmoFrameService                    m_GizmoFrameService"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_GizmoFrameService.DriveInputForFrame("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_GizmoFrameService.BuildRenderPackets("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_GizmoFrameService.Interaction()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_GizmoFrameService.UndoStack()"),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.GizmoInteraction"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GizmoInteraction                      m_GizmoInteraction"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("GizmoUndoStack                        m_GizmoUndoStack"),
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

TEST(RuntimeEngineLayering, RenderExtractionServiceKeepsCachePoolAndStatsOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl = ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto serviceInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.RenderExtractionService.cppm");
    const auto serviceImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.RenderExtractionService.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.RenderExtractionService"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("RenderExtractionService               m_RenderExtractionService"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.ConfigurePool("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.Cache()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.Pool()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.ConsumeFrameIndex()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.PublishLastStats("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RenderExtractionService.ReleaseFrontSlot("),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.RenderExtraction;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.RenderWorldPool;"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.RenderExtraction;"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.RenderWorldPool;"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RenderExtractionCache                 m_RenderExtraction"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("std::unique_ptr<RenderWorldPool>      m_RenderWorldPool"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RuntimeRenderExtractionStats          m_LastExtractionStats"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("std::uint64_t                         m_FrameIndex"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderExtraction.Shutdown("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderExtraction.GetMaterialTextureAssetBindings("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderExtraction.SetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderExtraction.ClearVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderExtraction.GetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_RenderWorldPool->ReleaseFront("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_LastExtractionStats ="),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_FrameIndex++"),
              std::string::npos);

    EXPECT_NE(serviceInterface.find("export module Extrinsic.Runtime.RenderExtractionService"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("export import Extrinsic.Runtime.RenderExtraction"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("export import Extrinsic.Runtime.RenderWorldPool"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("RenderExtractionCache m_Cache"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<RenderWorldPool> m_Pool"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("RuntimeRenderExtractionStats m_LastStats"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::uint64_t m_FrameIndex"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_Cache.Shutdown(renderer)"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_Cache.SetVisualizationAdapterBinding("),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("RenderWorldPool::kDefaultBuffers"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_Pool->ReleaseFront(slot)"),
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
    EXPECT_NE(content.find("frameContext.FrameIndex = m_RenderExtractionService.ConsumeFrameIndex();"),
              std::string::npos);
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
    const auto content =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");

    const auto simTick = content.find("application.OnSimTick(engine, fixedDt);");
    const auto bundleRegistration = content.find(
        "RegisterPromotedEcsSystemBundle(frameGraph, scene)");
    const auto compile = content.find("frameGraph.Compile()");
    const auto bundleImport =
        engineImpl.find("import Extrinsic.Runtime.EcsSystemBundle");

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

TEST(RuntimeEngineLayering, AsyncWorkServiceKeepsStreamingAndDerivedJobOwnershipOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto serviceInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AsyncWorkService.cppm");
    const auto serviceImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AsyncWorkService.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.AsyncWorkService"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("AsyncWorkService                        m_AsyncWorkService"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AsyncWorkService.Initialize()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AsyncWorkService.Streaming()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("AsyncWork.ShutdownAndDrain()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("AsyncWork.Reset()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AsyncWorkService.SubmitDerivedJob("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AsyncWorkService.CancelDerivedJob("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_AsyncWorkService.SnapshotDerivedJobs()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.AsyncWorkService"),
              std::string::npos);
    EXPECT_NE(frameLoop.find("AsyncWork.DrainCompletions()"),
              std::string::npos);
    EXPECT_NE(frameLoop.find("AsyncWork.PumpBackground(maxLaunches)"),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.StreamingExecutor"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_StreamingExecutor"), std::string::npos);
    EXPECT_EQ(engineInterface.find("m_DerivedJobRegistry"), std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.StreamingExecutor"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("std::make_unique<StreamingExecutor>"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("std::make_unique<DerivedJobRegistry>"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_DerivedJobRegistry->Submit("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_DerivedJobRegistry->Cancel("),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("m_DerivedJobRegistry->SnapshotAll()"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.Runtime.StreamingExecutor"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_EQ(frameLoop.find("StreamingExecutor&"), std::string::npos);
    EXPECT_EQ(frameLoop.find("DerivedJobRegistry*"), std::string::npos);
    EXPECT_EQ(frameLoop.find("DerivedJobs->"), std::string::npos);
    EXPECT_EQ(frameLoop.find("Executor."), std::string::npos);

    EXPECT_NE(serviceInterface.find("export module Extrinsic.Runtime.AsyncWorkService"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("export import Extrinsic.Runtime.DerivedJobGraph"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("export import Extrinsic.Runtime.StreamingExecutor"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<StreamingExecutor> m_StreamingExecutor"),
              std::string::npos);
    EXPECT_NE(serviceInterface.find("std::unique_ptr<DerivedJobRegistry> m_DerivedJobRegistry"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("std::make_unique<StreamingExecutor>"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("std::make_unique<DerivedJobRegistry>(*m_StreamingExecutor)"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_DerivedJobRegistry->DrainReadbacks()"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_DerivedJobRegistry->ApplyMainThreadResults(maxApplyCount)"),
              std::string::npos);
    EXPECT_NE(serviceImpl.find("m_StreamingExecutor->ShutdownAndDrain()"),
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
    const auto bootstrap =
        ReadFile(RepoRoot() / "src/runtime/Runtime.DeviceBootstrap.cpp");
    const auto residency =
        ReadFile(RepoRoot() / "src/runtime/Runtime.AssetResidencyService.cpp");

    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.DeviceBootstrap"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("CreateRuntimeDevice(m_Config.Render)"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("InitializeRuntimeGpuAssetFallbackTexture("),
              std::string::npos);
    EXPECT_NE(residency.find("InitializeRuntimeGpuAssetFallbackTexture("),
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

    EXPECT_NE(bootstrap.find("MakeFallbackTextureBytes"), std::string::npos);
    EXPECT_NE(bootstrap.find("BuildFallbackTextureDesc"), std::string::npos);
    EXPECT_NE(bootstrap.find("CreateRuntimeDevice"), std::string::npos);
}

TEST(RuntimeEngineLayering, MeshPrimitiveViewControlsKeepRenderComponentPolicyOutOfEngine)
{
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto controls =
        ReadFile(RepoRoot() / "src/runtime/Runtime.MeshPrimitiveViewControls.cpp");

    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.MeshPrimitiveViewControls"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("ApplyMeshPrimitiveViewSettings(*m_Scene"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("ReadMeshPrimitiveViewSettings(*m_Scene"),
              std::string::npos);

    EXPECT_EQ(engineImpl.find("import Extrinsic.Graphics.Component.RenderGeometry"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("RenderEdges"), std::string::npos);
    EXPECT_EQ(engineImpl.find("RenderPoints"), std::string::npos);
    EXPECT_EQ(engineImpl.find("ToRenderPointType"), std::string::npos);
    EXPECT_EQ(engineImpl.find("ToMeshVertexViewRenderMode"), std::string::npos);
    EXPECT_EQ(engineImpl.find("entt::registry"), std::string::npos);

    EXPECT_NE(controls.find("import Extrinsic.Graphics.Component.RenderGeometry"),
              std::string::npos);
    EXPECT_NE(controls.find("RenderEdges"), std::string::npos);
    EXPECT_NE(controls.find("RenderPoints"), std::string::npos);
}

TEST(RuntimeEngineLayering, ReferenceSceneControlKeepsProviderLifecycleOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto controlInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ReferenceSceneControl.cppm");
    const auto controlImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ReferenceSceneControl.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.ReferenceSceneControl"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.ReferenceSceneControl"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_ReferenceSceneControl.InstallIfEnabled("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("ReferenceScene.TeardownIfInstalled("),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("ReferenceScenePopulation"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_ReferenceCamera"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("m_ReferenceSceneInstalled"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("IReferenceSceneProvider"), std::string::npos);
    EXPECT_EQ(engineImpl.find("RegisterDefaultReferenceProvidersIfAbsent"),
              std::string::npos);
    EXPECT_EQ(engineImpl.find("ReferenceScenePopulation"), std::string::npos);
    EXPECT_EQ(engineImpl.find("provider.Populate"), std::string::npos);
    EXPECT_EQ(engineImpl.find("provider->Teardown"), std::string::npos);

    EXPECT_NE(controlInterface.find("ReferenceScenePopulation m_Population"),
              std::string::npos);
    EXPECT_NE(controlImpl.find("RegisterDefaultReferenceProvidersIfAbsent"),
              std::string::npos);
    EXPECT_NE(controlImpl.find("IReferenceSceneProvider"), std::string::npos);
    EXPECT_NE(controlImpl.find("provider.Populate"), std::string::npos);
    EXPECT_NE(controlImpl.find("provider->Teardown"), std::string::npos);
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

    EXPECT_NE(engineInterface.find("export import Extrinsic.Runtime.InputActions"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("RuntimeInputActionRegistry            m_InputActions"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_InputActions.Register(std::move(desc))"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_InputActions.DispatchForFrame("),
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

TEST(RuntimeEngineLayering, RuntimeModuleScheduleKeepsContributionPolicyOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto scheduleInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ModuleSchedule.cppm");
    const auto scheduleImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.ModuleSchedule.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.ModuleSchedule"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("RuntimeModuleSchedule                  m_RuntimeModuleSchedule"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RuntimeModuleSchedule.Clear()"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RuntimeModuleSchedule.FinalizeForBoot("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RuntimeModuleSchedule.RegisterSimSystemsForTick("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_RuntimeModuleSchedule.RunFrameHooks("),
              std::string::npos);

    EXPECT_EQ(engineInterface.find("RuntimeModuleSimSystemRecord"),
              std::string::npos);
    EXPECT_EQ(engineInterface.find("RuntimeModuleFrameHookRecord"),
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
    EXPECT_NE(scheduleInterface.find("RuntimeModuleSimSystemRecord"),
              std::string::npos);
    EXPECT_NE(scheduleInterface.find("RuntimeModuleFrameHookRecord"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("std::vector<std::vector<std::size_t>> edges"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("waits for unprovided signal"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("Sim system dependency cycle detected"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("context.Graph.AddPass("),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("RuntimeFrameHookContext hookContext"),
              std::string::npos);
    EXPECT_NE(scheduleImpl.find("for (const RuntimeModuleFrameHookRecord& hook"),
              std::string::npos);
}

TEST(RuntimeEngineLayering, SelectionReadbackKeepsPickCorrelationCacheOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto sceneDocumentInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SceneDocument.cppm");
    const auto sceneDocumentImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SceneDocument.cpp");
    const auto readbackInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SelectionReadback.cppm");
    const auto readbackImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.SelectionReadback.cpp");

    EXPECT_NE(engineInterface.find("import Extrinsic.Runtime.SelectionReadback"),
              std::string::npos);
    EXPECT_NE(engineInterface.find("SelectionReadbackState                  m_SelectionReadback"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_SelectionReadback.DrainPendingPickForFrame("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_SelectionReadback.DrainCompletedReadbacksForFrame("),
              std::string::npos);
    EXPECT_NE(engineImpl.find("m_SelectionReadback.LastRefinedPrimitive()"),
              std::string::npos);
    EXPECT_NE(sceneDocumentInterface.find("SelectionReadbackState* SelectionReadback"),
              std::string::npos);
    EXPECT_NE(sceneDocumentImpl.find("SelectionReadback->ClearRefinedPrimitiveCache()"),
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
    EXPECT_NE(readbackImpl.find("ApplySelectionReadbackToController"),
              std::string::npos);
    EXPECT_NE(readbackImpl.find("selectionSystem.PopPickResult()"),
              std::string::npos);
    EXPECT_NE(readbackImpl.find("RefinePickReadbackResult(scene"),
              std::string::npos);
}

TEST(RuntimeEngineLayering, FramePacingDiagnosticsKeepsCounterMirroringOutOfEngine)
{
    const auto engineInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cppm");
    const auto engineImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.cpp");
    const auto frameLoop =
        ReadFile(RepoRoot() / "src/runtime/Runtime.Engine.FrameLoop.Internal.hpp");
    const auto diagnosticsInterface =
        ReadFile(RepoRoot() / "src/runtime/Runtime.FramePacingDiagnostics.cppm");
    const auto diagnosticsImpl =
        ReadFile(RepoRoot() / "src/runtime/Runtime.FramePacingDiagnostics.cpp");

    EXPECT_NE(engineInterface.find("export import Extrinsic.Runtime.FramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("import Extrinsic.Runtime.FramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(engineImpl.find("MirrorImGuiFramePacingDiagnostics("),
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
    EXPECT_NE(diagnosticsInterface.find("MirrorImGuiFramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(diagnosticsInterface.find("MirrorRenderGraphFramePacingDiagnostics"),
              std::string::npos);
    EXPECT_NE(diagnosticsImpl.find("pacing.ImGuiEditorCallbackMicros = imgui.LastEditorCallbackMicros"),
              std::string::npos);
    EXPECT_NE(diagnosticsImpl.find("pacing.RenderGraphCompileMicros = stats.Compile.TimeMicros"),
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
