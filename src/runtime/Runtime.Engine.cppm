module;

#include <memory>
#include <string_view>
#include <utility>

export module Extrinsic.Runtime.Engine;

import Extrinsic.Core.Config.Engine;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

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
        // Contract-test seam: replay a platform event through the same runtime
        // handler installed as the window listener during Initialize().
        void DispatchPlatformEventForTest(const Platform::Event& event);

        // UI-030 — last frame-loop pacing sample. Runtime owns the cross-layer
        // phase boundaries and the Sandbox report is the production reader;
        // renderer/backend-specific diagnostics remain on their owning surfaces.
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

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
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
