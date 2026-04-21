module;

#include <cstdint>
#include <memory>

export module Extrinsic.Runtime.Engine;

import Extrinsic.Core.Config.Engine;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.FrameClock;

namespace Extrinsic::Runtime
{
    export class Engine;

    // ============================================================
    // IApplication — the user-facing hook interface.
    //
    // Lifecycle order guaranteed by Engine:
    //
    //   OnInitialize(engine)           — once, after all subsystems are live.
    //
    //   per frame:
    //     OnSimTick(engine, fixedDt)   — 0..N times, fixed timestep.
    //                                    World state is authoritative after
    //                                    the last tick of each frame.
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
    // Owns: Window, IDevice, IRenderer, FrameClock.
    // Wires: subsystem instantiation order, deterministic shutdown.
    //
    // Frame shape (executed inside Run() ):
    //
    //   PollEvents → BeginFrame(clock)
    //     [if minimized: WaitForEventsTimeout → Resample → continue]
    //     [if resized:   WaitIdle → Resize]
    //   FixedStepLoop { OnSimTick × N }
    //   OnVariableTick(alpha, dt)
    //   BuildRenderFrameInput
    //   Renderer::BeginFrame
    //     [if skip: EndFrame(clock) → continue]
    //   Renderer::ExtractRenderWorld
    //   Renderer::PrepareFrame
    //   Renderer::ExecuteFrame
    //   Renderer::EndFrame → completedGpuValue
    //   Device::Present
    //   Device::CollectCompletedTransfers
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
        [[nodiscard]] Platform::IWindow&    GetWindow()   noexcept;
        [[nodiscard]] RHI::IDevice&         GetDevice()   noexcept;
        [[nodiscard]] Graphics::IRenderer&  GetRenderer() noexcept;

    private:
        void RunFrame();      // executes one full frame — called by Run()

        Core::Config::EngineConfig        m_Config;
        std::unique_ptr<IApplication>     m_Application;
        std::unique_ptr<Platform::IWindow> m_Window;
        std::unique_ptr<RHI::IDevice>     m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;

        FrameClock m_FrameClock{};

        // Fixed-step simulation state
        double m_Accumulator{0.0};
        double m_FixedDt{1.0 / 60.0};          // 60 Hz
        double m_MaxFrameDelta{0.25};           // 250 ms spiral-of-death clamp
        int    m_MaxSubSteps{8};

        bool m_Initialized{false};
        bool m_Running{false};
    };
}
