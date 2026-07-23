module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameClock;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.Logging;
import Extrinsic.Core.Tasks;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.DeviceBootstrap;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.JobServiceGpuQueueBridge;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleSchedule;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.EcsSystemBundle;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Registry;

#include "Runtime.Engine.FrameLoop.Internal.hpp"
#include "Runtime.RenderExtractionService.Internal.hpp"

namespace Extrinsic::Runtime
{
    struct Engine::Impl
    {
        explicit Impl(Core::Config::EngineConfig config)
            : m_Config(std::move(config))
        {
        }

        Core::Config::EngineConfig m_Config;
        std::vector<std::unique_ptr<IRuntimeModule>> m_RuntimeModules{};
        std::unique_ptr<Platform::IWindow> m_Window;
        std::unique_ptr<RHI::IDevice> m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;
        RenderExtractionService m_RenderExtractionService{};
        std::unique_ptr<Core::FrameGraph> m_FrameGraph;
        RuntimeInputActionRegistry m_InputActions{};
        CommandBus m_CommandBus{};
        KernelEventBus m_KernelEvents{};
        JobService m_JobService{};
        ServiceRegistry m_ServiceRegistry{};
        WorldRegistry m_WorldRegistry{};
        ECS::Scene::Registry* m_Scene{};
        RuntimeModuleSchedule m_RuntimeModuleSchedule{};
        Core::FrameClock m_FrameClock{};
        double m_Accumulator{0.0};
        double m_FixedDt{1.0 / 60.0};
        double m_MaxFrameDelta{0.25};
        int m_MaxSubSteps{8};
        bool m_Initialized{false};
        bool m_Running{false};
        bool m_ShutdownBegun{false};
        bool m_RendererOperational{false};
        bool m_WindowCloseLogged{false};
        RuntimeFramePacingDiagnostics m_LastFramePacingDiagnostics{};
        JobServiceGpuQueueBridge m_JobServiceGpuQueueBridge{};
    };

    // ── Construction / destruction ────────────────────────────────────────

    Engine::Engine(Core::Config::EngineConfig config)
        : m_Impl(std::make_unique<Impl>(std::move(config)))
    {
    }

    Engine::~Engine()
    {
        if (m_Impl && (m_Impl->m_Initialized || m_Impl->m_ShutdownBegun))
            Shutdown();
    }

    RuntimeRenderRecipeActivationKernel
    Engine::MakeRenderRecipeActivationKernel(
        RuntimeRenderRecipeState& state) noexcept
    {
        return RuntimeRenderRecipeActivationKernel{
            .ActiveConfig = &m_Impl->m_Config,
            .State = &state,
            .ReadFramebufferExtent =
                [this]()
                {
                    if (m_Impl->m_Window != nullptr)
                    {
                        const Platform::Extent2D extent =
                            m_Impl->m_Window->GetFramebufferExtent();
                        return Core::Extent2D{
                            .Width = extent.Width,
                            .Height = extent.Height,
                        };
                    }
                    return Core::Extent2D{
                        .Width = std::max(m_Impl->m_Config.Window.Width, 1),
                        .Height = std::max(m_Impl->m_Config.Window.Height, 1),
                    };
                },
            .SetFrameRecipeOverride =
                [this](
                    std::optional<Graphics::FrameRecipeOverride>
                        recipeOverride)
                {
                    if (m_Impl->m_Renderer == nullptr)
                        return;
                    if (recipeOverride.has_value())
                    {
                        m_Impl->m_Renderer->SetActiveFrameRecipeOverride(
                            std::move(recipeOverride));
                    }
                    else
                    {
                        m_Impl->m_Renderer->ClearActiveFrameRecipeOverride();
                    }
                },
        };
    }

    void Engine::AddModule(std::unique_ptr<IRuntimeModule> module)
    {
        if (!module)
        {
            Core::Log::Error("[RuntimeModule] Engine::AddModule received null.");
            std::terminate();
        }
        if (m_Impl->m_Initialized)
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

        for (const std::unique_ptr<IRuntimeModule>& existing : m_Impl->m_RuntimeModules)
        {
            if (existing && existing->Name() == module->Name())
            {
                Core::Log::Error(
                    "[RuntimeModule] Duplicate module name '{}' rejected.",
                    module->Name());
                std::terminate();
            }
        }

        m_Impl->m_RuntimeModules.push_back(std::move(module));
    }

