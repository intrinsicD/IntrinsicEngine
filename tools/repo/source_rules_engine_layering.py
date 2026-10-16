"""RuntimeEngineLayering source rules: Engine stays a thin composition root; retired surfaces stay gone."""
from check_source_rules import Rule

SUITE = "RuntimeEngineLayering"
ENGINE = "src/runtime/Kernel/Runtime.Engine.cpp"
ENGINE_INTERFACE = "src/runtime/Kernel/Runtime.Engine.cppm"
FRAME_LOOP = "src/runtime/Kernel/Runtime.Engine.FrameLoop.Internal.hpp"
RUNTIME_CMAKE = "src/runtime/CMakeLists.txt"
TESTS_CMAKE = "tests/CMakeLists.txt"
INVENTORY = "docs/api/generated/module_inventory.md"
SANDBOX_MAIN = "src/app/Sandbox/main.cpp"
WORKFLOW = "src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cpp"
INTERACTION_INTERFACE = "src/runtime/Scene/Runtime.SceneInteractionModule.cppm"
INTERACTION = "src/runtime/Scene/Runtime.SceneInteractionModule.cpp"
OPERATIONS = "src/runtime/Editor/Operations/"
SCAN_EXTS = (".cpp", ".cppm", ".h", ".hpp")

RUN_FRAME = ("void Engine::RunFrame()", "bool Engine::IsRunning() const noexcept")
BEGIN_SHUTDOWN = ("void Engine::BeginShutdown()", "void Engine::Shutdown()")
SHUTDOWN = ("void Engine::Shutdown()", "// ── Main loop")

BEGIN_FRAME = "m_Impl->m_Renderer->BeginFrame(frame)"
COLLECT_TRANSFERS = "m_Impl->m_Device->GetTransferQueue().CollectCompleted()"
ASSET_MAINTENANCE = "assetWorkflow->RunFrameMaintenance()"
DRAIN_COMPLETIONS = "m_Impl->m_JobService.DrainCompletions("
QUIESCE_PARTICIPANTS = "m_Impl->m_JobService.ShutdownGpuQueueParticipants("
RENDERER_PHASES = (BEGIN_FRAME, "m_Impl->m_Renderer->ExtractRenderWorld(",
                   "m_Impl->m_Renderer->PrepareFrame(renderWorld)",
                   "m_Impl->m_Renderer->ExecuteFrame(frame, renderWorld)",
                   "m_Impl->m_Renderer->EndFrame(frame)")


def rule(name: str, **fields) -> Rule:
    return Rule(SUITE, name, **fields)


def basenames(paths: tuple[str, ...]) -> tuple[str, ...]:
    return tuple(path.rsplit("/", 1)[-1] for path in paths)


def module_pair(stem: str) -> tuple[str, str]:
    return (f"{stem}.cppm", f"{stem}.cpp")


TEXTURE_BAKE_RETIRED = tuple(
    path for stem in (
        "src/runtime/Runtime.SelectedMeshTextureBake", "src/runtime/Runtime.MeshAttributeTextureBake",
        "src/runtime/Runtime.ObjectSpaceNormalBakeBinding", "src/runtime/Runtime.ObjectSpaceNormalBakeQueue",
        "src/runtime/Runtime.ObjectSpaceNormalBakeService", "src/runtime/Runtime.ObjectSpaceNormalBakeSubmission",
        "src/graphics/renderer/Graphics.ObjectSpaceNormalTextureBake")
    for path in module_pair(stem))
GIZMO_FRAME_SERVICE_RETIRED = module_pair("src/runtime/Gizmos/Runtime.GizmoFrameService")
# UI-078: the editor frontend (ImGuizmo, app-only) drives the session; runtime has no
# automatic mouse driver and builds no gizmo packets.
GIZMO_DRIVER_RETIRED = ("DriveGizmoInteractionForFrame", "GizmoPacketBuilder",
                        "TransformGizmoRenderPacketBuilder", "GizmoSelectedEntities",
                        "HitTest(", "DragTick(", "BeginDrag(", "ImGuizmo")
HISTORY_RETIRED = ("GizmoUndoStack", "CommandHistoryHook", "CommandHistoryRecord", "SetHistoryHook",
                   "RecordInverse", "MakeTransformEditCommand", "MakeVisualizationConfigCommand",
                   "MakePrimitiveViewSettingsCommand", "MakeSpatialDebugBindingCommand")
HISTORY_ADAPTERS_RETIRED = ("EditorTransformEditCommand", "EditorVisualizationConfigCommand",
                            "EditorPrimitiveViewSettingsCommand", "EditorSpatialDebugBindingCommand")
HISTORY_OWNER = "src/runtime/Editor/Runtime.EditorCommandHistory.cpp"  # prefix also covers .cppm
MUTATION_HELPER = "src/runtime/Editor/internal/Runtime.EditorMutation.Internal.hpp"
COMPOSITION_HELPERS_RETIRED = tuple(
    path for stem in ("Runtime.ModuleSchedule", "Runtime.EcsSystemBundle", "Runtime.JobServiceGpuQueueBridge")
    for path in module_pair(f"src/runtime/{stem}"))
SELECTION_READBACK_RETIRED = module_pair("src/runtime/Runtime.SelectionReadback")
PACKERS_RETIRED = tuple(
    path for stem in ("MeshGeometryPacker", "GraphGeometryPacker", "PointCloudGeometryPacker",
                      "ProceduralGeometryPacker", "MeshPrimitiveViewPacker", "ProceduralGeometry")
    for path in module_pair(f"src/runtime/Runtime.{stem}")) + tuple(
    f"tests/contract/runtime/Test.{name}.cpp" for name in (
        "MeshGeometryPacker", "GraphGeometryPacker", "PointCloudGeometryPacker",
        "MeshPrimitiveViewPacker", "ProceduralGeometryCache"))

