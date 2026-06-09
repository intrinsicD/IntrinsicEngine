module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.EditorCommandHistory;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace G = Extrinsic::Graphics::Components;

        [[nodiscard]] bool IsSuccessfulStatus(
            const EditorCommandHistoryStatus status) noexcept
        {
            switch (status)
            {
            case EditorCommandHistoryStatus::Applied:
            case EditorCommandHistoryStatus::Recorded:
            case EditorCommandHistoryStatus::Undone:
            case EditorCommandHistoryStatus::Redone:
            case EditorCommandHistoryStatus::NoChange:
                return true;
            case EditorCommandHistoryStatus::EmptyUndoStack:
            case EditorCommandHistoryStatus::EmptyRedoStack:
            case EditorCommandHistoryStatus::InvalidCommand:
            case EditorCommandHistoryStatus::CommandFailed:
            case EditorCommandHistoryStatus::UndoFailed:
            case EditorCommandHistoryStatus::RedoFailed:
            case EditorCommandHistoryStatus::StaleEntity:
            case EditorCommandHistoryStatus::MissingScene:
            case EditorCommandHistoryStatus::MissingSelectionController:
            case EditorCommandHistoryStatus::MissingTransform:
            case EditorCommandHistoryStatus::UnsupportedOperation:
                return false;
            }
            return false;
        }

        [[nodiscard]] std::string NonEmptyLabel(std::string label)
        {
            if (label.empty())
                return "Editor Command";
            return label;
        }

        [[nodiscard]] ECS::EntityHandle ResolveLiveEntity(
            ECS::Scene::Registry& registry,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity == ECS::InvalidEntityHandle || !registry.Raw().valid(entity))
                return ECS::InvalidEntityHandle;
            return entity;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyTransform(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const ECSC::Transform::Component& value)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity = ResolveLiveEntity(*scene, stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            auto* transform = raw.try_get<ECSC::Transform::Component>(entity);
            if (transform == nullptr)
                return EditorCommandHistoryStatus::MissingTransform;

            *transform = value;
            raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplySelection(
            ECS::Scene::Registry* scene,
            SelectionController* selection,
            const std::optional<std::uint32_t> stableEntityId)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;
            if (selection == nullptr)
                return EditorCommandHistoryStatus::MissingSelectionController;

            if (!stableEntityId.has_value())
            {
                selection->ClearSelection(*scene);
                return EditorCommandHistoryStatus::Applied;
            }

            if (!selection->SetSelectedByStableEntityId(*scene, *stableEntityId))
                return EditorCommandHistoryStatus::StaleEntity;
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyPrimitiveViewSettings(
            const EditorPrimitiveViewSettingsCommand& command,
            const MeshPrimitiveViewSettings settings)
        {
            if (!command.SetSettings || !command.ClearSettings)
                return EditorCommandHistoryStatus::UnsupportedOperation;

            if (settings.AnyEnabled())
                command.SetSettings(command.StableEntityId, settings);
            else
                command.ClearSettings(command.StableEntityId);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplyVisualizationConfig(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::optional<G::VisualizationConfig>& value)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity = ResolveLiveEntity(*scene, stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            if (!value.has_value())
            {
                if (!raw.all_of<G::VisualizationConfig>(entity))
                    return EditorCommandHistoryStatus::NoChange;
                raw.remove<G::VisualizationConfig>(entity);
                return EditorCommandHistoryStatus::Applied;
            }

            raw.emplace_or_replace<G::VisualizationConfig>(entity, *value);
            return EditorCommandHistoryStatus::Applied;
        }

        [[nodiscard]] EditorCommandHistoryStatus ApplySpatialDebugBinding(
            ECS::Scene::Registry* scene,
            const std::uint32_t stableEntityId,
            const std::optional<ECSC::SpatialDebugBinding>& value)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity = ResolveLiveEntity(*scene, stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            if (!value.has_value())
            {
                if (!raw.all_of<ECSC::SpatialDebugBinding>(entity))
                    return EditorCommandHistoryStatus::NoChange;
                raw.remove<ECSC::SpatialDebugBinding>(entity);
                return EditorCommandHistoryStatus::Applied;
            }

            raw.emplace_or_replace<ECSC::SpatialDebugBinding>(entity, *value);
            return EditorCommandHistoryStatus::Applied;
        }

        void AppendDescendantStableIds(
            const entt::registry& raw,
            const ECS::EntityHandle entity,
            std::vector<std::uint32_t>& output,
            std::vector<ECS::EntityHandle>& visited)
        {
            if (std::find(visited.begin(), visited.end(), entity) != visited.end())
                return;
            visited.push_back(entity);

            const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(entity);
            if (hierarchy == nullptr)
                return;

            ECS::EntityHandle child = hierarchy->FirstChild;
            while (child != ECS::InvalidEntityHandle && raw.valid(child))
            {
                if (std::find(visited.begin(), visited.end(), child) != visited.end())
                    return;
                const auto* childHierarchy =
                    raw.try_get<ECSC::Hierarchy::Component>(child);
                output.push_back(SelectionController::ToStableEntityId(child));
                AppendDescendantStableIds(raw, child, output, visited);
                child = childHierarchy != nullptr
                    ? childHierarchy->NextSibling
                    : ECS::InvalidEntityHandle;
            }
        }
    }

    const char* DebugNameForEditorCommandHistoryStatus(
        const EditorCommandHistoryStatus status) noexcept
    {
        switch (status)
        {
        case EditorCommandHistoryStatus::Applied:
            return "Applied";
        case EditorCommandHistoryStatus::Recorded:
            return "Recorded";
        case EditorCommandHistoryStatus::Undone:
            return "Undone";
        case EditorCommandHistoryStatus::Redone:
            return "Redone";
        case EditorCommandHistoryStatus::NoChange:
            return "NoChange";
        case EditorCommandHistoryStatus::EmptyUndoStack:
            return "EmptyUndoStack";
        case EditorCommandHistoryStatus::EmptyRedoStack:
            return "EmptyRedoStack";
        case EditorCommandHistoryStatus::InvalidCommand:
            return "InvalidCommand";
        case EditorCommandHistoryStatus::CommandFailed:
            return "CommandFailed";
        case EditorCommandHistoryStatus::UndoFailed:
            return "UndoFailed";
        case EditorCommandHistoryStatus::RedoFailed:
            return "RedoFailed";
        case EditorCommandHistoryStatus::StaleEntity:
            return "StaleEntity";
        case EditorCommandHistoryStatus::MissingScene:
            return "MissingScene";
        case EditorCommandHistoryStatus::MissingSelectionController:
            return "MissingSelectionController";
        case EditorCommandHistoryStatus::MissingTransform:
            return "MissingTransform";
        case EditorCommandHistoryStatus::UnsupportedOperation:
            return "UnsupportedOperation";
        }
        return "Unknown";
    }

    bool EditorCommandHistoryResult::Succeeded() const noexcept
    {
        return IsSuccessfulStatus(Status);
    }

    EditorCommandHistory::EditorCommandHistory(const std::size_t capacity)
        : m_Capacity(capacity)
    {
    }

    EditorCommandHistoryResult EditorCommandHistory::Execute(
        EditorCommandRecord command)
    {
        command.Label = NonEmptyLabel(std::move(command.Label));
        if (!command.Redo || !command.Undo)
            return MakeResult(EditorCommandHistoryStatus::InvalidCommand, command.Label);

        const EditorCommandHistoryStatus status = command.Redo();
        if (!IsSuccessfulStatus(status))
            return MakeResult(status, command.Label);
        if (status == EditorCommandHistoryStatus::NoChange)
            return MakeResult(EditorCommandHistoryStatus::NoChange, command.Label);

        AdvanceRevision(command.Dirtying);
        PushUndo(std::move(command));
        m_RedoStack.clear();
        return MakeResult(EditorCommandHistoryStatus::Applied,
                          m_UndoStack.empty() ? std::string{} : m_UndoStack.back().Label);
    }

    EditorCommandHistoryResult EditorCommandHistory::Record(
        EditorCommandRecord command)
    {
        command.Label = NonEmptyLabel(std::move(command.Label));
        if (!command.Redo || !command.Undo)
            return MakeResult(EditorCommandHistoryStatus::InvalidCommand, command.Label);

        AdvanceRevision(command.Dirtying);
        PushUndo(std::move(command));
        m_RedoStack.clear();
        return MakeResult(EditorCommandHistoryStatus::Recorded,
                          m_UndoStack.empty() ? std::string{} : m_UndoStack.back().Label);
    }

    EditorCommandHistoryResult EditorCommandHistory::Undo()
    {
        if (m_UndoStack.empty())
            return MakeResult(EditorCommandHistoryStatus::EmptyUndoStack);

        EditorCommandRecord command = std::move(m_UndoStack.back());
        const EditorCommandHistoryStatus status = command.Undo();
        if (!IsSuccessfulStatus(status))
        {
            m_UndoStack.back() = std::move(command);
            return MakeResult(status, m_UndoStack.back().Label);
        }

        m_UndoStack.pop_back();
        AdvanceRevision(command.Dirtying);
        m_RedoStack.push_back(std::move(command));
        return MakeResult(EditorCommandHistoryStatus::Undone, m_RedoStack.back().Label);
    }

    EditorCommandHistoryResult EditorCommandHistory::Redo()
    {
        if (m_RedoStack.empty())
            return MakeResult(EditorCommandHistoryStatus::EmptyRedoStack);

        EditorCommandRecord command = std::move(m_RedoStack.back());
        const EditorCommandHistoryStatus status = command.Redo();
        if (!IsSuccessfulStatus(status))
        {
            m_RedoStack.back() = std::move(command);
            return MakeResult(status, m_RedoStack.back().Label);
        }

        m_RedoStack.pop_back();
        AdvanceRevision(command.Dirtying);
        PushUndo(std::move(command));
        return MakeResult(EditorCommandHistoryStatus::Redone,
                          m_UndoStack.empty() ? std::string{} : m_UndoStack.back().Label);
    }

    EditorCommandHistoryResult EditorCommandHistory::MarkDirty(std::string label)
    {
        AdvanceRevision(true);
        return MakeResult(EditorCommandHistoryStatus::Recorded,
                          NonEmptyLabel(std::move(label)));
    }

    void EditorCommandHistory::ClearHistory()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

    void EditorCommandHistory::ResetDocument(std::string path)
    {
        ClearHistory();
        m_Revision = 0u;
        m_SavedRevision = 0u;
        SetActivePath(std::move(path));
    }

    void EditorCommandHistory::MarkSaved(std::string path)
    {
        if (!path.empty())
            SetActivePath(std::move(path));
        m_SavedRevision = m_Revision;
    }

    void EditorCommandHistory::SetActivePath(std::string path)
    {
        m_HasActivePath = !path.empty();
        m_ActivePath = std::move(path);
    }

    void EditorCommandHistory::SetCapacity(const std::size_t capacity)
    {
        m_Capacity = capacity;
        TrimToCapacity();
    }

    EditorCommandHistorySnapshot EditorCommandHistory::Snapshot() const
    {
        return EditorCommandHistorySnapshot{
            .CanUndo = !m_UndoStack.empty(),
            .CanRedo = !m_RedoStack.empty(),
            .Dirty = IsDirty(),
            .HasActivePath = m_HasActivePath,
            .ActivePath = m_ActivePath,
            .UndoLabel = m_UndoStack.empty() ? std::string{} : m_UndoStack.back().Label,
            .RedoLabel = m_RedoStack.empty() ? std::string{} : m_RedoStack.back().Label,
            .Revision = m_Revision,
            .SavedRevision = m_SavedRevision,
            .UndoCount = m_UndoStack.size(),
            .RedoCount = m_RedoStack.size(),
        };
    }

    EditorCommandHistoryResult EditorCommandHistory::MakeResult(
        const EditorCommandHistoryStatus status,
        std::string label) const
    {
        return EditorCommandHistoryResult{
            .Status = status,
            .Label = std::move(label),
            .UndoCount = m_UndoStack.size(),
            .RedoCount = m_RedoStack.size(),
            .Dirty = IsDirty(),
            .Revision = m_Revision,
            .SavedRevision = m_SavedRevision,
        };
    }

    void EditorCommandHistory::PushUndo(EditorCommandRecord command)
    {
        if (m_Capacity == 0u)
            return;

        m_UndoStack.push_back(std::move(command));
        TrimToCapacity();
    }

    void EditorCommandHistory::TrimToCapacity()
    {
        while (m_UndoStack.size() > m_Capacity)
            m_UndoStack.pop_front();
        while (m_RedoStack.size() > m_Capacity)
            m_RedoStack.pop_front();
    }

    void EditorCommandHistory::AdvanceRevision(const bool dirtying) noexcept
    {
        if (dirtying)
            ++m_Revision;
    }

    EditorCommandRecord MakeTransformEditCommand(EditorTransformEditCommand command)
    {
        return EditorCommandRecord{
            .Label = NonEmptyLabel(std::move(command.Label)),
            .Redo = [command]()
            {
                return ApplyTransform(command.Scene,
                                      command.StableEntityId,
                                      command.After);
            },
            .Undo = [command]()
            {
                return ApplyTransform(command.Scene,
                                      command.StableEntityId,
                                      command.Before);
            },
            .Dirtying = true,
        };
    }

    EditorCommandRecord MakeSelectionReplaceCommand(
        EditorSelectionReplaceCommand command)
    {
        return EditorCommandRecord{
            .Label = NonEmptyLabel(std::move(command.Label)),
            .Redo = [command]()
            {
                return ApplySelection(command.Scene,
                                      command.Selection,
                                      command.AfterStableEntityId);
            },
            .Undo = [command]()
            {
                return ApplySelection(command.Scene,
                                      command.Selection,
                                      command.BeforeStableEntityId);
            },
            .Dirtying = false,
        };
    }

    EditorCommandRecord MakePrimitiveViewSettingsCommand(
        EditorPrimitiveViewSettingsCommand command)
    {
        const bool dirtying = command.Dirtying;
        return EditorCommandRecord{
            .Label = NonEmptyLabel(command.Label),
            .Redo = [command]()
            {
                return ApplyPrimitiveViewSettings(command, command.After);
            },
            .Undo = [command]()
            {
                return ApplyPrimitiveViewSettings(command, command.Before);
            },
            .Dirtying = dirtying,
        };
    }

    EditorCommandRecord MakeVisualizationConfigCommand(
        EditorVisualizationConfigCommand command)
    {
        return EditorCommandRecord{
            .Label = NonEmptyLabel(std::move(command.Label)),
            .Redo = [command]()
            {
                return ApplyVisualizationConfig(command.Scene,
                                                command.StableEntityId,
                                                command.After);
            },
            .Undo = [command]()
            {
                return ApplyVisualizationConfig(command.Scene,
                                                command.StableEntityId,
                                                command.Before);
            },
            .Dirtying = true,
        };
    }

    EditorCommandRecord MakeSpatialDebugBindingCommand(
        EditorSpatialDebugBindingCommand command)
    {
        return EditorCommandRecord{
            .Label = NonEmptyLabel(std::move(command.Label)),
            .Redo = [command]()
            {
                return ApplySpatialDebugBinding(command.Scene,
                                                command.StableEntityId,
                                                command.After);
            },
            .Undo = [command]()
            {
                return ApplySpatialDebugBinding(command.Scene,
                                                command.StableEntityId,
                                                command.Before);
            },
            .Dirtying = true,
        };
    }

    EditorCommandRecord MakeCompoundEditorCommand(
        std::string label,
        std::vector<EditorCommandRecord> commands)
    {
        bool dirtying = false;
        for (const EditorCommandRecord& command : commands)
            dirtying = dirtying || command.Dirtying;

        return EditorCommandRecord{
            .Label = NonEmptyLabel(std::move(label)),
            .Redo = [commands]()
            {
                std::size_t appliedCount = 0u;
                for (; appliedCount < commands.size(); ++appliedCount)
                {
                    if (!commands[appliedCount].Redo)
                        return EditorCommandHistoryStatus::InvalidCommand;
                    const EditorCommandHistoryStatus status =
                        commands[appliedCount].Redo();
                    if (!IsSuccessfulStatus(status))
                    {
                        for (std::size_t i = appliedCount; i > 0u; --i)
                        {
                            const EditorCommandRecord& rollback = commands[i - 1u];
                            if (!rollback.Undo || !IsSuccessfulStatus(rollback.Undo()))
                                return EditorCommandHistoryStatus::RedoFailed;
                        }
                        return status;
                    }
                }
                return EditorCommandHistoryStatus::Applied;
            },
            .Undo = [commands]()
            {
                for (std::size_t i = commands.size(); i > 0u; --i)
                {
                    const EditorCommandRecord& command = commands[i - 1u];
                    if (!command.Undo)
                        return EditorCommandHistoryStatus::InvalidCommand;
                    const EditorCommandHistoryStatus status = command.Undo();
                    if (!IsSuccessfulStatus(status))
                        return status;
                }
                return EditorCommandHistoryStatus::Applied;
            },
            .Dirtying = dirtying,
        };
    }

    EditorHierarchyDeletePlan BuildHierarchyDeletePlan(
        const ECS::Scene::Registry& registry,
        const std::uint32_t rootStableId,
        const EditorHierarchyDeletePolicy policy)
    {
        const entt::registry& raw = registry.Raw();
        const ECS::EntityHandle root = SelectionController::ToEntityHandle(rootStableId);
        if (root == ECS::InvalidEntityHandle || !raw.valid(root))
        {
            return EditorHierarchyDeletePlan{
                .RootStableId = rootStableId,
                .Policy = policy,
                .Status = EditorCommandHistoryStatus::StaleEntity,
            };
        }

        std::vector<std::uint32_t> descendants{};
        std::vector<ECS::EntityHandle> visited{};
        AppendDescendantStableIds(raw, root, descendants, visited);

        EditorHierarchyDeletePlan plan{
            .RootStableId = rootStableId,
            .Policy = policy,
            .Status = EditorCommandHistoryStatus::Applied,
        };
        plan.DeletedStableIds.push_back(rootStableId);
        if (policy == EditorHierarchyDeletePolicy::DeleteDescendants)
        {
            plan.DeletedStableIds.insert(plan.DeletedStableIds.end(),
                                         descendants.begin(),
                                         descendants.end());
        }
        else
        {
            plan.OrphanedStableIds = std::move(descendants);
        }
        return plan;
    }
}
