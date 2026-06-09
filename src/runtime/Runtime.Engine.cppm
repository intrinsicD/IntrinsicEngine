module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module Extrinsic.Runtime.Engine;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
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

    export struct RuntimeAssetImportResult
    {
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
    };

    export struct RuntimeAssetImportEvent
    {
        std::uint64_t Sequence{0};
        std::string Path{};
        Assets::AssetPayloadKind RequestedPayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::optional<RuntimeAssetImportResult> Result{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Result.has_value() && Error == Core::ErrorCode::Success;
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
    //       Streaming TaskGraph.
    //
    // Three task graphs:
    //   CPU     — Core::FrameGraph wrapping a Dag::TaskGraph(Cpu).
    //             Drives ECS system scheduling each sim tick.
    //             IApplication::OnSimTick adds passes; Engine calls
    //             Compile → Execute → Reset per tick.
    //
    //   GPU     — Owned internally by IRenderer.
    //             Engine drives it via BeginFrame / ExecuteFrame / EndFrame.
    //
    //   Streaming — Dag::TaskGraph(Streaming) owned by Engine.
    //               Asset IO / geometry processing tasks.
    //               IApplication or systems add passes; Engine calls
    //               Compile → BuildPlan → dispatches via Tasks::Scheduler
    //               in Phase 10 (maintenance lane) each frame.
    //
    // Frame shape (executed inside Run()):
    //
    //   PollEvents → BeginFrame(clock)
    //     [if minimized: WaitForEventsTimeout → Resample → continue]
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
    //   StreamingGraph: Compile → BuildPlan → dispatch (Streaming task graph)
    //   AssetService::Tick
    //   StreamingGraph: Reset
    //   EndFrame(clock)
    // ============================================================

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
        [[nodiscard]] Assets::AssetService&   GetAssetService()  noexcept;
        [[nodiscard]] Graphics::GpuAssetCache& GetGpuAssetCache() noexcept;
        [[nodiscard]] ECS::Scene::Registry&   GetScene()         noexcept;
        // UI-001 Slice D — runtime-owned file/import command seam. Editor UI
        // submits a path + payload hint here; Engine composes the promoted
        // ASSETIO geometry/model/texture decoders, AssetService, and runtime
        // handoffs. Platform drop events route through the same facade.
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPath(
            RuntimeAssetImportRequest request);
        [[nodiscard]] const std::optional<RuntimeAssetImportEvent>&
            GetLastAssetImportEvent() const noexcept;
        void ImportDroppedFilePaths(std::span<const std::string> paths);
        // RUNTIME-098 — promoted scene persistence facade. Editor/UI code
        // submits file paths here; Engine owns file IO, scene replacement, and
        // runtime sidecar cleanup while the serializer stays backend-neutral.
        [[nodiscard]] Core::Expected<SceneSerializationResult> SaveSceneToPath(
            std::string path);
        [[nodiscard]] Core::Expected<SceneDeserializationResult> LoadSceneFromPath(
            std::string path);
        [[nodiscard]] Core::Result NewSceneDocument();
        [[nodiscard]] Core::Result CloseSceneDocument();
        // RUNTIME-089 Slice B — runtime/editor-owned selection authority.
        // Input ports / editor tools submit hover/click picks here; RunFrame
        // drains the coalesced pick into the renderer's SelectionSystem before
        // extraction, consumes the readback after present, and mirrors the
        // controller snapshot into RenderWorld::Selection.
        [[nodiscard]] SelectionController&    GetSelectionController() noexcept;
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
        [[nodiscard]] Core::FrameGraph&       GetFrameGraph()    noexcept;
        [[deprecated("Use Runtime.StreamingExecutor integration; TaskGraph bridge is temporary.")]]
        [[nodiscard]] Core::Dag::TaskGraph&   GetStreamingGraph() noexcept;

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
        // UI-001 Slice C — editor/runtime command seams. The engine remains
        // the owner of camera-controller slots and mesh primitive-view state;
        // editor code can request replacements/toggles without storing
        // authoritative camera or render state in UI objects.
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

    private:
        void RunFrame();      // executes one full frame — called by Run()
        void HandlePlatformEvent(const Platform::Event& event);
        void HandleWindowDropEvent(const Platform::WindowDropEvent& event);
        void QueueDroppedGeometryImport(
            std::string path,
            std::vector<Assets::AssetPayloadKind> payloadKinds);
        [[nodiscard]] Core::Expected<RuntimeAssetImportResult> ImportAssetFromPathImpl(
            RuntimeAssetImportRequest request);
        void RecordAssetImportEvent(
            const RuntimeAssetImportRequest& request,
            const Core::Expected<RuntimeAssetImportResult>& result);
        void ClearSceneRuntimeState();

        Core::Config::EngineConfig           m_Config;
        std::unique_ptr<IApplication>        m_Application;
        std::unique_ptr<Platform::IWindow>   m_Window;
        std::unique_ptr<RHI::IDevice>        m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;
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
        // Rebuilt each frame before readback consumption and attached to the
        // controller in Initialize() so selection resolves durable ids through
        // the single runtime authority (ECS/graphics hold no lookup state).
        StableEntityLookup                    m_StableEntityLookup{};
        // RUNTIME-093 Slice B2 — editor-facing refined sub-primitive of the last
        // pick hit. Updated from the pick-readback drain in RunFrame; never read
        // by graphics. Empty until the first pick resolves.
        std::optional<PrimitiveSelectionResult> m_LastRefinedPrimitive{};

        // CPU task graph — ECS system scheduling
        std::unique_ptr<Core::FrameGraph>      m_FrameGraph;
        // Streaming task graph — async asset IO / geometry processing
        std::unique_ptr<Core::Dag::TaskGraph>  m_StreamingGraph;
        // Persistent streaming executor — cross-frame background work
        std::unique_ptr<StreamingExecutor>      m_StreamingExecutor;
        // Asset service — CPU payload authority
        std::unique_ptr<Assets::AssetService>  m_AssetService;
        // GPU-side asset cache — bridges AssetId to refcounted GPU resources.
        // Constructed after the renderer; destroyed before the renderer so
        // BufferLease/TextureLease destructors run while their managers are
        // still alive.
        std::unique_ptr<Graphics::GpuAssetCache> m_GpuAssetCache;
        Assets::AssetEventBus::ListenerToken     m_GpuAssetCacheListener{
            Assets::AssetEventBus::InvalidToken};
        std::unique_ptr<AssetModelTextureHandoff> m_AssetModelTextureHandoff;
        std::unique_ptr<AssetModelSceneHandoff>   m_AssetModelSceneHandoff;
        std::optional<RuntimeAssetImportEvent>     m_LastAssetImportEvent{};
        std::uint64_t                              m_AssetImportEventSequence{0};
        // ECS scene registry
        std::unique_ptr<ECS::Scene::Registry>  m_Scene;

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
    };
}
