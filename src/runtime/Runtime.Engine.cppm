module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

export module Extrinsic.Runtime.Engine;

export import Extrinsic.Runtime.FramePacingDiagnostics;
export import Extrinsic.Runtime.InputActions;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoFrameService;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.JobServiceGpuQueueBridge;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleSchedule;
import Extrinsic.Runtime.ObjectSpaceNormalBakeService;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.ReferenceSceneControl;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneDocument;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.SelectionReadback;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Registry;

#include "Runtime.RenderExtractionService.Internal.hpp"

namespace Extrinsic::Runtime
{
    class AssetResidencyService;

    export class Engine;

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
    //                                    Engine calls
    //                                    Compile→Execute→ResetForReplay.
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
    //       AssetService, Scene::Registry, FrameGraph (CPU).
    //
    // Scheduling surfaces:
    //   CPU     — Core::FrameGraph wrapping a Dag::TaskGraph(Cpu).
    //             Drives ECS system scheduling each sim tick.
    //             IApplication::OnSimTick adds passes; Engine calls
    //             Compile → Execute → ResetForReplay per tick.
    //
    //   GPU     — Owned internally by IRenderer.
    //             Engine drives it via BeginFrame / ExecuteFrame / EndFrame.
    //
    //   Streaming — an app-composed runtime module may publish the existing
    //               streaming Maintenance hook and executor capabilities.
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
    //       FrameGraph: Compile → Execute → ResetForReplay  (CPU task graph)
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
        [[nodiscard]] Assets::AssetService&   GetAssetService()  noexcept;
        [[nodiscard]] Graphics::GpuAssetCache& GetGpuAssetCache() noexcept;
        [[nodiscard]] const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
            GetObjectSpaceNormalBakeQueueDiagnosticsForTest() const noexcept;
        [[nodiscard]] std::size_t
            GetPendingObjectSpaceNormalBakeCountForTest() const noexcept;
        [[nodiscard]] ECS::Scene::Registry&   GetScene()         noexcept;
        // ARCH-007 — kernel command bus (ADR-0024 D5). Enqueue from any
        // thread/phase; the Engine drains once per frame between platform
        // input and the fixed-step simulation.
        [[nodiscard]] CommandBus&             Commands()         noexcept;
        // ARCH-008 — queued-only kernel event bus (ADR-0024 D7). Publish
        // from any thread/phase; the Engine pumps once after the command
        // drain and once after fixed-step simulation.
        [[nodiscard]] KernelEventBus&         Events()           noexcept;
        // ARCH-009 — snapshot-in/result-out background jobs (ADR-0024 D8).
        // Workers receive immutable snapshots and cancellation views; only the
        // Engine-owned main-thread completion gate publishes job events.
        [[nodiscard]] JobService&             Jobs()             noexcept;
        // ARCH-010 — world registry owns scene lifetimes; legacy single-world
        // accessors resolve through ActiveWorld() until module extractions
        // carry WorldHandle explicitly.
        [[nodiscard]] WorldRegistry&          Worlds()           noexcept;
        [[nodiscard]] const WorldRegistry&    Worlds() const     noexcept;
        [[nodiscard]] WorldHandle             ActiveWorld() const noexcept;
        // ARCH-011 — runtime modules register against EngineSetup, never an
        // Engine&. Modules are registered before Initialize(); boot invokes all
        // OnRegister callbacks, then all OnResolve callbacks after the
        // two-phase ServiceRegistry has every provided service.
        void AddModule(std::unique_ptr<IRuntimeModule> module);
        template <typename TModule, typename... Args>
        TModule& EmplaceModule(Args&&... args)
        {
            auto module = std::make_unique<TModule>(std::forward<Args>(args)...);
            TModule& ref = *module;
            AddModule(std::move(module));
            return ref;
        }
        [[nodiscard]] ServiceRegistry& Services() noexcept;
        [[nodiscard]] const ServiceRegistry& Services() const noexcept;
        // RUNTIME-147 — runtime-owned asset import pipeline. Engine keeps
        // composition ownership; callers use the subsystem surface directly
        // instead of widening the Engine facade with import-specific methods.
        [[nodiscard]] AssetImportPipeline& GetAssetImportPipeline() noexcept;
        [[nodiscard]] const AssetImportPipeline& GetAssetImportPipeline() const noexcept;
        // RUNTIME-148 — runtime-owned scene-document subsystem. Engine keeps
        // composition ownership; callers use the subsystem surface directly
        // instead of widening the Engine facade with scene-file methods.
        [[nodiscard]] SceneDocument& GetSceneDocument() noexcept;
        [[nodiscard]] const SceneDocument& GetSceneDocument() const noexcept;
        [[nodiscard]] RuntimeInputActionHandle RegisterInputAction(
            RuntimeInputActionDesc desc);
        void UnregisterInputAction(RuntimeInputActionHandle handle);
        // Contract-test seam: replay a platform event through the same runtime
        // handler installed as the window listener during Initialize().
        void DispatchPlatformEventForTest(const Platform::Event& event);
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
        // ── Reference scene seam (GRAPHICS-029A/B) ────────────────────────
        // Accessible before Initialize() so tests and downstream impl
        // children register providers prior to subsystem wiring. After
        // Initialize() runs, the registry is locked to its installed
        // contents — Register() before Initialize(), Resolve() after.
        [[nodiscard]] ReferenceSceneRegistry& GetReferenceSceneRegistry() noexcept;
        [[nodiscard]] bool IsReferenceSceneInstalled() const noexcept;
        // GRAPHICS-029B/RUNTIME-081A: the optional CameraViewInput seed captured
        // from the installed reference-scene provider. Empty when the reference
        // scene is disabled or the provider returned no camera. The promoted
        // camera-controller surface consumes this as initial state; it is
        // retained as a test seam and no longer directly fills
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
        [[nodiscard]] RuntimeRenderRecipeActivationKernel
            MakeRenderRecipeActivationKernel(
                RuntimeRenderRecipeState& state) noexcept;

