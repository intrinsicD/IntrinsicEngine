module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.EditorCommandHistory;

import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.SelectionController;

export namespace Extrinsic::Runtime
{
    enum class EditorCommandHistoryStatus : std::uint8_t
    {
        Applied,
        Recorded,
        Undone,
        Redone,
        NoChange,
        EmptyUndoStack,
        EmptyRedoStack,
        InvalidCommand,
        CommandFailed,
        UndoFailed,
        RedoFailed,
        StaleEntity,
        MissingScene,
        MissingSelectionController,
        MissingTransform,
        UnsupportedOperation,
    };

    [[nodiscard]] const char* DebugNameForEditorCommandHistoryStatus(
        EditorCommandHistoryStatus status) noexcept;

    struct EditorCommandHistoryResult
    {
        EditorCommandHistoryStatus Status{EditorCommandHistoryStatus::NoChange};
        std::string Label{};
        std::size_t UndoCount{0u};
        std::size_t RedoCount{0u};
        bool Dirty{false};
        std::uint64_t Revision{0u};
        std::uint64_t SavedRevision{0u};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    struct EditorCommandHistorySnapshot
    {
        bool CanUndo{false};
        bool CanRedo{false};
        bool Dirty{false};
        bool HasActivePath{false};
        std::string ActivePath{};
        std::string UndoLabel{};
        std::string RedoLabel{};
        std::uint64_t Revision{0u};
        std::uint64_t SavedRevision{0u};
        std::size_t UndoCount{0u};
        std::size_t RedoCount{0u};
    };

    struct EditorCommandRecord
    {
        std::string Label{};
        std::function<EditorCommandHistoryStatus()> Redo{};
        std::function<EditorCommandHistoryStatus()> Undo{};
        bool Dirtying{true};
    };

    class EditorCommandHistory
    {
    public:
        explicit EditorCommandHistory(std::size_t capacity = 128u);

        [[nodiscard]] EditorCommandHistoryResult Execute(EditorCommandRecord command);
        [[nodiscard]] EditorCommandHistoryResult Record(EditorCommandRecord command);
        [[nodiscard]] EditorCommandHistoryResult Undo();
        [[nodiscard]] EditorCommandHistoryResult Redo();
        [[nodiscard]] EditorCommandHistoryResult MarkDirty(std::string label = {});

        void ClearHistory();
        void ResetDocument(std::string path = {});
        void MarkSaved(std::string path = {});
        void SetActivePath(std::string path);
        void SetCapacity(std::size_t capacity);

        [[nodiscard]] EditorCommandHistorySnapshot Snapshot() const;
        [[nodiscard]] std::size_t Capacity() const noexcept { return m_Capacity; }
        [[nodiscard]] std::size_t UndoCount() const noexcept { return m_UndoStack.size(); }
        [[nodiscard]] std::size_t RedoCount() const noexcept { return m_RedoStack.size(); }
        [[nodiscard]] bool CanUndo() const noexcept { return !m_UndoStack.empty(); }
        [[nodiscard]] bool CanRedo() const noexcept { return !m_RedoStack.empty(); }
        [[nodiscard]] bool IsDirty() const noexcept { return m_Revision != m_SavedRevision; }

    private:
        [[nodiscard]] EditorCommandHistoryResult MakeResult(
            EditorCommandHistoryStatus status,
            std::string label = {}) const;
        void PushUndo(EditorCommandRecord command);
        void TrimToCapacity();
        void AdvanceRevision(bool dirtying) noexcept;

        std::size_t m_Capacity{128u};
        std::deque<EditorCommandRecord> m_UndoStack{};
        std::deque<EditorCommandRecord> m_RedoStack{};
        std::uint64_t m_Revision{0u};
        std::uint64_t m_SavedRevision{0u};
        bool m_HasActivePath{false};
        std::string m_ActivePath{};
    };

    struct EditorTransformEditCommand
    {
        ECS::Scene::Registry* Scene{nullptr};
        std::uint32_t StableEntityId{0u};
        ECS::Components::Transform::Component Before{};
        ECS::Components::Transform::Component After{};
        std::string Label{"Edit Transform"};
    };

    struct EditorSelectionReplaceCommand
    {
        ECS::Scene::Registry* Scene{nullptr};
        SelectionController* Selection{nullptr};
        std::optional<std::uint32_t> BeforeStableEntityId{};
        std::optional<std::uint32_t> AfterStableEntityId{};
        std::string Label{"Change Selection"};
    };

    struct EditorPrimitiveViewSettingsCommand
    {
        std::uint32_t StableEntityId{0u};
        MeshPrimitiveViewSettings Before{};
        MeshPrimitiveViewSettings After{};
        std::function<void(std::uint32_t, MeshPrimitiveViewSettings)> SetSettings{};
        std::function<void(std::uint32_t)> ClearSettings{};
        bool Dirtying{false};
        std::string Label{"Change Primitive View"};
    };

    struct EditorVisualizationConfigCommand
    {
        ECS::Scene::Registry* Scene{nullptr};
        std::uint32_t StableEntityId{0u};
        std::optional<Graphics::Components::VisualizationConfig> Before{};
        std::optional<Graphics::Components::VisualizationConfig> After{};
        std::string Label{"Change Visualization"};
    };

    struct EditorSpatialDebugBindingCommand
    {
        ECS::Scene::Registry* Scene{nullptr};
        std::uint32_t StableEntityId{0u};
        std::optional<ECS::Components::SpatialDebugBinding> Before{};
        std::optional<ECS::Components::SpatialDebugBinding> After{};
        std::string Label{"Change Spatial Debug Binding"};
    };

    enum class EditorHierarchyDeletePolicy : std::uint8_t
    {
        DeleteDescendants,
        OrphanDescendants,
    };

    struct EditorHierarchyDeletePlan
    {
        std::uint32_t RootStableId{0u};
        std::vector<std::uint32_t> DeletedStableIds{};
        std::vector<std::uint32_t> OrphanedStableIds{};
        EditorHierarchyDeletePolicy Policy{EditorHierarchyDeletePolicy::DeleteDescendants};
        EditorCommandHistoryStatus Status{EditorCommandHistoryStatus::NoChange};
    };

    [[nodiscard]] EditorCommandRecord MakeTransformEditCommand(
        EditorTransformEditCommand command);
    [[nodiscard]] EditorCommandRecord MakeSelectionReplaceCommand(
        EditorSelectionReplaceCommand command);
    [[nodiscard]] EditorCommandRecord MakePrimitiveViewSettingsCommand(
        EditorPrimitiveViewSettingsCommand command);
    [[nodiscard]] EditorCommandRecord MakeVisualizationConfigCommand(
        EditorVisualizationConfigCommand command);
    [[nodiscard]] EditorCommandRecord MakeSpatialDebugBindingCommand(
        EditorSpatialDebugBindingCommand command);
    [[nodiscard]] EditorCommandRecord MakeCompoundEditorCommand(
        std::string label,
        std::vector<EditorCommandRecord> commands);
    [[nodiscard]] EditorHierarchyDeletePlan BuildHierarchyDeletePlan(
        const ECS::Scene::Registry& registry,
        std::uint32_t rootStableId,
        EditorHierarchyDeletePolicy policy);
}
