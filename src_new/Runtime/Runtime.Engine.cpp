module;

#include <memory>
#include <utility>

module Extrinsic.Runtime.Engine;

import Extrinsic.Backends.Null;
import Extrinsic.Core.Config.Render;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Runtime.FrameClock;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr double kIdleSleepSeconds = 0.016; // ~60 Hz event wake

        std::unique_ptr<RHI::IDevice> CreateDevice(
            const Core::Config::RenderConfig& config)
        {
            switch (config.Backend)
            {
            case Core::Config::GraphicsBackend::Vulkan:
                // Vulkan backend is not wired yet; route through the Null stub.
                // IDevice::IsOperational() returns false, so resource managers
                // surface DeviceNotOperational rather than faking GPU work.
                return Backends::Null::CreateNullDevice();
            }
            std::terminate();
        }
    }

    // ── Construction / destruction ────────────────────────────────────────

    Engine::Engine(Core::Config::EngineConfig config,
                   std::unique_ptr<IApplication> application)
        : m_Config(std::move(config))
        , m_Application(std::move(application))
    {
        if (!m_Application)
            std::terminate();
    }

    Engine::~Engine()
    {
        if (m_Initialized)
            Shutdown();
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Engine::Initialize()
    {
        m_Window   = Platform::CreateWindow(m_Config.Window);
        m_Device   = CreateDevice(m_Config.Render);
        m_Device->Initialize(*m_Window, m_Config.Render);
        m_Renderer = Graphics::CreateRenderer();
        m_Renderer->Initialize(*m_Device);

        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running     = true;
    }

    void Engine::Shutdown()
    {
        m_Running = false;

        if (m_Device)
            m_Device->WaitIdle();

        if (m_Application)
            m_Application->OnShutdown(*this);

        if (m_Renderer)
        {
            m_Renderer->Shutdown();
            m_Renderer.reset();
        }

        if (m_Device)
        {
            m_Device->Shutdown();
            m_Device.reset();
        }

        m_Window.reset();
        m_Initialized = false;
    }

    // ── Main loop ─────────────────────────────────────────────────────────

    void Engine::Run()
    {
        while (m_Running && !m_Window->ShouldClose())
            RunFrame();
    }

    void Engine::RunFrame()
    {
        // ── Phase 1: Platform ─────────────────────────────────────────────
        // Event pumping must happen before any frame-time measurement so that
        // resize / close events are visible before we commit to a frame.

        m_Window->PollEvents();
        m_FrameClock.BeginFrame();

        if (m_Window->IsMinimized())
        {
            // Block until the OS delivers an event (window restore, input, etc.)
            // then resample the clock so the sleep time does not count as
            // frame delta — preventing a single huge substep on restore.
            m_Window->WaitForEventsTimeout(kIdleSleepSeconds);
            m_FrameClock.Resample();
            return;
        }

        // Swapchain resize: drain GPU, resize resources, then proceed normally.
        if (m_Window->WasResized())
        {
            const auto extent = m_Window->GetFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                m_Device->WaitIdle();
                m_Device->Resize(static_cast<unsigned>(extent.Width),
                                 static_cast<unsigned>(extent.Height));
                m_Renderer->Resize(static_cast<unsigned>(extent.Width),
                                   static_cast<unsigned>(extent.Height));
            }
            m_Window->AcknowledgeResize();
        }

        // ── Phase 2: Fixed-step simulation ────────────────────────────────
        // Advance the accumulator by a wall-clock delta clamped to
        // m_MaxFrameDelta.  The clamp prevents the spiral-of-death when a
        // frame takes longer than the fixed timestep (e.g. debugger pause).

        const double frameDt = m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta);
        m_Accumulator += frameDt;

        int substeps = 0;
        while (m_Accumulator >= m_FixedDt && substeps < m_MaxSubSteps)
        {
            m_Application->OnSimTick(*this, m_FixedDt);
            m_Accumulator -= m_FixedDt;
            ++substeps;
        }

        // alpha ∈ [0, 1): blend factor between the last committed tick and
        // the next one.  Used by the renderer for position interpolation.
        const double alpha = m_Accumulator / m_FixedDt;

        // ── Phase 3: Variable tick ────────────────────────────────────────
        // Camera, UI state, input processing — anything that runs once per
        // rendered frame and is NOT part of the deterministic simulation.

        m_Application->OnVariableTick(*this, alpha, frameDt);

        // ── Phase 4: Build render snapshot ────────────────────────────────
        // Construct the immutable input that the renderer reads during
        // extraction.  No mutable engine / ECS / asset references are passed.

        const Graphics::RenderFrameInput renderInput{
            .Alpha    = alpha,
            .Viewport = m_Window->GetFramebufferExtent(),
        };

        // ── Phase 5: Renderer — BeginFrame ────────────────────────────────
        // Acquire the swapchain image and open command contexts.
        // Returns false on out-of-date / device-lost / other transient errors;
        // skip the rest of the frame in that case.

        RHI::FrameHandle frame{};
        if (!m_Renderer->BeginFrame(frame))
        {
            m_FrameClock.EndFrame();
            return;
        }

        // ── Phase 6: Renderer — ExtractRenderWorld ────────────────────────
        // Snapshot immutable render data from the committed world state.
        // After this call the renderer owns the extracted data; Engine does
        // not touch ECS / asset state until the next frame's Phase 2.

        Graphics::RenderWorld renderWorld =
            m_Renderer->ExtractRenderWorld(renderInput);

        // ── Phase 7: Renderer — PrepareFrame ──────────────────────────────
        // CPU frustum cull, draw-packet sort, per-frame staging uploads.
        // Reads renderWorld (populated above); may write culled draw lists
        // back into it for Phase 8.

        m_Renderer->PrepareFrame(renderWorld);

        // ── Phase 8: Renderer — ExecuteFrame ──────────────────────────────
        // Record and submit GPU command buffers.  The renderer drives all
        // pass sequencing internally; the loop knows nothing about passes.

        m_Renderer->ExecuteFrame(frame, renderWorld);

        // ── Phase 9: Renderer — EndFrame + Present ────────────────────────
        // Release the frame context back to the ring.
        // completedGpuValue is the timeline value of the oldest in-flight
        // frame that has now finished — used by the maintenance phase below.

        const std::uint64_t completedGpuValue = m_Renderer->EndFrame(frame);
        m_Device->Present(frame);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        // GPU-side resource retirement, staging GC, readback processing.
        // Keyed on completedGpuValue so work is only reclaimed after the
        // GPU has actually finished consuming the resources.
        //
        // Future: m_MaintenanceService->CollectGarbage(completedGpuValue);

        m_Device->CollectCompletedTransfers();
        (void)completedGpuValue; // suppress unused-variable until maintenance service lands

        // ── Phase 11: Clock EndFrame ──────────────────────────────────────
        m_FrameClock.EndFrame();
    }

    // ── Query / control ───────────────────────────────────────────────────

    bool Engine::IsRunning() const noexcept { return m_Running; }
    void Engine::RequestExit()      noexcept { m_Running = false; }

    Platform::IWindow&   Engine::GetWindow()   noexcept { return *m_Window;   }
    RHI::IDevice&        Engine::GetDevice()   noexcept { return *m_Device;   }
    Graphics::IRenderer& Engine::GetRenderer() noexcept { return *m_Renderer; }
}
