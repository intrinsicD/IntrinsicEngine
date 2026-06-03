module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module Extrinsic.Runtime.Engine;

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
import Extrinsic.Backends.Vulkan;
#endif
import Extrinsic.Backends.Null;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Logging;
import Extrinsic.Core.Tasks;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.EcsSystemBundle;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Scene.Registry;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr double kIdleSleepSeconds = 0.016; // ~60 Hz event wake

        // RUNTIME-070: runtime-baked fallback texture bytes for GpuAssetCache.
        // A 4×4 RGBA8_UNORM magenta-and-black checkerboard repeated from a 2×2
        // base pattern. The cache never reads files; runtime owns the bytes.
        // Layout: row-major, top-left origin, RGBA8 with alpha 0xFF so the
        // sampled colour is visually unambiguous when material code observes
        // `UsedFallback = true`.
        consteval std::array<std::byte, 4 * 4 * 4> MakeFallbackTextureBytes() noexcept
        {
            std::array<std::byte, 4 * 4 * 4> bytes{};
            for (std::size_t y = 0; y < 4; ++y)
            {
                for (std::size_t x = 0; x < 4; ++x)
                {
                    const bool magenta = (((x / 2) ^ (y / 2)) & 1u) == 0u;
                    const std::size_t base = (y * 4 + x) * 4;
                    bytes[base + 0] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 1] = static_cast<std::byte>(0x00);
                    bytes[base + 2] = static_cast<std::byte>(magenta ? 0xFF : 0x00);
                    bytes[base + 3] = static_cast<std::byte>(0xFF);
                }
            }
            return bytes;
        }

        constexpr auto kFallbackTextureBytes = MakeFallbackTextureBytes();

        [[nodiscard]] Graphics::GpuTextureFallbackDesc BuildFallbackTextureDesc() noexcept
        {
            Graphics::GpuTextureFallbackDesc desc{};
            desc.Bytes = std::span<const std::byte>(kFallbackTextureBytes);
            desc.Desc.Width = 4;
            desc.Desc.Height = 4;
            desc.Desc.MipLevels = 1;
            desc.Desc.Fmt = RHI::Format::RGBA8_UNORM;
            desc.Desc.Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst;
            desc.Desc.DebugName = "gpu-asset-fallback-texture";
            desc.SamplerDesc.MagFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MinFilter = RHI::FilterMode::Nearest;
            desc.SamplerDesc.MipFilter = RHI::MipmapMode::Nearest;
            desc.SamplerDesc.AddressU = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressV = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.AddressW = RHI::AddressMode::ClampToEdge;
            desc.SamplerDesc.DebugName = "gpu-asset-fallback-sampler";
            return desc;
        }

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
        constexpr bool kPromotedVulkanAvailable = true;
#else
        constexpr bool kPromotedVulkanAvailable = false;
