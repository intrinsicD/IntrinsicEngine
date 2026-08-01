module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.SceneEditingOperations;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    struct EditorSceneEditingCommandsAccess;
}

export namespace Extrinsic::Runtime
{
    using EditorAssetPayloadKind = Assets::AssetPayloadKind;
    using EditorCameraControllerKind = Core::Config::CameraControllerKind;
    [[nodiscard]] const char*
    DebugNameForEditorAssetPayloadKind(EditorAssetPayloadKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForEditorPrimitiveKind(RefinedPrimitiveKind kind) noexcept;

    [[nodiscard]] const char*
    DebugNameForEditorCameraControllerKind(Core::Config::CameraControllerKind kind) noexcept;

    struct EditorEntityRow
    {
        ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
        std::uint32_t StableEntityId{0u};
        std::string Name{};
        bool Selectable{false};
        bool Selected{false};
        bool Hovered{false};
        bool HasDurableStableId{false};
        ECS::Components::StableId DurableStableId{};
    };

    struct EditorTransformModel
    {
        bool HasLocalTransform{false};
        glm::vec3 LocalPosition{0.0f};
        glm::quat LocalRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 LocalScale{1.0f};
        bool HasWorldTransform{false};
        glm::vec3 WorldPosition{0.0f};
    };

    struct EditorPrimitiveDetailModel
    {
        bool HasPrimitive{false};
        PrimitiveSelectionResult Primitive{};
        bool HasFaceId{false};
        bool HasEdgeId{false};
        bool HasVertexId{false};
        bool HasPointId{false};
    };

    struct EditorSelectionModel
    {
        std::vector<std::uint32_t> SelectedStableIds{};
        std::vector<EditorEntityRow> SelectedEntities{};
        bool HasHovered{false};
        std::uint32_t HoveredStableId{0u};
        bool HasHoveredEntity{false};
        EditorEntityRow HoveredEntity{};
        EditorPrimitiveDetailModel Primitive{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    enum class EditorSceneFileOperation : std::uint8_t
    {
        New,
        Save,
        Load,
        Close,
    };

    struct EditorSceneFileCommand
    {
        std::string Path{};
    };

    struct EditorSceneFileResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        EditorSceneFileOperation Operation{EditorSceneFileOperation::Save};
        JobToken Task{};
        SceneSerializationStats Stats{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorSceneFileCommandSurface
    {
        std::function<EditorSceneFileResult()> New{};
        std::function<EditorSceneFileResult(const EditorSceneFileCommand&)> Save{};
        std::function<EditorSceneFileResult(const EditorSceneFileCommand&)> Load{};
        std::function<EditorSceneFileResult()> Close{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Save) && static_cast<bool>(Load);
        }

        [[nodiscard]] bool LifecycleAvailable() const noexcept
        {
            return static_cast<bool>(New) && static_cast<bool>(Close);
        }
    };

    struct EditorSceneFileModel
    {
        bool Enabled{false};
        bool LifecycleEnabled{false};
        bool PathEntryEnabled{true};
        bool NativeDialogsAvailable{false};
        bool CanNew{false};
        bool CanClose{false};
        bool CanSave{false};
        bool CanOpen{false};
        std::string PendingPath{};
        std::optional<EditorSceneFileResult> LastResult{};
        std::string StatusText{};
        std::string FileDialogBoundaryText{
            "Native file dialogs are deferred; use path entry or dropped paths."};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorDocumentModel
    {
        bool HistoryAvailable{false};
        bool Dirty{false};
        bool CanUndo{false};
        bool CanRedo{false};
        bool HasActivePath{false};
        std::string ActivePath{};
        std::string UndoLabel{};
        std::string RedoLabel{};
        std::uint64_t Revision{0u};
        std::uint64_t SavedRevision{0u};
        std::string StatusText{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorFileImportCommand
    {
        std::string Path{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
    };

    struct EditorFileImportResult
    {
        EditorCommandStatus Status{EditorCommandStatus::NoChange};
        RuntimeAssetIngestHandle Operation{};
        Assets::AssetId Asset{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::uint64_t PrimitiveEntitiesCreated{0};
        std::uint64_t EmbeddedTextureAssetsCreated{0};
        std::uint64_t TextureUploadRequests{0};
        bool MaterializedModelScene{false};
        bool RequestedTextureUpload{false};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    struct EditorAssetImportCommandSurface
    {
        std::function<EditorFileImportResult(const EditorFileImportCommand&)> Import{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Import);
        }
    };

    struct EditorFileImportPayloadOption
    {
        Assets::AssetPayloadKind Kind{Assets::AssetPayloadKind::Unknown};
        bool Enabled{false};
        std::string DisabledReason{};
    };

    struct EditorFileImportModel
    {
        bool Enabled{false};
        bool CanChoosePayloadHint{false};
        bool CanImport{false};
        std::string PendingPath{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Assets::AssetPayloadKind ResolvedPayloadKind{Assets::AssetPayloadKind::Unknown};
        std::array<EditorFileImportPayloadOption, 6> PayloadOptions{};
        std::string PayloadHintDisabledReason{};
        std::string ImportDisabledReason{};
        std::optional<EditorFileImportResult> LastResult{};
        std::string StatusText{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorAssetImportQueueCommandSurface
    {
        std::function<std::size_t()> ClearCompleted{};
        std::function<Core::Result(RuntimeAssetIngestHandle)> Cancel{};

        [[nodiscard]] bool ClearAvailable() const noexcept
        {
            return static_cast<bool>(ClearCompleted);
        }

        [[nodiscard]] bool CancelAvailable() const noexcept
        {
            return static_cast<bool>(Cancel);
        }
    };

    struct EditorAssetImportQueueRow
    {
        RuntimeAssetIngestHandle Operation{};
        std::uint64_t Sequence{0u};
        RuntimeAssetIngestSource Source{RuntimeAssetIngestSource::ManualImport};
        std::string SourcePath{};
        std::string PathBasename{};
        Assets::AssetPayloadKind PayloadKind{Assets::AssetPayloadKind::Unknown};
        Assets::AssetId Asset{};
        RuntimeAssetImportQueueStage Stage{RuntimeAssetImportQueueStage::Queued};
        RuntimeAssetImportQueueTerminalStatus TerminalStatus{
            RuntimeAssetImportQueueTerminalStatus::None};
        bool ProgressDeterminate{true};
        float NormalizedProgress{0.0f};
        std::string StageText{};
        std::string DiagnosticText{};
        double ElapsedSeconds{0.0};
        bool CanCancel{false};
        std::string CancelDisabledReason{};
    };

    struct EditorAssetImportQueueModel
    {
        std::vector<EditorAssetImportQueueRow> Rows{};
        std::size_t ActiveCount{0u};
        std::size_t TerminalCount{0u};
        bool ClearCompletedAvailable{false};
        bool CanClearCompleted{false};
        std::string ClearCompletedDisabledReason{};
        std::string StatusText{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorPrimitiveViewSettings
    {
        bool EnableEdgeView{false};
        bool EnableVertexView{false};
        MeshVertexViewRenderMode VertexRenderMode{MeshVertexViewRenderMode::ImpostorSphere};
        float VertexPointRadiusPx{6.0f};

        [[nodiscard]] bool AnyEnabled() const noexcept
        {
            return EnableEdgeView || EnableVertexView;
        }
    };

    struct EditorPrimitiveViewCommandSurface
    {
        std::function<EditorPrimitiveViewSettings(std::uint32_t)> GetSettings{};
        std::function<void(std::uint32_t, EditorPrimitiveViewSettings)> SetSettings{};
        std::function<void(std::uint32_t)> ClearSettings{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(GetSettings) && static_cast<bool>(SetSettings) &&
                   static_cast<bool>(ClearSettings);
        }
    };

    struct EditorCameraRenderModel
    {
        bool CameraControlsAvailable{false};
        bool RenderSettingsAvailable{false};
        bool PrimitiveViewControlsAvailable{false};
        bool HasMainCameraController{false};
        Core::Config::CameraControllerKind MainCameraControllerKind{
            Core::Config::CameraControllerKind::Orbit};
        bool HasPrimitiveViewEntity{false};
        std::uint32_t PrimitiveViewStableId{0u};
        EditorPrimitiveViewSettings PrimitiveView{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    struct EditorTransformEditCommand
    {
        std::uint32_t StableEntityId{0u};
        bool SetPosition{false};
        glm::vec3 Position{0.0f};
        bool SetRotation{false};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool SetScale{false};
        glm::vec3 Scale{1.0f};
    };

    struct EditorCameraControllerCommand
    {
        CameraControllerSlot Slot{CameraControllerSlot::Main};
        Core::Config::CameraControllerKind Kind{Core::Config::CameraControllerKind::Orbit};
        bool PreserveCurrentView{true};
        Core::Extent2D Viewport{};
    };

    struct EditorPrimitiveViewCommand
    {
        std::uint32_t StableEntityId{0u};
        bool SetEdgeView{false};
        bool EnableEdgeView{false};
        bool SetVertexView{false};
        bool EnableVertexView{false};
        bool SetVertexRenderMode{false};
        MeshVertexViewRenderMode VertexRenderMode{MeshVertexViewRenderMode::ImpostorSphere};
        bool SetVertexPointRadius{false};
        float VertexPointRadiusPx{6.0f};
    };

    struct EditorSceneEditingContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{DefaultWorldHandle};
        SelectionController* Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        Assets::AssetService* AssetService{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        std::uint64_t LastRefinedPrimitiveGeneration{0u};
        CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        EditorAssetImportCommandSurface AssetImportCommands{};
        EditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        EditorSceneFileCommandSurface SceneFileCommands{};
        EditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{Assets::AssetPayloadKind::Unknown};
        const EditorFileImportResult* LastAssetImportResult{nullptr};
        const EditorSceneFileResult* LastSceneFileResult{nullptr};
        std::function<bool()> AttachmentActive{};
        std::function<void()> InvalidateWorkspaceSnapshotCache{};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool SceneFileCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
    };

    struct EditorDocumentCommandSurface
    {
        std::function<EditorCommandHistoryResult()> Undo{};
        std::function<EditorCommandHistoryResult()> Redo{};

        [[nodiscard]] bool Available() const noexcept
        {
            return static_cast<bool>(Undo) && static_cast<bool>(Redo);
        }
    };

    // A bound, feature-local command capability for app composition. The
    // backing runtime services remain private to runtime; apps only retain
    // this typed handle for the duration of a prepared frame.
    class EditorSceneEditingCommands final
    {
    public:
        EditorSceneEditingCommands() = default;

        [[nodiscard]] bool IsBound() const noexcept;


    private:
        struct State;
        std::shared_ptr<const State> m_State{};

        explicit EditorSceneEditingCommands(std::shared_ptr<const State> state);
        friend struct EditorSceneEditingCommandsAccess;
        friend EditorSceneEditingCommands
        BindEditorSceneEditingCommands(EditorSceneEditingContext context);
    };

    [[nodiscard]] EditorSceneEditingCommands
    BindEditorSceneEditingCommands(EditorSceneEditingContext context);

    struct EditorSceneEditingPreparedFrame
    {
        EditorSceneEditingCommands Commands{};
        EditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        EditorDocumentCommandSurface DocumentCommands{};
        bool SceneAvailable{false};
        std::optional<EditorFileImportResult> LastAssetImportResult{};
        std::optional<EditorSceneFileResult> LastSceneFileResult{};
    };

    [[nodiscard]] EditorSceneEditingPreparedFrame
    PrepareEditorSceneEditingFrame(
        const EditorWorkspaceAttachment& attachment);

    bool SelectEditorEntity(const EditorSceneEditingContext& context, std::uint32_t stableEntityId);
    bool SelectEditorEntity(const EditorSceneEditingCommands& commands, std::uint32_t stableEntityId);
    EditorFileImportResult ApplyEditorFileImportCommand(const EditorSceneEditingContext& context,
                                                        const EditorFileImportCommand& command);
    EditorFileImportResult ApplyEditorFileImportCommand(const EditorSceneEditingCommands& commands,
                                                        const EditorFileImportCommand& command);
    EditorSceneFileResult ApplyEditorSceneSaveCommand(const EditorSceneEditingContext& context,
                                                      const EditorSceneFileCommand& command);
    EditorSceneFileResult ApplyEditorSceneSaveCommand(const EditorSceneEditingCommands& commands,
                                                      const EditorSceneFileCommand& command);
    EditorSceneFileResult ApplyEditorSceneLoadCommand(const EditorSceneEditingContext& context,
                                                      const EditorSceneFileCommand& command);
    EditorSceneFileResult ApplyEditorSceneLoadCommand(const EditorSceneEditingCommands& commands,
                                                      const EditorSceneFileCommand& command);
    EditorSceneFileResult ApplyEditorNewSceneCommand(const EditorSceneEditingContext& context);
    EditorSceneFileResult ApplyEditorNewSceneCommand(const EditorSceneEditingCommands& commands);
    EditorSceneFileResult ApplyEditorCloseSceneCommand(const EditorSceneEditingContext& context);
    EditorSceneFileResult ApplyEditorCloseSceneCommand(const EditorSceneEditingCommands& commands);
    EditorCommandStatus ApplyEditorTransformEdit(const EditorSceneEditingContext& context,
                                                 const EditorTransformEditCommand& command);
    EditorCommandStatus ApplyEditorTransformEdit(const EditorSceneEditingCommands& commands,
                                                 const EditorTransformEditCommand& command);
    EditorCommandStatus
    ApplyEditorCameraControllerCommand(const EditorSceneEditingContext& context,
                                       const EditorCameraControllerCommand& command);
    EditorCommandStatus
    ApplyEditorCameraControllerCommand(const EditorSceneEditingCommands& commands,
                                       const EditorCameraControllerCommand& command);
    EditorCommandStatus ApplyEditorPrimitiveViewCommand(const EditorSceneEditingContext& context,
                                                        const EditorPrimitiveViewCommand& command);
    EditorCommandStatus ApplyEditorPrimitiveViewCommand(const EditorSceneEditingCommands& commands,
                                                        const EditorPrimitiveViewCommand& command);
} // namespace Extrinsic::Runtime

namespace Extrinsic::Runtime
{
    struct EditorSceneEditingCommandsAccess final
    {
        [[nodiscard]] static const EditorSceneEditingContext*
        Resolve(const EditorSceneEditingCommands& commands) noexcept;
    };
}
