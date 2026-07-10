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

module Extrinsic.Runtime.SceneDocument;

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
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.StreamingExecutor;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::string FileNameFromPath(const std::string_view path)
        {
            if (path.empty())
            {
                return {};
            }

            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t begin = slash == std::string_view::npos
                ? 0u
                : slash + 1u;
            if (begin >= path.size())
            {
                return {};
            }
            return std::string(path.substr(begin));
        }

        template <typename Component>
        void CopySerializableComponent(const entt::registry& sourceRaw,
                                       entt::registry& destinationRaw,
                                       const ECS::EntityHandle source,
                                       const ECS::EntityHandle destination)
        {
            if (const auto* component = sourceRaw.try_get<Component>(source))
            {
                destinationRaw.emplace_or_replace<Component>(
                    destination,
                    *component);
            }
        }

        template <typename Component>
        void CopySerializableTag(const entt::registry& sourceRaw,
                                 entt::registry& destinationRaw,
                                 const ECS::EntityHandle source,
                                 const ECS::EntityHandle destination)
        {
            if (sourceRaw.all_of<Component>(source))
            {
                destinationRaw.emplace_or_replace<Component>(destination);
            }
        }

        void CopySerializableHierarchy(
            const entt::registry& sourceRaw,
            entt::registry& destinationRaw,
            const std::unordered_map<ECS::EntityHandle, ECS::EntityHandle>& remap,
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
            {
                return;
            }

            destinationRaw.emplace_or_replace<ECSC::Hierarchy::Component>(
                destination,
                ECSC::Hierarchy::Component{.Parent = parent->second});
        }

        void SnapshotSerializableScene(const ECS::Scene::Registry& source,
                                       ECS::Scene::Registry& destination)
        {
            namespace ECSC = ECS::Components;
            namespace GS = ECS::Components::GeometrySources;
            namespace G = Graphics::Components;
            namespace Sel = ECS::Components::Selection;

            const entt::registry& sourceRaw = source.Raw();
            entt::registry& destinationRaw = destination.Raw();

            std::vector<ECS::EntityHandle> entities;
            entities.reserve(sourceRaw.storage<entt::entity>()->size());
            for (const entt::entity entity : sourceRaw.view<entt::entity>())
            {
                entities.push_back(entity);
            }
            std::sort(entities.begin(),
                      entities.end(),
                      [](const ECS::EntityHandle lhs,
                         const ECS::EntityHandle rhs)
                      {
                          return static_cast<std::uint32_t>(lhs) <
                                 static_cast<std::uint32_t>(rhs);
                      });

            std::unordered_map<ECS::EntityHandle, ECS::EntityHandle> remap;
            remap.reserve(entities.size());
            for (const ECS::EntityHandle sourceEntity : entities)
            {
                remap.emplace(sourceEntity, destination.Create());
            }

            for (const ECS::EntityHandle sourceEntity : entities)
            {
                const ECS::EntityHandle destinationEntity =
                    remap.at(sourceEntity);

                CopySerializableComponent<ECSC::MetaData>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::StableId>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<Sel::SelectableTag>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Transform::Component>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableHierarchy(
                    sourceRaw,
                    destinationRaw,
                    remap,
                    sourceEntity,
                    destinationEntity);

                CopySerializableComponent<G::RenderSurface>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::RenderEdges>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::RenderPoints>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::VisualizationConfig>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<G::VisualizationLaneOverrides>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);

                CopySerializableComponent<GS::Vertices>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Edges>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Halfedges>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Faces>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<GS::Nodes>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<GS::HasMeshTopology>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<GS::HasGraphTopology>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);

                CopySerializableComponent<ProgressivePresentationBindings>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);

                CopySerializableTag<ECSC::Lights::LightTag>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::DirectionalLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::PointLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::SpotLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Lights::AmbientLight>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableTag<ECSC::Shadows::CasterTag>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::Collider::Component>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::RigidBody::Component>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::SpatialDebugBinding>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
                CopySerializableComponent<ECSC::AssetInstance::Source>(
                    sourceRaw,
                    destinationRaw,
                    sourceEntity,
                    destinationEntity);
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
    }

    SceneDocument::SceneDocument(SceneDocumentDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
    }

    void SceneDocument::SetDependencies(SceneDocumentDependencies dependencies)
    {
        m_Dependencies = std::move(dependencies);
    }

    bool SceneDocument::IsInitialized() const noexcept
    {
        return m_Dependencies.Initialized != nullptr &&
               *m_Dependencies.Initialized;
    }

    ECS::Scene::Registry* SceneDocument::CurrentScene() const noexcept
    {
        return m_Dependencies.Scene != nullptr
            ? *m_Dependencies.Scene
            : nullptr;
    }

    void SceneDocument::DisconnectStableEntityLookupTracking()
    {
        if (m_Dependencies.StableLookupBinding != nullptr)
            m_Dependencies.StableLookupBinding->Disconnect();
    }

    void SceneDocument::ConnectStableEntityLookupTracking()
    {
        ECS::Scene::Registry* scene = CurrentScene();
        if (m_Dependencies.StableLookupBinding != nullptr &&
            m_Dependencies.StableLookup != nullptr &&
            scene != nullptr)
        {
            m_Dependencies.StableLookupBinding->Connect(
                *m_Dependencies.StableLookup,
                *scene);
        }
    }

    void SceneDocument::RebuildStableEntityLookupAfterSceneReplacement()
    {
        if (m_Dependencies.StableLookupBinding != nullptr &&
            m_Dependencies.StableLookup != nullptr)
        {
            m_Dependencies.StableLookupBinding->Rebuild(
                *m_Dependencies.StableLookup,
                CurrentScene());
        }
    }

    void SceneDocument::RecordSceneFileEvent(RuntimeSceneFileEvent event)
    {
        event.Sequence = ++m_SceneFileEventSequence;
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
        m_LastSceneFileEvent = std::move(event);
    }

    void SceneDocument::ClearSceneRuntimeState()
    {
        if (m_Dependencies.Renderer != nullptr &&
            m_Dependencies.RenderExtraction != nullptr)
        {
            m_Dependencies.RenderExtraction->ClearSceneState(
                *m_Dependencies.Renderer);
        }
        if (m_Dependencies.ObjectSpaceNormalBakeQueue != nullptr)
        {
            m_Dependencies.ObjectSpaceNormalBakeQueue->Clear();
        }
        if (ECS::Scene::Registry* scene = CurrentScene();
            scene != nullptr && m_Dependencies.Selection != nullptr)
        {
            m_Dependencies.Selection->ClearSceneState(*scene);
        }
        if (m_Dependencies.SelectionReadback != nullptr)
            m_Dependencies.SelectionReadback->ClearRefinedPrimitiveCache();
    }

    Core::Expected<SceneSerializationResult> SceneDocument::SaveSceneToPath(
        std::string path)
    {
        ECS::Scene::Registry* scene = CurrentScene();
        if (!IsInitialized() || scene == nullptr)
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidState);
        if (path.empty())
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidPath);

        Core::IO::FileIOBackend backend;
        auto saved = SaveSceneDocument(*scene, path, backend);
        if (saved.has_value() && m_Dependencies.CommandHistory != nullptr)
            m_Dependencies.CommandHistory->MarkSaved(path);
        return saved;
    }

    Core::Expected<RuntimeQueuedSceneFileOperation>
    SceneDocument::QueueSceneSaveToPath(std::string path)
    {
        ECS::Scene::Registry* scene = CurrentScene();
        if (!IsInitialized() || scene == nullptr ||
            m_Dependencies.Streaming == nullptr)
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidPath);
        }

        auto state = std::make_shared<QueuedSceneSaveState>();
        state->Path = std::move(path);
        SnapshotSerializableScene(*scene, state->Snapshot);

        const StreamingTaskHandle handle = m_Dependencies.Streaming->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.SceneSave." + FileNameFromPath(state->Path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 3u,
                .Execute = [state]() mutable -> StreamingResult
                {
                    Core::IO::FileIOBackend backend;
                    auto saved = SaveSceneDocument(
                        state->Snapshot,
                        state->Path,
                        backend);
                    if (!saved.has_value())
                    {
                        state->Error = saved.error();
                        return StreamingResult{
                            StreamingCpuPayloadReady{.PayloadToken = 0u}};
                    }

                    state->Result = *saved;
                    state->Error = Core::ErrorCode::Success;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state](StreamingResult&& result) mutable
                {
                    Core::Expected<SceneSerializationResult> saved =
                        Core::Err<SceneSerializationResult>(
                            result.has_value() ? state->Error : result.error());

                    if (result.has_value() &&
                        state->Error == Core::ErrorCode::Success &&
                        state->Result.has_value())
                    {
                        if (!IsInitialized())
                        {
                            saved = Core::Err<SceneSerializationResult>(
                                Core::ErrorCode::InvalidState);
                        }
                        else
                        {
                            saved = *state->Result;
                            if (m_Dependencies.CommandHistory != nullptr)
                            {
                                m_Dependencies.CommandHistory->MarkSaved(
                                    state->Path);
                            }
                        }
                    }

                    RuntimeSceneFileEvent event{
                        .Operation = RuntimeSceneFileOperation::Save,
                        .Task = state->Task,
                        .Path = state->Path,
                        .Error = saved.has_value()
                            ? Core::ErrorCode::Success
                            : saved.error(),
                    };
                    if (saved.has_value())
                    {
                        event.SaveResult = *saved;
                    }
                    RecordSceneFileEvent(std::move(event));
                },
            });

        if (!handle.IsValid())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }

        state->Task = handle;
        Core::Log::Info(
            "[Runtime] Queued scene save: path='{}'",
            state->Path);
        return RuntimeQueuedSceneFileOperation{
            .Task = handle,
            .Operation = RuntimeSceneFileOperation::Save,
        };
    }

    Core::Expected<SceneDeserializationResult> SceneDocument::LoadSceneFromPath(
        std::string path)
    {
        ECS::Scene::Registry* scene = CurrentScene();
        if (!IsInitialized() || scene == nullptr)
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidState);
        if (path.empty())
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidPath);

        Core::IO::FileIOBackend backend;
        ECS::Scene::Registry loadedScene;
        auto loaded = LoadSceneDocument(loadedScene, path, backend);
        if (!loaded.has_value())
            return Core::Err<SceneDeserializationResult>(loaded.error());

        ClearSceneRuntimeState();
        DisconnectStableEntityLookupTracking();
        scene->Clear();
        scene->Raw() = std::move(loadedScene.Raw());
        RebuildStableEntityLookupAfterSceneReplacement();
        if (m_Dependencies.CommandHistory != nullptr)
        {
            m_Dependencies.CommandHistory->ResetDocument(path);
        }
        return loaded;
    }

    Core::Expected<RuntimeQueuedSceneFileOperation>
    SceneDocument::QueueSceneLoadFromPath(std::string path)
    {
        ECS::Scene::Registry* scene = CurrentScene();
        if (!IsInitialized() || scene == nullptr ||
            m_Dependencies.Streaming == nullptr)
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }
        if (path.empty())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidPath);
        }

        auto state = std::make_shared<QueuedSceneLoadState>();
        state->Path = std::move(path);

        const StreamingTaskHandle handle = m_Dependencies.Streaming->Submit(
            StreamingTaskDesc{
                .Name = "Runtime.SceneLoad." + FileNameFromPath(state->Path),
                .Kind = Core::Dag::TaskKind::AssetDecode,
                .Priority = Core::Dag::TaskPriority::Normal,
                .EstimatedCost = 4u,
                .Execute = [state]() mutable -> StreamingResult
                {
                    Core::IO::FileIOBackend backend;
                    auto loaded = LoadSceneDocument(
                        state->LoadedScene,
                        state->Path,
                        backend);
                    if (!loaded.has_value())
                    {
                        state->Error = loaded.error();
                        return StreamingResult{
                            StreamingCpuPayloadReady{.PayloadToken = 0u}};
                    }

                    state->Result = *loaded;
                    state->Error = Core::ErrorCode::Success;
                    return StreamingResult{
                        StreamingCpuPayloadReady{.PayloadToken = 0u}};
                },
                .ApplyOnMainThread = [this, state](StreamingResult&& result) mutable
                {
                    Core::Expected<SceneDeserializationResult> loaded =
                        Core::Err<SceneDeserializationResult>(
                            result.has_value() ? state->Error : result.error());

                    ECS::Scene::Registry* currentScene = CurrentScene();
                    if (result.has_value() &&
                        state->Error == Core::ErrorCode::Success &&
                        state->Result.has_value())
                    {
                        if (!IsInitialized() || currentScene == nullptr)
                        {
                            loaded = Core::Err<SceneDeserializationResult>(
                                Core::ErrorCode::InvalidState);
                        }
                        else
                        {
                            loaded = *state->Result;
                            ClearSceneRuntimeState();
                            DisconnectStableEntityLookupTracking();
                            currentScene->Clear();
                            currentScene->Raw() =
                                std::move(state->LoadedScene.Raw());
                            RebuildStableEntityLookupAfterSceneReplacement();
                            if (m_Dependencies.CommandHistory != nullptr)
                            {
                                m_Dependencies.CommandHistory->ResetDocument(
                                    state->Path);
                            }
                        }
                    }

                    RuntimeSceneFileEvent event{
                        .Operation = RuntimeSceneFileOperation::Load,
                        .Task = state->Task,
                        .Path = state->Path,
                        .Error = loaded.has_value()
                            ? Core::ErrorCode::Success
                            : loaded.error(),
                    };
                    if (loaded.has_value())
                    {
                        event.LoadResult = *loaded;
                    }
                    RecordSceneFileEvent(std::move(event));
                },
            });

        if (!handle.IsValid())
        {
            return Core::Err<RuntimeQueuedSceneFileOperation>(
                Core::ErrorCode::InvalidState);
        }

        state->Task = handle;
        Core::Log::Info(
            "[Runtime] Queued scene load: path='{}'",
            state->Path);
        return RuntimeQueuedSceneFileOperation{
            .Task = handle,
            .Operation = RuntimeSceneFileOperation::Load,
        };
    }

    const std::optional<RuntimeSceneFileEvent>&
    SceneDocument::GetLastSceneFileEvent() const noexcept
    {
        return m_LastSceneFileEvent;
    }

    Core::Result SceneDocument::NewSceneDocument()
    {
        ECS::Scene::Registry* scene = CurrentScene();
        if (!IsInitialized() || scene == nullptr)
            return Core::Err(Core::ErrorCode::InvalidState);

        ClearSceneRuntimeState();
        DisconnectStableEntityLookupTracking();
        scene->Clear();
        if (m_Dependencies.StableLookup != nullptr)
        {
            m_Dependencies.StableLookup->Clear();
        }
        ConnectStableEntityLookupTracking();
        if (m_Dependencies.CommandHistory != nullptr)
        {
            m_Dependencies.CommandHistory->ResetDocument();
        }
        return Core::Ok();
    }

    Core::Result SceneDocument::CloseSceneDocument()
    {
        return NewSceneDocument();
    }
}