    void Engine::RegisterRuntimeModulesForBoot(
        const RuntimeRenderRecipeActivationKernel& recipeActivation)
    {
        std::sort(
            m_Impl->m_RuntimeModules.begin(),
            m_Impl->m_RuntimeModules.end(),
            [](const std::unique_ptr<IRuntimeModule>& lhs,
               const std::unique_ptr<IRuntimeModule>& rhs)
            {
                return lhs->Name() < rhs->Name();
            });

        m_Impl->m_RuntimeModuleSchedule.Clear();

        m_Impl->m_ServiceRegistry.BeginRegistration();
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
        requireProvide(m_Impl->m_ServiceRegistry.Provide<JobService>(
                           m_Impl->m_JobService, "Engine"),
                       "JobService");
        requireProvide(m_Impl->m_ServiceRegistry.Provide<RenderExtractionCache>(
                           m_Impl->m_RenderExtractionService.Cache(), "Engine"),
                       "RenderExtractionCache");
        requireProvide(m_Impl->m_ServiceRegistry.Provide<RHI::IDevice>(
                           *m_Impl->m_Device, "Engine"),
                       "RHI::IDevice");
        requireProvide(m_Impl->m_ServiceRegistry.Provide<Platform::IWindow>(
                           *m_Impl->m_Window, "Engine"),
                       "Platform::IWindow");
        requireProvide(m_Impl->m_ServiceRegistry.Provide<Graphics::IRenderer>(
                           *m_Impl->m_Renderer, "Engine"),
                       "Graphics::IRenderer");
        requireProvide(m_Impl->m_ServiceRegistry.Provide<RuntimeInputActionRegistry>(
                           m_Impl->m_InputActions, "Engine"),
                       "RuntimeInputActionRegistry");

        for (const std::unique_ptr<IRuntimeModule>& module : m_Impl->m_RuntimeModules)
        {
            const std::string moduleName{module->Name()};
            EngineSetup setup(
                m_Impl->m_CommandBus,
                m_Impl->m_KernelEvents,
                m_Impl->m_JobService,
                m_Impl->m_WorldRegistry,
                m_Impl->m_ServiceRegistry,
                [this, moduleName](FramePhase phase, RuntimeFrameHook hook)
                {
                    m_Impl->m_RuntimeModuleSchedule.RegisterFrameHook(
                        moduleName, phase, std::move(hook));
                },
                recipeActivation,
                [this, moduleName](RuntimeViewportInputHook hook)
                {
                    m_Impl->m_RuntimeModuleSchedule.RegisterViewportInputHook(
                        moduleName, std::move(hook));
                },
                &m_Impl->m_Initialized);

            Core::Result result = module->OnRegister(setup);
            if (!result.has_value())
            {
                Core::Log::Error(
                    "[RuntimeModule] OnRegister failed for module '{}': {}",
                    moduleName,
                    Core::Error::ToString(result.error()));
                std::terminate();
            }
            if (m_Impl->m_ServiceRegistry.HasBootErrors())
            {
                Core::Log::Error(
                    "[RuntimeModule] Registration failed for module '{}': {}",
                    moduleName,
                    m_Impl->m_ServiceRegistry.LastBootError());
                std::terminate();
            }
        }

    }

    void Engine::ResolveRuntimeModulesForBoot(
        const RuntimeRenderRecipeActivationKernel& recipeActivation)
    {
        m_Impl->m_ServiceRegistry.BeginResolution();
        for (const std::unique_ptr<IRuntimeModule>& module : m_Impl->m_RuntimeModules)
        {
            const std::string moduleName{module->Name()};
            EngineSetup setup(
                m_Impl->m_CommandBus,
                m_Impl->m_KernelEvents,
                m_Impl->m_JobService,
                m_Impl->m_WorldRegistry,
                m_Impl->m_ServiceRegistry,
                {},
                recipeActivation,
                {},
                &m_Impl->m_Initialized);

            Core::Result result = module->OnResolve(setup);
            if (!result.has_value())
            {
                Core::Log::Error(
                    "[RuntimeModule] OnResolve failed for module '{}': {}",
                    moduleName,
                    Core::Error::ToString(result.error()));
                std::terminate();
            }
            if (m_Impl->m_ServiceRegistry.HasBootErrors())
            {
                Core::Log::Error(
                    "[RuntimeModule] Resolution failed for module '{}': {}",
                    moduleName,
                    m_Impl->m_ServiceRegistry.LastBootError());
                std::terminate();
            }
        }

        if (Core::Result result = m_Impl->m_ServiceRegistry.ValidateBoot();
            !result.has_value())
        {
            Core::Log::Error(
                "[RuntimeModule] ServiceRegistry boot validation failed: {}",
                m_Impl->m_ServiceRegistry.LastBootError());
            std::terminate();
        }
        m_Impl->m_ServiceRegistry.Lock();

        m_Impl->m_RuntimeModuleSchedule.FinalizeForBoot();
    }

