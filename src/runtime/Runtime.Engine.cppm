module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>
#include <entt/signal/sigh.hpp>

export module Extrinsic.Runtime.Engine;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.IOBackend;
import Extrinsic.ECS.Scene.Handle;
import Geometry.HalfedgeMesh.IO;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Registry;

namespace Extrinsic::Runtime
{
    export class Engine;

    export struct RuntimeDeviceSelection
    {
        bool UsePromotedVulkanDevice{false};
        bool FallsBackToNullDevice{true};
    };

    export struct RuntimeAssetImportRequest
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export struct RuntimeAssetReimportRequest
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export using RuntimeIOBackendFactory =
        std::function<std::unique_ptr<Core::IO::IIOBackend>()>;

    export struct RuntimeGpuJobParticipantHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeGpuJobParticipantHandle,
            RuntimeGpuJobParticipantHandle) noexcept = default;
    };

    export struct RuntimeGpuJobParticipantDesc
    {
        std::string DebugName{};
        std::function<void(RHI::ICommandContext&)> RecordFrameCommands{};
        std::function<void()> DrainCompletedTransfers{};
        std::function<bool()> HasInFlightWork{};
        std::function<void()> ShutdownAfterDeviceIdle{};
    };

    export struct RuntimePostImportProcessorHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimePostImportProcessorHandle,
            RuntimePostImportProcessorHandle) noexcept = default;
    };

    export struct RuntimePostImportProcessorContext
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        const Geometry::MeshIO::MeshIOResult* MeshPayload{};
    };

    export struct RuntimePostImportProcessorServices
    {
        StreamingExecutor* Streaming{};
        Assets::AssetService* AssetService{};
        Graphics::GpuAssetCache* GpuAssetCache{};
        RenderExtractionCache* RenderExtraction{};
        ECS::Scene::Registry* Scene{};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{};
        bool ObjectSpaceNormalBakeGraphicsBackendOperational{false};
    };

    export struct RuntimePostImportProcessorDesc
    {
        std::string DebugName{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::function<Core::Result(
            const RuntimePostImportProcessorContext&,
            RuntimePostImportProcessorServices&)> Process{};
    };

    struct RuntimePostImportProcessorRecord
    {
        RuntimePostImportProcessorHandle Handle{};
        RuntimePostImportProcessorDesc Desc{};
    };

    export struct RuntimeImportEntityAuthoringPolicyHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeImportEntityAuthoringPolicyHandle,
            RuntimeImportEntityAuthoringPolicyHandle) noexcept = default;
    };

    export struct RuntimeImportEntityAuthoringPolicyContext
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
    };

    export struct RuntimeImportEntityAuthoringPolicyServices
    {
        ECS::Scene::Registry* Scene{};
    };

    export struct RuntimeImportEntityAuthoringPolicyDesc
    {
        std::string DebugName{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::function<Core::Result(
            const RuntimeImportEntityAuthoringPolicyContext&,
            RuntimeImportEntityAuthoringPolicyServices&)> Apply{};
    };

    struct RuntimeImportEntityAuthoringPolicyRecord
    {
        RuntimeImportEntityAuthoringPolicyHandle Handle{};
        RuntimeImportEntityAuthoringPolicyDesc Desc{};
    };

    export struct RuntimeImportCompletedHandlerHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeImportCompletedHandlerHandle,
            RuntimeImportCompletedHandlerHandle) noexcept = default;
    };

    export struct RuntimeImportCompletedContext
    {
        std::string_view Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::span<const ECS::EntityHandle> CreatedEntities{};
        std::optional<CameraFocusTarget> FocusTarget{};
    };

    export struct RuntimeImportCompletedServices
    {
        ECS::Scene::Registry* Scene{};
        CameraControllerRegistry* CameraControllers{};
        SelectionController* Selection{};
        const Core::Config::EngineConfig* Config{};
    };

    export struct RuntimeImportCompletedHandlerDesc
    {
        std::string DebugName{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::function<Core::Result(
            const RuntimeImportCompletedContext&,
            RuntimeImportCompletedServices&)> Handle{};
    };

    struct RuntimeImportCompletedHandlerRecord
    {
        RuntimeImportCompletedHandlerHandle Handle{};
        RuntimeImportCompletedHandlerDesc Desc{};
    };

    export enum class RuntimeInputActionTrigger : std::uint8_t
    {
        KeyJustPressed = 0,
    };

    export struct RuntimeInputActionBinding
    {
        int KeyCode{-1};
        RuntimeInputActionTrigger Trigger{
            RuntimeInputActionTrigger::KeyJustPressed};
        bool SuppressWhenImGuiCapturesKeyboard{true};
    };

    export struct RuntimeInputActionHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(
            RuntimeInputActionHandle,
            RuntimeInputActionHandle) noexcept = default;
    };

    export struct RuntimeInputActionContext
    {
        RuntimeInputActionBinding Binding{};
        Core::Extent2D Viewport{};
        double FrameDeltaSeconds{0.0};
        std::uint64_t FrameIndex{0};
        bool ImGuiCapturesKeyboard{false};
    };

    export struct RuntimeInputActionServices
    {
        ECS::Scene::Registry* Scene{};
        CameraControllerRegistry* CameraControllers{};
        SelectionController* Selection{};
        Graphics::RenderFrameInput* RenderInput{};
        const Core::Config::EngineConfig* Config{};
    };

    export struct RuntimeInputActionDesc
    {
        std::string DebugName{};
        RuntimeInputActionBinding Binding{};
        std::function<Core::Result(
            const RuntimeInputActionContext&,
            RuntimeInputActionServices&)> Execute{};
    };

    struct RuntimeInputActionRecord
    {
        RuntimeInputActionHandle Handle{};
        RuntimeInputActionDesc Desc{};
    };

    export struct RuntimeAssetImportResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t GeneratedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        std::uint64_t GeneratedTextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
    };

    export struct RuntimeAssetImportEvent
    {
        std::uint64_t Sequence{0};
        std::string Path{};
        Assets::AssetPayloadKind RequestedPayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        RuntimeAssetIngestDiagnostic IngestDiagnostic{RuntimeAssetIngestDiagnostic::None};
        std::optional<RuntimeAssetImportResult> Result{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Result.has_value() && Error == Core::ErrorCode::Success;
        }
    };

    export struct RuntimeQueuedAssetImport
    {
        RuntimeAssetIngestHandle Operation{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    export enum class RuntimeSceneFileOperation : std::uint8_t
    {
        None,
        Save,
        Load,
    };

    export struct RuntimeQueuedSceneFileOperation
    {
        StreamingTaskHandle Task{};
        RuntimeSceneFileOperation Operation{RuntimeSceneFileOperation::None};
    };

    export struct RuntimeSceneFileEvent
    {
        std::uint64_t Sequence{0};
        RuntimeSceneFileOperation Operation{RuntimeSceneFileOperation::None};
        StreamingTaskHandle Task{};
        std::string Path{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::optional<SceneSerializationResult> SaveResult{};
        std::optional<SceneDeserializationResult> LoadResult{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Error == Core::ErrorCode::Success;
        }
    };

    export [[nodiscard]] RuntimeDeviceSelection SelectRuntimeDeviceBackend(
        const Core::Config::RenderConfig& config,
        bool promotedVulkanAvailable) noexcept;

    // GRAPHICS-033B: pure decision for the runtime startup breadcrumb. Returns
    // true when the runtime requested the promoted Vulkan device but the
    // resolved device is not operational, matching the truth-table rows in
    // `src/graphics/vulkan/README.md`. The breadcrumb is emitted exactly once
    // per `Engine::Initialize()` because the call site evaluates this once;
    // no internal guard is needed here.
    export [[nodiscard]] bool ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
        const Core::Config::RenderConfig& config,
        bool isDeviceOperational) noexcept;

    export [[nodiscard]] Core::Config::EngineConfig CreateReferenceEngineConfig();

    export enum class EngineConfigBootSource : std::uint8_t
    {
        ReferenceDefaults = 0,
        DefaultPath,
        Environment,
        CommandLine,
    };

    export struct EngineConfigBootOptions
    {
        std::string DefaultConfigPath{"config/engine.json"};
        std::string EnvironmentVariable{"INTRINSIC_ENGINE_CONFIG"};
        std::string CliFlag{"--engine-config"};
    };

    export struct EngineConfigBootResult
    {
        Core::Config::EngineConfig Config{};
        EngineConfigBootSource Source{EngineConfigBootSource::ReferenceDefaults};
        std::string SourcePath{};
        Core::Config::EngineConfigLoadResult LoadResult{};
        bool LoadedFile{false};
        bool UsedReferenceFallback{true};
    };

    export [[nodiscard]] EngineConfigBootResult ResolveEngineConfigForBoot(
        std::span<const std::string_view> args,
        const EngineConfigBootOptions& options = {});

    export enum class RuntimeRenderRecipeActivationSource : std::uint8_t
    {
        None = 0,
        StartupConfigFile,
        AgentCli,
        Editor,
        Programmatic,
    };

    export enum class RuntimeConfigControlSource : std::uint8_t
    {
        None = 0,
        AgentCli,
        Editor,
        Programmatic,
    };

    export enum class RuntimeRenderRecipeApplyStatus : std::uint8_t
    {
        None = 0,
        Applied,
        Rejected,
        MissingRenderer,
    };

    export struct RuntimeRenderRecipeApplyResult
    {
        RuntimeRenderRecipeApplyStatus Status{RuntimeRenderRecipeApplyStatus::None};
        RuntimeRenderRecipeActivationSource Source{RuntimeRenderRecipeActivationSource::None};
        Graphics::RenderRecipeConfigLoadResult LoadResult{};
        bool RendererOverrideInstalled{false};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeRenderRecipeApplyStatus::Applied;
        }
    };

    export struct RuntimeRenderRecipeState
    {
        std::optional<Graphics::FrameRecipeOverride> ActiveOverride{};
        Graphics::RenderRecipeConfigLoadResult ActiveConfig{};
        bool HasActiveConfig{false};
        RuntimeRenderRecipeActivationSource ActiveSource{
            RuntimeRenderRecipeActivationSource::None};
        RuntimeRenderRecipeApplyResult LastApply{};
        bool HasLastApply{false};
    };

    export enum class RuntimeEngineConfigApplyStatus : std::uint8_t
    {
        None = 0,
        Applied,
        NoChange,
        Rejected,
    };

    export struct RuntimeEngineConfigApplyResult
    {
        RuntimeEngineConfigApplyStatus Status{
            RuntimeEngineConfigApplyStatus::None};
        RuntimeConfigControlSource Source{RuntimeConfigControlSource::None};
        Core::Config::EngineConfigLoadResult LoadResult{};
        RuntimeRenderRecipeApplyResult RecipeApply{};
        bool EngineConfigApplied{false};
        bool DefaultRecipeConfigPathChanged{false};
        bool SandboxProgressivePoissonChanged{false};
        std::vector<std::string> RejectedBootOnlyFields{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RuntimeEngineConfigApplyStatus::Applied ||
                   Status == RuntimeEngineConfigApplyStatus::NoChange;
        }
    };

    export struct RuntimeEngineConfigControlState
    {
        Core::Config::EngineConfig ActiveConfig{};
        RuntimeEngineConfigApplyResult LastApply{};
        bool HasLastApply{false};
    };

    // ============================================================
    // IApplication — the user-facing hook interface.
    //
    // Lifecycle order guaranteed by Engine:
    //
    //   OnInitialize(engine)           — once, after all subsystems are live.
    //                                    Register FrameGraph system passes here.
    //
    //   per frame:
    //     OnSimTick(engine, fixedDt)   — 0..N times, fixed timestep.
    //                                    Add FrameGraph passes each tick;
    //                                    Engine calls Compile→Execute→Reset.
    //     OnVariableTick(engine,       — once per frame, after all sim ticks.
    //                    alpha, dt)      alpha = accumulator / fixedDt ∈ [0,1).
    //                                    Use for camera, UI, interpolation.
    //
    //   OnShutdown(engine)             — once, before subsystem teardown.
    //
    // Contract: implementations must be non-throwing.  The engine
    // builds with -fno-exceptions; std::terminate fires on violation.
    // ============================================================

    export class IApplication
    {
    public:
        virtual ~IApplication() = default;

        virtual void OnInitialize(Engine& engine) = 0;

        /// Called 0..N times per frame with the fixed simulation timestep.
        /// N is bounded by EngineConfig::Simulation::MaxSubSteps.
        /// Add FrameGraph passes via engine.GetFrameGraph().AddPass(...) here.
        virtual void OnSimTick(Engine& engine, double fixedDt) = 0;

        /// Called once per frame after all sim ticks complete.
        /// @param alpha  interpolation blend in [0, 1) — use to smooth
        ///               rendered positions between committed ticks.
        /// @param dt     wall-clock delta of this frame (clamped).
        virtual void OnVariableTick(Engine& engine, double alpha, double dt) = 0;

        virtual void OnShutdown(Engine& engine) = 0;
    };

    // ============================================================
    // Engine — composition root and frame-loop owner.
    //
    // Owns: Window, IDevice, IRenderer, FrameClock,
    //       Tasks::Scheduler (static — initialized/shutdown here),
    //       AssetService, Scene::Registry, FrameGraph (CPU),
    //       StreamingExecutor.
    //
    // Scheduling surfaces:
    //   CPU     — Core::FrameGraph wrapping a Dag::TaskGraph(Cpu).
    //             Drives ECS system scheduling each sim tick.
    //             IApplication::OnSimTick adds passes; Engine calls
    //             Compile → Execute → Reset per tick.
    //
    //   GPU     — Owned internally by IRenderer.
    //             Engine drives it via BeginFrame / ExecuteFrame / EndFrame.
    //
    //   Streaming — Runtime.StreamingExecutor owned by Engine.
    //               Asset IO / geometry processing tasks submit persistent
    //               executor work and publish main-thread apply callbacks in
    //               Phase 10 (maintenance lane) each frame.
    //
    // Frame shape (executed inside Run()):
    //
    //   PollEvents → ShouldClose
    //     [if close: RequestExit → return before renderer work]
    //     [if minimized: WaitForEventsTimeout → BeginFrame/Resample → continue]
    //   BeginFrame(clock)
    //     [if resized:   WaitIdle → Resize]
    //   FixedStepLoop {
    //     OnSimTick × N
    //       FrameGraph: Compile → Execute → Reset  (CPU task graph)
    //   }
    //   OnVariableTick(alpha, dt)
    //   BuildRenderFrameInput
    //   Renderer::BeginFrame      (GPU task graph — acquire)
    //     [if skip: EndFrame(clock) → continue]
    //   Runtime::RenderExtractionCache::ExtractAndSubmit
    //   Renderer::ExtractRenderWorld
    //   Renderer::PrepareFrame
    //   Renderer::ExecuteFrame    (GPU task graph — compile/record/submit)
    //   Renderer::EndFrame → completedGpuValue
    //   Device::Present
    //   Device::CollectCompletedTransfers
    //   AssetService::Tick
    //   EndFrame(clock)
    // ============================================================

    export struct RuntimeFramePacingDiagnostics
    {
        bool Valid{false};
        bool PlatformContinueFrame{false};
        bool RendererBeganFrame{false};
        bool RendererCompletedFrame{false};
        std::uint64_t FrameIndex{0u};
        std::uint64_t TotalMicros{0u};
        std::uint64_t PlatformBeginMicros{0u};
        std::uint64_t ResizeMicros{0u};
        std::uint64_t OperationalTransitionMicros{0u};
        std::uint64_t FixedStepMicros{0u};
        std::uint64_t ImGuiBeginMicros{0u};
        std::uint64_t VariableTickMicros{0u};
        std::uint64_t ImGuiEndMicros{0u};
        std::uint64_t ImGuiEditorCallbackMicros{0u};
        std::uint64_t ImGuiDrawDataCopyMicros{0u};
        std::uint32_t ImGuiDrawListCount{0u};
        std::uint32_t ImGuiVertexCount{0u};
        std::uint32_t ImGuiIndexCount{0u};
        std::uint32_t ImGuiCommandCount{0u};
        std::uint32_t ImGuiFontAtlasCopyCount{0u};
        std::uint32_t ImGuiFontAtlasReuseCount{0u};
        bool          ImGuiFontAtlasCopied{false};
        bool          ImGuiFrameUsedUserTexture{false};
        std::uint64_t ImGuiFontAtlasByteCount{0u};
        std::uint64_t ImGuiFontAtlasCopyBytes{0u};
        std::uint64_t ImGuiVertexCopyBytes{0u};
        std::uint64_t ImGuiIndexCopyBytes{0u};
        std::uint64_t ImGuiCommandCopyBytes{0u};
        std::uint64_t ImGuiOverlayCopyBytes{0u};
        std::uint64_t PreRenderSetupMicros{0u};
        std::uint64_t PreRenderTransformFlushMicros{0u};
        bool          PreRenderTransformFlushRan{false};
        std::uint32_t PreRenderTransformWorldUpdatedObserved{0u};
        std::uint32_t PreRenderTransformDirtyTransformStamped{0u};
        std::uint32_t PreRenderTransformWorldUpdatedCleared{0u};
        std::uint64_t SelectionPickDrainMicros{0u};
        std::uint64_t RenderContractMicros{0u};
        std::uint64_t RenderBeginFrameMicros{0u};
        std::uint64_t RenderExtractionMicros{0u};
        std::uint64_t RenderPrepareMicros{0u};
        std::uint64_t RenderExecuteMicros{0u};
        std::uint64_t RenderEndFrameMicros{0u};
        std::uint64_t RenderGraphCompileMicros{0u};
        std::uint64_t RenderGraphExecuteMicros{0u};
        std::uint64_t PresentMicros{0u};
        std::uint64_t MaintenanceMicros{0u};
        std::uint64_t SelectionReadbackMicros{0u};
        std::uint64_t ReleaseRenderWorldMicros{0u};
    };

    export class Engine
    {
    public:
        Engine(Core::Config::EngineConfig config,
               std::unique_ptr<IApplication> application);
        ~Engine();

        Engine(const Engine&)            = delete;
        Engine& operator=(const Engine&) = delete;

        void Initialize();
        void Run();
        void Shutdown();

        [[nodiscard]] bool IsRunning()    const noexcept;
        void RequestExit()                      noexcept;

        // ── Subsystem accessors (valid after Initialize()) ────────────────
        [[nodiscard]] Platform::IWindow&      GetWindow()        noexcept;
        [[nodiscard]] RHI::IDevice&           GetDevice()        noexcept;
        [[nodiscard]] Graphics::IRenderer&    GetRenderer()      noexcept;
        [[nodiscard]] const Core::Config::EngineConfig&
            GetEngineConfig() const noexcept;
        [[nodiscard]] Graphics::RenderRecipeConfigContext
            CreateRenderRecipeConfigContext() const;
        [[nodiscard]] Graphics::RenderRecipeConfigLoadResult
            PreviewRenderRecipeConfigDocument(
                std::string_view document,
                std::string sourceId = "<memory>") const;
        [[nodiscard]] Graphics::RenderRecipeConfigLoadResult
            LoadRenderRecipeConfigPreviewFile(std::string path) const;
        [[nodiscard]] RuntimeRenderRecipeApplyResult
            ActivateRenderRecipeConfigDocument(
                std::string_view document,
                std::string sourceId = "<memory>",
                RuntimeRenderRecipeActivationSource source =
                    RuntimeRenderRecipeActivationSource::Programmatic);
        [[nodiscard]] RuntimeRenderRecipeApplyResult ApplyRenderRecipeConfigPreview(
            const Graphics::RenderRecipeConfigLoadResult& loadResult,
            RuntimeRenderRecipeActivationSource source =
                RuntimeRenderRecipeActivationSource::Programmatic);
        [[nodiscard]] RuntimeRenderRecipeApplyResult LoadAndApplyRenderRecipeConfigFile(
            std::string path,
            RuntimeRenderRecipeActivationSource source =
                RuntimeRenderRecipeActivationSource::Programmatic);
        void ClearActiveRenderRecipeOverride() noexcept;
        [[nodiscard]] const RuntimeRenderRecipeState&
            GetRenderRecipeState() const noexcept;
        [[nodiscard]] Core::Config::EngineConfigLoadResult
            PreviewEngineConfigControlDocument(
                std::string_view document,
                std::string sourceId = "<memory>") const;
        [[nodiscard]] Core::Config::EngineConfigLoadResult
            LoadEngineConfigControlFile(std::string path) const;
        [[nodiscard]] RuntimeEngineConfigApplyResult ApplyEngineConfigHotSubset(
            const Core::Config::EngineConfigLoadResult& loadResult,
            RuntimeConfigControlSource source =
                RuntimeConfigControlSource::Programmatic);
        [[nodiscard]] RuntimeEngineConfigApplyResult
            LoadAndApplyEngineConfigHotSubsetFile(
                std::string path,
                RuntimeConfigControlSource source =
                    RuntimeConfigControlSource::Programmatic);
        [[nodiscard]] const RuntimeEngineConfigControlState&
            GetEngineConfigControlState() const noexcept;
        [[nodiscard]] Assets::AssetService&   GetAssetService()  noexcept;
        [[nodiscard]] Graphics::GpuAssetCache& GetGpuAssetCache() noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
            GetObjectSpaceNormalBakeQueueDiagnosticsForTest() const noexcept;
        [[nodiscard]] std::size_t
            GetPendingObjectSpaceNormalBakeCountForTest() const noexcept;
        [[nodiscard]] ECS::Scene::Registry&   GetScene()         noexcept;
        // UI-001 Slice D — runtime-owned file/import command seam. Editor UI
        // submits a path + payload hint here; Engine composes the promoted
        // ASSETIO geometry/model/texture decoders, AssetService, and runtime
        // handoffs. Platform drop events route through the same facade.
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPath(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport> QueueModelTextureImport(
            RuntimeAssetImportRequest request);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ReimportAsset(
            RuntimeAssetReimportRequest request);
        [[nodiscard]] RuntimePostImportProcessorHandle RegisterPostImportProcessor(
            RuntimePostImportProcessorDesc desc);
        void UnregisterPostImportProcessor(
            RuntimePostImportProcessorHandle handle);
        [[nodiscard]] RuntimeImportEntityAuthoringPolicyHandle
            RegisterImportEntityAuthoringPolicy(
                RuntimeImportEntityAuthoringPolicyDesc desc);
        void UnregisterImportEntityAuthoringPolicy(
            RuntimeImportEntityAuthoringPolicyHandle handle);
        [[nodiscard]] RuntimeImportCompletedHandlerHandle
            RegisterImportCompletedHandler(RuntimeImportCompletedHandlerDesc desc);
        void UnregisterImportCompletedHandler(
            RuntimeImportCompletedHandlerHandle handle);
        [[nodiscard]] RuntimeInputActionHandle RegisterInputAction(
            RuntimeInputActionDesc desc);
        void UnregisterInputAction(RuntimeInputActionHandle handle);
        [[nodiscard]] const std::optional<RuntimeAssetImportEvent>&
            GetLastAssetImportEvent() const noexcept;
        [[nodiscard]] std::vector<RuntimeAssetIngestRecord>
            GetAssetIngestRecordsForTest() const;
        void SetModelTextureImportIOBackendFactoryForTest(
            RuntimeIOBackendFactory factory);
        [[nodiscard]] RuntimeAssetImportQueueSnapshot
            GetAssetImportQueueSnapshot() const;
        [[nodiscard]] std::size_t ClearCompletedAssetImports();
        [[nodiscard]] Core::Result CancelAssetImport(
            RuntimeAssetIngestHandle operation);
        void ImportDroppedFilePaths(std::span<const std::string> paths);
        // Contract-test seam: replay a platform event through the same runtime
        // handler installed as the window listener during Initialize().
        void DispatchPlatformEventForTest(const Platform::Event& event);
        // RUNTIME-098 — promoted scene persistence facade. Editor/UI code
        // submits file paths here; Engine owns file IO, scene replacement, and
        // runtime sidecar cleanup while the serializer stays backend-neutral.
        [[nodiscard]] Core::Expected<SceneSerializationResult> SaveSceneToPath(
            std::string path);
        [[nodiscard]] Core::Expected<RuntimeQueuedSceneFileOperation>
            QueueSceneSaveToPath(std::string path);
        [[nodiscard]] Core::Expected<SceneDeserializationResult> LoadSceneFromPath(
            std::string path);
        [[nodiscard]] Core::Expected<RuntimeQueuedSceneFileOperation>
            QueueSceneLoadFromPath(std::string path);
        [[nodiscard]] const std::optional<RuntimeSceneFileEvent>&
            GetLastSceneFileEvent() const noexcept;
        [[nodiscard]] Core::Result NewSceneDocument();
        [[nodiscard]] Core::Result CloseSceneDocument();
        // RUNTIME-089 Slice B — runtime/editor-owned selection authority.
        // Input ports / editor tools submit hover/click picks here; RunFrame
        // drains the coalesced pick into the renderer's SelectionSystem before
        // extraction, consumes the readback after present, and mirrors the
        // controller snapshot into RenderWorld::Selection.
        [[nodiscard]] SelectionController&    GetSelectionController() noexcept;
        // RUNTIME-145 Slice A — expose the engine-owned durable-id lookup for
        // editor/runtime consumers and contract tests. The lookup itself stays
        // owned by Engine; this API resolves through the maintained sidecar and
        // updates lookup diagnostics in the same way direct sidecar use does.
        [[nodiscard]] std::optional<ECS::EntityHandle>
            ResolveEntityByStableId(ECS::Components::StableId id);
        [[nodiscard]] const StableEntityLookupDiagnostics&
            GetStableEntityLookupDiagnostics() const noexcept;
        [[nodiscard]] EditorCommandHistory&   GetEditorCommandHistory() noexcept;
        [[nodiscard]] const EditorCommandHistory&
            GetEditorCommandHistory() const noexcept;
        // RUNTIME-084 Slice B — runtime/editor-owned transform-gizmo authority.
        // Engine reads platform input and the active camera snapshot each frame,
        // drives hit-test / drag tick / commit against selected ECS authoring
        // transforms, and submits only render-safe TransformGizmoRenderPacket
        // spans to graphics through RenderExtractionCache.
        [[nodiscard]] GizmoInteraction&       GetGizmoInteraction() noexcept;
        [[nodiscard]] const GizmoInteraction& GetGizmoInteraction() const noexcept;
        [[nodiscard]] GizmoUndoStack&         GetGizmoUndoStack() noexcept;
        [[nodiscard]] const GizmoUndoStack&   GetGizmoUndoStack() const noexcept;
        // RUNTIME-093 Slice B2 — editor-facing refined-primitive selection cache.
        // RunFrame refines each pick readback's encoded primitive hint against the
        // hit entity's authoritative `GeometrySources` (newest pick wins; a
        // background readback clears it; an empty-drain frame retains the prior
        // value). Tracks the sub-primitive under the last pick hit, keyed by render
        // id for correlation with the controller's selection; empty until the first
        // pick resolves. Graphics never reads this — it only produced the hint.
        [[nodiscard]] const std::optional<PrimitiveSelectionResult>&
            GetLastRefinedPrimitiveSelection() const noexcept;
        [[nodiscard]] std::uint64_t
            GetLastRefinedPrimitiveSelectionGeneration() const noexcept;
        [[nodiscard]] Core::FrameGraph&       GetFrameGraph()    noexcept;

        // ── GRAPHICS-036C — pipelined-frames render-world pool ────────────
        // Runtime-owned slot-lifecycle pool (`GRAPHICS-036A`) driven by RunFrame:
        // extraction acquires/publishes a back slot, the renderer consumes/releases
        // the current front in synchronous mode or previous front in pipelined
        // mode, and the pool's three diagnostics counters mirror onto the last
        // extraction stats each frame. Sized from
        // `RenderConfig::SynchronousExtraction` in Initialize() (1 buffer when
        // synchronous, triple-buffered otherwise). Valid after Initialize();
        // pipelined mode consumes the previous front after publishing the new
        // front so render-N observes the retained N-1 snapshot.
        [[nodiscard]] const RenderWorldPool&  GetRenderWorldPool() const noexcept;
        // The `RuntimeRenderExtractionStats` produced by the most recent frame's
        // `ExtractAndSubmit`, including the mirrored `RenderWorldPool*` counters.
        // Zero-initialized until the first frame extracts.
        [[nodiscard]] const RuntimeRenderExtractionStats&
            GetLastRenderExtractionStats() const noexcept;
        [[nodiscard]] std::optional<Graphics::MaterialTextureAssetBindings>
            GetMaterialTextureAssetBindingsForTest(std::uint32_t stableEntityId) const noexcept;

        // ── Reference scene seam (GRAPHICS-029A/B) ────────────────────────
        // Accessible before Initialize() so tests and downstream impl
        // children register providers prior to subsystem wiring. After
        // Initialize() runs, the registry is locked to its installed
        // contents — Register() before Initialize(), Resolve() after.
        [[nodiscard]] ReferenceSceneRegistry& GetReferenceSceneRegistry() noexcept;
        [[nodiscard]] bool IsReferenceSceneInstalled() const noexcept;
        // GRAPHICS-029B/RUNTIME-081A: the optional CameraViewInput seed captured from
        // the resolved provider's ReferenceScenePopulation. Empty when the
        // reference scene is disabled or the provider returned no camera.
        // The promoted camera-controller surface consumes this as initial
        // state; it is retained as a test seam and no longer directly fills
        // RenderFrameInput::Camera.
        [[nodiscard]] const std::optional<Graphics::CameraViewInput>&
            GetReferenceCameraSeed() const noexcept;
        // UI-001 Slice C / RUNTIME-106 — editor/runtime command seams. The
        // engine remains the owner of camera-controller slots. The legacy mesh
        // primitive-view accessors are compatibility shims that translate to
        // ECS `RenderEdges` / `RenderPoints`; render components are the
        // authoritative view toggles.
        [[nodiscard]] CameraControllerRegistry& GetCameraControllerRegistry() noexcept;
        void SetMeshPrimitiveViewSettings(std::uint32_t stableEntityId,
                                          MeshPrimitiveViewSettings settings);
        void ClearMeshPrimitiveViewSettings(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] MeshPrimitiveViewSettings GetMeshPrimitiveViewSettings(
            std::uint32_t stableEntityId) const noexcept;
        void SetVisualizationAdapterBinding(
            std::uint32_t stableEntityId,
            RenderExtractionCache::VisualizationAdapterBinding binding);
        void ClearVisualizationAdapterBinding(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<RenderExtractionCache::VisualizationAdapterBinding>
            GetVisualizationAdapterBinding(std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::uint64_t
            GetVisualizationAdapterBindingRevision() const noexcept;

        // Runtime-owned GPU work lanes that must record commands inside the
        // renderer frame command context and drain completion on the maintenance
        // path. Engine owns only lifecycle ordering; participants own their
        // domain-specific queues and public command surfaces.
        [[nodiscard]] RuntimeGpuJobParticipantHandle
            RegisterRuntimeGpuJobParticipant(RuntimeGpuJobParticipantDesc desc);
        void UnregisterRuntimeGpuJobParticipant(
            RuntimeGpuJobParticipantHandle handle);
        [[nodiscard]] DerivedJobHandle SubmitDerivedJob(DerivedJobDesc desc);
        void CancelDerivedJob(DerivedJobHandle handle);
        [[nodiscard]] DerivedJobQueueSnapshot GetDerivedJobQueueSnapshot() const;

        // ── RUNTIME-090 Slice B — Dear ImGui editor hook ──────────────────
        // Registers the per-frame editor callback invoked between the
        // adapter's BeginFrame and EndFrame so editor/UI code can issue ImGui
        // panel draws without modifying the adapter. May be called before or
        // after Initialize(); the stored callback is applied to the adapter
        // when it is constructed. RunFrame brackets OnVariableTick with the
        // adapter so one ImGuiOverlayFrame is produced per engine frame.
        void SetImGuiEditorCallback(std::function<void()> callback);
        // Read-only access to the runtime-side ImGui adapter (valid after
        // Initialize()). Exposes the produce-path diagnostics for tests; the
        // Engine owns the BeginFrame/EndFrame cadence.
        [[nodiscard]] const ImGuiAdapter& GetImGuiAdapter() const noexcept;
        // UI-030 — last frame-loop pacing sample. Runtime owns the cross-layer
        // phase boundaries; renderer/backend-specific diagnostics remain on
        // their owning surfaces and are mirrored here only as copied counters.
        [[nodiscard]] const RuntimeFramePacingDiagnostics&
            GetLastFramePacingDiagnostics() const noexcept;

    private:
        void RunFrame();      // executes one full frame — called by Run()
        void HandlePlatformEvent(const Platform::Event& event);
        void RequestExitFromWindowClose(std::string_view source);
        void HandleWindowDropEvent(const Platform::WindowDropEvent& event);
        void QueueDroppedGeometryImport(
            std::string path,
            std::vector<Assets::AssetPayloadKind> payloadKinds);
        [[nodiscard]] Core::Expected<RuntimeQueuedAssetImport>
            QueueModelTextureImportWithIngest(
                RuntimeAssetImportRequest request,
                RuntimeAssetIngestSource source,
                Assets::AssetId existingAsset = {});
        void QueueDroppedModelTextureImport(
            std::string path,
            Assets::AssetPayloadKind payloadKind);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPathWithIngest(
            RuntimeAssetImportRequest request,
            RuntimeAssetIngestSource source,
            Assets::AssetId existingAsset);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPathImpl(
            RuntimeAssetImportRequest request,
            Assets::AssetId existingAsset);
        void RecordAssetImportEvent(
            const RuntimeAssetImportRequest& request,
            const Core::Expected<RuntimeAssetImportResult>& result,
            RuntimeAssetIngestDiagnostic ingestDiagnostic);
        void RecordSceneFileEvent(RuntimeSceneFileEvent event);
        void ClearSceneRuntimeState();

        Core::Config::EngineConfig           m_Config;
        std::unique_ptr<IApplication>        m_Application;
        std::unique_ptr<Platform::IWindow>   m_Window;
        std::unique_ptr<RHI::IDevice>        m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;
        RuntimeRenderRecipeState             m_RenderRecipeState{};
        RuntimeEngineConfigControlState      m_ConfigControlState{};
        // RUNTIME-090 Slice B — runtime-side Dear ImGui adapter + the graphics
        // overlay system it produces into. The overlay system instance is
        // runtime-owned composition (the allowed runtime -> graphics edge) so
        // the producer (this adapter) and the Pass.ImGui consumer
        // (GRAPHICS-079) share one instance through
        // IRenderer::SetImGuiOverlaySystem. The adapter is constructed in
        // Initialize() after the Window and Renderer exist and torn down first
        // in Shutdown() (it references the Window and the overlay system).
        // Declared after m_Renderer / before m_RenderExtraction so the unique_ptr
        // adapter is destroyed before the value-typed overlay system and the
        // Window it borrows. The editor callback is stashed so it can be
        // registered before Initialize() and re-applied across rebuilds.
        Graphics::ImGuiOverlaySystem         m_ImGuiOverlay{};
        std::function<void()>                m_ImGuiEditorCallback{};
        std::unique_ptr<ImGuiAdapter>        m_ImGuiAdapter{};
        struct RuntimeGpuJobParticipantRecord
        {
            RuntimeGpuJobParticipantHandle Handle{};
            RuntimeGpuJobParticipantDesc Desc{};
            Graphics::RuntimeFrameCommandHookHandle RendererHook{};
        };
        std::vector<RuntimeGpuJobParticipantRecord> m_RuntimeGpuJobParticipants{};
        std::uint64_t m_NextRuntimeGpuJobParticipantHandle{1u};
        RenderExtractionCache                 m_RenderExtraction;
        // GRAPHICS-036C — runtime-owned render-world slot pool. Constructed in
        // Initialize() sized from RenderConfig::SynchronousExtraction (held by
        // unique_ptr because RenderWorldPool owns atomics and is neither copyable
        // nor movable, so it cannot be resized by assignment). RunFrame drives the
        // acquire/publish/acquire-current-or-previous/release sequence and mirrors
        // its diagnostics into m_LastExtractionStats once per frame.
        std::unique_ptr<RenderWorldPool>      m_RenderWorldPool{};
        RuntimeRenderExtractionStats          m_LastExtractionStats{};
        // Monotonic frame counter stamped onto pool slots for the consumer's
        // frame-age computation; incremented once per RunFrame.
        std::uint64_t                         m_FrameIndex{0u};
        // RUNTIME-089 Slice B — selection authority; persists across frames so
        // in-flight picks correlate with their later readbacks.
        SelectionController                   m_SelectionController{};
        // RUNTIME-102 — runtime/editor-owned undo/redo and document dirty-state
        // source. UI reads snapshots from this service and command facades mark
        // save/load/import state here instead of keeping authoritative document
        // state in panel objects.
        EditorCommandHistory                  m_EditorCommandHistory{};
        // RUNTIME-084 Slice B — runtime/editor transform-gizmo interaction and
        // transient packet production. These are runtime-owned state; graphics
        // receives copied TransformGizmoRenderPacket values only.
        GizmoInteraction                      m_GizmoInteraction{};
        GizmoUndoStack                        m_GizmoUndoStack{};
        TransformGizmoRenderPacketBuilder     m_GizmoPacketBuilder{};
        std::vector<ECS::EntityHandle>        m_GizmoSelectedEntities{};
        // RUNTIME-092 Slice B — runtime-owned StableId/render-id lookup sidecar.
        // Maintained incrementally from StableId component construct/update/
        // destroy events and attached to the controller in Initialize() so
        // selection resolves durable ids through the single runtime authority
        // (ECS/graphics hold no lookup state). Whole-scene replacement still
        // uses Rebuild() once at the replacement boundary.
        StableEntityLookup                    m_StableEntityLookup{};
        // RUNTIME-093 Slice B2 — editor-facing refined sub-primitive of the last
        // pick hit. Updated from the pick-readback drain in RunFrame; never read
        // by graphics. Empty until the first pick resolves. The generation bumps
        // whenever this optional is replaced/cleared so editor caches can key
        // primitive-sensitive selected analysis without owning pick state.
        std::optional<PrimitiveSelectionResult> m_LastRefinedPrimitive{};
        std::uint64_t m_LastRefinedPrimitiveGeneration{0u};
        // BUG-026 — per-sequence pick context captured when RunFrame drains a
        // pending pick (issuing frame's inverse view-projection, viewport,
        // pick ray, pixel-radius scale). Replayed when the matching readback
        // arrives so the depth sample unprojects against the camera that
        // issued the pick. Bounded mirror of the controller's in-flight FIFO.
        struct InFlightPickContext
        {
            std::uint64_t       Sequence{0u};
            PickReadbackContext Context{};
        };
        std::vector<InFlightPickContext>        m_InFlightPickContexts{};

        // CPU task graph — ECS system scheduling
        std::unique_ptr<Core::FrameGraph>      m_FrameGraph;
        // Persistent streaming executor — cross-frame background work
        std::unique_ptr<StreamingExecutor>      m_StreamingExecutor;
        RuntimeIOBackendFactory                 m_ModelTextureImportIOBackendFactoryForTest{};
        // Runtime/editor derived jobs — submitted through the streaming
        // executor and applied on the maintenance lane.
        std::unique_ptr<DerivedJobRegistry>     m_DerivedJobRegistry;
        // Asset service — CPU payload authority
        std::unique_ptr<Assets::AssetService>  m_AssetService;
        // GPU-side asset cache — bridges AssetId to refcounted GPU resources.
        // Constructed after the renderer; destroyed before the renderer so
        // BufferLease/TextureLease destructors run while their managers are
        // still alive.
        std::unique_ptr<Graphics::GpuAssetCache> m_GpuAssetCache;
        RuntimeObjectSpaceNormalBakeQueue         m_ObjectSpaceNormalBakeQueue{};
        Assets::AssetEventBus::ListenerToken     m_GpuAssetCacheListener{
            Assets::AssetEventBus::InvalidToken};
        std::unique_ptr<AssetModelTextureHandoff> m_AssetModelTextureHandoff;
        std::unique_ptr<AssetModelSceneHandoff>   m_AssetModelSceneHandoff;
        RuntimeAssetIngestStateMachine             m_AssetIngestStateMachine{};
        std::vector<RuntimePostImportProcessorRecord> m_PostImportProcessors{};
        std::uint64_t m_NextPostImportProcessorHandle{1u};
        std::vector<RuntimeImportEntityAuthoringPolicyRecord>
            m_ImportEntityAuthoringPolicies{};
        std::uint64_t m_NextImportEntityAuthoringPolicyHandle{1u};
        std::vector<RuntimeImportCompletedHandlerRecord>
            m_ImportCompletedHandlers{};
        std::uint64_t m_NextImportCompletedHandlerHandle{1u};
        std::vector<RuntimeInputActionRecord> m_InputActions{};
        std::uint64_t m_NextInputActionHandle{1u};
        struct RuntimeAssetImportStreamingTask
        {
            RuntimeAssetIngestHandle Ingest{};
            StreamingTaskHandle Streaming{};
        };
        std::vector<RuntimeAssetImportStreamingTask> m_AssetImportStreamingTasks{};
        std::optional<RuntimeAssetImportEvent>     m_LastAssetImportEvent{};
        std::uint64_t                              m_AssetImportEventSequence{0};
        std::optional<RuntimeSceneFileEvent>       m_LastSceneFileEvent{};
        std::uint64_t                              m_SceneFileEventSequence{0};
        // ECS scene registry
        std::unique_ptr<ECS::Scene::Registry>  m_Scene;
        // Declared after m_Scene so scoped disconnection runs before the
        // registry is destroyed during fallback/destructor unwinding.
        entt::scoped_connection m_StableIdConstructConnection{};
        entt::scoped_connection m_StableIdUpdateConnection{};
        entt::scoped_connection m_StableIdDestroyConnection{};

        // Reference-scene seam (GRAPHICS-029A/B): the registry is
        // constructed empty so tests/impl-B can Register() before
        // Initialize(). Initialize() then idempotently installs the
        // production defaults for any unregistered selectors via
        // RegisterDefaultReferenceProvidersIfAbsent before resolving the
        // configured selector once. The returned entities/camera are stored
        // so Shutdown can route teardown through the same provider and
        // RunFrame can substitute RenderFrameInput::Camera until
        // RUNTIME-081 (CameraControllers) takes over.
        ReferenceSceneRegistry                  m_ReferenceSceneRegistry{};
        ReferenceScenePopulation                m_ReferenceScenePopulation{};
        std::optional<Graphics::CameraViewInput> m_ReferenceCamera{};
        CameraControllerRegistry                m_CameraControllers{};
        bool                                    m_ReferenceSceneInstalled{false};

        Core::FrameClock m_FrameClock{};

        // Fixed-step simulation state
        double m_Accumulator{0.0};
        double m_FixedDt{1.0 / 60.0};          // 60 Hz
        double m_MaxFrameDelta{0.25};           // 250 ms spiral-of-death clamp
        int    m_MaxSubSteps{8};

        bool m_Initialized{false};
        bool m_Running{false};
        bool m_RendererOperational{false};
        bool m_WindowCloseLogged{false};
        RuntimeFramePacingDiagnostics m_LastFramePacingDiagnostics{};

        [[nodiscard]] RuntimeGpuJobParticipantRecord*
            FindRuntimeGpuJobParticipant(
                RuntimeGpuJobParticipantHandle handle) noexcept;
        [[nodiscard]] const RuntimeGpuJobParticipantRecord*
            FindRuntimeGpuJobParticipant(
                RuntimeGpuJobParticipantHandle handle) const noexcept;
        void InstallRuntimeGpuJobParticipantFrameHook(
            RuntimeGpuJobParticipantRecord& participant);
        void UninstallRuntimeGpuJobParticipantFrameHook(
            RuntimeGpuJobParticipantRecord& participant) noexcept;
        void ShutdownRuntimeGpuJobParticipants();
        void ConnectStableEntityLookupTracking();
        void DisconnectStableEntityLookupTracking() noexcept;
        void RebuildStableEntityLookupAfterSceneReplacement();
        void OnStableIdConstruct(entt::registry& registry, entt::entity entity);
        void OnStableIdUpdate(entt::registry& registry, entt::entity entity);
        void OnStableIdDestroy(entt::registry& registry, entt::entity entity);
    };
}