RULES: list[Rule] = [
    # Runtime orchestrates renderer phases, not GPU barriers/resources.
    rule("RunFrameDoesNotUseGpuResourceOrPassLevelDetails", files=(ENGINE,), slice=RUN_FRAME,
         require=RENDERER_PHASES,
         forbid=("TextureBarrier", "BufferBarrier", "TextureUsage::", "BufferUsage::", "VkImage",
                 "VkBuffer", "vkCmd")),
    rule("RunFrameDoesNotUseGpuResourceOrPassLevelDetails/imports", files=(ENGINE,),
         forbid=("import Extrinsic.Core.FrameLoop",)),

    rule("ProductionSourceHasNoApplicationCallbackLifecycle", trees=("src",), exts=SCAN_EXTS,
         forbid=("IApplication", "OnSimTick", "OnVariableTick")),
    rule("ProductionSourceHasNoApplicationCallbackLifecycle/app", trees=("src/app",), exts=SCAN_EXTS,
         forbid=("Runtime::Engine&", "Runtime::Engine &")),

    rule("RunFrameOwnsDocumentedBroadPhaseOrder", files=(ENGINE,), slice=RUN_FRAME,
         first_order=("RuntimeFrameContext frameContext{};", "m_Impl->m_Window->PollEvents()",
                      "RunFixedStepSimulationTicks(", "FramePhase::Simulation", "FramePhase::UiBuild",
                      BEGIN_FRAME, "m_Impl->m_Device->Present(frame);", COLLECT_TRANSFERS)),
    # The last clock EndFrame (rfind) follows maintenance; the transfer collect is unique in the file.
    rule("RunFrameOwnsDocumentedBroadPhaseOrder/clock-end", files=(ENGINE,),
         slice=(COLLECT_TRANSFERS, RUN_FRAME[1]), require=("m_Impl->m_FrameClock.EndFrame();",)),

    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired/engine",
         files=(ENGINE_INTERFACE, ENGINE), forbid=("PhysicsModule",)),
    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired/module-interface",
         files=("src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cppm",),
         require=("PhysicsModule final : public IRuntimeModule",),
         forbid=("PhysicsService", "GetWorld", "BodyHandle")),
    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired/module-impl",
         files=("src/runtime/Modules/PhysicsIntegration/Runtime.PhysicsModule.cpp",),
         require=("import Extrinsic.Physics.World",), forbid=("Provide<PhysicsModule>",)),
    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired", files=(RUNTIME_CMAKE,),
         require=module_pair("Runtime.PhysicsModule"), forbid=("PhysicsBridge",),
         absent=module_pair("src/runtime/Runtime.PhysicsBridge")),
    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired/sandbox", files=(SANDBOX_MAIN,),
         require=("import Extrinsic.Runtime.PhysicsModule", "engine.AddModule(std::move(physicsModule))")),
    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired/sandbox-config",
         files=("src/app/Sandbox/Sandbox.ConfigSections.cpp",),
         require=("MakePhysicsModuleConfigSectionRegistration",)),
    rule("PhysicsModuleIsAppComposedAndThePublicBridgeSurfaceIsRetired/sources", trees=("src", "tests"),
         exts=SCAN_EXTS, forbid=("import Extrinsic.Runtime.PhysicsBridge",)),

    rule("RunFrameAppliesJobCompletionsWithBoundedBudget", files=(ENGINE,),
         require=("constexpr std::uint32_t kJobCompletionApplyBudgetPerFrame = 8u;",
                  # Readiness gates have their own per-drain budget (UI-073 review follow-up).
                  "constexpr std::uint32_t kJobCompletionGateCheckBudgetPerFrame = 32u;")),
    rule("RunFrameAppliesJobCompletionsWithBoundedBudget/run-frame", files=(ENGINE,), slice=RUN_FRAME,
         count=((DRAIN_COMPLETIONS, 1),),
         require=("kJobCompletionApplyBudgetPerFrame", "kJobCompletionGateCheckBudgetPerFrame")),

    # The two DrainCompletions sites are mutually exclusive: RunFrame returns right after the
    # minimized path, so a frame never drains completions twice.
    rule("MinimizedFrameWorkKeepsTheFrameOrderAndExcludesTheFullFrame", files=(ENGINE,),
         slice=("void Engine::RunMinimizedFrameWork(", "void Engine::RunFrame()"),
         count=((DRAIN_COMPLETIONS, 1),),
         require=("kJobCompletionApplyBudgetPerFrame", "kJobCompletionGateCheckBudgetPerFrame"),
         order=("m_Impl->m_CommandBus.Drain(", "m_Impl->m_KernelEvents.Pump()",
                "RunRuntimeModuleFrameHooks(FramePhase::Idle", DRAIN_COMPLETIONS,
                "m_Impl->m_KernelEvents.Pump()", "m_Impl->m_JobService.ReapCompleted()"),
         # Nothing of the presented frame leaks into the minimized path.
         forbid=("RunFixedStepSimulationTicks", "ExtractAndSubmit", "BeginFrame(", "FramePhase::UiBuild")),
    # The minimized path returns before the full frame starts.
    rule("MinimizedFrameWorkKeepsTheFrameOrderAndExcludesTheFullFrame/returns-first", files=(ENGINE,),
         slice=RUN_FRAME, first_order=("RunMinimizedFrameWork(", "RunFixedStepSimulationTicks(")),
    # The call is unique in the file; the slice also requires its `return;`.
    rule("MinimizedFrameWorkKeepsTheFrameOrderAndExcludesTheFullFrame/no-fixed-step-before-return",
         files=(ENGINE,), slice=("RunMinimizedFrameWork(frameContext", "return;"),
         forbid=("RunFixedStepSimulationTicks(",)),

    rule("RunFrameStopsAfterPlatformCloseBeforeRenderer", files=(ENGINE,), slice=RUN_FRAME,
         first_order=("m_Impl->m_Window->PollEvents()", "if (platformShouldClose)", BEGIN_FRAME)),
    # `if (platformShouldClose)` occurs once in the file; its branch requests exit and returns
    # before any renderer BeginFrame.
    rule("RunFrameStopsAfterPlatformCloseBeforeRenderer/close-branch", files=(ENGINE,),
         slice=("if (platformShouldClose)", "return;"),
         require=("RequestExitFromWindowClose(",), forbid=(BEGIN_FRAME,)),

    rule("EnginePrivatelyOwnsGpuQueueHookLifecycle", files=(ENGINE,), slice=BEGIN_SHUTDOWN,
         order=("m_Impl->m_Renderer->UnregisterRuntimeFrameCommandHook(",
                "m_Impl->m_JobServiceGpuQueueHook = {};", QUIESCE_PARTICIPANTS,
                "m_Impl->m_Device->WaitIdle();")),
    rule("EnginePrivatelyOwnsGpuQueueHookLifecycle/shutdown", files=(ENGINE,), slice=SHUTDOWN,
         first_order=("BeginShutdown();", "ShutdownRuntimeModules();")),
    rule("EnginePrivatelyOwnsGpuQueueHookLifecycle/install", files=(ENGINE,),
         order=("m_Impl->m_Renderer->RegisterRuntimeFrameCommandHook(",
                "m_Impl->m_JobService.RecordGpuQueueFrameCommands("),
         forbid=("RegisterRuntimeGpuJobParticipant",)),
    rule("EnginePrivatelyOwnsGpuQueueHookLifecycle/interface", files=(ENGINE_INTERFACE,),
         forbid=("RuntimeFrameCommandHookHandle", "JobServiceGpuQueueHook", "RuntimeGpuJobParticipant")),

    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("ObjectSpaceNormalBakeService", "import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue",
                 "import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;",
                 "export import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;",
                 "RuntimeObjectSpaceNormalBakeGpuQueue")),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/engine", files=(ENGINE,),
         forbid=("ObjectSpaceNormalBakeService", "import Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue",
                 "import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;",
                 "RuntimeObjectSpaceNormalBakeGpuQueueDependencies", "MakeGpuQueueParticipantDesc()",
                 "GetGlobalFrameNumber() + 1u")),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/workflow", files=(WORKFLOW,),
         require=("import Extrinsic.Runtime.TextureBakeModule;",),
         forbid=("import Extrinsic.Runtime.ObjectSpaceNormalBakeService;", "TextureBake->ProducerContext()",
                 ".ObjectSpaceNormalBakeQueue =", "&Bake->Queue()"),
         count=((".TextureBake = TextureBake", 2),)),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/workflow-interface",
         files=("src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cppm",),
         require=("export module Extrinsic.Runtime.AssetWorkflowModule",),
         forbid=("ObjectSpaceNormalBakeService",)),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/module",
         files=("src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp",),
         require=("Provide<TextureBakeService>",),
         forbid=("import Extrinsic.Runtime.ObjectSpaceNormalBakeService;", "ObjectSpaceNormalBakeService Bake{}",
                 "state.Bake.SetDependencies(", "state.Bake.RegisterGpuQueueParticipant(",
                 "Bake.ClearDependencies()", "Bake.Queue().Clear()", "Provide<ObjectSpaceNormalBakeService>",
                 "QueueDiagnostics()"),
         count=(("RegisterGpuQueueParticipant(setup.Jobs())", 1),)),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/module-interface",
         files=("src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cppm",),
         require=("export module Extrinsic.Runtime.TextureBakeModule", "class TextureBakeService",
                  "class TextureBakeModule final"),
         forbid=("RuntimeObjectSpaceNormalBakeQueue", "TextureBakeProducerContext")),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition",
         files=(RUNTIME_CMAKE, "src/graphics/renderer/CMakeLists.txt"),
         forbid=basenames(TEXTURE_BAKE_RETIRED), absent=TEXTURE_BAKE_RETIRED),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/cmake", files=(RUNTIME_CMAKE,),
         require=module_pair("Runtime.TextureBakeModule")),
    rule("TextureBakeModuleOwnsSingleCanonicalGpuBakeComposition/inventory", files=(INVENTORY,),
         require=("Extrinsic.Runtime.TextureBakeModule`",),
         forbid=("ObjectSpaceNormalBake", "MeshAttributeTextureBake", "SelectedMeshTextureBake")),

    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("GizmoFrameService", "import Extrinsic.Runtime.GizmoInteraction", "GizmoInteraction",
                 "GizmoUndoStack", "TransformGizmoRenderPacketBuilder", "m_GizmoSelectedEntities")),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/engine", files=(ENGINE,),
         forbid=("GizmoFrameService", "import Extrinsic.Runtime.GizmoInteraction",
                 "DriveGizmoAndSelectionInputForFrame", "m_GizmoPacketBuilder.Build(",
                 "m_GizmoInteraction.Mode()", "m_GizmoSelectedEntities")),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/frame-loop", files=(FRAME_LOOP,),
         forbid=("import Extrinsic.Runtime.GizmoInteraction", "DriveGizmoInteractionForFrame")),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/interaction-interface", files=(INTERACTION_INTERFACE,),
         require=("class SceneInteractionModule final : public IRuntimeModule", "struct Impl;"),
         forbid=GIZMO_DRIVER_RETIRED + ("UndoStack",)),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/interaction", files=(INTERACTION,),
         require=("GizmoInteraction Gizmo{}", "Gizmo.DragCancel(*BoundRegistry)", "Gizmo = GizmoInteraction{}"),
         forbid=("import Extrinsic.Runtime.GizmoFrameService;",) + GIZMO_DRIVER_RETIRED),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/gizmo-interface",
         files=("src/runtime/Gizmos/Runtime.GizmoInteraction.cppm",),
         forbid=GIZMO_DRIVER_RETIRED + ("GizmoUndoStack",)),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/gizmo",
         files=("src/runtime/Gizmos/Runtime.GizmoInteraction.cpp",), forbid=GIZMO_DRIVER_RETIRED),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/editor-shell",
         files=("src/app/Sandbox/Editor/Sandbox.EditorShell.cpp",),
         require=("#include <ImGuizmo.h>", "BeginGizmoDrag(")),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/unified-clear", files=(INTERACTION,),
         slice=("void ClearWorldBoundState()", "void BindTo("),
         first_order=("Gizmo = GizmoInteraction{}", "Selection.ClearSceneState(*BoundRegistry)",
                      "InFlightPickContexts.clear()", "Selection.SetStableEntityLookup(nullptr)",
                      "LookupBinding.Disconnect()", "Lookup.Clear()", "PublishEmpty(BoundWorld)")),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState", files=(RUNTIME_CMAKE,),
         forbid=basenames(GIZMO_FRAME_SERVICE_RETIRED), absent=GIZMO_FRAME_SERVICE_RETIRED),
    rule("SceneInteractionPrivatelyOwnsGizmoFrameState/inventory", files=(INVENTORY,),
         forbid=("Extrinsic.Runtime.GizmoFrameService`",)),

    # RUNTIME-201: EditorCommandHistory is the only production undo/redo owner. Entity mutation
    # owners construct records through the internal transaction helper.
    rule("UnifiedEditorMutationHistoryHasNoParallelProductionSurface",
         files=("src/runtime/Editor/Runtime.EditorCommandHistory.cppm",),
         require=("class EditorCommandHistory",), forbid=HISTORY_RETIRED + HISTORY_ADAPTERS_RETIRED),
    rule("UnifiedEditorMutationHistoryHasNoParallelProductionSurface/helper", files=(MUTATION_HELPER,),
         require=("return history.Execute(",)),
    rule("UnifiedEditorMutationHistoryHasNoParallelProductionSurface/runtime", trees=("src/runtime",),
         exts=(".cpp", ".cppm", ".hpp"), forbid=HISTORY_RETIRED),
    rule("UnifiedEditorMutationHistoryHasNoParallelProductionSurface/stacks", trees=("src/runtime",),
         exts=(".cpp", ".cppm", ".hpp"), exclude=(HISTORY_OWNER,), forbid=("m_UndoStack", "m_RedoStack")),
    rule("UnifiedEditorMutationHistoryHasNoParallelProductionSurface/records", trees=("src/runtime",),
         exts=(".cpp", ".cppm", ".hpp"), exclude=(HISTORY_OWNER, MUTATION_HELPER),
         forbid=("EditorCommandRecord{",)),

    rule("RunFrameCarriesDataOnlyFrameContext", files=(FRAME_LOOP,),
         require=("struct RuntimeFrameContext", "double FrameDeltaSeconds", "double FixedStepAlpha",
                  "std::uint64_t FrameIndex", "Graphics::RenderFrameInput RenderInput",
                  "RuntimeRenderExtractionStats ExtractionStats", "std::uint32_t PooledFrontSlot")),
    rule("RunFrameCarriesDataOnlyFrameContext/engine", files=(ENGINE,),
         require=("frameContext.FrameDeltaSeconds = frameDt;", "frameContext.FixedStepAlpha = alpha;",
                  "frameContext.FrameIndex = m_Impl->m_RenderExtractionFrameIndex++;",
                  "frameContext.ExtractionStats", "frameContext.PooledFrontSlot")),

    rule("GpuProfilingConfigIsSampledOnceAfterUiCaptureBeforeExtraction", files=(ENGINE,), slice=RUN_FRAME,
         first_order=("FramePhase::UiEndCapture", ".EnableGpuProfiling =", "FramePhase::BeforeExtraction"),
         count=((".EnableGpuProfiling =", 1),),
         order=(".EnableGpuProfiling =", "m_Impl->m_Config.Render.EnableGpuProfiling")),
    rule("GpuProfilingConfigIsSampledOnceAfterUiCaptureBeforeExtraction/before-render", files=(ENGINE,),
         slice=RUN_FRAME, first_order=(".EnableGpuProfiling =", BEGIN_FRAME)),

    rule("RuntimeOwnedFrameLoopPreservesRendererAndMaintenanceOrder", files=(ENGINE,), slice=RUN_FRAME,
         first_order=RENDERER_PHASES, forbid=("streaming.",)),
    rule("RuntimeOwnedFrameLoopPreservesRendererAndMaintenanceOrder/maintenance", files=(ENGINE,),
         slice=RUN_FRAME, first_order=(COLLECT_TRANSFERS, ASSET_MAINTENANCE)),

    # The promoted baseline is the complete fixed-step system bundle. BUG-069.
    rule("RunFrameRegistersPromotedEcsSystemsBeforeCompile", files=(FRAME_LOOP,),
         first_order=("ECS::Systems::TransformHierarchy::RegisterSystem(",
                      "ECS::Systems::BoundsPropagation::RegisterSystem(",
                      "ECS::Systems::RenderSync::RegisterSystem(", "frameGraph.Compile()",
                      "frameGraph.Execute()", "frameGraph.ResetForReplay()"),
         forbid=("registerModuleSystems", "frameGraph.Reset();")),
    rule("RunFrameRegistersPromotedEcsSystemsBeforeCompile/imports", files=(ENGINE,),
         require=("import Extrinsic.ECS.System.TransformHierarchy;",
                  "import Extrinsic.ECS.System.BoundsPropagation;", "import Extrinsic.ECS.System.RenderSync;")),

    rule("SupersededWorkSurfacesAreRetiredFromBuild", files=(RUNTIME_CMAKE,),
         forbid=("Runtime.StreamingExecutor", "Runtime.DerivedJobGraph"),
         absent=module_pair("src/runtime/Runtime.StreamingExecutor")
         + module_pair("src/runtime/Runtime.DerivedJobGraph")),
    rule("SupersededWorkSurfacesAreRetiredFromBuild/tests", files=(TESTS_CMAKE,),
         forbid=("Test.RuntimeStreamingExecutor.cpp", "Test.DerivedJobGraph.cpp")),

    rule("SupersededGpuResultReadbackSurfacesAreRetiredFromBuild", files=(RUNTIME_CMAKE,),
         forbid=("Runtime.AsyncBufferReadback", "Runtime.GpuReadbackJob"),
         absent=module_pair("src/runtime/Runtime.AsyncBufferReadback")
         + module_pair("src/runtime/Runtime.GpuReadbackJob")
         + ("tests/contract/runtime/Test.AsyncBufferReadback.cpp", "tests/contract/runtime/Test.GpuReadbackJob.cpp",
            "tests/integration/runtime/Test.GpuReadbackJobGpuSmoke.cpp")),
    rule("SupersededGpuResultReadbackSurfacesAreRetiredFromBuild/tests", files=(TESTS_CMAKE,),
         require=("Test.GpuResultReadbackJob.cpp", "Test.GpuResultReadbackGpuSmoke.cpp"),
         forbid=("Test.AsyncBufferReadback.cpp", "Test.GpuReadbackJob.cpp")),
    rule("SupersededGpuResultReadbackSurfacesAreRetiredFromBuild/sources", trees=("src/runtime", "methods"),
         exts=(".cpp", ".cppm"),
         forbid=("ReadBuffer(", "import Extrinsic.Runtime.AsyncBufferReadback",
                 "import Extrinsic.Runtime.GpuReadbackJob")),

    # The generic Engine composition root may borrow capabilities by service type, but must not
    # know, own, construct, or facade the concrete module.
    rule("AsyncWorkModulePublishesOnlyKernelJobService/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("AsyncWorkModule",)),
    rule("AsyncWorkModulePublishesOnlyKernelJobService/engine", files=(ENGINE,),
         require=("JobService m_JobService{}",),
         forbid=("AsyncWorkModule", "Provide<JobService>", "IStreamingFrameHooks")),
    rule("AsyncWorkModulePublishesOnlyKernelJobService/module-interface",
         files=("src/runtime/Kernel/Runtime.AsyncWorkModule.cppm",),
         require=("export module Extrinsic.Runtime.AsyncWorkModule", "public IRuntimeModule",
                  "bool m_Registered{false}"),
         forbid=("IStreamingFrameHooks",)),
    rule("AsyncWorkModulePublishesOnlyKernelJobService/module",
         files=("src/runtime/Kernel/Runtime.AsyncWorkModule.cpp",),
         require=("Provide<JobService>(setup.Jobs(), Name())", "Withdraw<JobService>(context.Jobs)",
                  "context.Jobs.CancelAll()"),
         forbid=("StreamingExecutor", "DerivedJobRegistry")),
    rule("AsyncWorkModulePublishesOnlyKernelJobService/sandbox", files=(SANDBOX_MAIN,),
         require=("import Extrinsic.Runtime.AsyncWorkModule",
                  "EmplaceModule<Extrinsic::Runtime::AsyncWorkModule>()")),

    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("EngineConfigControl", "EngineConfigSectionRegistry", "GetConfigControl")),
    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/engine", files=(ENGINE,),
         require=("LoadAndApplyRuntimeRenderRecipeConfigFile(",
                  "ResetRuntimeRenderRecipeActivation(recipeActivation)"),
         forbid=("EngineConfigControl", "EngineConfigSectionRegistry")),
    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/control-interface",
         files=("src/runtime/Config/Runtime.EngineConfigControl.cppm",),
         require=("EngineConfigControl final : public IRuntimeModule",
                  "RuntimeEngineConfigSectionRegistry m_SectionRegistry"),
         forbid=("EngineConfigControlDependencies", "IWindow", "IRenderer")),
    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/control",
         files=("src/runtime/Config/Runtime.EngineConfigControl.cpp",),
         require=("Provide<EngineConfigControl>", "Withdraw<EngineConfigControl>(*this)")),
    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/activation",
         files=("src/runtime/Config/Runtime.RenderRecipeActivation.cppm",),
         require=("struct RuntimeRenderRecipeActivationKernel",)),
    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/sandbox", files=(SANDBOX_MAIN,),
         require=("std::make_unique<Extrinsic::Runtime::EngineConfigControl>",
                  "configControl->SectionRegistry()", "engine.AddModule(std::move(configControl))")),
    rule("EngineConfigControlIsAnAppComposedModuleOutsideEngine/cmake", files=(RUNTIME_CMAKE,),
         require=module_pair("Runtime.RenderRecipeActivation")),

    rule("MaintenanceHasNoOptionalStreamingFacade", files=(ENGINE,), slice=RUN_FRAME,
         first_order=(COLLECT_TRANSFERS, ASSET_MAINTENANCE), forbid=("IStreamingFrameHooks", "streaming")),

    # Every production async submission carries its owning world scope. Migrated JobService lanes
    # keep the `.Scope =` designator rather than MakeCpuJobDesc's positional scope (RUNTIME-194).
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/import-executor",
         files=("src/runtime/AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp",),
         count=(("JobDesc{", 2), (".Scope = submissionWorld", 2))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/scene-document",
         files=("src/runtime/Scene/Runtime.SceneDocumentModule.cpp",),
         count=(("JobDesc{", 2), (".Scope = binding.World", 2))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/import-policies",
         files=("src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp",),
         count=(("JobDesc{", 2), (".Scope = world", 2))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/visualization",
         files=("src/runtime/Visualization/Runtime.VisualizationRecipes.cpp",),
         count=(("JobDesc{", 1), (".Scope = request.World", 1))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/model-handoff",
         files=("src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp",),
         count=(("JobDesc ", 3), (".Scope = ", 3))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/geometry-operations",
         files=(OPERATIONS + "Runtime.GeometryProcessingOperations.cpp",),
         count=(("JobDesc desc{", 1), (".Scope = context.World", 1))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/clustering",
         files=("src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp",),
         count=(("MakeCpuJobDesc<KMeansJobResult>(", 1), ("const WorldHandle world = snapshot.World", 1))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/clustering-snapshot",
         files=("src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp",), compact=True,
         require=("returnKMeansSnapshot{.Command=command,.World=world",)),
    # Every remaining geometry factory carries its owning world scope; the resident compute and
    # Accept jobs of every GPU transaction (normals included) are the shared lifecycle's (RUNTIME-311).
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/mesh-operations",
         files=tuple(OPERATIONS + name for name in (
             "Runtime.GeometryProcessingOperations.Mesh.cpp", "Runtime.GeometryProcessingOperations.MeshSupport.cpp",
             "Runtime.GeometryProcessingOperations.Registration.cpp", "Runtime.GeometryProcessingOperations.Uv.cpp",
             "Runtime.MeshFieldOperations.Curvature.cpp", "Runtime.MeshFieldOperations.Geodesics.cpp",
             "Runtime.MeshTopologyOperations.Topology.cpp")),
         forbid=("return DerivedJobDesc{",),
         count=(("return JobDesc{", 4), (".Scope = context.World", 4))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/normal-operations",
         files=(OPERATIONS + "Runtime.GeometryProcessingOperations.Normals.cpp",),
         count=(("JobDesc desc{", 1), ("JobDesc ", 1), (".Scope = context.World", 1))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/gpu-transactions",
         files=(OPERATIONS + "Runtime.GpuTransactionLifecycle.cpp",),
         count=(("JobDesc job{", 1), ("JobDesc accept{", 1), (".Scope = t->Context.World", 2))),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/workspace-session",
         files=("src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp",),
         count=(("desc.Scope = m_Worlds->ActiveWorld()", 1),)),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/context-adapters",
         files=("src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp",), compact=True,
         require=(".World=activeWorld",)),
    rule("ProductionAsyncSubmissionsCarryOwningWorldScope/asset-workflow", files=(WORKFLOW,),
         count=((".World = BoundWorld", 2), (".Worlds = Worlds", 1))),

    rule("SandboxEditorJobsUseSingleJobServiceSurface/editor-features",
         files=("src/runtime/Editor/internal/Runtime.EditorFeatures.Internal.hpp",),
         require=("EditorJobCommandSurface JobCommands",),
         forbid=("import Extrinsic.Runtime.DerivedJobGraph",)),
    rule("SandboxEditorJobsUseSingleJobServiceSurface/workspace-session",
         files=("src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp",),
         forbid=("import Extrinsic.Runtime.DerivedJobGraph", "DerivedJobRegistry", "m_DerivedJobSnapshot")),
    rule("SandboxEditorJobsUseSingleJobServiceSurface/geometry-operations",
         files=(OPERATIONS + "Runtime.GeometryProcessingOperations.cpp",),
         forbid=("import Extrinsic.Runtime.DerivedJobGraph", "import Extrinsic.Runtime.StreamingExecutor",
                 "FindActiveEditorDerivedJob")),
    rule("SandboxEditorJobsUseSingleJobServiceSurface/job-projection",
         files=("src/runtime/Editor/Runtime.EditorJobProjection.cppm",),
         require=("struct EditorJobCommandSurface", "FindActive{}", "SnapshotEntity{}"),
         forbid=("EditorDerivedJobCommandSurface", "DerivedJobCommands", "ToEditorJobQueueSnapshot")),

    rule("CoreFrameLoopModuleStaysRetiredAndRuntimeOwnsComposition", files=("src/core/CMakeLists.txt",),
         forbid=("Core.FrameLoop",), absent=module_pair("src/core/Core.FrameLoop")),
    rule("CoreFrameLoopModuleStaysRetiredAndRuntimeOwnsComposition/engine", files=(ENGINE,),
         require=("m_Impl->m_Window->PollEvents()", BEGIN_FRAME, COLLECT_TRANSFERS)),

    rule("DeviceBootstrapKeepsBackendAndFallbackPolicyOutOfEngine", files=(ENGINE,),
         require=("import Extrinsic.Runtime.DeviceBootstrap", "CreateRuntimeDevice(m_Impl->m_Config.Render)"),
         forbid=("MakeFallbackTextureBytes", "BuildFallbackTextureDesc", "CreateVulkanDevice",
                 "CreateNullDevice", "std::make_unique<Graphics::GpuAssetCache>")),
    rule("DeviceBootstrapKeepsBackendAndFallbackPolicyOutOfEngine/initialize", files=(ENGINE,),
         slice=("void Engine::Initialize()", "void Engine::Shutdown()"),
         forbid=("InitializeRuntimeGpuAssetFallbackTexture(",)),
    rule("DeviceBootstrapKeepsBackendAndFallbackPolicyOutOfEngine/workflow-register", files=(WORKFLOW,),
         slice=("Core::Result AssetWorkflowModule::OnRegister(", "Core::Result AssetWorkflowModule::OnResolve("),
         require=("InitializeRuntimeGpuAssetFallbackTexture(", "std::make_unique<Graphics::GpuAssetCache>")),
    rule("DeviceBootstrapKeepsBackendAndFallbackPolicyOutOfEngine/interface", files=(ENGINE_INTERFACE,),
         forbid=("RuntimeDeviceSelection", "SelectRuntimeDeviceBackend",
                 "ShouldEmitVulkanRequestedButNotOperationalBreadcrumb")),
    rule("DeviceBootstrapKeepsBackendAndFallbackPolicyOutOfEngine/bootstrap",
         files=("src/runtime/Kernel/Runtime.DeviceBootstrap.cpp",),
         require=("MakeFallbackTextureBytes", "BuildFallbackTextureDesc", "CreateRuntimeDevice")),

    rule("ObsoleteMeshPrimitiveViewControlsAreDeleted",
         files=(ENGINE_INTERFACE, ENGINE, "src/runtime/Rendering/Runtime.RenderExtraction.cppm",
                "src/runtime/Rendering/Runtime.RenderExtraction.Internal.cpp"),
         forbid=("MeshPrimitiveViewSettings",),
         absent=module_pair("src/runtime/Runtime.MeshPrimitiveViewControls")),
    rule("ObsoleteMeshPrimitiveViewControlsAreDeleted/engine", files=(ENGINE,),
         forbid=("import Extrinsic.Graphics.Component.RenderGeometry", "RenderEdges", "RenderPoints",
                 "ToRenderPointType", "ToMeshVertexViewRenderMode", "entt::registry")),

    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("import Extrinsic.Runtime.ReferenceScene", "import Extrinsic.Runtime.CameraControllers",
                 "import Extrinsic.Graphics.CameraSnapshots", "ReferenceScenePopulation",
                 "CameraControllerRegistry", "GetReferenceSceneRegistry", "GetReferenceCameraSeed",
                 "IsReferenceSceneInstalled", "GetCameraControllerRegistry")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/engine", files=(ENGINE,),
         forbid=("import Extrinsic.Runtime.ReferenceScene", "import Extrinsic.Runtime.CameraControllers",
                 "import Extrinsic.Graphics.CameraSnapshots", "ReferenceScenePopulation",
                 "CameraControllerRegistry")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/camera-interface",
         files=("src/runtime/Cameras/Runtime.CameraModule.cppm",),
         require=("class CameraModule final : public IRuntimeModule", "CameraControllerRegistry m_Registry")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/camera",
         files=("src/runtime/Cameras/Runtime.CameraModule.cpp",),
         require=("Provide<CameraControllerRegistry>(", "Withdraw<CameraControllerRegistry>(",
                  "setup.Subscribe<ActiveWorldChanged>", "setup.Subscribe<WorldWillBeDestroyed>",
                  "setup.RegisterViewportInputHook(",
                  "context.ActiveWorldHandle != m_Registry.BoundWorld()")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/reference-interface",
         files=("src/runtime/Scene/Runtime.ReferenceScene.cppm",),
         require=("BootstrapReferenceScene(", "TeardownReferenceScene("),
         forbid=("IReferenceSceneProvider", "ReferenceSceneRegistry", "TriangleProvider")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/reference",
         files=("src/runtime/Scene/Runtime.ReferenceScene.cpp",),
         forbid=("IReferenceSceneProvider", "ReferenceSceneRegistry")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/sandbox-main", files=(SANDBOX_MAIN,),
         require=("engine.EmplaceModule<Extrinsic::Runtime::CameraModule>()",)),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine/sandbox",
         files=("src/app/Sandbox/Sandbox.cpp",),
         require=("Runtime::BootstrapReferenceScene(", "Runtime::TeardownReferenceScene(",
                  "Runtime::WorldHandle World", "services.Find<Runtime::CameraControllerRegistry>()")),
    rule("CameraModuleAndAppOwnedReferenceBootstrapStayOutOfEngine", files=(RUNTIME_CMAKE,),
         require=module_pair("Cameras/Runtime.CameraModule"), forbid=("Runtime.ReferenceSceneControl",),
         absent=module_pair("src/runtime/Runtime.ReferenceSceneControl")),

    rule("InputActionsKeepRegistryAndDispatchOutOfEngine/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("export import Extrinsic.Runtime.InputActions", "import Extrinsic.Runtime.InputActions;",
                 "RegisterInputAction(", "UnregisterInputAction(", "struct RuntimeInputActionRecord",
                 "m_NextInputActionHandle")),
    rule("InputActionsKeepRegistryAndDispatchOutOfEngine/engine", files=(ENGINE,),
         require=("import Extrinsic.Runtime.InputActions;", "m_Impl->m_InputActions.DispatchForFrame("),
         forbid=("m_InputActions.Register(std::move(desc))", "RuntimeInputActionRecord",
                 "m_NextInputActionHandle", "m_InputActions.push_back",
                 "std::erase_if(\n            m_InputActions")),
    rule("InputActionsKeepRegistryAndDispatchOutOfEngine/engine-registry", files=(ENGINE,), compact=True,
         require=("RuntimeInputActionRegistry m_InputActions",)),
    rule("InputActionsKeepRegistryAndDispatchOutOfEngine/frame-loop", files=(FRAME_LOOP,),
         forbid=("RuntimeInputActionTriggered", "DispatchRuntimeInputActionsForFrame")),
    rule("InputActionsKeepRegistryAndDispatchOutOfEngine/input-interface",
         files=("src/runtime/Kernel/Runtime.InputActions.cppm",),
         require=("export module Extrinsic.Runtime.InputActions", "struct RuntimeInputActionRecord",
                  "std::vector<RuntimeInputActionRecord> m_Actions", "std::uint64_t m_NextHandle")),
    rule("InputActionsKeepRegistryAndDispatchOutOfEngine/input",
         files=("src/runtime/Kernel/Runtime.InputActions.cpp",),
         require=("RuntimeInputActionTriggered", "std::erase_if(", "Core::Log::Warn")),

    rule("RetiredEngineCompositionHelperBmisAreAbsent", files=(RUNTIME_CMAKE,),
         forbid=basenames(COMPOSITION_HELPERS_RETIRED), absent=COMPOSITION_HELPERS_RETIRED),
    rule("RetiredEngineCompositionHelperBmisAreAbsent/tests", files=(TESTS_CMAKE,),
         forbid=("Test.RuntimeEcsSystemBundle.cpp",)),
    rule("RetiredEngineCompositionHelperBmisAreAbsent/inventory", files=(INVENTORY,),
         forbid=("Extrinsic.Runtime.ModuleSchedule`", "Extrinsic.Runtime.EcsSystemBundle`",
                 "Extrinsic.Runtime.JobServiceGpuQueueBridge`")),
    rule("RetiredEngineCompositionHelperBmisAreAbsent/interface", files=(ENGINE_INTERFACE,),
         forbid=("FrameHookRecord", "ViewportInputHookRecord", "RuntimeFrameCommandHookHandle")),
    rule("RetiredEngineCompositionHelperBmisAreAbsent/engine", files=(ENGINE,),
         require=("struct FrameHookRecord", "struct ViewportInputHookRecord", "m_FrameHooks.clear()",
                  "m_ViewportInputHooks.clear()", "m_NextHookRegistrationSequence = 0u",
                  "for (const Impl::FrameHookRecord& record",
                  "for (const Impl::ViewportInputHookRecord& record",
                  "return lhs.ModuleName < rhs.ModuleName;"),
         forbid=("RegisterSimSystemsForTick", "std::vector<std::vector<std::size_t>> edges",
                 "waits for unprovided signal", "Sim system dependency cycle detected", "record.Desc.Options")),

    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/engine-interface", files=(ENGINE_INTERFACE,),
         forbid=("SelectionReadback", "GetLastRefinedPrimitive", "struct InFlightPickContext",
                 "m_InFlightPickContexts", "m_LastRefinedPrimitive", "m_LastRefinedPrimitiveGeneration")),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/engine", files=(ENGINE,),
         forbid=("SelectionReadback", "RUNTIME-188.EngineInteractionTransition")),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/scene-document-interface",
         files=("src/runtime/Scene/Runtime.SceneDocumentModule.cppm",),
         forbid=("SelectionReadback", "LastRefinedPrimitive{}", "LastRefinedPrimitiveGeneration{}")),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/scene-document",
         files=("src/runtime/Scene/Runtime.SceneDocumentModule.cpp",), forbid=("SelectionReadback",)),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/interaction-interface",
         files=(INTERACTION_INTERFACE,),
         require=("class SceneInteractionModule final : public IRuntimeModule", "struct Impl;")),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/interaction", files=(INTERACTION,),
         require=("struct InFlightPickContext", "std::vector<InFlightPickContext> InFlightPickContexts",
                  "std::optional<PrimitiveSelectionResult> LastRefinedPrimitive",
                  "RegisterReplacementParticipant(", "BuildPickReadbackContextForFrame",
                  "selectionSystem.PopPickResult()", "result->Sequence == 0u",
                  "pickContext.World != BoundWorld", "pickContext.InteractionEpoch !=",
                  "RefinePickReadbackResult(")),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/frame-loop", files=(FRAME_LOOP,),
         forbid=("BuildPickReadbackContextForFrame", "RememberPickReadbackContextForFrame",
                 "DrainPendingSelectionPickForFrame", "ApplySelectionReadbackToController",
                 "RefineSelectionReadbackForFrame", "DrainCompletedSelectionReadbacksForFrame",
                 "RefinePickReadbackResult(scene")),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState", files=(RUNTIME_CMAKE,),
         forbid=basenames(SELECTION_READBACK_RETIRED), absent=SELECTION_READBACK_RETIRED),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/tests", files=(TESTS_CMAKE,),
         forbid=("Test.SelectionReadbackCorrelation.cpp",)),
    rule("SceneInteractionPrivatelyOwnsPickCorrelationState/inventory", files=(INVENTORY,),
         forbid=("Extrinsic.Runtime.SelectionReadback`",)),

    rule("EditorUiModulePrivatelyMirrorsImGuiFramePacingDiagnostics/engine-interface",
         files=(ENGINE_INTERFACE,), require=("import Extrinsic.Runtime.FramePacingDiagnostics;",),
         forbid=("export import Extrinsic.Runtime.FramePacingDiagnostics",
                 "export struct RuntimeFramePacingDiagnostics")),
    rule("EditorUiModulePrivatelyMirrorsImGuiFramePacingDiagnostics/engine", files=(ENGINE,),
         require=("import Extrinsic.Runtime.FramePacingDiagnostics",),
         forbid=("MirrorImGuiFramePacingDiagnostics(", "pacing.ImGuiEditorCallbackMicros =",
                 "pacing.ImGuiDrawDataCopyMicros =")),
    rule("EditorUiModulePrivatelyMirrorsImGuiFramePacingDiagnostics/diagnostics",
         files=("src/runtime/Kernel/Runtime.FramePacingDiagnostics.cppm",),
         require=("export module Extrinsic.Runtime.FramePacingDiagnostics",
                  "export struct RuntimeFramePacingDiagnostics"),
         forbid=("import Extrinsic.Runtime.ImGuiAdapter", "MirrorImGuiFramePacingDiagnostics")),
    rule("EditorUiModulePrivatelyMirrorsImGuiFramePacingDiagnostics/editor-ui-interface",
         files=("src/runtime/Editor/Runtime.EditorUiModule.cppm",),
         forbid=("ImGuiAdapter", "MirrorImGuiDiagnostics")),
    rule("EditorUiModulePrivatelyMirrorsImGuiFramePacingDiagnostics/editor-ui",
         files=("src/runtime/Editor/Runtime.EditorUiModule.cpp",),
         require=("import Extrinsic.Runtime.FramePacingDiagnostics;", "import Extrinsic.Runtime.ImGuiAdapter;",
                  "void MirrorImGuiDiagnostics(", "pacing.ImGuiEditorCallbackMicros =",
                  "pacing.ImGuiDrawDataCopyMicros =", "MirrorImGuiDiagnostics(context.Pacing, diagnostics);")),

    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/removed-engine-tokens",
         files=(ENGINE_INTERFACE, ENGINE),
         forbid=("EngineSceneReplacementTransitions", "BindActiveSceneAssetHandoffs",
                 "RegisterSceneReplacementParticipants", "CancelActiveAssetImportsForShutdown",
                 "AnnounceAndShutdownRuntimeModules", "m_SelectionController")),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/workflow-resolve", files=(WORKFLOW,),
         slice=("Core::Result AssetWorkflowModule::OnResolve(", "void AssetWorkflowModule::OnShutdown("),
         require=("Require<SceneDocumentModule>", "Require<EditorCommandHistory>", "Find<SelectionController>()",
                  "RegisterReplacementParticipant(")),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence", files=(WORKFLOW,),
         slice=("void AnnounceShutdown()", "AssetWorkflowImportExecutor ImportExecutor{}"),
         first_order=("ImportExecutor->CancelActiveAssetImportsForShutdown()", "DetachImportExecutor();",
                      "AcceptingCallbacks = false;", "ModelMaterializer.reset();",
                      "ReleaseDocumentParticipant();", "Selection = nullptr;")),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/announcement", files=(ENGINE,),
         slice=("void Engine::AnnounceRuntimeShutdown()", "void Engine::ShutdownRuntimeModules()"),
         first_order=("m_Impl->m_Initialized = false;",
                      "m_Impl->m_KernelEvents.Publish(RuntimeShutdownAnnounced{});",
                      "(void)m_Impl->m_KernelEvents.Pump();")),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/begin-shutdown", files=(ENGINE,),
         slice=BEGIN_SHUTDOWN,
         first_order=("m_Impl->m_CommandBus.DiscardPending();", "AnnounceRuntimeShutdown();",
                      QUIESCE_PARTICIPANTS)),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/shutdown", files=(ENGINE,), slice=SHUTDOWN,
         first_order=("BeginShutdown();", "ShutdownRuntimeModules();"), forbid=("ShutdownApplication",)),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/module-shutdown", files=(ENGINE,),
         slice=("void Engine::ShutdownRuntimeModules()", "void Engine::RefreshActiveWorldScenePointer()"),
         first_order=("m_Impl->m_RuntimeModules.rbegin()", "m_Impl->m_ServiceRegistry.Reset();")),
    rule("AssetWorkflowShutdownDetachesBeforeGlobalGpuQuiescence/workflow", files=(WORKFLOW,),
         require=("setup.Subscribe<RuntimeShutdownAnnounced>", "state->AnnounceShutdown();",
                  "m_Impl->Shared->AnnounceShutdown();")),

    rule("RenderGraphStaysOutOfECSAndCoreStaysOutOfGpuBarriers",
         files=tuple(f"src/graphics/framegraph/Graphics.RenderGraph{name}" for name in (
             ".cppm", ".Resources.cppm", ".Pass.cppm", ".Compiler.cppm", ".Barriers.cppm",
             ".TransientAllocator.cppm", ".Executor.cppm", ".cpp", ".Compiler.cpp", ".Executor.cpp",
             ".TransientAllocator.cpp")),
         forbid=("import Extrinsic.ECS", "import ECS", "Vk")),
    rule("RenderGraphStaysOutOfECSAndCoreStaysOutOfGpuBarriers/core",
         files=tuple(f"src/core/Core.{name}" for name in (
             "Dag.Scheduler.cppm", "Dag.Scheduler.Types.cppm", "Dag.Scheduler.Hazards.cppm",
             "Dag.TaskGraph.cppm", "Dag.TaskGraph.cpp", "FrameGraph.cppm", "FrameGraph.cpp")),
         forbid=("TextureBarrier", "BufferBarrier", "TextureUsage", "BufferUsage", "Vk")),

    # RUNTIME-199: the spatial-debug adapter registry was retired (no production workflow ever
    # registered an adapter). The graphics `SpatialDebugVisualizers` packet contract survives.
    rule("RetiredSpatialDebugRegistryHasNoProductionSurface", files=(INVENTORY,),
         forbid=("Extrinsic.Runtime.SpatialDebugAdapters", "Extrinsic.Runtime.SpatialDebugClosestFace",
                 "Extrinsic.ECS.Component.SpatialDebugBinding")),
    rule("RetiredSpatialDebugRegistryHasNoProductionSurface/cmake", files=(RUNTIME_CMAKE,),
         forbid=("SpatialDebug",)),
    rule("RetiredSpatialDebugRegistryHasNoProductionSurface/ecs-cmake", files=("src/ecs/Components/CMakeLists.txt",),
         forbid=("SpatialDebugBinding",)),
    rule("RetiredSpatialDebugRegistryHasNoProductionSurface/extraction",
         files=("src/runtime/Rendering/Runtime.RenderExtraction.cppm",),
         forbid=("ISpatialDebugAdapter", "SpatialDebugAdapterRegistry", "RegisterSpatialDebugAdapter",
                 "SpatialDebugBindingsObserved")),
    rule("RetiredSpatialDebugRegistryHasNoProductionSurface/editor",
         files=("src/runtime/Editor/Runtime.EditorCommandHistory.cppm",
                "src/runtime/Editor/internal/Runtime.EditorFeatures.Internal.hpp"),
         forbid=("SpatialDebug",)),
    rule("RetiredSpatialDebugRegistryHasNoProductionSurface/renderer",
         files=("src/graphics/renderer/Graphics.Renderer.cppm",), require=("SpatialDebugBounds",)),

    # RUNTIME-193: one general geometry-presentation recipe and snapshot path.
    rule("RetiredProgressivePresentationModulesAndSymbolsStayDeleted", files=(RUNTIME_CMAKE,),
         forbid=("Runtime.ProgressiveRenderData", "Runtime.ProgressivePresentationExtraction"),
         absent=module_pair("src/runtime/Runtime.ProgressiveRenderData")
         + module_pair("src/runtime/Runtime.ProgressivePresentationExtraction")),
    rule("RetiredProgressivePresentationModulesAndSymbolsStayDeleted/sources", trees=("src",), exts=SCAN_EXTS,
         forbid=("Extrinsic.Runtime.ProgressiveRenderData", "Extrinsic.Runtime.ProgressivePresentationExtraction",
                 "ProgressivePresentationBindings", "ProgressivePresentationSnapshot",
                 "BuildProgressivePresentationSnapshot")),
    rule("RetiredProgressivePresentationModulesAndSymbolsStayDeleted/presentation",
         files=("src/runtime/GeometryIntegration/Runtime.GeometryPresentation.cppm",),
         require=("struct GeometryPresentationRecipe", "struct GeometryPresentationRuntimeState",
                  "struct GeometryPresentationSnapshot")),

    # RUNTIME-192: one canonical geometry-property vocabulary; consumers no longer mirror
    # GeometryElementDomain + PropertyValueKind behind their own enums.
    rule("NoDuplicateGeometryPropertyVocabularyRemains",
         files=("src/runtime/GeometryIntegration/Runtime.GeometryPresentation.cppm",
                "src/runtime/GeometryIntegration/Runtime.GeometryPresentation.cpp",
                "src/runtime/Editor/internal/Runtime.EditorFeatures.Internal.hpp",
                "src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp",
                "src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp")
         + tuple(OPERATIONS + name for name in (
             "Runtime.GeometryProcessingOperations.Mesh.cpp", "Runtime.GeometryProcessingOperations.MeshSupport.cpp",
             "Runtime.GeometryProcessingOperations.Registration.cpp", "Runtime.GeometryProcessingOperations.Uv.cpp",
             "Runtime.GeometryProcessingOperations.MeshSupport.hpp", "Runtime.GeometryProcessingOperations.MeshSoup.hpp",
             "Runtime.MeshFieldOperations.Curvature.cpp", "Runtime.MeshTopologyOperations.Topology.cpp"))
         + ("src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp",),
         forbid=("ProgressiveGeometryDomain", "ProgressivePropertyValueKind",
                 "EditorVisualizationPropertyValueKind", "EditorPropertyCatalogValueKind")),
    # Authoring vocabulary has one owner, separate from live source catalogs; the retired
    # derived-job-only MeshSurface scope was not promoted into it.
    rule("NoDuplicateGeometryPropertyVocabularyRemains/property-types",
         files=("src/runtime/GeometryIntegration/Runtime.GeometryProperty.Types.cppm",),
         require=("struct GeometryPropertyRef", "using GeometryPropertyValueKindFilter"),
         forbid=("MeshSurface",)),
    rule("NoDuplicateGeometryPropertyVocabularyRemains/availability",
         files=("src/runtime/GeometryIntegration/Runtime.GeometryAvailability.cppm",),
         require=("export import Extrinsic.Runtime.GeometryProperty.Types;", "GeometryPropertyCatalogSnapshot")),
    # The persisted wire format keeps its legacy spellings, owned by the serializer.
    rule("NoDuplicateGeometryPropertyVocabularyRemains/serialization",
         files=("src/runtime/Scene/Runtime.SceneSerialization.cpp",),
         require=('"ScalarFloat"', '"ScalarDouble"', '"GraphVertex"', "kPropertyValueKindWire",
                  "kPropertyDomainWire")),

    # RUNTIME-197: one graphics-owned upload-plan/residency lifecycle; runtime keeps typed
    # topology adapters private.
    rule("RetiredGeometryPackerAndLifecycleSurfacesStayDeleted", files=(RUNTIME_CMAKE,),
         first_order=("FILE_SET render_extraction_impl TYPE CXX_MODULES FILES",
                      "Runtime.GeometryPlanBuilders.cppm"),
         require=("Runtime.MeshSurfaceTopology.cppm", "Runtime.MeshPrimitiveView.cppm"),
         absent=PACKERS_RETIRED),
    rule("RetiredGeometryPackerAndLifecycleSurfacesStayDeleted/runtime", trees=("src/runtime",),
         exts=(".cpp", ".cppm"),
         forbid=tuple(f"Extrinsic.Runtime.{name}" for name in (
             "MeshGeometryPacker", "GraphGeometryPacker", "PointCloudGeometryPacker",
             "ProceduralGeometryPacker", "MeshPrimitiveViewPacker"))
         + ("ProceduralGeometryCache", "GetProceduralGeometryCacheForTest", "TickProceduralGeometry",
            "TickMeshGeometry", "TickGraphGeometry", "TickPointCloudGeometry", "TickMeshPrimitiveViewGeometry",
            ".UploadGeometry(", ".UpdateGeometryChannels(", ".FreeGeometry(")),
    rule("RetiredGeometryPackerAndLifecycleSurfacesStayDeleted/extraction",
         files=("src/runtime/Rendering/Runtime.RenderExtraction.cppm",),
         count=(("void TickGeometryResidency(", 1),)),
    rule("RetiredGeometryPackerAndLifecycleSurfacesStayDeleted/channel-bindings",
         files=("src/runtime/GeometryIntegration/Runtime.VertexChannelBindings.cppm",),
         require=("GeometryPropertyRef Property",),
         forbid=("AttributeSourceType SourceType", "std::string SourceProperty")),

    # Slices above start at the first marker in the whole file; the C++ sliced inside RunFrame.
    Rule(SUITE, "SliceMarkersStayUnique",
         files=(ENGINE,),
         count=((COLLECT_TRANSFERS, 1), ("if (platformShouldClose)", 1),
                ("RunMinimizedFrameWork(frameContext", 1))),
]