    void Engine::RunRuntimeModuleFrameHooks(
        const FramePhase phase,
        const double frameDt,
        const double alpha,
        EditorInputCaptureSnapshot& editorCapture,
        RuntimeFramePacingDiagnostics& pacing)
    {
        if (!m_Impl->m_Scene)
            return;

        m_Impl->m_RuntimeModuleSchedule.RunFrameHooks(
            RuntimeModuleFrameHookDispatchContext{
                .Phase = phase,
                .ActiveWorld = *m_Impl->m_Scene,
                .ActiveWorldHandle = ActiveWorld(),
                .Commands = m_Impl->m_CommandBus,
                .Events = m_Impl->m_KernelEvents,
                .Jobs = m_Impl->m_JobService,
                .Worlds = m_Impl->m_WorldRegistry,
                .Services = m_Impl->m_ServiceRegistry,
                .EditorCapture = editorCapture,
                .Pacing = pacing,
                .FrameIndex = m_Impl->m_RenderExtractionService.CurrentFrameIndex(),
                .FrameDeltaSeconds = frameDt,
                .FixedStepAlpha = alpha,
            });
    }

    void Engine::AnnounceRuntimeShutdown()
    {
        m_Impl->m_Initialized = false;
        m_Impl->m_KernelEvents.Publish(RuntimeShutdownAnnounced{});
        (void)m_Impl->m_KernelEvents.Pump();
    }

    void Engine::ShutdownRuntimeModules()
    {
        RuntimeModuleShutdownContext context{
            .Commands = m_Impl->m_CommandBus,
            .Events = m_Impl->m_KernelEvents,
            .Jobs = m_Impl->m_JobService,
            .Worlds = m_Impl->m_WorldRegistry,
            .Services = m_Impl->m_ServiceRegistry,
        };
        for (auto it = m_Impl->m_RuntimeModules.rbegin();
             it != m_Impl->m_RuntimeModules.rend();
             ++it)
        {
            if (*it)
                (*it)->OnShutdown(context);
        }
        if (m_Impl->m_Device)
            (void)m_Impl->m_ServiceRegistry.Withdraw<RHI::IDevice>(*m_Impl->m_Device);
        // Module-owned services may be destroyed during OnShutdown. Clear all
        // borrowed records before control returns to the remaining kernel
        // teardown so no post-shutdown lookup can observe a dangling service.
        m_Impl->m_ServiceRegistry.Reset();
    }

    void Engine::RefreshActiveWorldScenePointer() noexcept
    {
        m_Impl->m_Scene = m_Impl->m_WorldRegistry.Get(m_Impl->m_WorldRegistry.ActiveWorld());
    }