        Core::Config::EngineConfig           m_Config;
        std::unique_ptr<IApplication>        m_Application;
        std::vector<std::unique_ptr<IRuntimeModule>> m_RuntimeModules{};
        std::unique_ptr<Platform::IWindow>   m_Window;
        std::unique_ptr<RHI::IDevice>        m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;
        // RUNTIME-163 — render extraction owner state and compatibility facades.
        // Engine keeps frame ordering and dependent-subsystem wiring; the service
        // owns the cache, pool, last stats, and frame-index counter.
        RenderExtractionService               m_RenderExtractionService{};
        // RUNTIME-089 Slice B — selection authority; persists across frames so
        // in-flight picks correlate with their later readbacks.
        SelectionController                   m_SelectionController{};
        // RUNTIME-102 — runtime/editor-owned undo/redo and document dirty-state
        // source. UI reads snapshots from this service and command facades mark
        // save/load/import state here instead of keeping authoritative document
        // state in panel objects.
        EditorCommandHistory                  m_EditorCommandHistory{};
        // RUNTIME-162 — runtime/editor transform-gizmo frame service. Engine
        // keeps frame order and public facades; the service owns interaction
        // state, undo stack, selected-entity scratch, and packet production.
        GizmoFrameService                    m_GizmoFrameService{};
        // RUNTIME-092 Slice B — runtime-owned StableId/render-id lookup sidecar.
        // Maintained incrementally from StableId component construct/update/
        // destroy events and attached to the controller in Initialize() so
        // selection resolves durable ids through the single runtime authority
        // (ECS/graphics hold no lookup state). Whole-scene replacement still
        // uses Rebuild() once at the replacement boundary.
        StableEntityLookup                    m_StableEntityLookup{};
        // RUNTIME-157 — runtime-owned bridge for graphics pick readbacks,
        // controller mutation, and editor-facing primitive refinement cache.
        SelectionReadbackState                  m_SelectionReadback{};

