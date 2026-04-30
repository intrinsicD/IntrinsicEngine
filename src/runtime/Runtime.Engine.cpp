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
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Runtime.FrameClock;
import Extrinsic.Runtime.FrameLoop;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Registry;
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

        // ── 5b. GPU asset cache ───────────────────────────────────────────
        // Bridges AssetId to refcounted Buffer/Texture leases.  Subscribes
        // to AssetEventBus for Failed / Reloaded / Destroyed transitions;
        // type-specific bridges drive RequestUpload separately.
        m_GpuAssetCache = std::make_unique<Graphics::GpuAssetCache>(
            m_Renderer->GetBufferManager(),
            m_Renderer->GetTextureManager(),
            m_Device->GetTransferQueue());
        m_GpuAssetCacheListener = m_AssetService->SubscribeAll(
            [cache = m_GpuAssetCache.get()](Assets::AssetId id, Assets::AssetEvent ev)
            {
                switch (ev)
                {
                case Assets::AssetEvent::Failed:    cache->NotifyFailed(id);    break;
                case Assets::AssetEvent::Reloaded:  cache->NotifyReloaded(id);  break;
                case Assets::AssetEvent::Destroyed: cache->NotifyDestroyed(id); break;
                case Assets::AssetEvent::Ready:     /* no-op: type-specific bridges
                                                       drive RequestUpload */    break;
                }
            });

        // ── 6. ECS scene ──────────────────────────────────────────────────
        m_Scene = std::make_unique<ECS::Scene::Registry>();

        // ── 7. Application ────────────────────────────────────────────────
        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running     = true;
    }

    void Engine::Shutdown()
    {
        struct ShutdownHooks final : IRuntimeShutdownHooks
        {
            Engine& Owner;
            bool& Running;
            bool& Initialized;
            std::unique_ptr<IApplication>& Application;
            std::unique_ptr<Platform::IWindow>& Window;
            std::unique_ptr<RHI::IDevice>& Device;
            std::unique_ptr<Graphics::IRenderer>& Renderer;
            std::unique_ptr<Core::FrameGraph>& FrameGraph;
            std::unique_ptr<Core::Dag::TaskGraph>& StreamingGraph;
            std::unique_ptr<StreamingExecutor>& StreamingExecutorPtr;
            std::unique_ptr<Assets::AssetService>& AssetService;
            std::unique_ptr<Graphics::GpuAssetCache>& GpuAssetCache;
            Assets::AssetEventBus::ListenerToken& GpuAssetCacheListener;
            std::unique_ptr<ECS::Scene::Registry>& Scene;

            ShutdownHooks(Engine& owner,
                          bool& running,
                          bool& initialized,
                          std::unique_ptr<IApplication>& application,
                          std::unique_ptr<Platform::IWindow>& window,
                          std::unique_ptr<RHI::IDevice>& device,
                          std::unique_ptr<Graphics::IRenderer>& renderer,
                          std::unique_ptr<Core::FrameGraph>& frameGraph,
                          std::unique_ptr<Core::Dag::TaskGraph>& streamingGraph,
                          std::unique_ptr<StreamingExecutor>& streamingExecutor,
                          std::unique_ptr<Assets::AssetService>& assetService,
                          std::unique_ptr<Graphics::GpuAssetCache>& gpuAssetCache,
                          Assets::AssetEventBus::ListenerToken& gpuAssetCacheListener,
                          std::unique_ptr<ECS::Scene::Registry>& scene)
                : Owner(owner)
                , Running(running)
                , Initialized(initialized)
                , Application(application)
                , Window(window)
                , Device(device)
                , Renderer(renderer)
                , FrameGraph(frameGraph)
                , StreamingGraph(streamingGraph)
                , StreamingExecutorPtr(streamingExecutor)
                , AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , GpuAssetCacheListener(gpuAssetCacheListener)
                , Scene(scene)
            {
            }

            void StopRunning() override { Running = false; }
            void WaitDeviceIdle() override
            {
                if (Device)
                    Device->WaitIdle();
            }
            void ShutdownApplication() override
            {
                if (Application)
                    Application->OnShutdown(Owner);
            }
            void ShutdownStreaming() override
            {
                if (StreamingExecutorPtr)
                    StreamingExecutorPtr->ShutdownAndDrain();
            }
            void DestroyScene() override { Scene.reset(); }
            void DestroyAssets() override
            {
                // Unsubscribe before destroying the cache so a late event
                // flush cannot reach a freed cache.  The cache is destroyed
                // before the renderer (which owns Buffer/Texture managers)
                // so leases unwind through live managers.
                if (AssetService &&
                    GpuAssetCacheListener != Assets::AssetEventBus::InvalidToken)
                {
                    AssetService->UnsubscribeAll(GpuAssetCacheListener);
                    GpuAssetCacheListener = Assets::AssetEventBus::InvalidToken;
                }
                GpuAssetCache.reset();
                AssetService.reset();
            }
            void DestroyStreamingState() override
            {
                StreamingExecutorPtr.reset();
                StreamingGraph.reset();
            }
            void DestroyFrameGraph() override { FrameGraph.reset(); }
            void ShutdownRenderer() override
            {
                if (Renderer)
                {
                    Renderer->Shutdown();
                    Renderer.reset();
                }
            }
            void ShutdownDevice() override
            {
                if (Device)
                {
                    Device->Shutdown();
                    Device.reset();
                }
            }
            void DestroyWindow() override { Window.reset(); }
            void ShutdownScheduler() override
            {
                // Shut down the fiber scheduler last — worker threads must exit cleanly
                // before any other thread-local storage or allocators are destroyed.
                Core::Tasks::Scheduler::Shutdown();
            }
            void MarkUninitialized() override { Initialized = false; }
        };

        ShutdownHooks hooks(*this,
                            m_Running,
                            m_Initialized,
                            m_Application,
                            m_Window,
                            m_Device,
                            m_Renderer,
                            m_FrameGraph,
                            m_StreamingGraph,
                            m_StreamingExecutor,
                            m_AssetService,
                            m_GpuAssetCache,
                            m_GpuAssetCacheListener,
                            m_Scene);
        ExecuteRuntimeShutdownContract(hooks);
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

        // ── Phases 5–9: promoted render-frame contract ───────────────────
        RHI::FrameHandle frame{};
        Graphics::RenderWorld renderWorld{};

        struct RenderFrameHooks final : IRuntimeRenderFrameHooks
        {
            Graphics::IRenderer& Renderer;
            RHI::FrameHandle& Frame;
            const Graphics::RenderFrameInput& Input;
            Graphics::RenderWorld& World;

            RenderFrameHooks(Graphics::IRenderer& renderer,
                             RHI::FrameHandle& frame,
                             const Graphics::RenderFrameInput& input,
                             Graphics::RenderWorld& world)
                : Renderer(renderer)
                , Frame(frame)
                , Input(input)
                , World(world)
            {
            }

            bool BeginFrame() override { return Renderer.BeginFrame(Frame); }
            void ExtractRenderWorld() override { World = Renderer.ExtractRenderWorld(Input); }
            void PrepareFrame() override { Renderer.PrepareFrame(World); }
            void ExecuteFrame() override { Renderer.ExecuteFrame(Frame, World); }
            std::uint64_t EndFrame() override { return Renderer.EndFrame(Frame); }
        };

        RenderFrameHooks renderHooks(*m_Renderer, frame, renderInput, renderWorld);

        const RuntimeRenderFrameResult renderResult = ExecuteRuntimeRenderFrameContract(renderHooks);
        if (!renderResult.BeganFrame)
        {
            m_FrameClock.EndFrame();
            return;
        }

        const std::uint64_t completedGpuValue = renderResult.CompletedGpuValue;
        m_Device->Present(frame);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        struct TransferHooks final : IRuntimeTransferFrameHooks
        {
            RHI::IDevice& Device;

            explicit TransferHooks(RHI::IDevice& device)
                : Device(device)
            {
            }

            void CollectCompletedTransfers() override
            {
                // GPU-side resource retirement, staging GC, readback processing.
                Device.GetTransferQueue().CollectCompleted();
            }
        };

        struct StreamingHooks final : IRuntimeStreamingFrameHooks
        {
            Core::Dag::TaskGraph& Graph;
            StreamingExecutor& Executor;

            StreamingHooks(Core::Dag::TaskGraph& graph, StreamingExecutor& executor)
                : Graph(graph)
                , Executor(executor)
            {
            }

            void DrainCompletions() override { Executor.DrainCompletions(); }
            void ApplyMainThreadResults() override { Executor.ApplyMainThreadResults(); }
            void SubmitFrameWork() override { SubmitStreamingGraphToExecutor(Graph, Executor); }
            void PumpBackground(std::uint32_t maxLaunches) override { Executor.PumpBackground(maxLaunches); }
        };

        struct AssetHooks final : IRuntimeAssetFrameHooks
        {
            Assets::AssetService&     AssetService;
            Graphics::GpuAssetCache*  GpuAssetCache;
            RHI::IDevice&             Device;

            AssetHooks(Assets::AssetService& assetService,
                       Graphics::GpuAssetCache* gpuAssetCache,
                       RHI::IDevice& device)
                : AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , Device(device)
            {
            }

            void TickAssets() override
            {
                // Asset service main-thread tick: advances state machines, fires
                // AssetEventBus::Ready / Reloaded / Destroyed callbacks.  The
                // cache subscribed in Engine::Initialize observes those events
                // synchronously during this Tick.
                AssetService.Tick();
                if (GpuAssetCache)
                {
                    GpuAssetCache->Tick(Device.GetGlobalFrameNumber(),
                                        Device.GetFramesInFlight());
                }
            }
        };

        TransferHooks transferHooks(*m_Device);
        StreamingHooks streamingHooks(*m_StreamingGraph, *m_StreamingExecutor);
        AssetHooks assetHooks(*m_AssetService, m_GpuAssetCache.get(), *m_Device);
        ExecuteRuntimeMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);
        // completedGpuValue is the renderer's per-frame timeline value.  The
        // GpuAssetCache currently retires on the CPU frame counter (which is
        // a conservative proxy for GPU completion); a follow-up may key
        // retirement directly on completedGpuValue for tighter recycling.
        (void)completedGpuValue;

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
    Graphics::GpuAssetCache& Engine::GetGpuAssetCache() noexcept { return *m_GpuAssetCache; }
    ECS::Scene::Registry& Engine::GetScene()         noexcept { return *m_Scene;         }
    Core::FrameGraph&     Engine::GetFrameGraph()    noexcept { return *m_FrameGraph;    }
    Core::Dag::TaskGraph& Engine::GetStreamingGraph() noexcept { return *m_StreamingGraph; }
}