    void Engine::ApplyWorldRegistryMaintenance()
    {
        const WorldHandle previousActive = m_Impl->m_WorldRegistry.ActiveWorld();
        const WorldRegistryMaintenanceStats stats =
            m_Impl->m_WorldRegistry.ApplyMaintenance(m_Impl->m_KernelEvents, m_Impl->m_JobService);

        if (stats.AppliedActiveWorldChanges == 0u ||
            m_Impl->m_WorldRegistry.ActiveWorld() == previousActive)
        {
            return;
        }

        // Active-world switching is a kernel maintenance path. Clear generic
        // render-extraction state immediately; optional modules validate and
        // rebind their own world-scoped borrowers independently.
        if (m_Impl->m_Renderer != nullptr)
        {
            m_Impl->m_RenderExtractionService.Cache().ClearSceneState(
                *m_Impl->m_Renderer);
        }

        RefreshActiveWorldScenePointer();
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Engine::Initialize()
    {
        m_Impl->m_ShutdownBegun = false;
        // ── 1. CPU fiber scheduler ────────────────────────────────────────
        // Must be first — all three graphs dispatch through it.
        Core::Tasks::Scheduler::Initialize(m_Impl->m_Config.Simulation.WorkerThreadCount);

        // ARCH-007 — built-in kernel command (ADR-0024 D13): the sanctioned
        // shutdown path for module/UI code. Future module code enqueues
        // `QuitRequested` instead of reaching for an Engine& to call
        // shutdown entry points directly.
        m_Impl->m_CommandBus.RegisterHandler<QuitRequested>(
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
        m_Impl->m_Window   = Platform::CreateWindow(m_Impl->m_Config.Window);
        if (m_Impl->m_Window && m_Impl->m_Window->ShouldClose())
        {
            Core::Log::Warn(
                "[Runtime] Platform window initialized closed; Engine::Run() will execute zero frames unless the test or caller requests a headless-capable window backend.");
        }
        m_Impl->m_Device   = CreateRuntimeDevice(m_Impl->m_Config.Render);
        const Platform::Extent2D initialExtent = m_Impl->m_Window->GetFramebufferExtent();
        m_Impl->m_Device->Initialize(RHI::MakeDeviceCreateDesc(
            m_Impl->m_Config.Render,
            initialExtent,
            m_Impl->m_Window->GetNativeHandle()));

        // GRAPHICS-033B: emit the Vulkan-requested-but-not-operational
        // breadcrumb and bump the operational diagnostics counters exactly
        // once per startup when the runtime requested the promoted Vulkan
        // device but the resolved device is not operational. Runtime never
        // aborts on this fallback — see the truth table in
        // `src/graphics/vulkan/README.md`.
        if (ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(
                m_Impl->m_Config.Render, m_Impl->m_Device->IsOperational()))
        {
#if defined(EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN)
            const Backends::Vulkan::VulkanOperationalStatus status =
                Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus(m_Impl->m_Device.get());
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

        m_Impl->m_Renderer = Graphics::CreateRenderer();
        m_Impl->m_Renderer->Initialize(*m_Impl->m_Device);
        m_Impl->m_RendererOperational = m_Impl->m_Device->IsOperational();
        RuntimeRenderRecipeState startupRecipeState{};
        const RuntimeRenderRecipeActivationKernel recipeActivation =
            MakeRenderRecipeActivationKernel(startupRecipeState);
        ResetRuntimeRenderRecipeActivation(recipeActivation);
        m_Impl->m_JobServiceGpuQueueBridge.Install(*m_Impl->m_Renderer, m_Impl->m_JobService);
        if (!m_Impl->m_Config.Render.DefaultRecipeConfigPath.empty())
        {
            (void)LoadAndApplyRuntimeRenderRecipeConfigFile(
                recipeActivation,
                m_Impl->m_Config.Render.DefaultRecipeConfigPath,
                RuntimeRenderRecipeActivationSource::StartupConfigFile);
        }

        // ── 2d. Render-world pool (GRAPHICS-036C) ─────────────────────────
        // Size the runtime-owned slot pool from the render config: one logical
        // buffer in the default synchronous mode (serial extraction/render,
        // behavior-preserving), or the triple-buffered default when pipelined
        // extraction is requested. The production default remains synchronous;
        // GRAPHICS-036D proves the opt-in render-N-1 path by consuming the
        // previous front while extraction writes the newly acquired back slot.
        m_Impl->m_RenderExtractionService.ConfigurePool(
            m_Impl->m_Config.Render.SynchronousExtraction);

        // ── 3. CPU task graph (ECS system scheduling) ─────────────────────
        m_Impl->m_FrameGraph = std::make_unique<Core::FrameGraph>();

        // ── 4. World registry / boot ECS scene ────────────────────────────
        // Create the boot world before module registration so every
        // EngineSetup observes the same live kernel substrate as before.
        m_Impl->m_WorldRegistry.Clear();
        const WorldHandle bootWorld = m_Impl->m_WorldRegistry.CreateWorld("Main");
        m_Impl->m_Scene = m_Impl->m_WorldRegistry.Get(bootWorld);
        // ── 5. Runtime-module registration (ARCH-011) ──────────────────────
        // App-composed owners publish narrow construction capabilities after
        // every required Engine built-in is live. Resolution/finalization
        // remains at the pre-application boundary after every provider has
        // registered.
        RegisterRuntimeModulesForBoot(recipeActivation);

        // ── 6. Runtime-module resolution/finalization (ARCH-011) ──────────
        ResolveRuntimeModulesForBoot(recipeActivation);

        m_Impl->m_Initialized = true;
        m_Impl->m_Running     = true;
        m_Impl->m_WindowCloseLogged = false;

        m_Impl->m_Window->Listen(
            [this](const Platform::Event& event)
            {
                HandlePlatformEvent(event);
            });
    }

    void Engine::BeginShutdown()
    {
        if (m_Impl->m_ShutdownBegun || (!m_Impl->m_Initialized && !m_Impl->m_Window && !m_Impl->m_Device))
            return;

        // ARCH-007 — drop commands enqueued after the final frame's drain
        // (e.g. from a frame hook just before RequestExit()). The engine
        // is documented as reusable via Shutdown() + Initialize(); stale
        // commands must not replay into the next session's fresh scene.
        m_Impl->m_CommandBus.DiscardPending();
        AnnounceRuntimeShutdown();

        if (m_Impl->m_Window)
            m_Impl->m_Window->Listen({});

        (void)m_Impl->m_JobServiceGpuQueueBridge.ShutdownParticipants(
            m_Impl->m_Renderer.get(),
            m_Impl->m_JobService,
            [this]
            {
                if (m_Impl->m_Device)
                    m_Impl->m_Device->WaitIdle();
            });

        m_Impl->m_Running = false;
        if (m_Impl->m_Device)
            m_Impl->m_Device->WaitIdle();
        m_Impl->m_ShutdownBegun = true;
    }

    void Engine::Shutdown()
    {
        BeginShutdown();
        if (!m_Impl->m_ShutdownBegun)
            return;

        struct ShutdownHooks final : Core::IShutdownHooks
        {
            Engine& Owner;
            bool& Initialized;
            std::unique_ptr<Platform::IWindow>& Window;
            std::unique_ptr<RHI::IDevice>& Device;
            std::unique_ptr<Graphics::IRenderer>& Renderer;
            std::unique_ptr<Core::FrameGraph>& FrameGraph;
            WorldRegistry& Worlds;
            ECS::Scene::Registry*& Scene;

            ShutdownHooks(Engine& owner,
                          bool& initialized,
                          std::unique_ptr<Platform::IWindow>& window,
                          std::unique_ptr<RHI::IDevice>& device,
                          std::unique_ptr<Graphics::IRenderer>& renderer,
                          std::unique_ptr<Core::FrameGraph>& frameGraph,
                          WorldRegistry& worlds,
                          ECS::Scene::Registry*& scene)
                : Owner(owner)
                , Initialized(initialized)
                , Window(window)
                , Device(device)
                , Renderer(renderer)
                , FrameGraph(frameGraph)
                , Worlds(worlds)
                , Scene(scene)
            {
            }
            void ShutdownStreaming() override
            {
                Owner.ShutdownRuntimeModules();
            }
            void DestroyScene() override
            {
                Scene = nullptr;
                Worlds.Clear();
            }
            void DestroyAssets() override {}
            void DestroyStreamingState() override
            {
                // App-composed async work resets in module OnShutdown.
            }
            void DestroyFrameGraph() override { FrameGraph.reset(); }
            void ShutdownRenderer() override
            {
                if (Renderer)
                {
                    Owner.m_Impl->m_RenderExtractionService.Shutdown(*Renderer);
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
                            m_Impl->m_Initialized,
                            m_Impl->m_Window,
                            m_Impl->m_Device,
                            m_Impl->m_Renderer,
                            m_Impl->m_FrameGraph,
                            m_Impl->m_WorldRegistry,
                            m_Impl->m_Scene);
        Core::ExecuteShutdownContract(hooks);
        m_Impl->m_ShutdownBegun = false;
    }

    // ── Main loop ─────────────────────────────────────────────────────────

    void Engine::Run()
    {
        while (m_Impl->m_Running && m_Impl->m_Window != nullptr && !m_Impl->m_Window->ShouldClose())
            RunFrame();

        if (m_Impl->m_Running && m_Impl->m_Window != nullptr && m_Impl->m_Window->ShouldClose())
            RequestExitFromWindowClose("native-poll");
    }

    void Engine::RunFrame()
    {
        RuntimeFrameContext frameContext{};
        RuntimeFramePacingDiagnostics pacing{};
        pacing.Valid = true;
        pacing.FrameIndex = m_Impl->m_RenderExtractionService.CurrentFrameIndex();
        const auto framePacingBegin = std::chrono::steady_clock::now();
        const auto publishPacingSample = [&]()
        {
            if (m_Impl->m_Renderer)
                MirrorRenderGraphFramePacingDiagnostics(
                    pacing, m_Impl->m_Renderer->GetLastRenderGraphStats());
            pacing.TotalMicros = ElapsedMicros(framePacingBegin);
            m_Impl->m_LastFramePacingDiagnostics = pacing;
        };

        // ── Phase 1: Platform ─────────────────────────────────────────────
        PlatformFrameHooks platformHooks{*m_Impl->m_Window};
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
        if (!m_Impl->m_Running)
        {
            publishPacingSample();
            return;
        }

        m_Impl->m_FrameClock.BeginFrame();

        if (!platformResult.ContinueFrame)
        {
            m_Impl->m_FrameClock.Resample();
            publishPacingSample();
            return;
        }

        // Swapchain resize: drain GPU, resize resources, then proceed normally.
        const auto resizeBegin = std::chrono::steady_clock::now();
        if (m_Impl->m_Window->WasResized())
        {
            const auto extent = m_Impl->m_Window->GetFramebufferExtent();
            if (extent.Width > 0 && extent.Height > 0)
            {
                m_Impl->m_Device->WaitIdle();
                m_Impl->m_Device->Resize(static_cast<unsigned>(extent.Width),
                                 static_cast<unsigned>(extent.Height));
                m_Impl->m_Renderer->Resize(static_cast<unsigned>(extent.Width),
                                   static_cast<unsigned>(extent.Height));
            }
            m_Impl->m_Window->AcknowledgeResize();
        }
        pacing.ResizeMicros = ElapsedMicros(resizeBegin);

        OperationalTransitionHooks operationalHooks(*m_Impl->m_Device, *m_Impl->m_Renderer, m_Impl->m_RendererOperational);
        const auto operationalBegin = std::chrono::steady_clock::now();
        (void)Core::ExecuteOperationalTransitionContract(operationalHooks);
        pacing.OperationalTransitionMicros = ElapsedMicros(operationalBegin);

        // ── Command drain (pre-sim; ARCH-007 / ADR-0024 D5) ───────────────
        // The single mutation window before simulation: everything the
        // fixed-step ticks and the render snapshot observe this frame is
        // post-command, pre-tick. Commands enqueued during the drain (by
        // handlers) or later in the frame execute at the next frame's drain.
        m_Impl->m_CommandBus.Drain(*m_Impl->m_Scene,
                           CommandDrainServices{
                               .Events = &m_Impl->m_KernelEvents,
                               .Jobs = &m_Impl->m_JobService,
                               .Worlds = &m_Impl->m_WorldRegistry,
                           });

        // ── Event pump A (post-drain; ARCH-008 / ADR-0024 D7) ─────────────
        // Command-published events become visible before simulation. Events
        // published by listeners during this pump land in pump B or the next
        // frame, never recursively within this pump.
        (void)m_Impl->m_KernelEvents.Pump();

        // ── Phase 2: Fixed-step simulation + CPU task graph ───────────────
        // Each tick: the promoted ECS bundle adds FrameGraph passes → Engine
        // compiles and executes the DAG → resets for exact-plan replay.

        const double frameDt = m_Impl->m_FrameClock.FrameDeltaClamped(m_Impl->m_MaxFrameDelta);
        frameContext.FrameDeltaSeconds = frameDt;
        m_Impl->m_Accumulator += frameDt;

        const auto fixedStepBegin = std::chrono::steady_clock::now();
        RunFixedStepSimulationTicks(*m_Impl->m_FrameGraph,
                                    *m_Impl->m_Scene,
                                    m_Impl->m_Accumulator,
                                    m_Impl->m_FixedDt,
                                    m_Impl->m_MaxSubSteps);
        pacing.FixedStepMicros = ElapsedMicros(fixedStepBegin);

        // ── Job completion gate (pre-pump B; ARCH-009 / ADR-0024 D8) ─────
        // Workers deposit opaque results into JobService. The main thread
        // checks token/world cancellation here and publishes completion events
        // only for survivors, so pump B owns all completion commits.
        (void)m_Impl->m_JobService.DrainCompletions(m_Impl->m_KernelEvents);

        // ── Event pump B (post-sim; ARCH-008 / ADR-0024 D7) ───────────────
        // Simulation/job events reach runtime modules before UI/extraction.
        (void)m_Impl->m_KernelEvents.Pump();

        const double alpha = m_Impl->m_Accumulator / m_Impl->m_FixedDt;
        frameContext.FixedStepAlpha = alpha;
        bool preRenderTransformFlushNeeded =
            HasPendingPreRenderTransformFlush(*m_Impl->m_Scene);

        // Optional editor UI modules open their frame immediately before the
        // variable tick. Minimized frames return before this point, so a
        // UiBegin hook is never left without its paired UiEndCapture hook.
        RunRuntimeModuleFrameHooks(
            FramePhase::UiBegin,
            frameDt,
            alpha,
            frameContext.EditorCapture,
            pacing);

        // ── Phase 3: Variable-frame hooks ────────────────────────────────
        const auto variableTickBegin = std::chrono::steady_clock::now();
        RunRuntimeModuleFrameHooks(
            FramePhase::UiBuild,
            frameDt,
            alpha,
            frameContext.EditorCapture,
            pacing);
        pacing.VariableTickMicros = ElapsedMicros(variableTickBegin);
        preRenderTransformFlushNeeded =
            preRenderTransformFlushNeeded ||
            HasPendingPreRenderTransformFlush(*m_Impl->m_Scene);

        // Optional editor UI modules close, publish overlay data, and write the
        // single frame-owned capture snapshot at the existing pre-render
        // boundary.
        RunRuntimeModuleFrameHooks(
            FramePhase::UiEndCapture,
            frameDt,
            alpha,
            frameContext.EditorCapture,
            pacing);
        preRenderTransformFlushNeeded =
            preRenderTransformFlushNeeded ||
            HasPendingPreRenderTransformFlush(*m_Impl->m_Scene);

        const EditorInputCaptureSnapshot& editorCapture =
            frameContext.EditorCapture;
        const bool imguiCapturesKeyboard =
            editorCapture.CapturedKeyboard || editorCapture.WidgetsActive;

        // ── Phase 4: Build render snapshot ────────────────────────────────
        const auto preRenderSetupBegin = std::chrono::steady_clock::now();
        const Platform::Extent2D viewport = m_Impl->m_Window->GetFramebufferExtent();
        frameContext.RenderInput = Graphics::RenderFrameInput{
            .Alpha    = alpha,
            .Viewport = viewport,
            .EnableGpuProfiling =
                m_Impl->m_Config.Render.EnableGpuProfiling,
        };
        Graphics::RenderFrameInput& renderInput = frameContext.RenderInput;

        const Platform::IWindow& inputWindow = *m_Impl->m_Window;
        m_Impl->m_RuntimeModuleSchedule.RunViewportInputHooks(
            RuntimeViewportInputHookContext{
                .Config = m_Impl->m_Config,
                .ActiveWorldHandle = ActiveWorld(),
                .Input = inputWindow.GetInput(),
                .Viewport = viewport,
                .EditorCapture = editorCapture,
                .RenderInput = renderInput,
                .FrameDeltaSeconds = frameDt,
            });
        preRenderTransformFlushNeeded =
            preRenderTransformFlushNeeded ||
            HasPendingPreRenderTransformFlush(*m_Impl->m_Scene);
        pacing.PreRenderSetupMicros += ElapsedMicros(preRenderSetupBegin);

        // ── BUG-024/RUNTIME-145: pre-render transform flush ───────────────
        // Local-transform mutations made after the fixed-step ECS bundle —
        // Sandbox Editor UI inspector edits (applied inside the ImGui editor
        // hook during EndFrame above), variable-frame hook mutations, and the
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
                FlushPreRenderTransformState(*m_Impl->m_Scene);
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
        // this frame's module-frame-hook / editor-hook / gizmo transform edits.
        // The default `F` focus action is edge-triggered and suppressed while
        // Dear ImGui owns the keyboard; successful camera actions rebuild the
        // render camera so the snapped view reaches transform-gizmo packet
        // building and render extraction this same frame.
        const auto postFlushSetupBegin = std::chrono::steady_clock::now();
        m_Impl->m_InputActions.DispatchForFrame(m_Impl->m_Config,
                                        *m_Impl->m_Scene,
                                        inputWindow.GetInput(),
                                        viewport,
                                        imguiCapturesKeyboard,
                                        frameDt,
                                        frameContext.FrameIndex,
                                        renderInput);

        pacing.PreRenderSetupMicros += ElapsedMicros(postFlushSetupBegin);

        RunRuntimeModuleFrameHooks(
            FramePhase::BeforeExtraction,
            frameDt,
            alpha,
            frameContext.EditorCapture,
            pacing);

        // ── Phases 5–9: promoted render-frame contract ───────────────────
        RHI::FrameHandle frame{};
        Graphics::RenderWorld renderWorld{};

        // GRAPHICS-036C — the render-world pool slot lifecycle is driven around
        // extraction inside the hook (producer: AcquireBack/PublishFront;
        // consumer: AcquireFront) and the front reference is released after the
        // frame retires below. `frameIndex` stamps the acquired slot so the
        // consumer's frame-age diagnostic reads 0 in the synchronous baseline.
        frameContext.FrameIndex = m_Impl->m_RenderExtractionService.ConsumeFrameIndex();

        Graphics::GpuAssetCache* const gpuAssetCache =
            m_Impl->m_ServiceRegistry.Find<Graphics::GpuAssetCache>();
        RuntimeRenderFrameHooks renderHooks(*m_Impl->m_Renderer,
                                            *m_Impl->m_Scene,
                                            m_Impl->m_RenderExtractionService.Cache(),
                                            gpuAssetCache,
                                            m_Impl->m_RenderExtractionService.Pool(),
                                            m_Impl->m_Config.Render.SynchronousExtraction,
                                            frameContext.ExtractionStats,
                                            frameContext.FrameIndex,
                                            frameContext.PooledFrontSlot,
                                            frame,
                                            renderInput,
                                            ActiveWorld(),
                                            renderWorld,
                                            &pacing);

        const auto renderContractBegin = std::chrono::steady_clock::now();
        const Core::RenderFrameResult renderResult = Core::ExecuteRenderFrameContract(renderHooks);
        pacing.RenderContractMicros = ElapsedMicros(renderContractBegin);
        pacing.RendererBeganFrame = renderResult.BeganFrame;
        pacing.RendererCompletedFrame = renderResult.CompletedFrame;
        if (!renderResult.BeganFrame)
        {
            // BeginFrame failed before extraction ran, so no slot was acquired
            // (PooledFrontSlot stays kInvalidSlot) — nothing to release.
            m_Impl->m_FrameClock.EndFrame();
            publishPacingSample();
            return;
        }

        const std::uint64_t completedGpuValue = renderResult.CompletedGpuValue;
        const auto presentBegin = std::chrono::steady_clock::now();
        m_Impl->m_Device->Present(frame);
        pacing.PresentMicros = ElapsedMicros(presentBegin);

        // ── Phase 10: Maintenance ─────────────────────────────────────────
        TransferHooks transferHooks(*m_Impl->m_Device);
        AssetHooks assetHooks(
                              m_Impl->m_ServiceRegistry.Find<
                                  Core::IAssetFrameHooks>(),
                              *m_Impl->m_Device,
                              m_Impl->m_RenderExtractionService.Cache(),
                              *m_Impl->m_Renderer);
        const auto maintenanceBegin = std::chrono::steady_clock::now();
        if (Core::IStreamingFrameHooks* streamingHooks =
                m_Impl->m_ServiceRegistry.Find<Core::IStreamingFrameHooks>();
            streamingHooks != nullptr)
        {
            Core::ExecuteMaintenanceContract(
                transferHooks, *streamingHooks, assetHooks, 8u);
        }
        else
        {
            // Async work is app-optional. Preserve the non-async portions of
            // the Maintenance lane without installing a fake hook provider.
            transferHooks.CollectCompletedTransfers();
            assetHooks.TickAssets();
        }
        // ARCH-009 — drop terminal job records after the frame has observed
        // their completion/cancellation state. Does not wait on workers.
        (void)m_Impl->m_JobService.ReapCompleted();
        (void)m_Impl->m_JobService.DrainGpuQueueCompletedTransfers();
        RunRuntimeModuleFrameHooks(
            FramePhase::Maintenance,
            frameDt,
            alpha,
            frameContext.EditorCapture,
            pacing);
        pacing.MaintenanceMicros = ElapsedMicros(maintenanceBegin);

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
        m_Impl->m_RenderExtractionService.ReleaseFrontSlot(frameContext.PooledFrontSlot);
        pacing.ReleaseRenderWorldMicros = ElapsedMicros(releaseFrontBegin);

        // ARCH-010 — world mutations are deferred to the Maintenance boundary.
        // Active-world changes occur only after all scene-dependent frame work
        // above has completed; the next frame observes the new active scene.
        const auto worldMaintenanceBegin = std::chrono::steady_clock::now();
        ApplyWorldRegistryMaintenance();
        pacing.MaintenanceMicros += ElapsedMicros(worldMaintenanceBegin);

        // ── Phase 11: Clock EndFrame ──────────────────────────────────────
        m_Impl->m_FrameClock.EndFrame();
        publishPacingSample();
    }

    // ── Query / control ───────────────────────────────────────────────────

    CommandBus& Engine::Commands() noexcept { return m_Impl->m_CommandBus; }
    KernelEventBus& Engine::Events() noexcept { return m_Impl->m_KernelEvents; }
    JobService& Engine::Jobs() noexcept { return m_Impl->m_JobService; }
    WorldRegistry& Engine::Worlds() noexcept { return m_Impl->m_WorldRegistry; }
    const WorldRegistry& Engine::Worlds() const noexcept { return m_Impl->m_WorldRegistry; }
    WorldHandle Engine::ActiveWorld() const noexcept
    {
        return m_Impl->m_WorldRegistry.ActiveWorld();
    }
    ServiceRegistry& Engine::Services() noexcept { return m_Impl->m_ServiceRegistry; }
    const ServiceRegistry& Engine::Services() const noexcept
    {
        return m_Impl->m_ServiceRegistry;
    }
    bool Engine::IsRunning() const noexcept { return m_Impl->m_Running; }
    void Engine::RequestExit()      noexcept { m_Impl->m_Running = false; }

    void Engine::RequestExitFromWindowClose(const std::string_view source)
    {
        if (!m_Impl->m_WindowCloseLogged)
        {
            Core::Log::Info(
                "[Runtime] Window close requested; stopping Engine::Run loop. source={}",
                source);
            m_Impl->m_WindowCloseLogged = true;
        }
        RequestExit();
    }

    Platform::IWindow&    Engine::GetWindow()        noexcept { return *m_Impl->m_Window;        }
    RHI::IDevice&         Engine::GetDevice()        noexcept { return *m_Impl->m_Device;        }
    Graphics::IRenderer&  Engine::GetRenderer()      noexcept { return *m_Impl->m_Renderer;      }
    const Core::Config::EngineConfig& Engine::GetEngineConfig() const noexcept
    {
        return m_Impl->m_Config;
    }

    const RuntimeFramePacingDiagnostics&
    Engine::GetLastFramePacingDiagnostics() const noexcept
    {
        return m_Impl->m_LastFramePacingDiagnostics;
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
        if (AssetImportPipeline* const pipeline =
                m_Impl->m_ServiceRegistry.Find<AssetImportPipeline>();
            pipeline != nullptr)
        {
            pipeline->ImportDroppedFilePaths(event.Paths);
        }
    }

}