        // CPU task graph — ECS system scheduling
        std::unique_ptr<Core::FrameGraph>      m_FrameGraph;
        // Asset service — CPU payload authority
        std::unique_ptr<Assets::AssetService>  m_AssetService;
        // RUNTIME-164 — GPU-side asset residency owner state. Engine keeps
        // lifecycle/frame ordering and public facades; the service owns the
        // cache, asset-event listener, model handoffs, and cache/handoff ticks.
        std::unique_ptr<AssetResidencyService>   m_AssetResidencyService;
        ObjectSpaceNormalBakeService             m_ObjectSpaceNormalBakeService{};
        std::unique_ptr<AssetImportPipeline>      m_AssetImportPipeline;
        std::unique_ptr<SceneDocument>            m_SceneDocument;
        RuntimeInputActionRegistry            m_InputActions{};
        // ARCH-007 — kernel command bus; drained in RunFrame() pre-sim.
        CommandBus                             m_CommandBus{};
        // ARCH-008 — queued-only kernel events; pumped post-drain and post-sim.
        KernelEventBus                         m_KernelEvents{};
        // ARCH-009 — kernel background jobs; completions gate before pump B.
        JobService                             m_JobService{};
        // ARCH-011 — two-phase module service registry. Engine provides the
        // always-present kernel services during module registration; feature
        // modules provide their own synchronous infrastructure there and
        // require/find dependencies only during resolution.
        ServiceRegistry                        m_ServiceRegistry{};
        // ARCH-010 — kernel world registry owns scene registries; m_Scene is
        // the cached active-world pointer used by legacy single-world call
        // sites until later module extractions carry WorldHandle explicitly.
        WorldRegistry                          m_WorldRegistry{};
        ECS::Scene::Registry*                  m_Scene{};
        // RUNTIME-151 — owns StableId signal connections outside the Engine
        // interface so EnTT plumbing stays behind StableEntityLookup.
        // Declared after m_WorldRegistry so scoped disconnection runs before
        // scene registries are destroyed during fallback/destructor unwinding.
        StableEntityLookupSceneBinding         m_StableEntityLookupBinding{};
        RuntimeModuleSchedule                  m_RuntimeModuleSchedule{};

        // Reference-scene seam (GRAPHICS-029A/B): constructed empty so
        // tests/impl-B can Register() before Initialize(); install and teardown
        // policy lives behind ReferenceSceneControl.
        ReferenceSceneControl                  m_ReferenceSceneControl{};
        CameraControllerRegistry                m_CameraControllers{};

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

        // RUNTIME-160 — runtime-owned bridge from JobService GPU queue
        // participants to the renderer frame-command hook.
        JobServiceGpuQueueBridge m_JobServiceGpuQueueBridge{};
        void RegisterRuntimeModulesForBoot(
            const RuntimeRenderRecipeActivationKernel& recipeActivation);
        void ResolveRuntimeModulesForBoot(
            const RuntimeRenderRecipeActivationKernel& recipeActivation);
        void RegisterRuntimeModuleSimSystemsForTick(
            Core::FrameGraph& graph,
            ECS::Scene::Registry& scene,
            double fixedDt);
        void RunRuntimeModuleFrameHooks(
            FramePhase phase,
            double frameDt,
            double alpha,
            EditorInputCaptureSnapshot& editorCapture,
            RuntimeFramePacingDiagnostics& pacing);
        void AnnounceAndShutdownRuntimeModules();
        void RefreshActiveWorldScenePointer() noexcept;
        void ApplyWorldRegistryMaintenance();
        void RebuildStableEntityLookupAfterSceneReplacement();
        void BindActiveSceneAssetHandoffs();
    };
}
