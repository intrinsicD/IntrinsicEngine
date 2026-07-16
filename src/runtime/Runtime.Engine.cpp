module;

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Extrinsic.Runtime.Engine;

#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
import Extrinsic.Backends.Vulkan;
#endif
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Tasks;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.AsyncWorkService;
import Extrinsic.Runtime.AssetGeometryIO;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetMeshNormals;
import Extrinsic.Runtime.AssetModelTextureIO;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.CameraFocusCommand;
import Extrinsic.Runtime.DeviceBootstrap;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.GizmoFrameService;
import Extrinsic.Runtime.ImGuiAdapter;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobServiceGpuQueueBridge;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleSchedule;
import Extrinsic.Runtime.MeshPrimitiveViewControls;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ObjectSpaceNormalBakeService;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.EcsSystemBundle;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.ReferenceScene;
import Extrinsic.Runtime.ReferenceSceneControl;
import Extrinsic.Runtime.SceneDocument;
import Extrinsic.Runtime.SelectionReadback;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Asset.GeometryIOBridge;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.ModelTextureIOBridge;
import Extrinsic.Asset.ModelTexturePayload;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Light;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.ShadowCaster;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Registry;
import Geometry.Graph.IO;
import Geometry.Graph;
import Geometry.AABB;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.IO;
import Geometry.OBB;
import Geometry.PointCloud;
import Geometry.PointCloud.IO;
import Geometry.Properties;
import Geometry.Sphere;

