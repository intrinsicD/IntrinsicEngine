module;

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.SceneDocumentModule;

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.Core.Logging;
import Extrinsic.ECS.Component.Collider;
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
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::string FileNameFromPath(
            const std::string_view path)
        {
            if (path.empty())
                return {};

            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin =
                slash == std::string_view::npos ? 0u : slash + 1u;
            if (begin >= path.size())
                return {};
            return std::string(path.substr(begin));
        }

        template <typename Component>
        void CopySerializableComponent(
            const entt::registry& sourceRaw,
            entt::registry& destinationRaw,
            const ECS::EntityHandle source,
            const ECS::EntityHandle destination)
        {
            if (const auto* component =
                    sourceRaw.try_get<Component>(source))
            {
                destinationRaw.emplace_or_replace<Component>(
                    destination, *component);
            }
        }

        template <typename Component>
        void CopySerializableTag(
            const entt::registry& sourceRaw,
            entt::registry& destinationRaw,
            const ECS::EntityHandle source,
            const ECS::EntityHandle destination)
        {
            if (sourceRaw.all_of<Component>(source))
            {
                destinationRaw.emplace_or_replace<Component>(
                    destination);
            }
        }

        void CopySerializableHierarchy(
            const entt::registry& sourceRaw,
            entt::registry& destinationRaw,
            const std::unordered_map<ECS::EntityHandle,
                                     ECS::EntityHandle>& remap,
            const ECS::EntityHandle source,
            const ECS::EntityHandle destination)
        {
            namespace ECSC = ECS::Components;

            const auto* hierarchy =
                sourceRaw.try_get<ECSC::Hierarchy::Component>(source);
            if (hierarchy == nullptr ||
                hierarchy->Parent == ECS::InvalidEntityHandle)
            {
                return;
            }

            const auto parent = remap.find(hierarchy->Parent);
            if (parent == remap.end())
                return;

            destinationRaw.emplace_or_replace<
                ECSC::Hierarchy::Component>(
                destination,
                ECSC::Hierarchy::Component{
                    .Parent = parent->second});
        }

        void SnapshotSerializableScene(
            const ECS::Scene::Registry& source,
            ECS::Scene::Registry& destination)
        {
            namespace ECSC = ECS::Components;
            namespace GS = ECS::Components::GeometrySources;
            namespace G = Graphics::Components;
            namespace Sel = ECS::Components::Selection;

            const entt::registry& sourceRaw = source.Raw();
            entt::registry& destinationRaw = destination.Raw();

            std::vector<ECS::EntityHandle> entities;
            entities.reserve(
                sourceRaw.storage<entt::entity>()->size());
            for (const entt::entity entity :
                 sourceRaw.view<entt::entity>())
            {
                entities.push_back(entity);
            }
            std::sort(
                entities.begin(),
                entities.end(),
                [](const ECS::EntityHandle lhs,
                   const ECS::EntityHandle rhs)
                {
                    return static_cast<std::uint32_t>(lhs) <
                           static_cast<std::uint32_t>(rhs);
                });

            std::unordered_map<ECS::EntityHandle,
                               ECS::EntityHandle> remap;
            remap.reserve(entities.size());
            for (const ECS::EntityHandle sourceEntity : entities)
                remap.emplace(sourceEntity, destination.Create());

            for (const ECS::EntityHandle sourceEntity : entities)
            {
                const ECS::EntityHandle destinationEntity =
                    remap.at(sourceEntity);

                CopySerializableComponent<ECSC::MetaData>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<ECSC::StableId>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableTag<Sel::SelectableTag>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::Transform::Component>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableHierarchy(
                    sourceRaw, destinationRaw, remap,
                    sourceEntity, destinationEntity);

                CopySerializableComponent<G::RenderSurface>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<G::RenderEdges>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<G::RenderPoints>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<G::VisualizationConfig>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    G::VisualizationLaneOverrides>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);

                CopySerializableComponent<GS::Vertices>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<GS::Edges>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<GS::Halfedges>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<GS::Faces>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<GS::Nodes>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableTag<GS::HasMeshTopology>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableTag<GS::HasGraphTopology>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);

                CopySerializableComponent<
                    ProgressivePresentationBindings>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);

                CopySerializableTag<ECSC::Lights::LightTag>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::Lights::DirectionalLight>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::Lights::PointLight>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::Lights::SpotLight>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::Lights::AmbientLight>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableTag<ECSC::Shadows::CasterTag>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::Collider::Component>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::RigidBody::Component>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::SpatialDebugBinding>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
                CopySerializableComponent<
                    ECSC::AssetInstance::Source>(
                    sourceRaw, destinationRaw,
                    sourceEntity, destinationEntity);
            }
        }

        struct QueuedSceneLoadState
        {
            std::string Path{};
            StreamingTaskHandle Task{};
            ECS::Scene::Registry LoadedScene{};
            std::optional<SceneDeserializationResult> Result{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        struct QueuedSceneSaveState
        {
            std::string Path{};
            StreamingTaskHandle Task{};
            ECS::Scene::Registry Snapshot{};
            std::optional<SceneSerializationResult> Result{};
            Core::ErrorCode Error{Core::ErrorCode::Unknown};
        };

        struct ParticipantInvocation
        {
            std::string Name{};
            std::uint64_t Sequence{0u};
            std::function<void(const SceneReplacementContext&)>
                BeforeReplace{};
            std::function<void(const SceneReplacementContext&)>
                AfterReplace{};
        };
    }

    struct SceneDocumentModule::Impl
    {
        struct ParticipantSlot
        {
            std::uint32_t Generation{1u};
            bool Occupied{false};
            std::uint64_t Sequence{0u};
            SceneReplacementParticipantDesc Desc{};
        };

        struct State
        {
            WorldRegistry* Worlds{nullptr};
            StreamingExecutor* Streaming{nullptr};
            EditorCommandHistory History{};
            WorldHandle BoundWorld{};
            ECS::Scene::Registry* BoundRegistry{nullptr};
            std::uint64_t BindingEpoch{1u};
            std::uint64_t ModuleGeneration{1u};
            std::string CurrentDocumentPath{};
            std::optional<RuntimeSceneFileEvent> LastEvent{};
            std::uint64_t EventSequence{0u};
            std::vector<StreamingTaskHandle> OwnedTasks{};
            std::vector<ParticipantSlot> ParticipantSlots{};
            std::vector<std::uint32_t> FreeParticipantSlots{};
            std::uint64_t NextParticipantSequence{1u};
            bool AcceptingOperations{true};

            void AdvanceBindingEpoch() noexcept
            {
                ++BindingEpoch;
                if (BindingEpoch == 0u)
                    BindingEpoch = 1u;
            }

            void CancelOwnedTasks()
            {
                std::vector<StreamingTaskHandle> tasks =
                    std::exchange(
                        OwnedTasks,
                        std::vector<StreamingTaskHandle>{});
                if (Streaming == nullptr)
                    return;
                for (const StreamingTaskHandle task : tasks)
                {
                    if (task.IsValid())
                        Streaming->Cancel(task);
                }
            }

            void ResetDurableDocumentState()
            {
                CurrentDocumentPath.clear();
                LastEvent.reset();
                EventSequence = 0u;
                History.ResetDocument();
            }

            void RebindTo(
                const WorldHandle world,
                ECS::Scene::Registry* const registry)
            {
                AdvanceBindingEpoch();
                CancelOwnedTasks();
                BoundWorld = world;
                BoundRegistry = registry;
                ResetDurableDocumentState();
            }

            [[nodiscard]] bool ValidateBinding()
            {
                if (!AcceptingOperations || Worlds == nullptr)
                    return false;

                const WorldHandle active = Worlds->ActiveWorld();
                ECS::Scene::Registry* const registry =
                    Worlds->Get(active);
                if (active != BoundWorld ||
                    registry != BoundRegistry)
                {
                    RebindTo(active, registry);
                }
                return BoundWorld.IsValid() &&
                       BoundRegistry != nullptr;
            }

            [[nodiscard]] bool MatchesCapturedBinding(
                const std::uint64_t moduleGeneration,
                const std::uint64_t bindingEpoch,
                const WorldHandle world,
                const ECS::Scene::Registry* const registry)
            {
                if (!ValidateBinding())
                    return false;
                return ModuleGeneration == moduleGeneration &&
                       BindingEpoch == bindingEpoch &&
                       BoundWorld == world &&
                       BoundRegistry == registry &&
                       Worlds->ActiveWorld() == world &&
                       Worlds->Get(world) == registry;
            }

            void ForgetOwnedTask(
                const StreamingTaskHandle task)
            {
                std::erase(OwnedTasks, task);
            }

            void RecordSceneFileEvent(
                RuntimeSceneFileEvent event)
            {
                event.Sequence = ++EventSequence;
                const char* operationName = "None";
                switch (event.Operation)
                {
                case RuntimeSceneFileOperation::Load:
                    operationName = "Load";
                    break;
                case RuntimeSceneFileOperation::Save:
                    operationName = "Save";
                    break;
                case RuntimeSceneFileOperation::None:
                    break;
                }

                if (event.Succeeded())
                {
                    Core::Log::Info(
                        "[Runtime] Scene file operation succeeded: operation={} path='{}'",
                        operationName,
                        event.Path);
                }
                else
                {
                    Core::Log::Warn(
                        "[Runtime] Scene file operation failed: operation={} path='{}' error={}",
                        operationName,
                        event.Path,
                        Core::Error::ToString(event.Error));
                }
                LastEvent = std::move(event);
            }

            [[nodiscard]] std::vector<ParticipantInvocation>
            SnapshotParticipants() const
            {
                std::vector<ParticipantInvocation> participants;
                participants.reserve(ParticipantSlots.size());
                for (const ParticipantSlot& slot : ParticipantSlots)
                {
                    if (!slot.Occupied)
                        continue;
                    participants.push_back(
                        ParticipantInvocation{
                            .Name = slot.Desc.Name,
                            .Sequence = slot.Sequence,
                            .BeforeReplace =
                                slot.Desc.BeforeReplace,
                            .AfterReplace =
                                slot.Desc.AfterReplace,
                        });
                }
                std::sort(
                    participants.begin(),
                    participants.end(),
                    [](const ParticipantInvocation& lhs,
                       const ParticipantInvocation& rhs)
                    {
                        if (lhs.Name != rhs.Name)
                            return lhs.Name < rhs.Name;
                        return lhs.Sequence < rhs.Sequence;
                    });
                return participants;
            }

            [[nodiscard]] Core::Result Replace(
                const SceneReplacementKind kind,
                ECS::Scene::Registry* loadedScene,
                std::string path)
            {
                if (!ValidateBinding())
                    return Core::Err(
                        Core::ErrorCode::InvalidState);

                const std::vector<ParticipantInvocation>
                    participants = SnapshotParticipants();

                // Advancing before the first callback invalidates all older
                // queued operations while the outgoing registry is still
                // live. The context names the replacement generation shared
                // by every participant in this transaction.
                AdvanceBindingEpoch();
                const SceneReplacementContext context{
                    .Kind = kind,
                    .World = BoundWorld,
                    .Registry = *BoundRegistry,
                    .BindingEpoch = BindingEpoch,
                };
                for (const ParticipantInvocation& participant :
                     participants)
                {
                    if (participant.BeforeReplace)
                        participant.BeforeReplace(context);
                }

                BoundRegistry->Clear();
                if (loadedScene != nullptr)
                {
                    BoundRegistry->Raw() =
                        std::move(loadedScene->Raw());
                }

                for (const ParticipantInvocation& participant :
                     participants)
                {
                    if (participant.AfterReplace)
                        participant.AfterReplace(context);
                }

                CurrentDocumentPath = std::move(path);
                History.ResetDocument(CurrentDocumentPath);
                return Core::Ok();
            }

            void AnnounceShutdown()
            {
                if (!AcceptingOperations)
                    return;
                AcceptingOperations = false;
                ++ModuleGeneration;
                if (ModuleGeneration == 0u)
                    ModuleGeneration = 1u;
                AdvanceBindingEpoch();
                CancelOwnedTasks();
                BoundWorld = {};
                BoundRegistry = nullptr;
                ResetDurableDocumentState();
            }
        };

        std::shared_ptr<State> Shared{};
        KernelEventBus* Events{nullptr};
        ServiceRegistry* Services{nullptr};
        KernelEventSubscription ActiveWorldChangedSubscription{};
        KernelEventSubscription WorldDestroyedSubscription{};
        KernelEventSubscription ShutdownSubscription{};
        bool ModulePublished{false};
        bool HistoryPublished{false};

        void ShutdownAndReset()
        {
            if (Shared)
                Shared->AnnounceShutdown();

            if (Events != nullptr)
            {
                if (ActiveWorldChangedSubscription.IsValid())
                    Events->Unsubscribe(
                        ActiveWorldChangedSubscription);
                if (WorldDestroyedSubscription.IsValid())
                    Events->Unsubscribe(
                        WorldDestroyedSubscription);
                if (ShutdownSubscription.IsValid())
                    Events->Unsubscribe(ShutdownSubscription);
            }
            ActiveWorldChangedSubscription = {};
            WorldDestroyedSubscription = {};
            ShutdownSubscription = {};

            if (Services != nullptr && Shared)
            {
                if (HistoryPublished)
                {
                    (void)Services->Withdraw<
                        EditorCommandHistory>(
                            Shared->History);
                }
            }
            HistoryPublished = false;
        }
    };

    SceneDocumentModule::SceneDocumentModule()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    SceneDocumentModule::~SceneDocumentModule()
    {
        // The normal path is OnShutdown, which cancels every owned task while
        // dependencies are live. Destruction still drops the sole strong
        // state reference so every late callback's weak lock fails without
        // touching module storage.
        if (m_Impl)
            m_Impl->Shared.reset();
    }

    std::string_view SceneDocumentModule::Name() const noexcept
    {
        return "Runtime.SceneDocumentModule";
    }

    Core::Result SceneDocumentModule::OnRegister(
        EngineSetup& setup)
    {
        if (!m_Impl ||
            m_Impl->Shared ||
            m_Impl->ModulePublished ||
            m_Impl->HistoryPublished ||
            setup.Services().Phase() !=
                ServiceRegistryPhase::Registration ||
            setup.Services().Find<SceneDocumentModule>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->Shared = std::make_shared<Impl::State>();
        m_Impl->Shared->Worlds = &setup.Worlds();
        m_Impl->Shared->BoundWorld =
            setup.Worlds().ActiveWorld();
        m_Impl->Shared->BoundRegistry =
            setup.Worlds().Get(
                m_Impl->Shared->BoundWorld);
        if (!m_Impl->Shared->BoundWorld.IsValid() ||
            m_Impl->Shared->BoundRegistry == nullptr)
        {
            m_Impl->Shared.reset();
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->Events = &setup.Events();
        m_Impl->Services = &setup.Services();

        if (Core::Result provided =
                setup.Services().Provide<SceneDocumentModule>(
                    *this, Name());
            !provided.has_value())
        {
            m_Impl->Shared.reset();
            m_Impl->Events = nullptr;
            m_Impl->Services = nullptr;
            return provided;
        }
        m_Impl->ModulePublished = true;

        if (Core::Result provided =
                setup.Services().Provide<EditorCommandHistory>(
                    m_Impl->Shared->History, Name());
            !provided.has_value())
        {
            (void)setup.Services().Withdraw<
                SceneDocumentModule>(*this);
            m_Impl->ModulePublished = false;
            m_Impl->Shared.reset();
            m_Impl->Events = nullptr;
            m_Impl->Services = nullptr;
            return provided;
        }
        m_Impl->HistoryPublished = true;

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
                [weakState](const WorldWillBeDestroyed& event)
                {
                    if (const auto state = weakState.lock();
                        state && event.World == state->BoundWorld)
                    {
                        state->RebindTo({}, nullptr);
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
            RuntimeModuleShutdownContext shutdownContext{
                .Commands = setup.Commands(),
                .Events = setup.Events(),
                .Jobs = setup.Jobs(),
                .Worlds = setup.Worlds(),
                .Services = setup.Services(),
            };
            OnShutdown(shutdownContext);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        return Core::Ok();
    }

    Core::Result SceneDocumentModule::OnResolve(
        EngineSetup& setup)
    {
        if (!m_Impl ||
            !m_Impl->Shared ||
            !m_Impl->ModulePublished ||
            !m_Impl->HistoryPublished ||
            setup.Services().Find<SceneDocumentModule>() != this ||
            setup.Services().Find<EditorCommandHistory>() !=
                &m_Impl->Shared->History)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        m_Impl->Shared->Streaming =
            setup.Services().Find<StreamingExecutor>();
        return Core::Ok();
    }

    void SceneDocumentModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        if (!m_Impl)
            return;

        m_Impl->Events = &context.Events;
        m_Impl->Services = &context.Services;
        m_Impl->ShutdownAndReset();
        if (m_Impl->ModulePublished)
        {
            (void)context.Services.Withdraw<
                SceneDocumentModule>(*this);
        }
        m_Impl->ModulePublished = false;
        m_Impl->Shared.reset();
        m_Impl->Events = nullptr;
        m_Impl->Services = nullptr;
    }

    Core::Expected<SceneSerializationResult>
    SceneDocumentModule::SaveSceneToPath(std::string path)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state || !state->ValidateBinding())
        {
            return Core::Err<SceneSerializationResult>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<SceneSerializationResult>(
                Core::ErrorCode::InvalidPath);
        }

        Core::IO::FileIOBackend backend;
        auto saved = SaveSceneDocument(
            *state->BoundRegistry, path, backend);
        if (saved.has_value())
        {
            state->CurrentDocumentPath = path;
            state->History.MarkSaved(std::move(path));
        }
        return saved;
    }

    Core::Expected<RuntimeQueuedSceneFileOperation>
    SceneDocumentModule::QueueSceneSaveToPath(std::string path)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state || !state->ValidateBinding() ||
            state->Streaming == nullptr)
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidPath);
        }

        const WorldHandle world = state->BoundWorld;
        ECS::Scene::Registry* const registry =
            state->BoundRegistry;
        const std::uint64_t bindingEpoch =
            state->BindingEpoch;
        const std::uint64_t moduleGeneration =
            state->ModuleGeneration;
        auto operation =
            std::make_shared<QueuedSceneSaveState>();
        operation->Path = std::move(path);
        SnapshotSerializableScene(
            *registry, operation->Snapshot);

        const std::weak_ptr<Impl::State> weakState = state;
        const StreamingTaskHandle handle =
            state->Streaming->Submit(
                StreamingTaskDesc{
                    .Name = "Runtime.SceneSave." +
                        FileNameFromPath(operation->Path),
                    .Kind = RuntimeTaskKinds::AssetDecode,
                    .Priority =
                        Core::Dag::TaskPriority::Normal,
                    .EstimatedCost = 3u,
                    .Scope = world,
                    .Execute =
                        [operation]() mutable -> StreamingResult
                        {
                            Core::IO::FileIOBackend backend;
                            auto saved = SaveSceneDocument(
                                operation->Snapshot,
                                operation->Path,
                                backend);
                            if (!saved.has_value())
                            {
                                operation->Error =
                                    saved.error();
                            }
                            else
                            {
                                operation->Result = *saved;
                                operation->Error =
                                    Core::ErrorCode::Success;
                            }
                            return StreamingResult{
                                StreamingCpuPayloadReady{
                                    .PayloadToken = 0u}};
                        },
                    .ApplyOnMainThread =
                        [weakState,
                         operation,
                         world,
                         registry,
                         bindingEpoch,
                         moduleGeneration](
                            StreamingResult&& result) mutable
                        {
                            const auto locked =
                                weakState.lock();
                            if (!locked)
                                return;
                            locked->ForgetOwnedTask(
                                operation->Task);
                            const bool bindingMatches =
                                locked->MatchesCapturedBinding(
                                    moduleGeneration,
                                    bindingEpoch,
                                    world,
                                    registry);
                            if (!bindingMatches)
                                return;

                            Core::Expected<
                                SceneSerializationResult> saved =
                                Core::Err<
                                    SceneSerializationResult>(
                                    result.has_value()
                                        ? operation->Error
                                        : result.error());
                            if (result.has_value() &&
                                operation->Error ==
                                    Core::ErrorCode::Success &&
                                operation->Result.has_value())
                            {
                                saved = *operation->Result;
                                locked->CurrentDocumentPath =
                                    operation->Path;
                                locked->History.MarkSaved(
                                    operation->Path);
                            }

                            RuntimeSceneFileEvent event{
                                .Operation =
                                    RuntimeSceneFileOperation::Save,
                                .Task = operation->Task,
                                .Path = operation->Path,
                                .Error = saved.has_value()
                                    ? Core::ErrorCode::Success
                                    : saved.error(),
                            };
                            if (saved.has_value())
                                event.SaveResult = *saved;
                            locked->RecordSceneFileEvent(
                                std::move(event));
                        },
                    .FinalizeCancellationOnMainThread =
                        [weakState,
                         operation,
                         world,
                         registry,
                         bindingEpoch,
                         moduleGeneration]
                        {
                            const auto locked =
                                weakState.lock();
                            if (!locked)
                                return;
                            locked->ForgetOwnedTask(
                                operation->Task);
                            const bool bindingMatches =
                                locked->MatchesCapturedBinding(
                                    moduleGeneration,
                                    bindingEpoch,
                                    world,
                                    registry);
                            if (!bindingMatches)
                                return;
                            locked->RecordSceneFileEvent(
                                RuntimeSceneFileEvent{
                                    .Operation =
                                        RuntimeSceneFileOperation::Save,
                                    .Task = operation->Task,
                                    .Path = operation->Path,
                                    .Error =
                                        Core::ErrorCode::InvalidState,
                                });
                        },
                });

        if (!handle.IsValid())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        operation->Task = handle;
        state->OwnedTasks.push_back(handle);
        Core::Log::Info(
            "[Runtime] Queued scene save: path='{}'",
            operation->Path);
        return RuntimeQueuedSceneFileOperation{
            .Task = handle,
            .Operation = RuntimeSceneFileOperation::Save,
        };
    }

    Core::Expected<SceneDeserializationResult>
    SceneDocumentModule::LoadSceneFromPath(std::string path)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state || !state->ValidateBinding())
        {
            return Core::Err<SceneDeserializationResult>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<SceneDeserializationResult>(
                Core::ErrorCode::InvalidPath);
        }

        // Freeze the complete candidate before any participant observes the
        // outgoing registry. A parse failure therefore cannot mutate scene,
        // path, history, event state, or participant-owned sidecars.
        Core::IO::FileIOBackend backend;
        ECS::Scene::Registry loadedScene;
        auto loaded = LoadSceneDocument(
            loadedScene, path, backend);
        if (!loaded.has_value())
        {
            return Core::Err<SceneDeserializationResult>(
                loaded.error());
        }

        if (Core::Result replaced = state->Replace(
                SceneReplacementKind::Load,
                &loadedScene,
                std::move(path));
            !replaced.has_value())
        {
            return Core::Err<SceneDeserializationResult>(
                replaced.error());
        }
        return loaded;
    }

    Core::Expected<RuntimeQueuedSceneFileOperation>
    SceneDocumentModule::QueueSceneLoadFromPath(std::string path)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state || !state->ValidateBinding() ||
            state->Streaming == nullptr)
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidPath);
        }

        const WorldHandle world = state->BoundWorld;
        ECS::Scene::Registry* const registry =
            state->BoundRegistry;
        const std::uint64_t bindingEpoch =
            state->BindingEpoch;
        const std::uint64_t moduleGeneration =
            state->ModuleGeneration;
        auto operation =
            std::make_shared<QueuedSceneLoadState>();
        operation->Path = std::move(path);

        const std::weak_ptr<Impl::State> weakState = state;
        const StreamingTaskHandle handle =
            state->Streaming->Submit(
                StreamingTaskDesc{
                    .Name = "Runtime.SceneLoad." +
                        FileNameFromPath(operation->Path),
                    .Kind = RuntimeTaskKinds::AssetDecode,
                    .Priority =
                        Core::Dag::TaskPriority::Normal,
                    .EstimatedCost = 4u,
                    .Scope = world,
                    .Execute =
                        [operation]() mutable -> StreamingResult
                        {
                            Core::IO::FileIOBackend backend;
                            auto loaded = LoadSceneDocument(
                                operation->LoadedScene,
                                operation->Path,
                                backend);
                            if (!loaded.has_value())
                            {
                                operation->Error =
                                    loaded.error();
                            }
                            else
                            {
                                operation->Result = *loaded;
                                operation->Error =
                                    Core::ErrorCode::Success;
                            }
                            return StreamingResult{
                                StreamingCpuPayloadReady{
                                    .PayloadToken = 0u}};
                        },
                    .ApplyOnMainThread =
                        [weakState,
                         operation,
                         world,
                         registry,
                         bindingEpoch,
                         moduleGeneration](
                            StreamingResult&& result) mutable
                        {
                            const auto locked =
                                weakState.lock();
                            if (!locked)
                                return;
                            locked->ForgetOwnedTask(
                                operation->Task);
                            const bool bindingMatches =
                                locked->MatchesCapturedBinding(
                                    moduleGeneration,
                                    bindingEpoch,
                                    world,
                                    registry);
                            if (!bindingMatches)
                                return;

                            Core::Expected<
                                SceneDeserializationResult> loaded =
                                Core::Err<
                                    SceneDeserializationResult>(
                                    result.has_value()
                                        ? operation->Error
                                        : result.error());
                            if (result.has_value() &&
                                operation->Error ==
                                    Core::ErrorCode::Success &&
                                operation->Result.has_value())
                            {
                                loaded = *operation->Result;
                                if (Core::Result replaced =
                                        locked->Replace(
                                            SceneReplacementKind::Load,
                                            &operation->LoadedScene,
                                            operation->Path);
                                    !replaced.has_value())
                                {
                                    loaded = Core::Err<
                                        SceneDeserializationResult>(
                                        replaced.error());
                                }
                            }

                            RuntimeSceneFileEvent event{
                                .Operation =
                                    RuntimeSceneFileOperation::Load,
                                .Task = operation->Task,
                                .Path = operation->Path,
                                .Error = loaded.has_value()
                                    ? Core::ErrorCode::Success
                                    : loaded.error(),
                            };
                            if (loaded.has_value())
                                event.LoadResult = *loaded;
                            locked->RecordSceneFileEvent(
                                std::move(event));
                        },
                    .FinalizeCancellationOnMainThread =
                        [weakState,
                         operation,
                         world,
                         registry,
                         bindingEpoch,
                         moduleGeneration]
                        {
                            const auto locked =
                                weakState.lock();
                            if (!locked)
                                return;
                            locked->ForgetOwnedTask(
                                operation->Task);
                            const bool bindingMatches =
                                locked->MatchesCapturedBinding(
                                    moduleGeneration,
                                    bindingEpoch,
                                    world,
                                    registry);
                            if (!bindingMatches)
                                return;
                            locked->RecordSceneFileEvent(
                                RuntimeSceneFileEvent{
                                    .Operation =
                                        RuntimeSceneFileOperation::Load,
                                    .Task = operation->Task,
                                    .Path = operation->Path,
                                    .Error =
                                        Core::ErrorCode::InvalidState,
                                });
                        },
                });

        if (!handle.IsValid())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        operation->Task = handle;
        state->OwnedTasks.push_back(handle);
        Core::Log::Info(
            "[Runtime] Queued scene load: path='{}'",
            operation->Path);
        return RuntimeQueuedSceneFileOperation{
            .Task = handle,
            .Operation = RuntimeSceneFileOperation::Load,
        };
    }

    const std::optional<RuntimeSceneFileEvent>&
    SceneDocumentModule::GetLastSceneFileEvent() const noexcept
    {
        static const std::optional<RuntimeSceneFileEvent> empty{};
        if (!m_Impl || !m_Impl->Shared)
            return empty;
        (void)m_Impl->Shared->ValidateBinding();
        return m_Impl->Shared->LastEvent;
    }

    Core::Result SceneDocumentModule::NewSceneDocument()
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state)
            return Core::Err(Core::ErrorCode::InvalidState);
        return state->Replace(
            SceneReplacementKind::New, nullptr, {});
    }

    Core::Result SceneDocumentModule::CloseSceneDocument()
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state)
            return Core::Err(Core::ErrorCode::InvalidState);
        return state->Replace(
            SceneReplacementKind::Close, nullptr, {});
    }

    Core::Expected<SceneReplacementParticipantHandle>
    SceneDocumentModule::RegisterReplacementParticipant(
        SceneReplacementParticipantDesc desc)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state || !state->AcceptingOperations ||
            !state->ValidateBinding())
        {
            return Core::Err<
                SceneReplacementParticipantHandle>(
                    Core::ErrorCode::InvalidState);
        }
        if (desc.Name.empty())
        {
            return Core::Err<
                SceneReplacementParticipantHandle>(
                    Core::ErrorCode::InvalidArgument);
        }
        if (std::any_of(
                state->ParticipantSlots.begin(),
                state->ParticipantSlots.end(),
                [&desc](const Impl::ParticipantSlot& slot)
                {
                    return slot.Occupied &&
                           slot.Desc.Name == desc.Name;
                }))
        {
            return Core::Err<
                SceneReplacementParticipantHandle>(
                    Core::ErrorCode::InvalidState);
        }

        std::uint32_t index = 0u;
        if (!state->FreeParticipantSlots.empty())
        {
            index = state->FreeParticipantSlots.back();
            state->FreeParticipantSlots.pop_back();
        }
        else
        {
            index = static_cast<std::uint32_t>(
                state->ParticipantSlots.size());
            state->ParticipantSlots.push_back(
                Impl::ParticipantSlot{});
        }

        Impl::ParticipantSlot& slot =
            state->ParticipantSlots[index];
        slot.Occupied = true;
        slot.Sequence = state->NextParticipantSequence++;
        slot.Desc = std::move(desc);
        return SceneReplacementParticipantHandle{
            index, slot.Generation};
    }

    Core::Result
    SceneDocumentModule::UnregisterReplacementParticipant(
        const SceneReplacementParticipantHandle handle)
    {
        const auto state =
            m_Impl ? m_Impl->Shared : nullptr;
        if (!state)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        // Shutdown announcement invalidates document operations before
        // reverse module teardown, but later announcement listeners must
        // still be able to detach their exact participant handles. Detaching
        // touches only module-owned slots and intentionally requires no live
        // world binding.
        if (state->AcceptingOperations &&
            !state->ValidateBinding())
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        if (!handle.IsValid() ||
            handle.Index >= state->ParticipantSlots.size())
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        Impl::ParticipantSlot& slot =
            state->ParticipantSlots[handle.Index];
        if (!slot.Occupied ||
            slot.Generation != handle.Generation)
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        slot.Occupied = false;
        slot.Sequence = 0u;
        slot.Desc = {};
        ++slot.Generation;
        if (slot.Generation == 0u)
            slot.Generation = 1u;
        state->FreeParticipantSlots.push_back(handle.Index);
        return Core::Ok();
    }
}
