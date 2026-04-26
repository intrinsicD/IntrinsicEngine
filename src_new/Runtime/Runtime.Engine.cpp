module;

#include <memory>
#include <limits>
#include <utility>
#include <vector>

module Extrinsic.Runtime.Engine;

import Extrinsic.Backends.Null;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Logging;
import Extrinsic.Core.Tasks;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Runtime.FrameClock;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Scene.Registry;

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

        // Converts frame-recorded streaming passes into persistent executor tasks.
        // Kept as compatibility bridge while call sites still populate GetStreamingGraph().
        void SubmitStreamingGraphToExecutor(Core::Dag::TaskGraph& graph, StreamingExecutor& executor)
        {
            if (graph.PassCount() == 0)
                return;

            if (auto r = graph.Compile(); !r.has_value())
            {
                Core::Log::Error("[Runtime] StreamingGraph Compile() failed: error={}",
                           static_cast<int>(r.error()));
                graph.Reset();
                return;
            }

            auto plan = graph.BuildPlan();
            if (!plan.has_value())
            {
                Core::Log::Error("[Runtime] StreamingGraph BuildPlan() failed: error={}",
                           static_cast<int>(plan.error()));
                graph.Reset();
                return;
            }

            // Convert layer order into coarse dependencies:
            // every task in batch N depends on all submitted tasks from batches < N.
            // This preserves correctness and determinism with possible over-serialization.
            std::vector<StreamingTaskHandle> priorBatches{};
            std::vector<StreamingTaskHandle> currentBatch{};
            std::uint32_t activeBatch = std::numeric_limits<std::uint32_t>::max();

            for (const auto& task : *plan)
            {
                if (task.batch != activeBatch)
                {
                    priorBatches.insert(priorBatches.end(), currentBatch.begin(), currentBatch.end());
                    currentBatch.clear();
                    activeBatch = task.batch;
                }

                auto fn = graph.TakePassExecute(task.id.Index);
                if (fn)
                {
                    auto handle = executor.Submit(StreamingTaskDesc{
                        .Name = "StreamingPass",
                        .DependsOn = priorBatches,
                        .Execute = [f = std::move(fn)]() mutable
                        {
                            f();
                            return StreamingResult{};
                        },
                    });

                    if (handle.IsValid())
                    {
                        currentBatch.push_back(handle);
                    }
                }
            }

            graph.Reset();
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
        // ── 1. CPU fiber scheduler ────────────────────────────────────────
        // Must be first — all three graphs dispatch through it.
        Core::Tasks::Scheduler::Initialize(m_Config.Simulation.WorkerThreadCount);

        // ── 2. Subsystems ─────────────────────────────────────────────────
        m_Window   = Platform::CreateWindow(m_Config.Window);
        m_Device   = CreateDevice(m_Config.Render);
        m_Device->Initialize(*m_Window, m_Config.Render);
        m_Renderer = Graphics::CreateRenderer();
        m_Renderer->Initialize(*m_Device);

        // ── 3. CPU task graph (ECS system scheduling) ─────────────────────
        m_FrameGraph = std::make_unique<Core::FrameGraph>();

        // ── 4. Streaming task graph (asset IO / geometry processing) ──────
        m_StreamingGraph = Core::Dag::CreateTaskGraph(Core::Dag::QueueDomain::Streaming);
        m_StreamingExecutor = std::make_unique<StreamingExecutor>();

        // ── 5. Asset service ──────────────────────────────────────────────
        m_AssetService = std::make_unique<Assets::AssetService>();

        // ── 6. ECS scene ──────────────────────────────────────────────────
        m_Scene = std::make_unique<ECS::Scene::Registry>();

        // ── 7. Application ────────────────────────────────────────────────
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

        if (m_StreamingExecutor)
        {
            m_StreamingExecutor->ShutdownAndDrain();
        }

        m_Scene.reset();
        m_AssetService.reset();
        m_StreamingExecutor.reset();
        m_StreamingGraph.reset();
        m_FrameGraph.reset();

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

        // Shut down the fiber scheduler last — worker threads must exit cleanly
        // before any other thread-local storage or allocators are destroyed.
        Core::Tasks::Scheduler::Shutdown();

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
        m_Window->PollEvents();
        m_FrameClock.BeginFrame();

        if (m_Window->IsMinimized())
        {
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

        // ── Phase 2: Fixed-step simulation + CPU task graph ───────────────
        // Each tick: app adds FrameGraph passes → Engine compiles and executes
        // the ECS system DAG → reset for next tick.

        const double frameDt = m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta);
        m_Accumulator += frameDt;

        int substeps = 0;
        while (m_Accumulator >= m_FixedDt && substeps < m_MaxSubSteps)
        {
            // App registers system passes via engine.GetFrameGraph().AddPass(...)
            m_Application->OnSimTick(*this, m_FixedDt);

            // CPU task graph: compile dependency order, execute in topo-layer
            // sequence (currently sequential execution), then reset.
            if (m_FrameGraph->PassCount() > 0)
            {
                if (auto r = m_FrameGraph->Compile(); r.has_value())
                {
                    if (auto exec = m_FrameGraph->Execute(); !exec.has_value())
                    {
                        Core::Log::Error("[Runtime] FrameGraph Execute() failed: error={}",
                                   static_cast<int>(exec.error()));
                    }
                }
                else
                {
                    Core::Log::Error("[Runtime] FrameGraph Compile() failed: error={}",
                               static_cast<int>(r.error()));
                }
                m_FrameGraph->Reset();
            }

            m_Accumulator -= m_FixedDt;
            ++substeps;
        }

        const double alpha = m_Accumulator / m_FixedDt;

        // ── Phase 3: Variable tick ────────────────────────────────────────
        m_Application->OnVariableTick(*this, alpha, frameDt);

        // ── Phase 4: Build render snapshot ────────────────────────────────
        const Graphics::RenderFrameInput renderInput{
            .Alpha    = alpha,
            .Viewport = m_Window->GetFramebufferExtent(),
        };

        // ── Phase 5: Renderer — BeginFrame (GPU task graph — acquire) ─────
        RHI::FrameHandle frame{};
        if (!m_Renderer->BeginFrame(frame))
        {
            m_FrameClock.EndFrame();
            return;
        }

        // ── Phase 6: Renderer — ExtractRenderWorld ────────────────────────
        Graphics::RenderWorld renderWorld =
            m_Renderer->ExtractRenderWorld(renderInput);

        // ── Phase 7: Renderer — PrepareFrame ──────────────────────────────
        m_Renderer->PrepareFrame(renderWorld);

        // ── Phase 8: Renderer — ExecuteFrame (GPU task graph) ─────────────
        // IRenderer owns its GPU Dag::TaskGraph(Gpu) internally.
        // It compiles render-pass dependencies, resolves virtual-resource
        // lifetimes, emits Vulkan Sync2 barriers, and records command buffers.
        m_Renderer->ExecuteFrame(frame, renderWorld);

        // ── Phase 9: Renderer — EndFrame + Present ────────────────────────
        const std::uint64_t completedGpuValue = m_Renderer->EndFrame(frame);
        m_Device->Present(frame);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        // GPU-side resource retirement, staging GC, readback processing.
        m_Device->GetTransferQueue().CollectCompleted();
        (void)completedGpuValue; // placeholder until GpuAssetCache / deferred-delete lands

        m_StreamingExecutor->DrainCompletions();
        m_StreamingExecutor->ApplyMainThreadResults();

        // Asset service main-thread tick: advances state machines, fires
        // AssetEventBus::Ready / Reloaded / Destroyed callbacks.
        m_AssetService->Tick();

        SubmitStreamingGraphToExecutor(*m_StreamingGraph, *m_StreamingExecutor);
        m_StreamingExecutor->PumpBackground(8);

        // ── Phase 11: Clock EndFrame ──────────────────────────────────────
        m_FrameClock.EndFrame();
    }

    // ── Query / control ───────────────────────────────────────────────────

    bool Engine::IsRunning() const noexcept { return m_Running; }
    void Engine::RequestExit()      noexcept { m_Running = false; }

    Platform::IWindow&    Engine::GetWindow()        noexcept { return *m_Window;        }
    RHI::IDevice&         Engine::GetDevice()        noexcept { return *m_Device;        }
    Graphics::IRenderer&  Engine::GetRenderer()      noexcept { return *m_Renderer;      }
    Assets::AssetService& Engine::GetAssetService()  noexcept { return *m_AssetService;  }
    ECS::Scene::Registry& Engine::GetScene()         noexcept { return *m_Scene;         }
    Core::FrameGraph&     Engine::GetFrameGraph()    noexcept { return *m_FrameGraph;    }
    Core::Dag::TaskGraph& Engine::GetStreamingGraph() noexcept { return *m_StreamingGraph; }
}
