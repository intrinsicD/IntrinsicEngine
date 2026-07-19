module;

#include <cstdint>
#include <optional>
#include <string>

export module Extrinsic.Runtime.SceneDocument;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionReadback;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    export enum class RuntimeSceneFileOperation : std::uint8_t
    {
        None,
        Save,
        Load,
    };

    export struct RuntimeQueuedSceneFileOperation
    {
        StreamingTaskHandle Task{};
        RuntimeSceneFileOperation Operation{RuntimeSceneFileOperation::None};
    };

    export struct RuntimeSceneFileEvent
    {
        std::uint64_t Sequence{0};
        RuntimeSceneFileOperation Operation{RuntimeSceneFileOperation::None};
        StreamingTaskHandle Task{};
        std::string Path{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::optional<SceneSerializationResult> SaveResult{};
        std::optional<SceneDeserializationResult> LoadResult{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Error == Core::ErrorCode::Success;
        }
    };

    export struct SceneDocumentDependencies
    {
        bool* Initialized{};
        ECS::Scene::Registry** Scene{};
        StreamingExecutor* Streaming{};
        WorldRegistry* Worlds{};
        EditorCommandHistory* CommandHistory{};
        Graphics::IRenderer* Renderer{};
        RenderExtractionCache* RenderExtraction{};
        RuntimeObjectSpaceNormalBakeQueue* ObjectSpaceNormalBakeQueue{};
        SelectionController* Selection{};
        SelectionReadbackState* SelectionReadback{};
        StableEntityLookup* StableLookup{};
        StableEntityLookupSceneBinding* StableLookupBinding{};
    };

    export class SceneDocument
    {
    public:
        SceneDocument() = default;
        explicit SceneDocument(SceneDocumentDependencies dependencies);

        void SetDependencies(SceneDocumentDependencies dependencies);

        [[nodiscard]] Core::Expected<SceneSerializationResult>
            SaveSceneToPath(std::string path);
        [[nodiscard]] Core::Expected<RuntimeQueuedSceneFileOperation>
            QueueSceneSaveToPath(std::string path);
        [[nodiscard]] Core::Expected<SceneDeserializationResult>
            LoadSceneFromPath(std::string path);
        [[nodiscard]] Core::Expected<RuntimeQueuedSceneFileOperation>
            QueueSceneLoadFromPath(std::string path);
        [[nodiscard]] const std::optional<RuntimeSceneFileEvent>&
            GetLastSceneFileEvent() const noexcept;
        [[nodiscard]] Core::Result NewSceneDocument();
        [[nodiscard]] Core::Result CloseSceneDocument();

        // Engine world-maintenance uses the same outgoing-scene sidecar drain
        // that document replacement uses before swapping the active scene.
        void ClearSceneRuntimeState();

    private:
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] ECS::Scene::Registry* CurrentScene() const noexcept;
        void RecordSceneFileEvent(RuntimeSceneFileEvent event);
        void DisconnectStableEntityLookupTracking();
        void ConnectStableEntityLookupTracking();
        void RebuildStableEntityLookupAfterSceneReplacement();

        SceneDocumentDependencies m_Dependencies{};
        std::optional<RuntimeSceneFileEvent> m_LastSceneFileEvent{};
        std::uint64_t m_SceneFileEventSequence{0};
        std::uint64_t m_TargetBindingEpoch{1u};
    };
}
