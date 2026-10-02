// Scene command records and undo/redo state shared by editor operations.
module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.EditorCommandHistory;

import Extrinsic.ECS.Scene.Handle;
extern "C++" {
    namespace Extrinsic::ECS::Scene { class Registry; }
    namespace Extrinsic::Runtime { class SelectionController; }
}

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

        // Commands executed or recorded between BeginGroup and the matching
        // EndGroup form one undo step (nesting joins the outermost group).
        // A group of one command keeps that command; an empty group records
        // nothing. Prefer `ScopedEditorCommandGroup`.
        void BeginGroup();
        void EndGroup(std::string label);

        void ClearHistory();
        void ResetDocument(std::string path = {});
        void MarkSaved(std::string path = {});
        void SetActivePath(std::string path);
        void SetCapacity(std::size_t capacity);
        // Prepended to every label recorded while set (e.g. "Agent: " for
        // commands issued through the agent lane); empty disables it.
        void SetLabelPrefix(std::string prefix) { m_LabelPrefix = std::move(prefix); }
        [[nodiscard]] const std::string& LabelPrefix() const noexcept { return m_LabelPrefix; }

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
        void CommitApplied(EditorCommandRecord command);
        void TrimToCapacity();
        void AdvanceRevision(bool dirtying) noexcept;

        std::size_t m_Capacity{128u};
        std::deque<EditorCommandRecord> m_UndoStack{};
        std::deque<EditorCommandRecord> m_RedoStack{};
        std::uint64_t m_Revision{0u};
        std::uint64_t m_SavedRevision{0u};
        bool m_HasActivePath{false};
        std::string m_ActivePath{};
        // Main-thread state: the agent scope, job completions and panels all run there.
        std::string m_LabelPrefix{};
        std::uint32_t m_GroupDepth{0u};
        std::vector<EditorCommandRecord> m_GroupRecords{};
    };

    // Groups every command issued during its lifetime into one undo step;
    // a null history is a no-op.
    class ScopedEditorCommandGroup
    {
    public:
        ScopedEditorCommandGroup(EditorCommandHistory* history, std::string label)
            : m_History(history), m_Label(std::move(label))
        {
            if (m_History != nullptr) m_History->BeginGroup();
        }
        ~ScopedEditorCommandGroup()
        {
            if (m_History != nullptr) m_History->EndGroup(std::move(m_Label));
        }
        ScopedEditorCommandGroup(const ScopedEditorCommandGroup&) = delete;
        ScopedEditorCommandGroup& operator=(const ScopedEditorCommandGroup&) = delete;

    private:
        EditorCommandHistory* m_History{nullptr};
        std::string m_Label{};
    };

    // Sets a history label prefix for the lifetime of the scope and restores
    // the previous prefix afterwards; a null history is a no-op.
    class ScopedEditorCommandLabelPrefix
    {
    public:
        ScopedEditorCommandLabelPrefix(EditorCommandHistory* history, std::string prefix)
            : m_History(history)
        {
            if (m_History == nullptr) return;
            m_Previous = m_History->LabelPrefix();
            m_History->SetLabelPrefix(std::move(prefix));
        }
        ~ScopedEditorCommandLabelPrefix()
        {
            if (m_History != nullptr) m_History->SetLabelPrefix(std::move(m_Previous));
        }
        ScopedEditorCommandLabelPrefix(const ScopedEditorCommandLabelPrefix&) = delete;
        ScopedEditorCommandLabelPrefix& operator=(const ScopedEditorCommandLabelPrefix&) = delete;

    private:
        EditorCommandHistory* m_History{nullptr};
        std::string m_Previous{};
    };

    struct EditorSelectionReplaceCommand
    {
        ECS::Scene::Registry* Scene{nullptr};
        SelectionController* Selection{nullptr};
        std::optional<std::uint32_t> BeforeStableEntityId{};
        std::optional<std::uint32_t> AfterStableEntityId{};
        std::string Label{"Change Selection"};
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

    [[nodiscard]] EditorCommandRecord MakeSelectionReplaceCommand(
        EditorSelectionReplaceCommand command);
    [[nodiscard]] EditorCommandRecord MakeCompoundEditorCommand(
        std::string label,
        std::vector<EditorCommandRecord> commands);
    [[nodiscard]] EditorHierarchyDeletePlan BuildHierarchyDeletePlan(
        const ECS::Scene::Registry& registry,
        std::uint32_t rootStableId,
        EditorHierarchyDeletePolicy policy);
}