#include "Runtime.Engine.FrameLoop.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
    }

    // ── Construction / destruction ────────────────────────────────────────

    Engine::Engine(Core::Config::EngineConfig config,
                   std::unique_ptr<IApplication> application)
        : m_Config(std::move(config))
        , m_Application(std::move(application))
    {
        if (!m_Application)
            std::terminate();
        m_ConfigControl = std::make_unique<EngineConfigControl>(
            EngineConfigControlDependencies{
                .Config = &m_Config,
            });
        m_AssetImportPipeline = std::make_unique<AssetImportPipeline>(
            AssetImportPipelineDependencies{
                .Initialized = &m_Initialized,
                .Config = &m_Config,
                .CommandHistory = &m_EditorCommandHistory,
            });
        m_SceneDocument = std::make_unique<SceneDocument>(
            SceneDocumentDependencies{
                .Initialized = &m_Initialized,
                .Scene = &m_Scene,
                .CommandHistory = &m_EditorCommandHistory,
            });
    }

    Engine::~Engine()
    {
        if (m_Initialized)
            Shutdown();
    }

    void Engine::AddModule(std::unique_ptr<IRuntimeModule> module)
    {
        if (!module)
        {
            Core::Log::Error("[RuntimeModule] Engine::AddModule received null.");
            std::terminate();
        }
        if (m_Initialized)
        {
            Core::Log::Error(
                "[RuntimeModule] Engine::AddModule called after Initialize().");
            std::terminate();
        }
        if (module->Name().empty())
        {
            Core::Log::Error(
                "[RuntimeModule] Engine::AddModule received an unnamed module.");
            std::terminate();
        }

        for (const std::unique_ptr<IRuntimeModule>& existing : m_RuntimeModules)
        {
            if (existing && existing->Name() == module->Name())
            {
                Core::Log::Error(
                    "[RuntimeModule] Duplicate module name '{}' rejected.",
                    module->Name());
                std::terminate();
            }
        }

        m_RuntimeModules.push_back(std::move(module));
    }

    void Engine::RegisterRuntimeModulesForBoot()
    {
        std::sort(
            m_RuntimeModules.begin(),
            m_RuntimeModules.end(),
            [](const std::unique_ptr<IRuntimeModule>& lhs,
               const std::unique_ptr<IRuntimeModule>& rhs)
            {
                return lhs->Name() < rhs->Name();
            });

        m_RuntimeModuleSchedule.Clear();

        m_ServiceRegistry.BeginRegistration();
        const auto requireProvide = [](Core::Result result,
                                       std::string_view serviceName)
        {
            if (!result.has_value())
            {
                Core::Log::Error(
                    "[RuntimeModule] Engine failed to provide built-in service '{}': {}",
                    serviceName,
                    Core::Error::ToString(result.error()));
                std::terminate();
            }
        };
        requireProvide(m_ServiceRegistry.Provide<CommandBus>(
                           m_CommandBus, "Engine"),
                       "CommandBus");
        requireProvide(m_ServiceRegistry.Provide<KernelEventBus>(
                           m_KernelEvents, "Engine"),
                       "KernelEventBus");
        requireProvide(m_ServiceRegistry.Provide<JobService>(
                           m_JobService, "Engine"),
                       "JobService");
        requireProvide(m_ServiceRegistry.Provide<WorldRegistry>(
                           m_WorldRegistry, "Engine"),
                       "WorldRegistry");

        for (const std::unique_ptr<IRuntimeModule>& module : m_RuntimeModules)
        {
            const std::string moduleName{module->Name()};
            EngineSetup setup(
                m_CommandBus,
                m_KernelEvents,
                m_JobService,
                m_WorldRegistry,
                m_ServiceRegistry,
                [this, moduleName](SimSystemDesc desc)
                {
                    m_RuntimeModuleSchedule.RegisterSimSystem(
                        moduleName, std::move(desc));
                },
                [this, moduleName](FramePhase phase, RuntimeFrameHook hook)
                {
                    m_RuntimeModuleSchedule.RegisterFrameHook(
                        moduleName, phase, std::move(hook));
                });

            Core::Result result = module->OnRegister(setup);
            if (!result.has_value())
            {
                Core::Log::Error(
                    "[RuntimeModule] OnRegister failed for module '{}': {}",
                    moduleName,
                    Core::Error::ToString(result.error()));
                std::terminate();
            }
            if (m_ServiceRegistry.HasBootErrors())
            {
                Core::Log::Error(
                    "[RuntimeModule] Registration failed for module '{}': {}",
                    moduleName,
                    m_ServiceRegistry.LastBootError());
                std::terminate();
            }
        }

    }

    void Engine::ResolveRuntimeModulesForBoot()
    {
        m_ServiceRegistry.BeginResolution();
        for (const std::unique_ptr<IRuntimeModule>& module : m_RuntimeModules)
        {
            const std::string moduleName{module->Name()};
            EngineSetup setup(
                m_CommandBus,
                m_KernelEvents,
                m_JobService,
                m_WorldRegistry,
                m_ServiceRegistry,
                [this, moduleName](SimSystemDesc desc)
                {
                    m_RuntimeModuleSchedule.RegisterSimSystem(
                        moduleName, std::move(desc));
                },
                [this, moduleName](FramePhase phase, RuntimeFrameHook hook)
                {
                    m_RuntimeModuleSchedule.RegisterFrameHook(
                        moduleName, phase, std::move(hook));
                });

            Core::Result result = module->OnResolve(setup);
            if (!result.has_value())
            {
                Core::Log::Error(
                    "[RuntimeModule] OnResolve failed for module '{}': {}",
                    moduleName,
                    Core::Error::ToString(result.error()));
                std::terminate();
            }
            if (m_ServiceRegistry.HasBootErrors())
            {
                Core::Log::Error(
                    "[RuntimeModule] Resolution failed for module '{}': {}",
                    moduleName,
                    m_ServiceRegistry.LastBootError());
                std::terminate();
            }
        }

        if (Core::Result result = m_ServiceRegistry.ValidateBoot();
            !result.has_value())
        {
            Core::Log::Error(
                "[RuntimeModule] ServiceRegistry boot validation failed: {}",
                m_ServiceRegistry.LastBootError());
            std::terminate();
        }
        m_ServiceRegistry.Lock();

        // BUG-071: finalize the schedule after BOTH the register and resolve
        // phases. OnResolve can register sim-systems (the resolve EngineSetup
        // wires the sim-system registrar), so finalizing at the end of the
        // register phase left resolve-registered systems appended after the
        // sort — unordered and skipping the duplicate/cycle/unprovided-signal
        // validation in FinalizeForBoot.
        //
        // Seed the promoted baseline ECS bundle's signals as externally
        // provided so a module may wait on e.g. "TransformUpdate" without boot
        // failing — the bundle is appended to the fixed-step FrameGraph ahead of
        // module systems (BUG-069), so the per-tick WaitFor edge resolves.
        const std::array<Core::Hash::StringID, 3> baselineSignals =
            PromotedEcsSystemBundleSignalLabels();
        if (Core::Result finalizeResult =
                m_RuntimeModuleSchedule.FinalizeForBoot(baselineSignals);
            !finalizeResult.has_value())
        {
            Core::Log::Error(
                "[RuntimeModule] Sim system schedule finalize failed: {}",
                Core::Error::ToString(finalizeResult.error()));
            std::terminate();
        }
    }

    void Engine::RegisterRuntimeModuleSimSystemsForTick(
        Core::FrameGraph& graph,
        ECS::Scene::Registry& scene,
        const double fixedDt)
    {
        m_RuntimeModuleSchedule.RegisterSimSystemsForTick(
            RuntimeModuleSimSystemScheduleContext{
                .Graph = graph,
                .ActiveWorld = scene,
                .ActiveWorldHandle = ActiveWorld(),
                .Commands = m_CommandBus,
                .Events = m_KernelEvents,
                .Jobs = m_JobService,
                .Worlds = m_WorldRegistry,
                .Services = m_ServiceRegistry,
                .FrameIndex = m_RenderExtractionService.CurrentFrameIndex(),
                .FixedDeltaSeconds = fixedDt,
            });
    }

    void Engine::RunRuntimeModuleFrameHooks(
        const FramePhase phase,
        const double frameDt,
        const double alpha)
    {
        if (!m_Scene)
            return;

        m_RuntimeModuleSchedule.RunFrameHooks(
            RuntimeModuleFrameHookDispatchContext{
                .Phase = phase,
                .ActiveWorld = *m_Scene,
                .ActiveWorldHandle = ActiveWorld(),
                .Commands = m_CommandBus,
                .Events = m_KernelEvents,
                .Jobs = m_JobService,
                .Worlds = m_WorldRegistry,
                .Services = m_ServiceRegistry,
                .FrameIndex = m_RenderExtractionService.CurrentFrameIndex(),
                .FrameDeltaSeconds = frameDt,
                .FixedStepAlpha = alpha,
            });
    }

    void Engine::AnnounceAndShutdownRuntimeModules()
    {
        m_KernelEvents.Publish(RuntimeShutdownAnnounced{});
        (void)m_KernelEvents.Pump();

        RuntimeModuleShutdownContext context{
            .Commands = m_CommandBus,
            .Events = m_KernelEvents,
            .Jobs = m_JobService,
            .Worlds = m_WorldRegistry,
            .Services = m_ServiceRegistry,
        };
        for (auto it = m_RuntimeModules.rbegin();
             it != m_RuntimeModules.rend();
             ++it)
        {
            if (*it)
                (*it)->OnShutdown(context);
        }
    }

    void Engine::RefreshActiveWorldScenePointer() noexcept
    {
        m_Scene = m_WorldRegistry.Get(m_WorldRegistry.ActiveWorld());
    }

    void Engine::ApplyWorldRegistryMaintenance()
    {
        const WorldHandle previousActive = m_WorldRegistry.ActiveWorld();
        const WorldRegistryMaintenanceStats stats =
            m_WorldRegistry.ApplyMaintenance(m_KernelEvents, m_JobService);

        if (stats.AppliedActiveWorldChanges == 0u ||
            m_WorldRegistry.ActiveWorld() == previousActive)
        {
            return;
        }

        m_SceneDocument->ClearSceneRuntimeState();
        RefreshActiveWorldScenePointer();
        RebuildStableEntityLookupAfterSceneReplacement();
        // Scene handoffs borrow the registry by reference. Active-world changes
        // precede deferred destruction, so rebind before the old world retires.
        BindActiveSceneAssetHandoffs();
    }

    void Engine::RebuildStableEntityLookupAfterSceneReplacement()
    {
        m_StableEntityLookupBinding.Rebuild(m_StableEntityLookup, m_Scene);
    }

    void Engine::BindActiveSceneAssetHandoffs()
    {
        if (m_Scene == nullptr)
        {
            m_AssetResidencyService.DestroySceneBorrowers();
            m_SelectionController.SetStableEntityLookup(nullptr);
        }
        else
        {
            m_AssetResidencyService.InitializeSceneHandoffs(
                *m_AssetService,
                *m_Scene,
                *m_Renderer,
                AssetResidencySceneHandoffOptions{
                    .ObjectSpaceNormalBakeQueue =
                        &m_ObjectSpaceNormalBakeService.Queue(),
                    .ObjectSpaceNormalBakeGraphicsBackendOperational =
                        m_Device != nullptr && m_Device->IsOperational(),
                });
        }

        m_AssetImportPipeline->SetDependencies(
            AssetImportPipelineDependencies{
                .Initialized = &m_Initialized,
                .Config = &m_Config,
                .Streaming = m_AsyncWorkService.Streaming(),
                .AssetService = m_AssetService.get(),
                .GpuAssetCache = m_AssetResidencyService.CachePtr(),
                .ModelTextureHandoff =
                    m_AssetResidencyService.ModelTextureHandoff(),
                .ModelSceneHandoff =
                    m_AssetResidencyService.ModelSceneHandoff(),
                .RenderExtraction = &m_RenderExtractionService.Cache(),
                .Scene = m_Scene,
                .CameraControllers = &m_CameraControllers,
                .Selection = &m_SelectionController,
                .CommandHistory = &m_EditorCommandHistory,
                .ObjectSpaceNormalBakeQueue =
                    &m_ObjectSpaceNormalBakeService.Queue(),
                .Device = m_Device.get(),
            });
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Engine::Initialize()
    {
        // ── 1. CPU fiber scheduler ────────────────────────────────────────
        // Must be first — all three graphs dispatch through it.
        Core::Tasks::Scheduler::Initialize(m_Config.Simulation.WorkerThreadCount);

        // ARCH-007 — built-in kernel command (ADR-0024 D13): the sanctioned
        // shutdown path for module/UI code. Future module code enqueues
        // `QuitRequested` instead of reaching for an Engine& to call
        // shutdown entry points directly.
        m_CommandBus.RegisterHandler<QuitRequested>(
            [this](CommandContext&, const QuitRequested&) -> CommandOutcome
            {
                RequestExit();
                return CommandOutcome::Ok();
            });

        // ── 2. Subsystems ─────────────────────────────────────────────────
        // ARCH-005 / WORKSHOP-002: runtime owns the cross-layer composition
        // between platform window and graphics backend. RHI is platform-
        // neutral, so we fill a backend-agnostic `RHI::DeviceCreateDesc`
        // from the live `IWindow` here.
        m_Window   = Platform::CreateWindow(m_Config.Window);
        if (m_Window && m_Window->ShouldClose())
        {
            Core::Log::Warn(
                "[Runtime] Platform window initialized closed; Engine::Run() will execute zero frames unless the test or caller requests a headless-capable window backend.");
        }
        m_Device   = CreateRuntimeDevice(m_Config.Render);
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
        m_ConfigControl->SetDependencies(
            EngineConfigControlDependencies{
                .Config = &m_Config,
                .Window = m_Window.get(),
                .Renderer = m_Renderer.get(),
            });
        m_JobServiceGpuQueueBridge.Install(*m_Renderer, m_JobService);
        if (!m_Config.Render.DefaultRecipeConfigPath.empty())
        {
            (void)m_ConfigControl->LoadAndApplyRenderRecipeConfigFile(
                m_Config.Render.DefaultRecipeConfigPath,
                RuntimeRenderRecipeActivationSource::StartupConfigFile);
        }

        // RUNTIME-159: Dear ImGui overlay/adapter ownership lives behind the
        // bridge; Engine keeps only composition order and frame phase calls.
        m_ImGuiEditorBridge.Initialize(*m_Window, *m_Renderer);

        // ── 2d. Render-world pool (GRAPHICS-036C) ─────────────────────────
        // Size the runtime-owned slot pool from the render config: one logical
        // buffer in the default synchronous mode (serial extraction/render,
        // behavior-preserving), or the triple-buffered default when pipelined
        // extraction is requested. The production default remains synchronous;
        // GRAPHICS-036D proves the opt-in render-N-1 path by consuming the
        // previous front while extraction writes the newly acquired back slot.
        m_RenderExtractionService.ConfigurePool(
            m_Config.Render.SynchronousExtraction);

        // ── 3. CPU task graph (ECS system scheduling) ─────────────────────
        m_FrameGraph = std::make_unique<Core::FrameGraph>();

        // ── 4. Streaming executor (asset IO / geometry processing) ────────
        m_AsyncWorkService.Initialize();

        // ── 5. Asset service ──────────────────────────────────────────────
        m_AssetService = std::make_unique<Assets::AssetService>();

        // ── 5b. GPU asset residency ───────────────────────────────────────
        m_AssetResidencyService.InitializeGpuCache(
            *m_AssetService,
            *m_Renderer,
            *m_Device);

        m_ObjectSpaceNormalBakeService.SetDependencies(
            ObjectSpaceNormalBakeServiceDependencies{
                .GpuAssets = m_AssetResidencyService.CachePtr(),
                .RenderExtraction = &m_RenderExtractionService.Cache(),
                .Device = m_Device.get(),
            });
        (void)m_ObjectSpaceNormalBakeService.RegisterGpuQueueParticipant(
            m_JobService);

        // ── 6. World registry / boot ECS scene ───────────────────────────
        m_WorldRegistry.Clear();
        const WorldHandle bootWorld = m_WorldRegistry.CreateWorld("Main");
        m_Scene = m_WorldRegistry.Get(bootWorld);
        if (m_Scene != nullptr)
            m_StableEntityLookupBinding.Connect(m_StableEntityLookup, *m_Scene);

        BindActiveSceneAssetHandoffs();
        m_SceneDocument->SetDependencies(
            SceneDocumentDependencies{
                .Initialized = &m_Initialized,
                .Scene = &m_Scene,
                .Streaming = m_AsyncWorkService.Streaming(),
                .CommandHistory = &m_EditorCommandHistory,
                .Renderer = m_Renderer.get(),
                .RenderExtraction = &m_RenderExtractionService.Cache(),
                .ObjectSpaceNormalBakeQueue =
                    &m_ObjectSpaceNormalBakeService.Queue(),
                .Selection = &m_SelectionController,
                .SelectionReadback = &m_SelectionReadback,
                .StableLookup = &m_StableEntityLookup,
                .StableLookupBinding = &m_StableEntityLookupBinding,
            });

        // RUNTIME-092 Slice B — attach the runtime-owned stable-entity lookup
        // to the selection authority so render-id resolution flows through the
        // single runtime sidecar (which decodes + validates against the
        // registry) rather than a bare cast. The durable-id winner map is
        // maintained incrementally from StableId component events.
        m_SelectionController.SetStableEntityLookup(&m_StableEntityLookup);

        // ── 6b. Reference scene bootstrap (GRAPHICS-029A/B) ───────────────
        m_ReferenceSceneControl.InstallIfEnabled(m_Config.ReferenceScene, *m_Scene);

        // ── 7. Runtime modules (ARCH-011) ────────────────────────────────
        RegisterRuntimeModulesForBoot();
        ResolveRuntimeModulesForBoot();

        // ── 8. Application ────────────────────────────────────────────────
        m_Application->OnInitialize(*this);

        m_Initialized = true;
        m_Running     = true;
        m_WindowCloseLogged = false;

        m_Window->Listen(
            [this](const Platform::Event& event)
            {
                HandlePlatformEvent(event);
            });
    }

    void Engine::Shutdown()
    {
        // ARCH-007 — drop commands enqueued after the final frame's drain
        // (e.g. from OnVariableTick just before RequestExit()). The engine
        // is documented as reusable via Shutdown() + Initialize(); stale
        // commands must not replay into the next session's fresh scene.
        m_CommandBus.DiscardPending();

        if (m_Window)
            m_Window->Listen({});

        m_ImGuiEditorBridge.Shutdown(m_Renderer.get());
        (void)m_JobServiceGpuQueueBridge.ShutdownParticipants(
            m_Renderer.get(),
            m_JobService,
            [this]
            {
                if (m_Device)
                    m_Device->WaitIdle();
            });

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
            AsyncWorkService& AsyncWork;
            std::unique_ptr<Assets::AssetService>& AssetService;
            AssetResidencyService& AssetResidency;
            WorldRegistry& Worlds;
            ECS::Scene::Registry*& Scene;
            ReferenceSceneControl& ReferenceScene;
            CameraControllerRegistry& CameraControllers;
            const Core::Config::ReferenceSceneConfig& ReferenceConfig;

            ShutdownHooks(Engine& owner,
                          bool& running,
                          bool& initialized,
                          std::unique_ptr<IApplication>& application,
                          std::unique_ptr<Platform::IWindow>& window,
                          std::unique_ptr<RHI::IDevice>& device,
                          std::unique_ptr<Graphics::IRenderer>& renderer,
                          std::unique_ptr<Core::FrameGraph>& frameGraph,
                          AsyncWorkService& asyncWork,
                          std::unique_ptr<Assets::AssetService>& assetService,
                          AssetResidencyService& assetResidency,
                          WorldRegistry& worlds,
                          ECS::Scene::Registry*& scene,
                          ReferenceSceneControl& referenceScene,
                          CameraControllerRegistry& cameraControllers,
                          const Core::Config::ReferenceSceneConfig& referenceConfig)
                : Owner(owner)
                , Running(running)
                , Initialized(initialized)
                , Application(application)
                , Window(window)
                , Device(device)
                , Renderer(renderer)
                , FrameGraph(frameGraph)
                , AsyncWork(asyncWork)
                , AssetService(assetService)
                , AssetResidency(assetResidency)
                , Worlds(worlds)
                , Scene(scene)
                , ReferenceScene(referenceScene)
                , CameraControllers(cameraControllers)
                , ReferenceConfig(referenceConfig)
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
                Owner.AnnounceAndShutdownRuntimeModules();
            }
            void ShutdownStreaming() override
            {
                AsyncWork.ShutdownAndDrain();
            }
            void DestroyScene() override
            {
                Owner.m_StableEntityLookupBinding.Disconnect();
                Owner.m_StableEntityLookup.Clear();

                AssetResidency.DestroySceneBorrowers();

                ReferenceScene.TeardownIfInstalled(ReferenceConfig, Scene);
                CameraControllers = CameraControllerRegistry{};
                Scene = nullptr;
                Worlds.Clear();
            }
            void DestroyAssets() override
            {
                AssetResidency.DestroyAssets(AssetService.get());
                AssetService.reset();
            }
            void DestroyStreamingState() override
            {
                AsyncWork.Reset();
            }
            void DestroyFrameGraph() override { FrameGraph.reset(); }
            void ShutdownRenderer() override
            {
                if (Renderer)
                {
                    Owner.m_RenderExtractionService.Shutdown(*Renderer);
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
                            m_AsyncWorkService,
                            m_AssetService,
                            m_AssetResidencyService,
                            m_WorldRegistry,
                            m_Scene,
                            m_ReferenceSceneControl,
                            m_CameraControllers,
                            m_Config.ReferenceScene);
        Core::ExecuteShutdownContract(hooks);
        m_ObjectSpaceNormalBakeService.ClearDependencies();
        if (m_ConfigControl)
        {
            m_ConfigControl->SetDependencies(
                EngineConfigControlDependencies{
                    .Config = &m_Config,
                });
        }
        if (m_AssetImportPipeline)
        {
            m_AssetImportPipeline->SetDependencies(
                AssetImportPipelineDependencies{
                    .Initialized = &m_Initialized,
                    .Config = &m_Config,
                    .CommandHistory = &m_EditorCommandHistory,
                });
        }
        if (m_SceneDocument)
        {
            m_SceneDocument->SetDependencies(
                SceneDocumentDependencies{
                    .Initialized = &m_Initialized,
                    .Scene = &m_Scene,
                    .CommandHistory = &m_EditorCommandHistory,
                });
        }
    }

    // ── Main loop ─────────────────────────────────────────────────────────

    void Engine::Run()
    {
        while (m_Running && m_Window != nullptr && !m_Window->ShouldClose())
            RunFrame();

        if (m_Running && m_Window != nullptr && m_Window->ShouldClose())
            RequestExitFromWindowClose("native-poll");
    }

    void Engine::RunFrame()
    {
        RuntimeFrameContext frameContext{};
        RuntimeFramePacingDiagnostics pacing{};
        pacing.Valid = true;
        pacing.FrameIndex = m_RenderExtractionService.CurrentFrameIndex();
        const auto framePacingBegin = std::chrono::steady_clock::now();
        const auto publishPacingSample = [&]()
        {
            if (const ImGuiAdapterDiagnostics* imguiDiagnostics =
                    m_ImGuiEditorBridge.Diagnostics())
            {
                MirrorImGuiFramePacingDiagnostics(pacing, *imguiDiagnostics);
            }
            if (m_Renderer)
                MirrorRenderGraphFramePacingDiagnostics(
                    pacing, m_Renderer->GetLastRenderGraphStats());
            pacing.TotalMicros = ElapsedMicros(framePacingBegin);
            m_LastFramePacingDiagnostics = pacing;
        };

        // ── Phase 1: Platform ─────────────────────────────────────────────
        PlatformFrameHooks platformHooks{*m_Window};
        const auto platformBegin = std::chrono::steady_clock::now();
        const Core::PlatformFrameResult platformResult =
            Core::ExecutePlatformBeginFrameContract(platformHooks,
                                                    kIdleSleepSeconds);
        pacing.PlatformBeginMicros = ElapsedMicros(platformBegin);
        pacing.PlatformContinueFrame = platformResult.ContinueFrame;
        if (platformResult.ShouldClose)
        {
            RequestExitFromWindowClose("platform-poll");
            publishPacingSample();
            return;
        }
        if (!m_Running)
        {
            publishPacingSample();
            return;
        }

        m_FrameClock.BeginFrame();

        if (!platformResult.ContinueFrame)
        {
            m_FrameClock.Resample();
            publishPacingSample();
            return;
        }

        // Swapchain resize: drain GPU, resize resources, then proceed normally.
        const auto resizeBegin = std::chrono::steady_clock::now();
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
        pacing.ResizeMicros = ElapsedMicros(resizeBegin);

        OperationalTransitionHooks operationalHooks(*m_Device, *m_Renderer, m_RendererOperational);
        const auto operationalBegin = std::chrono::steady_clock::now();
        (void)Core::ExecuteOperationalTransitionContract(operationalHooks);
        pacing.OperationalTransitionMicros = ElapsedMicros(operationalBegin);

        // ── Command drain (pre-sim; ARCH-007 / ADR-0024 D5) ───────────────
        // The single mutation window before simulation: everything the
        // fixed-step ticks and the render snapshot observe this frame is
        // post-command, pre-tick. Commands enqueued during the drain (by
        // handlers) or later in the frame execute at the next frame's drain.
        m_CommandBus.Drain(*m_Scene,
                           CommandDrainServices{
                               .Events = &m_KernelEvents,
                               .Jobs = &m_JobService,
                               .Worlds = &m_WorldRegistry,
                           });

        RunRuntimeModuleFrameHooks(
            FramePhase::AfterCommandDrain, 0.0, 0.0);

        // ── Event pump A (post-drain; ARCH-008 / ADR-0024 D7) ─────────────
        // Command-published events become visible before simulation. Events
        // published by listeners during this pump land in pump B or the next
        // frame, never recursively within this pump.
        (void)m_KernelEvents.Pump();

        // ── Phase 2: Fixed-step simulation + CPU task graph ───────────────
        // Each tick: app adds FrameGraph passes → Engine compiles and executes
        // the ECS system DAG → reset for next tick.

        const double frameDt = m_FrameClock.FrameDeltaClamped(m_MaxFrameDelta);
        frameContext.FrameDeltaSeconds = frameDt;
        m_Accumulator += frameDt;

        const auto fixedStepBegin = std::chrono::steady_clock::now();
        RunFixedStepSimulationTicks(*this,
                                    *m_Application,
                                    *m_FrameGraph,
                                    *m_Scene,
                                    m_Accumulator,
                                    m_FixedDt,
                                    m_MaxSubSteps,
                                    [this](Core::FrameGraph& graph,
                                           ECS::Scene::Registry& scene,
                                           const double fixedDt)
                                    {
                                        RegisterRuntimeModuleSimSystemsForTick(
                                            graph, scene, fixedDt);
                                    });
        pacing.FixedStepMicros = ElapsedMicros(fixedStepBegin);

        // ── Job completion gate (pre-pump B; ARCH-009 / ADR-0024 D8) ─────
        // Workers deposit opaque results into JobService. The main thread
        // checks token/world cancellation here and publishes completion events
        // only for survivors, so pump B owns all completion commits.
        (void)m_JobService.DrainCompletions(m_KernelEvents);

        // ── Event pump B (post-sim; ARCH-008 / ADR-0024 D7) ───────────────
        // Simulation/job events reach runtime modules before UI/extraction.
        (void)m_KernelEvents.Pump();

        const double alpha = m_Accumulator / m_FixedDt;
        frameContext.FixedStepAlpha = alpha;
        bool preRenderTransformFlushNeeded =
            HasPendingPreRenderTransformFlush(*m_Scene);

        // ── RUNTIME-090 Slice B: open the Dear ImGui frame ────────────────
        // BeginFrame runs after Window::PollEvents (Phase 1) and the
        // minimize/resize early returns, immediately before the variable tick,
        // so the editor hook and any ImGui draws issued during OnVariableTick
        // run inside the NewFrame()/Render() scope. Minimized frames return
        // before this point, so a NewFrame is never left without a matching
        // Render() in EndFrame.
        const auto imguiBegin = std::chrono::steady_clock::now();
        m_ImGuiEditorBridge.BeginFrame(frameDt);
        pacing.ImGuiBeginMicros = ElapsedMicros(imguiBegin);

        // ── Phase 3: Variable tick ────────────────────────────────────────
        const auto variableTickBegin = std::chrono::steady_clock::now();
        m_Application->OnVariableTick(*this, alpha, frameDt);
        RunRuntimeModuleFrameHooks(FramePhase::UiBuild, frameDt, alpha);
        pacing.VariableTickMicros = ElapsedMicros(variableTickBegin);
        preRenderTransformFlushNeeded =
            preRenderTransformFlushNeeded ||
            HasPendingPreRenderTransformFlush(*m_Scene);

        // ── RUNTIME-090 Slice B: close the Dear ImGui frame ───────────────
        // EndFrame runs after the variable tick and before the render
        // contract's IRenderer::PrepareFrame(): it invokes the editor hook,
        // calls ImGui::Render(), walks ImDrawData, and submits one
        // ImGuiOverlayFrame to the overlay system (per GRAPHICS-013CQ). The
        // renderer consumer is attached in Initialize(); graphics-side
        // draw upload + recorded Pass.ImGui execution remain later GRAPHICS-079
        // slices.
        const auto imguiEnd = std::chrono::steady_clock::now();
        m_ImGuiEditorBridge.EndFrame();
        pacing.ImGuiEndMicros = ElapsedMicros(imguiEnd);
        preRenderTransformFlushNeeded =
            preRenderTransformFlushNeeded ||
            HasPendingPreRenderTransformFlush(*m_Scene);

        const EditorInputCaptureSnapshot editorCapture =
            m_ImGuiEditorBridge.CaptureSnapshot();
        const bool imguiCapturesMouse =
            editorCapture.CapturedMouse || editorCapture.WidgetsActive;
        const bool imguiCapturesKeyboard =
            editorCapture.CapturedKeyboard || editorCapture.WidgetsActive;
        const bool imguiCapturesInput =
            editorCapture.CapturesViewportInput();

        // ── Phase 4: Build render snapshot ────────────────────────────────
        const auto preRenderSetupBegin = std::chrono::steady_clock::now();
        const Platform::Extent2D viewport = m_Window->GetFramebufferExtent();
        frameContext.RenderInput = Graphics::RenderFrameInput{
            .Alpha    = alpha,
            .Viewport = viewport,
        };
        Graphics::RenderFrameInput& renderInput = frameContext.RenderInput;

        const Platform::IWindow& inputWindow = *m_Window;
        PopulateMainCameraForFrame(m_Config,
                                   m_CameraControllers,
                                   m_ReferenceSceneControl.CameraSeed(),
                                   inputWindow,
                                   viewport,
                                   frameDt,
                                   imguiCapturesInput,
                                   renderInput);
        m_GizmoFrameService.DriveInputForFrame(
            GizmoFrameServiceInput{
                .Scene = *m_Scene,
                .Selection = m_SelectionController,
                .Window = inputWindow,
                .Viewport = viewport,
                .ImGuiCapturesInput = imguiCapturesInput,
                .ImGuiCapturesMouse = imguiCapturesMouse,
                .Camera = renderInput.Camera,
            });
        preRenderTransformFlushNeeded =
            preRenderTransformFlushNeeded ||
            HasPendingPreRenderTransformFlush(*m_Scene);
        pacing.PreRenderSetupMicros += ElapsedMicros(preRenderSetupBegin);

        // ── BUG-024/RUNTIME-145: pre-render transform flush ───────────────
        // Local-transform mutations made after the fixed-step ECS bundle —
        // Sandbox Editor UI inspector edits (applied inside the ImGui editor
        // hook during EndFrame above), OnVariableTick app mutations, and the
        // GizmoInteraction drag just driven — would otherwise reach render
        // extraction with a stale Transform::WorldMatrix and only become
        // visible one frame late (or never, when no further fixed-step tick
        // runs). Flush TransformHierarchy → BoundsPropagation → RenderSync
        // here, before the transform-gizmo packets are built and before
        // ExtractRenderWorld observes the scene, so the rendered model
        // matrix and the gizmo packets agree with the authored transform in
        // the same frame.
        const auto preRenderFlushBegin = std::chrono::steady_clock::now();
        if (preRenderTransformFlushNeeded)
        {
            const PreRenderTransformFlushStats preRenderFlush =
                FlushPreRenderTransformState(*m_Scene);
            pacing.PreRenderTransformFlushRan = true;
            pacing.PreRenderTransformWorldUpdatedObserved =
                preRenderFlush.WorldUpdatedObserved;
            pacing.PreRenderTransformDirtyTransformStamped =
                preRenderFlush.DirtyTransformStamped;
            pacing.PreRenderTransformWorldUpdatedCleared =
                preRenderFlush.WorldUpdatedCleared;
        }
        pacing.PreRenderTransformFlushMicros =
            ElapsedMicros(preRenderFlushBegin);

        // Registered input actions run after the pre-render flush above so
        // camera commands gather refreshed `World::Bounds` that already reflect
        // this frame's OnVariableTick / editor-hook / gizmo transform edits.
        // The default `F` focus action is edge-triggered and suppressed while
        // Dear ImGui owns the keyboard; successful camera actions rebuild the
        // render camera so the snapped view reaches transform-gizmo packet
        // building and render extraction this same frame.
        const auto postFlushSetupBegin = std::chrono::steady_clock::now();
        m_InputActions.DispatchForFrame(m_Config,
                                        m_CameraControllers,
                                        m_SelectionController,
                                        *m_Scene,
                                        inputWindow.GetInput(),
                                        viewport,
                                        imguiCapturesKeyboard,
                                        frameDt,
                                        frameContext.FrameIndex,
                                        renderInput);

        const std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos =
            m_GizmoFrameService.BuildRenderPackets(*m_Scene);
        pacing.PreRenderSetupMicros += ElapsedMicros(postFlushSetupBegin);

        // ── RUNTIME-089 / BUG-026: drain coalesced selection pick ─────────
        const auto selectionPickDrainBegin = std::chrono::steady_clock::now();
        m_SelectionReadback.DrainPendingPickForFrame(
            m_SelectionController,
            m_Renderer->GetSelectionSystem(),
            viewport,
            renderInput);
        pacing.SelectionPickDrainMicros =
            ElapsedMicros(selectionPickDrainBegin);

        RunRuntimeModuleFrameHooks(
            FramePhase::BeforeExtraction, frameDt, alpha);

        // ── Phases 5–9: promoted render-frame contract ───────────────────
        RHI::FrameHandle frame{};
        Graphics::RenderWorld renderWorld{};

        // GRAPHICS-036C — the render-world pool slot lifecycle is driven around
        // extraction inside the hook (producer: AcquireBack/PublishFront;
        // consumer: AcquireFront) and the front reference is released after the
        // frame retires below. `frameIndex` stamps the acquired slot so the
        // consumer's frame-age diagnostic reads 0 in the synchronous baseline.
        frameContext.FrameIndex = m_RenderExtractionService.ConsumeFrameIndex();

        RuntimeRenderFrameHooks renderHooks(*m_Renderer,
                                            *m_Scene,
                                            m_RenderExtractionService.Cache(),
                                            m_AssetResidencyService.CachePtr(),
                                            m_SelectionController,
                                            m_RenderExtractionService.Pool(),
                                            m_Config.Render.SynchronousExtraction,
                                            frameContext.ExtractionStats,
                                            frameContext.FrameIndex,
                                            frameContext.PooledFrontSlot,
                                            frame,
                                            renderInput,
                                            ActiveWorld(),
                                            transformGizmos,
                                            renderWorld,
                                            &pacing);

        const auto renderContractBegin = std::chrono::steady_clock::now();
        const Core::RenderFrameResult renderResult = Core::ExecuteRenderFrameContract(renderHooks);
        pacing.RenderContractMicros = ElapsedMicros(renderContractBegin);
        pacing.RendererBeganFrame = renderResult.BeganFrame;
        pacing.RendererCompletedFrame = renderResult.CompletedFrame;
        m_RenderExtractionService.PublishLastStats(frameContext.ExtractionStats);
        if (!renderResult.BeganFrame)
        {
            // BeginFrame failed before extraction ran, so no slot was acquired
            // (PooledFrontSlot stays kInvalidSlot) — nothing to release.
            m_FrameClock.EndFrame();
            publishPacingSample();
            return;
        }

        const std::uint64_t completedGpuValue = renderResult.CompletedGpuValue;
        const auto presentBegin = std::chrono::steady_clock::now();
        m_Device->Present(frame);
        pacing.PresentMicros = ElapsedMicros(presentBegin);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        TransferHooks transferHooks(*m_Device);
        StreamingHooks streamingHooks(m_AsyncWorkService);
        AssetHooks assetHooks(*m_AssetService,
                              m_AssetResidencyService,
                              *m_Device,
                              m_RenderExtractionService.Cache(),
                              *m_Renderer);
        const auto maintenanceBegin = std::chrono::steady_clock::now();
        Core::ExecuteMaintenanceContract(transferHooks, streamingHooks, assetHooks, 8);
        // ARCH-009 — drop terminal job records after the frame has observed
        // their completion/cancellation state. Does not wait on workers.
        (void)m_JobService.ReapCompleted();
        (void)m_JobService.DrainGpuQueueCompletedTransfers();
        RunRuntimeModuleFrameHooks(FramePhase::Maintenance, frameDt, alpha);
        pacing.MaintenanceMicros = ElapsedMicros(maintenanceBegin);

        // ── RUNTIME-092 / RUNTIME-145: completed selection readbacks ───────
        // The durable StableId winner-map is maintained incrementally from
        // scene StableId component events; this frame phase only drains pick
        // readbacks. Render-id resolution still decodes + validates against the
        // live registry through the attached runtime-owned lookup.
        const auto readbackBegin = std::chrono::steady_clock::now();

        // ── RUNTIME-089 / RUNTIME-093 / BUG-026: completed pick readbacks ──
        m_SelectionReadback.DrainCompletedReadbacksForFrame(
            m_Renderer->GetSelectionSystem(),
            m_SelectionController,
            *m_Scene);
        pacing.SelectionReadbackMicros = ElapsedMicros(readbackBegin);

        // completedGpuValue is the renderer's per-frame timeline value.  The
        // GpuAssetCache currently retires on the CPU frame counter (which is
        // a conservative proxy for GPU completion); a follow-up may key
        // retirement directly on completedGpuValue for tighter recycling.
        (void)completedGpuValue;

        // ── GRAPHICS-036C: release the pooled front at frame retire ────────
        // The renderer consumed the acquired snapshot this frame (commands are
        // recorded by ExecuteFrame above). Synchronous mode releases the current
        // front; pipelined mode releases the previous front consumed by render-N
        // after extraction-N has already published the new front.
        const auto releaseFrontBegin = std::chrono::steady_clock::now();
        m_RenderExtractionService.ReleaseFrontSlot(frameContext.PooledFrontSlot);
        pacing.ReleaseRenderWorldMicros = ElapsedMicros(releaseFrontBegin);

        // ARCH-010 — world mutations are deferred to the Maintenance boundary.
        // Active-world changes occur only after all scene-dependent frame work
        // above has completed; the next frame observes the new active scene.
        const auto worldMaintenanceBegin = std::chrono::steady_clock::now();
        ApplyWorldRegistryMaintenance();
        pacing.MaintenanceMicros += ElapsedMicros(worldMaintenanceBegin);

        // ── Phase 11: Clock EndFrame ──────────────────────────────────────
        m_FrameClock.EndFrame();
        publishPacingSample();
    }

    // ── Query / control ───────────────────────────────────────────────────

    CommandBus& Engine::Commands() noexcept { return m_CommandBus; }
    KernelEventBus& Engine::Events() noexcept { return m_KernelEvents; }
    JobService& Engine::Jobs() noexcept { return m_JobService; }
    WorldRegistry& Engine::Worlds() noexcept { return m_WorldRegistry; }
    const WorldRegistry& Engine::Worlds() const noexcept { return m_WorldRegistry; }
    WorldHandle Engine::ActiveWorld() const noexcept
    {
        return m_WorldRegistry.ActiveWorld();
    }
    ServiceRegistry& Engine::Services() noexcept { return m_ServiceRegistry; }
    const ServiceRegistry& Engine::Services() const noexcept
    {
        return m_ServiceRegistry;
    }
    bool Engine::IsRunning() const noexcept { return m_Running; }
    void Engine::RequestExit()      noexcept { m_Running = false; }

    void Engine::RequestExitFromWindowClose(const std::string_view source)
    {
        if (!m_WindowCloseLogged)
        {
            Core::Log::Info(
                "[Runtime] Window close requested; stopping Engine::Run loop. source={}",
                source);
            m_WindowCloseLogged = true;
        }
        RequestExit();
    }

    Platform::IWindow&    Engine::GetWindow()        noexcept { return *m_Window;        }
    RHI::IDevice&         Engine::GetDevice()        noexcept { return *m_Device;        }
    Graphics::IRenderer&  Engine::GetRenderer()      noexcept { return *m_Renderer;      }
    const Core::Config::EngineConfig& Engine::GetEngineConfig() const noexcept
    {
        return m_Config;
    }

    EngineConfigControl& Engine::GetConfigControl() noexcept
    {
        return *m_ConfigControl;
    }

    const EngineConfigControl& Engine::GetConfigControl() const noexcept
    {
        return *m_ConfigControl;
    }

    Assets::AssetService& Engine::GetAssetService()  noexcept { return *m_AssetService;  }
    Graphics::GpuAssetCache& Engine::GetGpuAssetCache() noexcept
    {
        return m_AssetResidencyService.Cache();
    }
    AssetImportPipeline& Engine::GetAssetImportPipeline() noexcept
    {
        return *m_AssetImportPipeline;
    }

    const AssetImportPipeline& Engine::GetAssetImportPipeline() const noexcept
    {
        return *m_AssetImportPipeline;
    }

    SceneDocument& Engine::GetSceneDocument() noexcept
    {
        return *m_SceneDocument;
    }

    const SceneDocument& Engine::GetSceneDocument() const noexcept
    {
        return *m_SceneDocument;
    }

    const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
    Engine::GetObjectSpaceNormalBakeQueueDiagnosticsForTest() const noexcept
    {
        return m_ObjectSpaceNormalBakeService.QueueDiagnostics();
    }

    std::size_t Engine::GetPendingObjectSpaceNormalBakeCountForTest() const noexcept
    {
        return m_ObjectSpaceNormalBakeService.PendingCount();
    }

    ECS::Scene::Registry& Engine::GetScene()         noexcept { return *m_Scene;         }
    GizmoInteraction& Engine::GetGizmoInteraction() noexcept
    {
        return m_GizmoFrameService.Interaction();
    }

    const GizmoInteraction& Engine::GetGizmoInteraction() const noexcept
    {
        return m_GizmoFrameService.Interaction();
    }

    GizmoUndoStack& Engine::GetGizmoUndoStack() noexcept
    {
        return m_GizmoFrameService.UndoStack();
    }

    const GizmoUndoStack& Engine::GetGizmoUndoStack() const noexcept
    {
        return m_GizmoFrameService.UndoStack();
    }

    const RenderWorldPool& Engine::GetRenderWorldPool() const noexcept
    {
        return m_RenderExtractionService.Pool();
    }
    const RuntimeRenderExtractionStats& Engine::GetLastRenderExtractionStats() const noexcept
    {
        return m_RenderExtractionService.LastStats();
    }

    std::optional<Graphics::GpuGeometryHandle>
    Engine::FindSurfaceGpuGeometry(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto availability =
            m_RenderExtractionService.Cache().FindGpuRenderableAvailability(
                stableEntityId);
        if (!availability.has_value() ||
            !availability->Surface.HasGeometry)
        {
            return std::nullopt;
        }
        return availability->Surface.Geometry;
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    Engine::GetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtractionService.Cache()
            .GetMaterialTextureAssetBindings(stableEntityId);
    }

    const RuntimeFramePacingDiagnostics&
    Engine::GetLastFramePacingDiagnostics() const noexcept
    {
        return m_LastFramePacingDiagnostics;
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    Engine::GetMaterialTextureAssetBindingsForTest(
        const std::uint32_t stableEntityId) const noexcept
    {
        return GetMaterialTextureAssetBindings(stableEntityId);
    }

    RuntimeInputActionHandle Engine::RegisterInputAction(
        RuntimeInputActionDesc desc)
    {
        return m_InputActions.Register(std::move(desc));
    }

    void Engine::UnregisterInputAction(const RuntimeInputActionHandle handle)
    {
        m_InputActions.Unregister(handle);
    }

    void Engine::HandlePlatformEvent(const Platform::Event& event)
    {
        if (std::holds_alternative<Platform::WindowCloseEvent>(event))
        {
            RequestExitFromWindowClose("platform-event");
            return;
        }

        if (const auto* dropped = std::get_if<Platform::WindowDropEvent>(&event))
        {
            HandleWindowDropEvent(*dropped);
        }
    }

    void Engine::DispatchPlatformEventForTest(const Platform::Event& event)
    {
        HandlePlatformEvent(event);
    }

    void Engine::HandleWindowDropEvent(const Platform::WindowDropEvent& event)
    {
        Core::Log::Info("[Runtime] File drop received: path_count={}",
                        event.Paths.size());
        m_AssetImportPipeline->ImportDroppedFilePaths(event.Paths);
    }

    SelectionController&  Engine::GetSelectionController() noexcept { return m_SelectionController; }
    std::optional<ECS::EntityHandle>
    Engine::ResolveEntityByStableId(ECS::Components::StableId id)
    {
        if (!m_Scene)
            return std::nullopt;
        return m_StableEntityLookup.ResolveByStableId(*m_Scene, id);
    }
    const StableEntityLookupDiagnostics&
    Engine::GetStableEntityLookupDiagnostics() const noexcept
    {
        return m_StableEntityLookup.GetDiagnostics();
    }
    EditorCommandHistory& Engine::GetEditorCommandHistory() noexcept
    {
        return m_EditorCommandHistory;
    }
    const EditorCommandHistory& Engine::GetEditorCommandHistory() const noexcept
    {
        return m_EditorCommandHistory;
    }
    const std::optional<PrimitiveSelectionResult>&
    Engine::GetLastRefinedPrimitiveSelection() const noexcept
    {
        return m_SelectionReadback.LastRefinedPrimitive();
    }
    std::uint64_t
    Engine::GetLastRefinedPrimitiveSelectionGeneration() const noexcept
    {
        return m_SelectionReadback.LastRefinedPrimitiveGeneration();
    }
    Core::FrameGraph&     Engine::GetFrameGraph()    noexcept { return *m_FrameGraph;    }

    ReferenceSceneRegistry& Engine::GetReferenceSceneRegistry() noexcept
    {
        return m_ReferenceSceneControl.Registry();
    }

    bool Engine::IsReferenceSceneInstalled() const noexcept
    {
        return m_ReferenceSceneControl.IsInstalled();
    }

    const std::optional<Graphics::CameraViewInput>& Engine::GetReferenceCameraSeed() const noexcept
    {
        return m_ReferenceSceneControl.CameraSeed();
    }

    CameraControllerRegistry& Engine::GetCameraControllerRegistry() noexcept
    {
        return m_CameraControllers;
    }

    void Engine::SetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        if (!m_Scene)
        {
            return;
        }

        ApplyMeshPrimitiveViewSettings(*m_Scene, stableEntityId, settings);
        m_RenderExtractionService.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    void Engine::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        if (m_Scene)
        {
            Extrinsic::Runtime::ClearMeshPrimitiveViewSettings(*m_Scene,
                                                               stableEntityId);
        }
        m_RenderExtractionService.ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    MeshPrimitiveViewSettings Engine::GetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) const noexcept
    {
        if (!m_Scene)
        {
            return MeshPrimitiveViewSettings{};
        }

        return ReadMeshPrimitiveViewSettings(*m_Scene, stableEntityId);
    }

    void Engine::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        RenderExtractionCache::VisualizationAdapterBinding binding)
    {
        m_RenderExtractionService.SetVisualizationAdapterBinding(
            stableEntityId,
            std::move(binding));
    }

    void Engine::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_RenderExtractionService.ClearVisualizationAdapterBinding(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    Engine::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_RenderExtractionService.GetVisualizationAdapterBinding(
            stableEntityId);
    }

    std::uint64_t
    Engine::GetVisualizationAdapterBindingRevision() const noexcept
    {
        return m_RenderExtractionService.GetVisualizationAdapterBindingRevision();
    }

    DerivedJobHandle Engine::SubmitDerivedJob(DerivedJobDesc desc)
    {
        return m_AsyncWorkService.SubmitDerivedJob(std::move(desc));
    }

    void Engine::CancelDerivedJob(const DerivedJobHandle handle)
    {
        m_AsyncWorkService.CancelDerivedJob(handle);
    }

    DerivedJobQueueSnapshot Engine::GetDerivedJobQueueSnapshot() const
    {
        return m_AsyncWorkService.SnapshotDerivedJobs();
    }

    void Engine::SetImGuiEditorCallback(std::function<void()> callback)
    {
        m_ImGuiEditorBridge.SetEditorCallback(std::move(callback));
    }

    void Engine::SetImGuiEditorVisible(const bool visible) noexcept
    {
        m_ImGuiEditorBridge.SetEditorVisible(visible);
    }

    const ImGuiAdapter& Engine::GetImGuiAdapter() const noexcept
    {
        return m_ImGuiEditorBridge.Adapter();
    }
}
