module;

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.AssetWorkflowModule;

import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.DeviceBootstrap;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ObjectSpaceNormalBakeService;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    struct AssetWorkflowModule::Impl
    {
        struct State
        {
            AssetImportPipeline* Pipeline{nullptr};
            ObjectSpaceNormalBakeService* Bake{nullptr};

            JobService* Jobs{nullptr};
            WorldRegistry* Worlds{nullptr};
            const bool* Initialized{nullptr};
            const Core::Config::EngineConfig* Config{nullptr};
            RHI::IDevice* Device{nullptr};
            Graphics::IRenderer* Renderer{nullptr};
            RenderExtractionCache* Extraction{nullptr};

            SceneDocumentModule* Documents{nullptr};
            EditorCommandHistory* History{nullptr};
            StreamingExecutor* Streaming{nullptr};
            SelectionController* Selection{nullptr};

            std::unique_ptr<Assets::AssetService> Assets{};
            std::unique_ptr<Graphics::GpuAssetCache> Cache{};
            Assets::AssetEventBus::ListenerToken CacheListener{
                Assets::AssetEventBus::InvalidToken};
            std::unique_ptr<AssetModelTextureHandoff> TextureHandoff{};
            std::unique_ptr<AssetModelSceneHandoff> SceneHandoff{};

            WorldHandle BoundWorld{};
            ECS::Scene::Registry* BoundRegistry{nullptr};
            std::uint64_t BindingEpoch{1u};
            SceneReplacementParticipantHandle DocumentParticipant{};
            GpuQueueParticipantHandle BakeParticipant{};
            bool AcceptingCallbacks{false};
            bool ShutdownAnnounced{false};
            std::weak_ptr<State> Self{};

            void AdvanceBindingEpoch() noexcept
            {
                ++BindingEpoch;
                if (BindingEpoch == 0u)
                    BindingEpoch = 1u;
            }

            [[nodiscard]] bool IsBindingCurrent(
                const std::uint64_t expectedEpoch) const noexcept
            {
                return AcceptingCallbacks &&
                       expectedEpoch == BindingEpoch &&
                       Worlds != nullptr &&
                       BoundWorld.IsValid() &&
                       BoundRegistry != nullptr &&
                       Worlds->ActiveWorld() == BoundWorld &&
                       Worlds->Get(BoundWorld) == BoundRegistry;
            }

            void DetachPipeline() noexcept
            {
                if (Pipeline != nullptr)
                    Pipeline->SetDependencies({});
            }

            void DestroySceneBinding(
                const bool clearRenderAndBakeState)
            {
                DetachPipeline();

                if (clearRenderAndBakeState)
                {
                    if (Extraction != nullptr &&
                        Renderer != nullptr)
                    {
                        Extraction->ClearSceneState(*Renderer);
                    }
                    if (Bake != nullptr)
                        Bake->Queue().Clear();
                }

                // The handoff destructor destroys its records through the
                // outgoing registry. Reset it before that registry may retire.
                SceneHandoff.reset();
                BoundWorld = {};
                BoundRegistry = nullptr;
                AdvanceBindingEpoch();
            }

            void BindTo(
                const WorldHandle world,
                ECS::Scene::Registry* const registry)
            {
                BoundWorld = world;
                BoundRegistry = registry;
                AdvanceBindingEpoch();

                if (!AcceptingCallbacks ||
                    !BoundWorld.IsValid() ||
                    BoundRegistry == nullptr ||
                    Assets == nullptr ||
                    Cache == nullptr ||
                    TextureHandoff == nullptr ||
                    Renderer == nullptr ||
                    Device == nullptr ||
                    Pipeline == nullptr ||
                    Bake == nullptr)
                {
                    DetachPipeline();
                    return;
                }

                const std::uint64_t expectedEpoch =
                    BindingEpoch;
                const std::weak_ptr<State> weakState = Self;
                SceneHandoff =
                    std::make_unique<AssetModelSceneHandoff>(
                        *Assets,
                        *Cache,
                        *BoundRegistry,
                        *Renderer,
                        AssetModelSceneHandoffOptions{
                            .World = BoundWorld,
                            .BindingValid =
                                [weakState, expectedEpoch]()
                                {
                                    if (const auto state =
                                            weakState.lock())
                                    {
                                        return state->
                                            IsBindingCurrent(
                                                expectedEpoch);
                                    }
                                    return false;
                                },
                            .ObjectSpaceNormalBakeQueue =
                                &Bake->Queue(),
                            .ObjectSpaceNormalBakeGraphicsBackendOperational =
                                Device->IsOperational(),
                        });

                Pipeline->SetDependencies(
                    AssetImportPipelineDependencies{
                        .Initialized = Initialized,
                        .Config = Config,
                        .Streaming = Streaming,
                        .Worlds = Worlds,
                        .World = BoundWorld,
                        .AssetService = Assets.get(),
                        .GpuAssetCache = Cache.get(),
                        .ModelTextureHandoff =
                            TextureHandoff.get(),
                        .ModelSceneHandoff =
                            SceneHandoff.get(),
                        .RenderExtraction = Extraction,
                        .Scene = BoundRegistry,
                        .Selection = Selection,
                        .CommandHistory = History,
                        .ObjectSpaceNormalBakeQueue =
                            &Bake->Queue(),
                        .Device = Device,
                    });
            }

            void ResetAndBindTo(
                const WorldHandle world,
                ECS::Scene::Registry* const registry)
            {
                DestroySceneBinding(true);
                BindTo(world, registry);
            }

            [[nodiscard]] bool ValidateBinding()
            {
                if (!AcceptingCallbacks || Worlds == nullptr)
                    return false;

                const WorldHandle active =
                    Worlds->ActiveWorld();
                ECS::Scene::Registry* const registry =
                    Worlds->Get(active);
                if (active != BoundWorld ||
                    registry != BoundRegistry)
                {
                    ResetAndBindTo(active, registry);
                }
                return IsBindingCurrent(BindingEpoch);
            }

            void BeforeDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (!AcceptingCallbacks)
                    return;
                if (BoundWorld != context.World ||
                    BoundRegistry != &context.Registry)
                {
                    (void)ValidateBinding();
                }
                if (BoundWorld == context.World &&
                    BoundRegistry == &context.Registry)
                {
                    DestroySceneBinding(true);
                }
            }

            void AfterDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (!AcceptingCallbacks)
                    return;
                BindTo(context.World, &context.Registry);
            }

            void ReleaseDocumentParticipant() noexcept
            {
                if (Documents != nullptr &&
                    DocumentParticipant.IsValid())
                {
                    (void)Documents->
                        UnregisterReplacementParticipant(
                            DocumentParticipant);
                }
                DocumentParticipant = {};
            }

            void AnnounceShutdown()
            {
                if (ShutdownAnnounced)
                    return;
                ShutdownAnnounced = true;

                // Cancellation needs the still-live optional streaming owner.
                if (Pipeline != nullptr)
                    Pipeline->CancelActiveAssetImportsForShutdown();
                DetachPipeline();

                AcceptingCallbacks = false;
                // Do not clear cache or bake state here. The global GPU queue
                // bridge must observe and quiesce every pending bake first.
                SceneHandoff.reset();
                BoundWorld = {};
                BoundRegistry = nullptr;
                AdvanceBindingEpoch();
                ReleaseDocumentParticipant();

                Documents = nullptr;
                History = nullptr;
                Streaming = nullptr;
                Selection = nullptr;
                Config = nullptr;
                Initialized = nullptr;
                Worlds = nullptr;
                Extraction = nullptr;
                Renderer = nullptr;
                Device = nullptr;
                Jobs = nullptr;
            }
        };

        AssetImportPipeline Pipeline{};
        ObjectSpaceNormalBakeService Bake{};
        std::shared_ptr<State> Shared{};
        KernelEventSubscription ActiveWorldChangedSubscription{};
        KernelEventSubscription WorldDestroyedSubscription{};
        KernelEventSubscription ShutdownSubscription{};
        bool AssetServicePublished{false};
        bool PipelinePublished{false};
        bool CachePublished{false};
        bool AssetHooksPublished{false};

        void Unsubscribe(
            KernelEventBus* const events) noexcept
        {
            if (events != nullptr)
            {
                if (ActiveWorldChangedSubscription.IsValid())
                {
                    events->Unsubscribe(
                        ActiveWorldChangedSubscription);
                }
                if (WorldDestroyedSubscription.IsValid())
                {
                    events->Unsubscribe(
                        WorldDestroyedSubscription);
                }
                if (ShutdownSubscription.IsValid())
                    events->Unsubscribe(ShutdownSubscription);
            }
            ActiveWorldChangedSubscription = {};
            WorldDestroyedSubscription = {};
            ShutdownSubscription = {};
        }

        void WithdrawServices(
            AssetWorkflowModule& module,
            ServiceRegistry* const services) noexcept
        {
            if (services == nullptr)
                return;

            if (AssetHooksPublished)
            {
                (void)services->Withdraw<Core::IAssetFrameHooks>(
                    module);
            }
            AssetHooksPublished = false;

            if (Shared != nullptr &&
                CachePublished &&
                Shared->Cache != nullptr)
            {
                (void)services->Withdraw<
                    Graphics::GpuAssetCache>(
                        *Shared->Cache);
            }
            CachePublished = false;

            if (PipelinePublished)
            {
                (void)services->Withdraw<AssetImportPipeline>(
                    Pipeline);
            }
            PipelinePublished = false;

            if (Shared != nullptr &&
                AssetServicePublished &&
                Shared->Assets != nullptr)
            {
                (void)services->Withdraw<Assets::AssetService>(
                    *Shared->Assets);
            }
            AssetServicePublished = false;
        }

        void DestroyPerBootAssets() noexcept
        {
            if (Shared == nullptr)
                return;

            auto& state = *Shared;
            state.SceneHandoff.reset();
            state.TextureHandoff.reset();
            if (state.Assets != nullptr &&
                state.CacheListener !=
                    Assets::AssetEventBus::InvalidToken)
            {
                state.Assets->UnsubscribeAll(
                    state.CacheListener);
            }
            state.CacheListener =
                Assets::AssetEventBus::InvalidToken;
            state.Cache.reset();
            state.Assets.reset();
        }

        void RollBack(
            AssetWorkflowModule& module,
            KernelEventBus* const events,
            JobService* const jobs,
            ServiceRegistry* const services,
            const bool waitForGpuIdle) noexcept
        {
            if (Shared != nullptr)
            {
                auto& state = *Shared;
                state.AcceptingCallbacks = false;
                state.DetachPipeline();
                state.ReleaseDocumentParticipant();

                JobService* const participantJobs =
                    jobs != nullptr ? jobs : state.Jobs;
                if (participantJobs != nullptr &&
                    state.BakeParticipant.IsValid())
                {
                    RHI::IDevice* const device =
                        state.Device;
                    participantJobs->
                        UnregisterGpuQueueParticipant(
                            state.BakeParticipant,
                            waitForGpuIdle &&
                                    device != nullptr
                                ? std::function<void()>{
                                      [device]
                                      {
                                          device->WaitIdle();
                                      }}
                                : std::function<void()>{});
                }
                state.BakeParticipant = {};
            }

            Unsubscribe(events);
            WithdrawServices(module, services);
            Bake.ClearDependencies();
            Bake.Queue().Clear();
            DestroyPerBootAssets();
            Shared.reset();
        }
    };

    AssetWorkflowModule::AssetWorkflowModule()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    AssetWorkflowModule::~AssetWorkflowModule() = default;

    std::string_view AssetWorkflowModule::Name() const noexcept
    {
        return "Runtime.AssetWorkflowModule";
    }

    Core::Result AssetWorkflowModule::OnRegister(
        EngineSetup& setup)
    {
        if (!m_Impl ||
            m_Impl->Shared ||
            m_Impl->AssetServicePublished ||
            m_Impl->PipelinePublished ||
            m_Impl->CachePublished ||
            m_Impl->AssetHooksPublished ||
            m_Impl->ActiveWorldChangedSubscription.IsValid() ||
            m_Impl->WorldDestroyedSubscription.IsValid() ||
            m_Impl->ShutdownSubscription.IsValid() ||
            setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<Assets::AssetService>() !=
                nullptr ||
            setup.Services().Find<AssetImportPipeline>() !=
                nullptr ||
            setup.Services().Find<Graphics::GpuAssetCache>() !=
                nullptr ||
            setup.Services().Find<Core::IAssetFrameHooks>() !=
                nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        const RuntimeRenderRecipeActivationKernel& activation =
            setup.RenderRecipeActivation();
        if (setup.InitializedState() == nullptr ||
            activation.ActiveConfig == nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto device =
            setup.Services().Require<RHI::IDevice>(Name());
        if (!device.has_value())
            return Core::Err(device.error());
        auto renderer =
            setup.Services().Require<Graphics::IRenderer>(
                Name());
        if (!renderer.has_value())
            return Core::Err(renderer.error());
        auto extraction =
            setup.Services().Require<RenderExtractionCache>(
                Name());
        if (!extraction.has_value())
            return Core::Err(extraction.error());

        m_Impl->Shared = std::make_shared<Impl::State>();
        auto& state = *m_Impl->Shared;
        state.Self = m_Impl->Shared;
        state.Pipeline = &m_Impl->Pipeline;
        state.Bake = &m_Impl->Bake;
        state.Jobs = &setup.Jobs();
        state.Worlds = &setup.Worlds();
        state.Initialized = setup.InitializedState();
        state.Config = activation.ActiveConfig;
        state.Device = &device->get();
        state.Renderer = &renderer->get();
        state.Extraction = &extraction->get();
        state.Assets =
            std::make_unique<Assets::AssetService>();
        state.Cache =
            std::make_unique<Graphics::GpuAssetCache>(
                state.Renderer->GetBufferManager(),
                state.Renderer->GetTextureManager(),
                state.Renderer->GetSamplerManager(),
                state.Device->GetTransferQueue());

        if (Core::Result fallback =
                InitializeRuntimeGpuAssetFallbackTexture(
                    *state.Cache, *state.Device);
            !fallback.has_value())
        {
            Core::Log::Warn(
                "[Runtime] AssetWorkflow fallback texture bootstrap failed: error={}; material code will use factor-only fallback.",
                Core::Error::ToString(fallback.error()));
        }

        state.CacheListener =
            state.Assets->SubscribeAll(
                [cache = state.Cache.get()](
                    const Assets::AssetId id,
                    const Assets::AssetEvent event)
                {
                    switch (event)
                    {
                    case Assets::AssetEvent::Failed:
                        cache->NotifyFailed(id);
                        break;
                    case Assets::AssetEvent::Reloaded:
                        cache->NotifyReloaded(id);
                        break;
                    case Assets::AssetEvent::Destroyed:
                        cache->NotifyDestroyed(id);
                        break;
                    case Assets::AssetEvent::Ready:
                        break;
                    }
                });
        state.TextureHandoff =
            std::make_unique<AssetModelTextureHandoff>(
                *state.Assets, *state.Cache);
        m_Impl->Bake.SetDependencies(
            ObjectSpaceNormalBakeServiceDependencies{
                .GpuAssets = state.Cache.get(),
                .RenderExtraction = state.Extraction,
                .Device = state.Device,
            });

        if (Core::Result provided =
                setup.Services().Provide<Assets::AssetService>(
                    *state.Assets, Name());
            !provided.has_value())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return provided;
        }
        m_Impl->AssetServicePublished = true;

        if (Core::Result provided =
                setup.Services().Provide<AssetImportPipeline>(
                    m_Impl->Pipeline, Name());
            !provided.has_value())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return provided;
        }
        m_Impl->PipelinePublished = true;

        if (Core::Result provided =
                setup.Services().Provide<Graphics::GpuAssetCache>(
                    *state.Cache, Name());
            !provided.has_value())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return provided;
        }
        m_Impl->CachePublished = true;

        if (Core::Result provided =
                setup.Services().Provide<Core::IAssetFrameHooks>(
                    *this, Name());
            !provided.has_value())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return provided;
        }
        m_Impl->AssetHooksPublished = true;

        const std::weak_ptr<Impl::State> weakState =
            m_Impl->Shared;
        m_Impl->ActiveWorldChangedSubscription =
            setup.Subscribe<ActiveWorldChanged>(
                [weakState](const ActiveWorldChanged&)
                {
                    if (const auto state = weakState.lock())
                        (void)state->ValidateBinding();
                });
        m_Impl->WorldDestroyedSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [weakState](
                    const WorldWillBeDestroyed& event)
                {
                    if (const auto state = weakState.lock();
                        state &&
                        event.World == state->BoundWorld)
                    {
                        state->DestroySceneBinding(true);
                    }
                });
        m_Impl->ShutdownSubscription =
            setup.Subscribe<RuntimeShutdownAnnounced>(
                [weakState](const RuntimeShutdownAnnounced&)
                {
                    if (const auto state = weakState.lock())
                        state->AnnounceShutdown();
                });

        if (!m_Impl->ActiveWorldChangedSubscription.IsValid() ||
            !m_Impl->WorldDestroyedSubscription.IsValid() ||
            !m_Impl->ShutdownSubscription.IsValid())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        return Core::Ok();
    }

    Core::Result AssetWorkflowModule::OnResolve(
        EngineSetup& setup)
    {
        if (!m_Impl)
            return Core::Err(Core::ErrorCode::InvalidState);

        if (!m_Impl->Shared ||
            !m_Impl->AssetServicePublished ||
            !m_Impl->PipelinePublished ||
            !m_Impl->CachePublished ||
            !m_Impl->AssetHooksPublished ||
            setup.Services().Find<Assets::AssetService>() !=
                m_Impl->Shared->Assets.get() ||
            setup.Services().Find<AssetImportPipeline>() !=
                &m_Impl->Pipeline ||
            setup.Services().Find<Graphics::GpuAssetCache>() !=
                m_Impl->Shared->Cache.get() ||
            setup.Services().Find<Core::IAssetFrameHooks>() !=
                this)
        {
            if (m_Impl->Shared)
            {
                m_Impl->RollBack(
                    *this,
                    &setup.Events(),
                    &setup.Jobs(),
                    &setup.Services(),
                    true);
            }
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto documents =
            setup.Services().Require<SceneDocumentModule>(
                Name());
        if (!documents.has_value())
        {
            const Core::ErrorCode error = documents.error();
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(error);
        }
        auto history =
            setup.Services().Require<EditorCommandHistory>(
                Name());
        if (!history.has_value())
        {
            const Core::ErrorCode error = history.error();
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(error);
        }

        auto& state = *m_Impl->Shared;
        state.Documents = &documents->get();
        state.History = &history->get();
        state.Streaming =
            setup.Services().Find<StreamingExecutor>();
        state.Selection =
            setup.Services().Find<SelectionController>();
        state.AcceptingCallbacks = true;

        const std::weak_ptr<Impl::State> weakState =
            m_Impl->Shared;
        auto participant =
            state.Documents->RegisterReplacementParticipant(
                SceneReplacementParticipantDesc{
                    .Name = "Runtime.AssetWorkflowModule",
                    .BeforeReplace =
                        [weakState](
                            const SceneReplacementContext&
                                context)
                        {
                            if (const auto locked =
                                    weakState.lock())
                            {
                                locked->BeforeDocumentReplace(
                                    context);
                            }
                        },
                    .AfterReplace =
                        [weakState](
                            const SceneReplacementContext&
                                context)
                        {
                            if (const auto locked =
                                    weakState.lock())
                            {
                                locked->AfterDocumentReplace(
                                    context);
                            }
                        },
                });
        if (!participant.has_value())
        {
            const Core::ErrorCode error =
                participant.error();
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(error);
        }
        state.DocumentParticipant = *participant;

        state.BindTo(
            state.Worlds->ActiveWorld(),
            state.Worlds->Get(
                state.Worlds->ActiveWorld()));
        if (!state.ValidateBinding())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        state.BakeParticipant =
            m_Impl->Bake.RegisterGpuQueueParticipant(
                setup.Jobs());
        if (!state.BakeParticipant.IsValid())
        {
            m_Impl->RollBack(
                *this,
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        return Core::Ok();
    }

    void AssetWorkflowModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        if (!m_Impl)
            return;

        if (m_Impl->Shared)
            m_Impl->Shared->AnnounceShutdown();

        // Production has already run the global GPU-participant bridge. The
        // exact unregister remains necessary for direct lifecycle rollback and
        // is a no-op after the bridge removed this participant.
        m_Impl->RollBack(
            *this,
            &context.Events,
            &context.Jobs,
            &context.Services,
            false);
    }

    void AssetWorkflowModule::TickAssets()
    {
        if (!m_Impl || !m_Impl->Shared)
            return;

        auto& state = *m_Impl->Shared;
        (void)state.ValidateBinding();
        if (state.Assets != nullptr)
            state.Assets->Tick();
        if (state.Cache != nullptr &&
            state.Device != nullptr)
        {
            state.Cache->Tick(
                state.Device->GetGlobalFrameNumber(),
                state.Device->GetFramesInFlight());
        }
        if (state.SceneHandoff != nullptr &&
            state.IsBindingCurrent(state.BindingEpoch))
        {
            static_cast<void>(
                state.SceneHandoff->
                    ResolvePendingMaterialTextureBindings());
        }
    }
}
