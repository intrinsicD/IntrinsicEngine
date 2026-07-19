module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.SceneDocumentModule;

import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;

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

    export enum class SceneReplacementKind : std::uint8_t
    {
        New,
        Load,
        Close,
    };

    export struct SceneReplacementContext
    {
        SceneReplacementKind Kind;
        WorldHandle World;
        ECS::Scene::Registry& Registry;
        std::uint64_t BindingEpoch;
    };

    export struct SceneReplacementParticipantDesc
    {
        std::string Name;
        std::function<void(const SceneReplacementContext&)> BeforeReplace;
        std::function<void(const SceneReplacementContext&)> AfterReplace;
    };

    export struct SceneReplacementParticipantTag;
    export using SceneReplacementParticipantHandle =
        Core::StrongHandle<SceneReplacementParticipantTag>;

    export class SceneDocumentModule final : public IRuntimeModule
    {
    public:
        SceneDocumentModule();
        ~SceneDocumentModule() override;

        SceneDocumentModule(const SceneDocumentModule&) = delete;
        SceneDocumentModule& operator=(const SceneDocumentModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

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

        [[nodiscard]] Core::Expected<SceneReplacementParticipantHandle>
            RegisterReplacementParticipant(
                SceneReplacementParticipantDesc desc);
        [[nodiscard]] Core::Result UnregisterReplacementParticipant(
            SceneReplacementParticipantHandle handle);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
