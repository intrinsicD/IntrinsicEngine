module;

#include <cstdint>
#include <memory>
#include <optional>

export module Extrinsic.Runtime.Engine;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Asset.EventBus;
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

    export [[nodiscard]] inline RuntimeDeviceSelection SelectRuntimeDeviceBackend(
        const Core::Config::RenderConfig& config,
        const bool promotedVulkanAvailable) noexcept
    {
        switch (config.Backend)
        {
        case Core::Config::GraphicsBackend::Vulkan:
            if (config.EnablePromotedVulkanDevice && promotedVulkanAvailable)
            {
                return RuntimeDeviceSelection{
                    .UsePromotedVulkanDevice = true,
                    .FallsBackToNullDevice = false,
                };
            }
            return RuntimeDeviceSelection{};
        }
        return RuntimeDeviceSelection{};
    }

    export [[nodiscard]] inline Core::Config::EngineConfig CreateReferenceEngineConfig()
    {
        Core::Config::EngineConfig config{};
        config.Window.Title = "Modular Vulkan Engine";
        config.Window.Width = 1600;
        config.Window.Height = 900;
        config.Render.Backend = Core::Config::GraphicsBackend::Vulkan;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = true;
        config.Render.FramesInFlight = 2;
        config.ReferenceScene.Enabled = true;
        config.ReferenceScene.Selector = Core::Config::ReferenceSceneSelector::Triangle;
        return config;
    }

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
        [[nodiscard]] Core::FrameGraph&       GetFrameGraph()    noexcept;
        [[deprecated("Use Runtime.StreamingExecutor integration; TaskGraph bridge is temporary.")]]
        [[nodiscard]] Core::Dag::TaskGraph&   GetStreamingGraph() noexcept;

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

    private:
        void RunFrame();      // executes one full frame — called by Run()

        Core::Config::EngineConfig           m_Config;
        std::unique_ptr<IApplication>        m_Application;
        std::unique_ptr<Platform::IWindow>   m_Window;
        std::unique_ptr<RHI::IDevice>        m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;
        RenderExtractionCache                 m_RenderExtraction;

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
