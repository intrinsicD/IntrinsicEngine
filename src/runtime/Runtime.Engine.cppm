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
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.JobServiceGpuQueueBridge;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleSchedule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.ECS.Scene.Registry;

#include "Runtime.RenderExtractionService.Internal.hpp"

namespace Extrinsic::Runtime
{
    export class Engine;

    // ============================================================
    // Engine — composition root and frame-loop owner.
    //
    // Owns: Window, IDevice, IRenderer, FrameClock,
    //       Tasks::Scheduler (static — initialized/shutdown here),
    //       WorldRegistry, Scene::Registry, FrameGraph (CPU).
    //
    // Scheduling surfaces:
    //   CPU     — Core::FrameGraph wrapping a Dag::TaskGraph(Cpu).
    //             Drives ECS system scheduling each sim tick.
    //             The promoted ECS bundle adds passes; Engine calls
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
    //     Promoted ECS systems × N
    //       FrameGraph: Compile → Execute → ResetForReplay  (CPU task graph)
    //   }
    //   Module frame hooks
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
    //   Optional published asset hook + generic geometry retirement
    //   EndFrame(clock)
    // ============================================================

    export class Engine
    {
    public:
        explicit Engine(Core::Config::EngineConfig config);
        ~Engine();

        Engine(const Engine&)            = delete;
        Engine& operator=(const Engine&) = delete;

        void Initialize();
        void Run();
        // Stop runtime work, announce shutdown, drain GPU participants, and
        // wait for device idle while module services and worlds remain live.
        // An app with concrete teardown state calls this, tears that state down,
        // then calls Shutdown(). Shutdown() calls BeginShutdown() itself when no
        // app-owned teardown needs to be interposed.
        void BeginShutdown();
        void Shutdown();

        [[nodiscard]] bool IsRunning()    const noexcept;
        void RequestExit()                      noexcept;

        // ── Subsystem accessors (valid after Initialize()) ────────────────
        [[nodiscard]] Platform::IWindow&      GetWindow()        noexcept;
        [[nodiscard]] RHI::IDevice&           GetDevice()        noexcept;
        [[nodiscard]] Graphics::IRenderer&    GetRenderer()      noexcept;
        [[nodiscard]] const Core::Config::EngineConfig&
            GetEngineConfig() const noexcept;
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
        [[nodiscard]] RuntimeInputActionHandle RegisterInputAction(
            RuntimeInputActionDesc desc);
        void UnregisterInputAction(RuntimeInputActionHandle handle);
        // Contract-test seam: replay a platform event through the same runtime
        // handler installed as the window listener during Initialize().
        void DispatchPlatformEventForTest(const Platform::Event& event);
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
        std::vector<std::unique_ptr<IRuntimeModule>> m_RuntimeModules{};
        std::unique_ptr<Platform::IWindow>   m_Window;
        std::unique_ptr<RHI::IDevice>        m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;
        // RUNTIME-163 — render extraction owner state and compatibility facades.
        // Engine keeps frame ordering and dependent-subsystem wiring; the service
        // owns the cache, pool, last stats, and frame-index counter.
        RenderExtractionService               m_RenderExtractionService{};
        // CPU task graph — ECS system scheduling
        std::unique_ptr<Core::FrameGraph>      m_FrameGraph;
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
        RuntimeModuleSchedule                  m_RuntimeModuleSchedule{};

        Core::FrameClock m_FrameClock{};

        // Fixed-step simulation state
        double m_Accumulator{0.0};
        double m_FixedDt{1.0 / 60.0};          // 60 Hz
        double m_MaxFrameDelta{0.25};           // 250 ms spiral-of-death clamp
        int    m_MaxSubSteps{8};

        bool m_Initialized{false};
        bool m_Running{false};
        bool m_ShutdownBegun{false};
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
        void RunRuntimeModuleFrameHooks(
            FramePhase phase,
            double frameDt,
            double alpha,
            EditorInputCaptureSnapshot& editorCapture,
            RuntimeFramePacingDiagnostics& pacing);
        void AnnounceRuntimeShutdown();
        void ShutdownRuntimeModules();
        void RefreshActiveWorldScenePointer() noexcept;
        void ApplyWorldRegistryMaintenance();
    };
}
