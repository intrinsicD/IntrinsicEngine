module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.AssetWorkflowModule;

import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.AssetModelSceneHandoff;
import Extrinsic.Runtime.AssetModelTextureHandoff;
import Extrinsic.Runtime.DeviceBootstrap;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    struct AssetWorkflowModule::Impl
    {
        struct State
        {
            AssetImportPipeline* Pipeline{nullptr};
            TextureBakeService* TextureBake{nullptr};

            JobService* Jobs{nullptr};
            WorldRegistry* Worlds{nullptr};
            const bool* Initialized{nullptr};
            const Core::Config::EngineConfig* Config{nullptr};
            RHI::IDevice* Device{nullptr};
            Graphics::IRenderer* Renderer{nullptr};
            RenderExtractionCache* Extraction{nullptr};

            SceneDocumentModule* Documents{nullptr};
            EditorCommandHistory* History{nullptr};
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

            [[nodiscard]] static const PropertyTextureBakeRecord*
            FindTextureBakeOutput(
                const PropertyTextureBakeOutputs* const outputs,
                const std::string_view outputName) noexcept
            {
                if (outputs == nullptr)
                    return nullptr;
                const auto found =
                    std::ranges::find(outputs->Records,
                                      outputName,
                                      &PropertyTextureBakeRecord::OutputName);
                return found == outputs->Records.end() ? nullptr : &*found;
            }

            [[nodiscard]] static bool IsScalarProperty(
                const Geometry::PropertyValueKind kind) noexcept
            {
                return kind == Geometry::PropertyValueKind::Float ||
                       kind == Geometry::PropertyValueKind::Double;
            }

            static void ClearGeneratedMaterialTarget(
                Graphics::MaterialTextureAssetBindings& bindings,
                const GeometryPresentationSlotSemantic semantic,
                const Assets::AssetId texture) noexcept
            {
                if (!texture.IsValid())
                    return;
                switch (semantic)
                {
                case GeometryPresentationSlotSemantic::Albedo:
                case GeometryPresentationSlotSemantic::ScalarField:
                    if (bindings.Albedo == texture)
                    {
                        bindings.Albedo = {};
                        bindings.AlbedoInterpretation = Graphics::
                            MaterialAlbedoTextureInterpretation::Color;
                        bindings.AlbedoScalarColormap =
                            Graphics::Colormap::Type::Viridis;
                        bindings.AlbedoScalarRangeMin = 0.0f;
                        bindings.AlbedoScalarRangeMax = 1.0f;
                    }
                    break;
                case GeometryPresentationSlotSemantic::Normal:
                    if (bindings.Normal == texture)
                    {
                        bindings.Normal = {};
                        bindings.NormalSpace = Graphics::
                            MaterialNormalTextureSpace::TangentSpaceNormal;
                    }
                    break;
                case GeometryPresentationSlotSemantic::Roughness:
                case GeometryPresentationSlotSemantic::Metallic:
                    if (bindings.MetallicRoughness == texture)
                    {
                        bindings.MetallicRoughness = {};
                        bindings.RoughnessFromRed = false;
                        bindings.MetallicFromRed = false;
                    }
                    break;
                case GeometryPresentationSlotSemantic::Displacement:
                case GeometryPresentationSlotSemantic::PointColor:
                case GeometryPresentationSlotSemantic::PointScalarField:
                case GeometryPresentationSlotSemantic::PointSize:
                case GeometryPresentationSlotSemantic::PointNormalOrientation:
                case GeometryPresentationSlotSemantic::LineColor:
                case GeometryPresentationSlotSemantic::LineScalarField:
                case GeometryPresentationSlotSemantic::LineWidth:
                    break;
                }
            }

            static void ApplyGeneratedMaterialTarget(
                Graphics::MaterialTextureAssetBindings& bindings,
                const GeometryPresentationSlotRecipe& slot,
                const PropertyTextureBakeRecord& output) noexcept
            {
                switch (slot.Semantic)
                {
                case GeometryPresentationSlotSemantic::Albedo:
                case GeometryPresentationSlotSemantic::ScalarField:
                    bindings.Albedo = output.Texture;
                    bindings.AlbedoInterpretation =
                        IsScalarProperty(output.Source.ValueKind) &&
                                output.Storage ==
                                    PropertyTextureBakeStorage::RawFloat
                            ? Graphics::MaterialAlbedoTextureInterpretation::
                                  Scalar
                            : Graphics::MaterialAlbedoTextureInterpretation::
                                  Color;
                    bindings.AlbedoScalarColormap = slot.TextureColormap;
                    bindings.AlbedoScalarRangeMin = output.RangeMin;
                    bindings.AlbedoScalarRangeMax = output.RangeMax;
                    break;
                case GeometryPresentationSlotSemantic::Normal:
                    bindings.Normal = output.Texture;
                    bindings.NormalSpace =
                        slot.NormalSpace ==
                                GeometryPresentationNormalSpace::World
                            ? Graphics::MaterialNormalTextureSpace::
                                  WorldSpaceNormal
                            : Graphics::MaterialNormalTextureSpace::
                                  ObjectSpaceNormal;
                    break;
                case GeometryPresentationSlotSemantic::Roughness:
                    if (bindings.MetallicRoughness != output.Texture)
                    {
                        bindings.MetallicRoughness = output.Texture;
                        bindings.RoughnessFromRed = false;
                        bindings.MetallicFromRed = false;
                    }
                    bindings.RoughnessFromRed = true;
                    break;
                case GeometryPresentationSlotSemantic::Metallic:
                    if (bindings.MetallicRoughness != output.Texture)
                    {
                        bindings.MetallicRoughness = output.Texture;
                        bindings.RoughnessFromRed = false;
                        bindings.MetallicFromRed = false;
                    }
                    bindings.MetallicFromRed = true;
                    break;
                case GeometryPresentationSlotSemantic::Displacement:
                case GeometryPresentationSlotSemantic::PointColor:
                case GeometryPresentationSlotSemantic::PointScalarField:
                case GeometryPresentationSlotSemantic::PointSize:
                case GeometryPresentationSlotSemantic::PointNormalOrientation:
                case GeometryPresentationSlotSemantic::LineColor:
                case GeometryPresentationSlotSemantic::LineScalarField:
                case GeometryPresentationSlotSemantic::LineWidth:
                    break;
                }
            }

            void ReconcileTextureBakeOutputs()
            {
                if (!IsBindingCurrent(BindingEpoch) || Extraction == nullptr)
                {
                    return;
                }

                auto& raw = BoundRegistry->Raw();
                auto view = raw.view<GeometryPresentationRecipe>();
                for (const ECS::EntityHandle entity : view)
                {
                    GeometryPresentationRecipe& recipe =
                        view.get<GeometryPresentationRecipe>(entity);
                    const auto* outputs =
                        raw.try_get<PropertyTextureBakeOutputs>(entity);
                    auto& runtimeState =
                        raw.get_or_emplace<GeometryPresentationRuntimeState>(
                            entity);
                    const std::uint32_t stableId =
                        SelectionController::ToStableEntityId(entity);
                    Graphics::MaterialTextureAssetBindings bindings =
                        Extraction->GetMaterialTextureAssetBindings(stableId)
                            .value_or(Graphics::MaterialTextureAssetBindings{});
                    bool materialChanged = false;

                    for (GeometryPresentationSlotStatus& status :
                         runtimeState.Slots)
                    {
                        if (status.GeneratedOutputName.empty())
                            continue;
                        const auto* presentation =
                            FindGeometryPresentationBinding(
                                recipe, status.PresentationKey);
                        const auto* slot =
                            presentation != nullptr
                                ? FindGeometryPresentationSlot(*presentation,
                                                               status.Semantic)
                                : nullptr;
                        if (slot != nullptr &&
                            slot->SourceKind ==
                                GeometryPresentationSourceKind::PropertyBake &&
                            slot->GeneratedOutputName ==
                                status.GeneratedOutputName)
                        {
                            continue;
                        }
                        ClearGeneratedMaterialTarget(
                            bindings, status.Semantic, status.GeneratedTexture);
                        materialChanged = status.GeneratedTexture.IsValid() ||
                                          materialChanged;
                        status.GeneratedOutputName.clear();
                        status.GeneratedTexture = {};
                    }

                    for (const GeometryPresentationBindingRecipe& presentation :
                         recipe.Presentations)
                    {
                        for (const GeometryPresentationSlotRecipe& slot :
                             presentation.Slots)
                        {
                            if (slot.SourceKind !=
                                    GeometryPresentationSourceKind::
                                        PropertyBake ||
                                slot.GeneratedOutputName.empty())
                            {
                                continue;
                            }

                            GeometryPresentationSlotStatus* status =
                                FindGeometryPresentationSlotStatus(
                                    runtimeState,
                                    presentation.Key,
                                    slot.Semantic);
                            if (status == nullptr)
                            {
                                runtimeState.Slots.push_back(
                                    GeometryPresentationSlotStatus{
                                        .PresentationKey = presentation.Key,
                                        .Semantic = slot.Semantic,
                                    });
                                status = &runtimeState.Slots.back();
                            }
                            if (status->GeneratedOutputName !=
                                slot.GeneratedOutputName)
                            {
                                ClearGeneratedMaterialTarget(
                                    bindings,
                                    slot.Semantic,
                                    status->GeneratedTexture);
                                materialChanged =
                                    status->GeneratedTexture.IsValid() ||
                                    materialChanged;
                                status->GeneratedTexture = {};
                                status->GeneratedOutputName =
                                    slot.GeneratedOutputName;
                            }

                            const PropertyTextureBakeRecord* const output =
                                FindTextureBakeOutput(outputs,
                                                      slot.GeneratedOutputName);
                            if (output == nullptr)
                            {
                                ClearGeneratedMaterialTarget(
                                    bindings,
                                    slot.Semantic,
                                    status->GeneratedTexture);
                                materialChanged =
                                    status->GeneratedTexture.IsValid() ||
                                    materialChanged;
                                status->GeneratedTexture = {};
                                status->Readiness =
                                    GeometryPresentationReadiness::Pending;
                                status->Provenance =
                                    GeometryPresentationProvenance::
                                        PropertyBinding;
                                status->Diagnostic =
                                    "generated texture output is not scheduled";
                                continue;
                            }

                            status->SourceGeneration = output->SourceGeneration;
                            status->OutputGeneration = output->Generation;
                            status->Diagnostic = output->Diagnostic;
                            switch (output->State)
                            {
                            case PropertyTextureBakeOutputState::Pending:
                                status->Readiness =
                                    GeometryPresentationReadiness::Pending;
                                status->Provenance =
                                    GeometryPresentationProvenance::
                                        PropertyBinding;
                                break;
                            case PropertyTextureBakeOutputState::Ready:
                                if (Assets == nullptr ||
                                    !output->Texture.IsValid() ||
                                    !Assets->IsAlive(output->Texture))
                                {
                                    ClearGeneratedMaterialTarget(
                                        bindings,
                                        slot.Semantic,
                                        status->GeneratedTexture);
                                    materialChanged =
                                        status->GeneratedTexture.IsValid() ||
                                        materialChanged;
                                    status->GeneratedTexture = {};
                                    status->Readiness =
                                        GeometryPresentationReadiness::Failed;
                                    status->Provenance =
                                        GeometryPresentationProvenance::
                                            PropertyBinding;
                                    status->Diagnostic =
                                        "generated texture asset is no longer "
                                        "alive";
                                    break;
                                }
                                status->GeneratedTexture = output->Texture;
                                status->Readiness =
                                    GeometryPresentationReadiness::Ready;
                                status->Provenance =
                                    GeometryPresentationProvenance::
                                        GeneratedTextureAsset;
                                ApplyGeneratedMaterialTarget(
                                    bindings, slot, *output);
                                materialChanged = true;
                                break;
                            case PropertyTextureBakeOutputState::Failed:
                                ClearGeneratedMaterialTarget(
                                    bindings,
                                    slot.Semantic,
                                    status->GeneratedTexture);
                                materialChanged =
                                    status->GeneratedTexture.IsValid() ||
                                    materialChanged;
                                status->GeneratedTexture = {};
                                status->Readiness =
                                    GeometryPresentationReadiness::Failed;
                                status->Provenance =
                                    GeometryPresentationProvenance::
                                        PropertyBinding;
                                break;
                            }
                        }
                    }

                    if (materialChanged)
                    {
                        Extraction->SetMaterialTextureAssetBindings(stableId,
                                                                    bindings);
                    }
                }
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
                    Pipeline == nullptr)
                {
                    DetachPipeline();
                    return;
                }

                const std::uint64_t expectedEpoch =
                    BindingEpoch;
                const std::weak_ptr<State> weakState = Self;
                const std::function<bool()> bindingValid =
                    [weakState, expectedEpoch]
                    {
                        if (const auto state = weakState.lock())
                        {
                            return state->IsBindingCurrent(expectedEpoch);
                        }
                        return false;
                    };
                SceneHandoff =
                    std::make_unique<AssetModelSceneHandoff>(
                        *Assets,
                        *Cache,
                        *BoundRegistry,
                        *Renderer,
                        AssetModelSceneHandoffOptions{
                            .World = BoundWorld,
                            .BindingEpoch = expectedEpoch,
                            .BindingValid = bindingValid,
                            .TextureBake = TextureBake,
                        });

                Pipeline->SetDependencies(
                    AssetImportPipelineDependencies{
                        .Initialized = Initialized,
                        .Config = Config,
                        .Jobs = Jobs,
                        .Worlds = Worlds,
                        .World = BoundWorld,
                        .BindingValid = bindingValid,
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
                        .TextureBake = TextureBake,
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
                // TextureBakeModule owns and quiesces its GPU queue before
                // this module destroys the shared asset/cache state.
                SceneHandoff.reset();
                BoundWorld = {};
                BoundRegistry = nullptr;
                AdvanceBindingEpoch();
                ReleaseDocumentParticipant();

                Documents = nullptr;
                History = nullptr;
                Selection = nullptr;
                Config = nullptr;
                Initialized = nullptr;
                Worlds = nullptr;
                Extraction = nullptr;
                Renderer = nullptr;
                Device = nullptr;
                Jobs = nullptr;
                TextureBake = nullptr;
            }
        };

        AssetImportPipeline Pipeline{};
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
            JobService* const,
            ServiceRegistry* const services,
            const bool) noexcept
        {
            if (Shared != nullptr)
            {
                auto& state = *Shared;
                state.AcceptingCallbacks = false;
                state.DetachPipeline();
                state.ReleaseDocumentParticipant();
            }

            Unsubscribe(events);
            WithdrawServices(module, services);
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
        state.Selection =
            setup.Services().Find<SelectionController>();
        state.TextureBake =
            setup.Services().Find<TextureBakeService>();
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
        state.ReconcileTextureBakeOutputs();
        if (state.SceneHandoff != nullptr &&
            state.IsBindingCurrent(state.BindingEpoch))
        {
            static_cast<void>(
                state.SceneHandoff->
                    ResolvePendingMaterialTextureBindings());
        }
    }
}