#endif

        std::unique_ptr<RHI::IDevice> CreateDevice(
            const Core::Config::RenderConfig& config)
        {
            const RuntimeDeviceSelection selection = SelectRuntimeDeviceBackend(
                config,
                kPromotedVulkanAvailable);
            if (selection.UsePromotedVulkanDevice)
            {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
                Core::Log::Warn("[Runtime] Promoted Vulkan device selected; backend remains fail-closed until GRAPHICS-018 completes operational bring-up.");
                return Backends::Vulkan::CreateVulkanDevice();
#endif
            }

            if (config.EnablePromotedVulkanDevice && !kPromotedVulkanAvailable)
            {
                Core::Log::Warn("[Runtime] Promoted Vulkan device requested but not compiled into this build; using Null device fallback.");
            }

            // Vulkan execution is opt-in during GRAPHICS-018. The default path
            // routes through the Null stub so IDevice::IsOperational() remains
            // false and resource managers surface DeviceNotOperational rather
            // than faking GPU work.
            return Backends::Null::CreateNullDevice();
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
        // ARCH-005 / WORKSHOP-002: runtime owns the cross-layer composition
        // between platform window and graphics backend. RHI is platform-
        // neutral, so we fill a backend-agnostic `RHI::DeviceCreateDesc`
        // from the live `IWindow` here.
        m_Window   = Platform::CreateWindow(m_Config.Window);
        m_Device   = CreateDevice(m_Config.Render);
        const Platform::Extent2D initialExtent = m_Window->GetFramebufferExtent();
        m_Device->Initialize(RHI::MakeDeviceCreateDesc(
            m_Config.Render,
            initialExtent,
            m_Window->GetNativeHandle()));

        // GRAPHICS-033B: emit the Vulkan-requested-but-not-operational
        // breadcrumb and bump the operational diagnostics counters exactly
        // once per startup when the runtime requested the promoted Vulkan
        // device but the resolved device is not operational. Runtime never
        // aborts on this fallback — see the truth table in
        // `src/graphics/vulkan/README.md`.
        if (ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
                m_Config.Render, m_Device->IsOperational()))
        {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
            const Backends::Vulkan::VulkanOperationalStatus status =
                Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus(m_Device.get());
            Core::Log::Warn(
                "[Runtime] VulkanRequestedButNotOperational status={} reason={}",
                Backends::Vulkan::ToString(status.Code),
                Backends::Vulkan::ToString(status.Reason));
            Backends::Vulkan::RecordVulkanOperationalFallback(status);
#else
            // Vulkan backend was not compiled into this build; the truth
            // table row resolves to `NotCompiled` with no reason.
            Core::Log::Warn(
                "[Runtime] VulkanRequestedButNotOperational status={} reason={}",
                "NotCompiled",
                "None");
#endif
        }

        m_Renderer = Graphics::CreateRenderer();
        m_Renderer->Initialize(*m_Device);
        m_RendererOperational = m_Device->IsOperational();

        // ── 2c. Runtime-side Dear ImGui adapter (RUNTIME-090 / GRAPHICS-079) ─
        // Constructed after the Window and Renderer exist. The adapter owns the
        // ImGui context lifecycle and produces exactly one ImGuiOverlayFrame per
        // engine frame into the runtime-owned overlay system; RunFrame brackets
        // the variable tick with BeginFrame/EndFrame (the producer half per
        // GRAPHICS-013CQ). GRAPHICS-079 Slice B hands that same overlay instance
        // to the renderer consumer, so the producer and `ImGuiPass` route share
        // one system without graphics seeing live runtime/editor state.
        m_ImGuiAdapter = std::make_unique<ImGuiAdapter>(*m_Window, m_ImGuiOverlay);
        m_ImGuiAdapter->Initialize();
        if (m_ImGuiEditorCallback)
            m_ImGuiAdapter->SetEditorCallback(m_ImGuiEditorCallback);
        m_Renderer->SetImGuiOverlaySystem(&m_ImGuiOverlay);

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
        // type-specific bridges drive RequestUpload separately. The cache
        // receives the renderer's `SamplerManager` so RUNTIME-070's fallback
        // texture (and future texture-asset bridges) can resolve sampler
        // descriptors through the deduplicated manager path.
        m_GpuAssetCache = std::make_unique<Graphics::GpuAssetCache>(
            m_Renderer->GetBufferManager(),
            m_Renderer->GetTextureManager(),
            m_Renderer->GetSamplerManager(),
            m_Device->GetTransferQueue());

        // RUNTIME-070: bootstrap the runtime-owned 4×4 magenta-and-black
        // checkerboard fallback texture exactly once. Skipped when the
        // device is non-operational (e.g. the Null backend) — material
        // resolution then returns `GpuAssetFallbackReason::Unavailable` and
        // shaders route to factor-only shading, matching the documented
        // contract in `src/graphics/assets/README.md`.
        if (m_Device->IsOperational())
        {
            const Graphics::GpuTextureFallbackDesc fallbackDesc =
                BuildFallbackTextureDesc();
            if (auto r = m_GpuAssetCache->InitializeFallbackTexture(fallbackDesc);
                !r.has_value())
            {
                Core::Log::Warn(
                    "[Runtime] GpuAssetCache fallback texture bootstrap failed: error={}; material code will use factor-only fallback.",
                    static_cast<int>(r.error()));
            }
        }

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

        m_AssetModelTextureHandoff = std::make_unique<AssetModelTextureHandoff>(
            *m_AssetService,
            *m_GpuAssetCache);
        m_AssetModelSceneHandoff = std::make_unique<AssetModelSceneHandoff>(
            *m_AssetService,
            *m_GpuAssetCache,
            *m_Scene,
            *m_Renderer);

        // RUNTIME-092 Slice B — attach the runtime-owned stable-entity lookup
        // to the selection authority so render-id resolution flows through the
        // single runtime sidecar (which decodes + validates against the
        // registry) rather than a bare cast. The lookup is rebuilt each frame
        // in RunFrame before the pick-readback drain.
        m_SelectionController.SetStableEntityLookup(&m_StableEntityLookup);

        // ── 6b. Reference scene bootstrap (GRAPHICS-029A/B) ───────────────
        // Opt-in: only fires when EngineConfig::ReferenceScene::Enabled is
        // true. The default-off path leaves m_ReferenceScenePopulation
        // empty so RenderExtraction observes zero candidates. Double-install
        // is rejected via std::terminate to match the registry's
        // GRAPHICS-029 Decision 7 invariant.
        //
        // GRAPHICS-029B: install the production default providers for any
        // selector that does not yet have an explicit registration so the
        // resolve path is always covered without colliding with the strict
        // double-install guard.
        if (m_Config.ReferenceScene.Enabled)
        {
            if (m_ReferenceSceneInstalled)
                std::terminate();

            RegisterDefaultReferenceProvidersIfAbsent(m_ReferenceSceneRegistry);

            IReferenceSceneProvider& provider =
                m_ReferenceSceneRegistry.Resolve(m_Config.ReferenceScene.Selector);
            m_ReferenceScenePopulation = provider.Populate(*m_Scene);
            m_ReferenceCamera = m_ReferenceScenePopulation.Camera;
            m_ReferenceSceneInstalled = true;
        }

        // ── 7. Application ────────────────────────────────────────────────
        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running     = true;
    }

    void Engine::Shutdown()
    {
        // GRAPHICS-079 Slice B — detach the renderer consumer before the adapter
        // shuts the shared overlay system down, so the renderer never observes a
        // borrowed but inactive overlay during the rest of teardown.
        if (m_Renderer)
            m_Renderer->SetImGuiOverlaySystem(nullptr);
        // RUNTIME-090 Slice B — tear the Dear ImGui adapter down while the
        // Window and overlay system it references are still alive. The adapter
        // destructor shuts the overlay system + ImGui context down; the overlay
        // system value member is reusable on a later re-Initialize().
        m_ImGuiAdapter.reset();

        struct ShutdownHooks final : Core::IShutdownHooks
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
            std::unique_ptr<AssetModelTextureHandoff>& AssetModelTextureHandoffPtr;
            std::unique_ptr<AssetModelSceneHandoff>& AssetModelSceneHandoffPtr;
            Assets::AssetEventBus::ListenerToken& GpuAssetCacheListener;
            std::unique_ptr<ECS::Scene::Registry>& Scene;
            ReferenceSceneRegistry& ReferenceRegistry;
            ReferenceScenePopulation& ReferencePopulation;
            std::optional<Graphics::CameraViewInput>& ReferenceCameraSeed;
            CameraControllerRegistry& CameraControllers;
            bool& ReferenceInstalled;
            Core::Config::ReferenceSceneSelector ReferenceSelector;
            bool ReferenceEnabled;

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
                          std::unique_ptr<AssetModelTextureHandoff>& assetModelTextureHandoff,
                          std::unique_ptr<AssetModelSceneHandoff>& assetModelSceneHandoff,
                          Assets::AssetEventBus::ListenerToken& gpuAssetCacheListener,
                          std::unique_ptr<ECS::Scene::Registry>& scene,
                          ReferenceSceneRegistry& referenceRegistry,
                          ReferenceScenePopulation& referencePopulation,
                          std::optional<Graphics::CameraViewInput>& referenceCameraSeed,
                          CameraControllerRegistry& cameraControllers,
                          bool& referenceInstalled,
                          Core::Config::ReferenceSceneSelector referenceSelector,
                          bool referenceEnabled)
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
                , AssetModelTextureHandoffPtr(assetModelTextureHandoff)
                , AssetModelSceneHandoffPtr(assetModelSceneHandoff)
                , GpuAssetCacheListener(gpuAssetCacheListener)
                , Scene(scene)
                , ReferenceRegistry(referenceRegistry)
                , ReferencePopulation(referencePopulation)
                , ReferenceCameraSeed(referenceCameraSeed)
                , CameraControllers(cameraControllers)
                , ReferenceInstalled(referenceInstalled)
                , ReferenceSelector(referenceSelector)
                , ReferenceEnabled(referenceEnabled)
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
            void DestroyScene() override
            {
                // The model-scene handoff borrows the scene and renderer, so
                // detach it before provider teardown or wholesale scene reset.
                AssetModelSceneHandoffPtr.reset();

                // Reference scene teardown (GRAPHICS-029A/B): route entity
                // destruction through the same provider that authored them
                // before the scene registry is wholesale destroyed, and
                // clear the cached camera seed so a re-Initialize loop does
                // not republish a stale reference camera.
                if (ReferenceEnabled && ReferenceInstalled && Scene)
                {
                    if (IReferenceSceneProvider* provider =
                            ReferenceRegistry.ResolveOrNull(ReferenceSelector))
                    {
                        provider->Teardown(*Scene, ReferencePopulation.Entities);
                    }
                    ReferencePopulation = ReferenceScenePopulation{};
                    ReferenceInstalled = false;
                }
                ReferenceCameraSeed.reset();
                CameraControllers = CameraControllerRegistry{};
                Scene.reset();
            }
            void DestroyAssets() override
            {
                // Unsubscribe before destroying the cache so a late event
                // flush cannot reach a freed cache.  The cache is destroyed
                // before the renderer (which owns Buffer/Texture managers)
                // so leases unwind through live managers.
                AssetModelTextureHandoffPtr.reset();
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
                    Owner.m_RenderExtraction.Shutdown(*Renderer);
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
                            m_AssetModelTextureHandoff,
                            m_AssetModelSceneHandoff,
                            m_GpuAssetCacheListener,
                            m_Scene,
                            m_ReferenceSceneRegistry,
                            m_ReferenceScenePopulation,
                            m_ReferenceCamera,
                            m_CameraControllers,
                            m_ReferenceSceneInstalled,
                            m_Config.ReferenceScene.Selector,
                            m_Config.ReferenceScene.Enabled);
        Core::ExecuteShutdownContract(hooks);
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

        struct OperationalTransitionHooks final : Core::IOperationalTransitionHooks
        {
            RHI::IDevice& Device;
            Graphics::IRenderer& Renderer;
            bool& RendererOperational;

            OperationalTransitionHooks(RHI::IDevice& device,
                                       Graphics::IRenderer& renderer,
                                       bool& rendererOperational)
                : Device(device)
                , Renderer(renderer)
                , RendererOperational(rendererOperational)
            {
            }

            [[nodiscard]] bool IsDeviceOperational() const override { return Device.IsOperational(); }
            [[nodiscard]] bool IsRendererOperational() const override { return RendererOperational; }
            void WaitDeviceIdle() override { Device.WaitIdle(); }
            [[nodiscard]] bool RebuildRendererOperationalResources() override
            {
                return Renderer.RebuildOperationalResources(Device);
            }
            void MarkRendererOperational() override { RendererOperational = true; }
        };

        OperationalTransitionHooks operationalHooks(*m_Device, *m_Renderer, m_RendererOperational);
        (void)Core::ExecuteOperationalTransitionContract(operationalHooks);

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

            // RUNTIME-091: register the promoted baseline ECS systems
            // (TransformHierarchy, BoundsPropagation) after the app has
            // had a chance to add its own fixed-step passes. The FrameGraph
            // resolves the actual execution order through TypeToken reads/
            // writes and the named TransformUpdate / WorldBoundsUpdate
            // signals, so app passes that mutate transforms run before
            // TransformHierarchy and app passes that WaitFor either signal
            // run after the propagation seam.
            (void)RegisterPromotedEcsSystemBundle(*m_FrameGraph, *m_Scene);

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

        // ── RUNTIME-090 Slice B: open the Dear ImGui frame ────────────────
        // BeginFrame runs after Window::PollEvents (Phase 1) and the
        // minimize/resize early returns, immediately before the variable tick,
        // so the editor hook and any ImGui draws issued during OnVariableTick
        // run inside the NewFrame()/Render() scope. Minimized frames return
        // before this point, so a NewFrame is never left without a matching
        // Render() in EndFrame.
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->BeginFrame(frameDt);

        // ── Phase 3: Variable tick ────────────────────────────────────────
        m_Application->OnVariableTick(*this, alpha, frameDt);

        // ── RUNTIME-090 Slice B: close the Dear ImGui frame ───────────────
        // EndFrame runs after the variable tick and before the render
        // contract's IRenderer::PrepareFrame(): it invokes the editor hook,
        // calls ImGui::Render(), walks ImDrawData, and submits one
        // ImGuiOverlayFrame to the overlay system (per GRAPHICS-013CQ). The
        // renderer consumer is attached in Initialize(); graphics-side
        // draw upload + recorded Pass.ImGui execution remain later GRAPHICS-079
        // slices.
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->EndFrame();

        // ── Phase 4: Build render snapshot ────────────────────────────────
        const Platform::Extent2D viewport = m_Window->GetFramebufferExtent();
        Graphics::RenderFrameInput renderInput{
            .Alpha    = alpha,
            .Viewport = viewport,
        };

        if (m_Config.Camera.Enabled)
        {
            ICameraController* controller = m_CameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            if (controller == nullptr)
            {
                const Graphics::CameraViewInput seed = m_ReferenceCamera.has_value()
                    ? BuildReferenceCameraViewInput(*m_ReferenceCamera, viewport.Width, viewport.Height)
                    : Graphics::CameraViewInput{};
                m_CameraControllers.Register(
                    CameraControllerSlot::Main,
                    CreateCameraController(m_Config.Camera.Controller, seed));
                controller = m_CameraControllers.ResolveOrNull(CameraControllerSlot::Main);
            }

            if (controller != nullptr)
            {
                const Platform::IWindow& window = *m_Window;
                controller->Update(window.GetInput(), frameDt);
                renderInput.Camera = controller->GetView(viewport);
            }
        }

        // ── RUNTIME-089 Slice B: drain the coalesced selection pick ───────
        // Input ports / editor tools submit hover/click picks onto the
        // controller (GetSelectionController()); here we drain the single
        // coalesced survivor into the frame input and the renderer's
        // SelectionSystem so graphics issues the pick this frame. The
        // controller tracks the drained pick as in-flight; the matching
        // readback is consumed in the maintenance phase below. Graphics stays
        // reporting-only — it never reads live ECS or runtime selection state.
        if (const std::optional<PendingSelectionPick> pick =
                m_SelectionController.ConsumePendingPick())
        {
            renderInput.HasPendingPick = true;
            renderInput.Pick = Graphics::PickPixelRequest{
                .X        = pick->PixelX,
                .Y        = pick->PixelY,
                .Pending  = true,
                // Carry the controller's correlation token so the readback
                // resolves this exact request, not whichever pick is oldest.
                .Sequence = pick->Sequence,
            };
            m_Renderer->GetSelectionSystem().RequestPick(Graphics::PickRequest{
                .PixelX = pick->PixelX,
                .PixelY = pick->PixelY,
            });
        }

        // ── Phases 5–9: promoted render-frame contract ───────────────────
        RHI::FrameHandle frame{};
        Graphics::RenderWorld renderWorld{};

        struct RenderFrameHooks final : Core::IRenderFrameHooks
        {
            Graphics::IRenderer& Renderer;
            ECS::Scene::Registry& Scene;
            RenderExtractionCache& Extraction;
            Graphics::GpuAssetCache* GpuAssetCache;
            const SelectionController& Selection;
            RHI::FrameHandle& Frame;
            const Graphics::RenderFrameInput& Input;
            Graphics::RenderWorld& World;

            RenderFrameHooks(Graphics::IRenderer& renderer,
                             ECS::Scene::Registry& scene,
                             RenderExtractionCache& extraction,
                             Graphics::GpuAssetCache* gpuAssetCache,
                             const SelectionController& selection,
                             RHI::FrameHandle& frame,
                             const Graphics::RenderFrameInput& input,
                             Graphics::RenderWorld& world)
                : Renderer(renderer)
                , Scene(scene)
                , Extraction(extraction)
                , GpuAssetCache(gpuAssetCache)
                , Selection(selection)
                , Frame(frame)
                , Input(input)
                , World(world)
            {
            }

            bool BeginFrame() override { return Renderer.BeginFrame(Frame); }
            void ExtractRenderWorld() override
            {
                // RUNTIME-089 Slice B — mirror the runtime selection snapshot
                // into RenderWorld::Selection via the extraction batch.
                [[maybe_unused]] const RuntimeRenderExtractionStats stats =
                    Extraction.ExtractAndSubmit(Scene, Renderer, GpuAssetCache, &Selection);
                World = Renderer.ExtractRenderWorld(Input);
            }
            void PrepareFrame() override { Renderer.PrepareFrame(World); }
            void ExecuteFrame() override { Renderer.ExecuteFrame(Frame, World); }
            std::uint64_t EndFrame() override { return Renderer.EndFrame(Frame); }
        };

        RenderFrameHooks renderHooks(*m_Renderer,
                                     *m_Scene,
                                     m_RenderExtraction,
                                     m_GpuAssetCache.get(),
                                     m_SelectionController,
                                     frame,
                                     renderInput,
                                     renderWorld);

        const Core::RenderFrameResult renderResult = Core::ExecuteRenderFrameContract(renderHooks);
        if (!renderResult.BeganFrame)
        {
            m_FrameClock.EndFrame();
            return;
        }

        const std::uint64_t completedGpuValue = renderResult.CompletedGpuValue;
        m_Device->Present(frame);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        struct TransferHooks final : Core::ITransferFrameHooks
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

        struct StreamingHooks final : Core::IStreamingFrameHooks
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

        struct AssetHooks final : Core::IAssetFrameHooks
        {
            Assets::AssetService&     AssetService;
            Graphics::GpuAssetCache*  GpuAssetCache;
            RHI::IDevice&             Device;
            RenderExtractionCache&    Extraction;
            Graphics::IRenderer&      Renderer;

            AssetHooks(Assets::AssetService& assetService,
                       Graphics::GpuAssetCache* gpuAssetCache,
                       RHI::IDevice& device,
                       RenderExtractionCache& extraction,
                       Graphics::IRenderer& renderer)
                : AssetService(assetService)
                , GpuAssetCache(gpuAssetCache)
                , Device(device)
                , Extraction(extraction)
                , Renderer(renderer)
            {
            }

            void TickAssets() override
            {
                // Asset service main-thread tick: advances state machines, fires
                // AssetEventBus::Ready / Reloaded / Destroyed callbacks.  The
                // cache subscribed in Engine::Initialize observes those events
                // synchronously during this Tick.
                AssetService.Tick();
                const std::uint64_t currentFrame = Device.GetGlobalFrameNumber();
                const std::uint32_t framesInFlight = Device.GetFramesInFlight();
                if (GpuAssetCache)
                {
                    GpuAssetCache->Tick(currentFrame, framesInFlight);
                }
                // GRAPHICS-030C: drive the procedural geometry cache's
                // deferred-retire window with the same CPU frame counter and
                // framesInFlight the asset cache uses.  Final FreeGeometry
                // calls fall through to GpuWorld here.
                Extraction.TickProceduralGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-085 Slice C — mirror the same window for the
                // runtime-owned mesh-residency retire queue.
                Extraction.TickMeshGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-086 Slice B — and for the graph-residency queue.
                Extraction.TickGraphGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-087 — and for the point-cloud-residency queue.
                Extraction.TickPointCloudGeometry(currentFrame, framesInFlight, Renderer);
                // RUNTIME-088 Slice B — and for the mesh edge/vertex primitive
                // view residency queue (one queue for both view lanes).
                Extraction.TickMeshPrimitiveViewGeometry(currentFrame, framesInFlight, Renderer);
            }
        };

        TransferHooks transferHooks(*m_Device);
        StreamingHooks streamingHooks(*m_StreamingGraph, *m_StreamingExecutor);
        AssetHooks assetHooks(*m_AssetService,
                              m_GpuAssetCache.get(),
                              *m_Device,
                              m_RenderExtraction,
                              *m_Renderer);
        Core::ExecuteMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);

        // ── RUNTIME-092 Slice B: refresh the stable-entity lookup ──────────
        // Rebuild the runtime-owned StableId winner-map from the live registry
        // before consuming pick readbacks, so durable-id resolution and the
        // editor/serialization-facing ResolveByStableId/ResolveSelected APIs
        // observe this frame's entity set. Render-id resolution (the path the
        // controller takes for a pick hit) decodes + validates against the live
        // registry directly and does not depend on the map, so a recycled slot
        // is rejected regardless; the rebuild keeps the durable map coherent for
        // the other consumers and is the single per-frame maintenance point.
        m_StableEntityLookup.Rebuild(*m_Scene);

        // ── RUNTIME-089 Slice B: consume the completed pick readbacks ──────
        // DrainCompletedPickingSlots can publish several completed picking
        // slots into the SelectionSystem during the render/transfer phases, so
        // drain the whole FIFO — not just the newest — and resolve each result
        // by its correlation Sequence. ConsumeHit/ConsumeNoHit(reg, seq) replay
        // the exact in-flight request's kind/mode (hover vs click, Replace/Add/
        // Toggle) even when picks complete out of issue order or a slot is
        // recycled. A result with no Sequence (uncorrelated; e.g. a pick issued
        // outside the controller bridge) falls back to the oldest in-flight
        // pick. The controller resolves the stable id, rejects stale/
        // non-selectable hits, and mutates ECS Selected/Hovered tags.
        {
            Graphics::SelectionSystem& selectionSystem = m_Renderer->GetSelectionSystem();
            while (const std::optional<Graphics::PickReadbackResult> result =
                       selectionSystem.PopPickResult())
            {
                if (result->Sequence != 0u)
                {
                    if (result->Hit)
                        m_SelectionController.ConsumeHit(*m_Scene, result->StableEntityId, result->Sequence);
                    else
                        m_SelectionController.ConsumeNoHit(*m_Scene, result->Sequence);
                }
                else
                {
                    if (result->Hit)
                        m_SelectionController.ConsumeHit(*m_Scene, result->StableEntityId);
                    else
                        m_SelectionController.ConsumeNoHit(*m_Scene);
                }

                // ── RUNTIME-093 Slice B2: refine the pick into a sub-primitive ──
                // Bridge each readback's encoded primitive hint to the authoritative
                // CPU GeometrySources of the hit entity and cache the result for the
                // editor. The whole loop runs oldest→newest, so the last readback's
                // refinement wins, matching the controller's latest-pick-wins
                // coalescing; a background (no-hit) readback clears the cache. The
                // bridge mutates nothing and only ever reads the live registry.
                m_LastRefinedPrimitive = RefinePickReadbackResult(*m_Scene, *result);
            }
        }

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
    SelectionController&  Engine::GetSelectionController() noexcept { return m_SelectionController; }
    const std::optional<PrimitiveSelectionResult>&
    Engine::GetLastRefinedPrimitiveSelection() const noexcept { return m_LastRefinedPrimitive; }
    Core::FrameGraph&     Engine::GetFrameGraph()    noexcept { return *m_FrameGraph;    }
    Core::Dag::TaskGraph& Engine::GetStreamingGraph() noexcept { return *m_StreamingGraph; }

    ReferenceSceneRegistry& Engine::GetReferenceSceneRegistry() noexcept
    {
        return m_ReferenceSceneRegistry;
    }

    bool Engine::IsReferenceSceneInstalled() const noexcept
    {
        return m_ReferenceSceneInstalled;
    }

    const std::optional<Graphics::CameraViewInput>& Engine::GetReferenceCameraSeed() const noexcept
    {
        return m_ReferenceCamera;
    }

    CameraControllerRegistry& Engine::GetCameraControllerRegistry() noexcept
    {
        return m_CameraControllers;
    }

    void Engine::SetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        m_RenderExtraction.SetMeshPrimitiveViewSettings(stableEntityId, settings);
    }

    void Engine::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_RenderExtraction.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    MeshPrimitiveViewSettings Engine::GetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtraction.GetMeshPrimitiveViewSettings(stableEntityId);
    }

    void Engine::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        RenderExtractionCache::VisualizationAdapterBinding binding)
    {
        m_RenderExtraction.SetVisualizationAdapterBinding(
            stableEntityId,
            std::move(binding));
    }

    void Engine::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_RenderExtraction.ClearVisualizationAdapterBinding(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    Engine::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtraction.GetVisualizationAdapterBinding(stableEntityId);
    }

    void Engine::SetImGuiEditorCallback(std::function<void()> callback)
    {
        m_ImGuiEditorCallback = std::move(callback);
        if (m_ImGuiAdapter)
            m_ImGuiAdapter->SetEditorCallback(m_ImGuiEditorCallback);
    }

    const ImGuiAdapter& Engine::GetImGuiAdapter() const noexcept
    {
        return *m_ImGuiAdapter;
    }
}
