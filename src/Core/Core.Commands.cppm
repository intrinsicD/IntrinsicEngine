module;

#include <cassert>
#include <cstddef>
#include <deque>
#include <functional>
#include <concepts>
#include <string>
#include <utility>

#include <entt/entity/registry.hpp>

export module Core.Commands;

export namespace Core
{
    template <typename T>
    struct CmdComponentChange
    {
        entt::entity entity{entt::null};
        T oldState;
        T newState;

        [[nodiscard]] bool undo(entt::registry& reg) const
        {
            if (!reg.valid(entity)) return false;
            reg.emplace_or_replace<T>(entity, oldState);
            return true;
        }

        [[nodiscard]] bool redo(entt::registry& reg) const
        {
            if (!reg.valid(entity)) return false;
            reg.emplace_or_replace<T>(entity, newState);
            return true;
        }
    };

    struct EditorCommand
    {
        std::string name;
        std::move_only_function<void()> redo;
        std::move_only_function<void()> undo;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(redo) && static_cast<bool>(undo);
        }
    };

    template <typename T>
        requires std::copy_constructible<T>
    [[nodiscard]] EditorCommand MakeComponentChangeCommand(std::string name,
                                                           entt::registry* reg,
                                                           entt::entity entity,
                                                           T oldState,
                                                           T newState)
    {
        assert(reg && "MakeComponentChangeCommand: registry pointer must not be null");
        const CmdComponentChange<T> change{entity, std::move(oldState), std::move(newState)};

        return EditorCommand{
            .name = std::move(name),
            .redo = [reg, change]() { if (reg) (void)change.redo(*reg); },
            .undo = [reg, change]() { if (reg) (void)change.undo(*reg); },
        };
    }

    class CommandHistory
    {
        std::deque<EditorCommand> undoStack;
        std::deque<EditorCommand> redoStack;
        std::size_t m_Capacity = 256;

        void TrimToCapacity(std::deque<EditorCommand>& stack)
        {
            while (stack.size() > m_Capacity)
            {
                stack.pop_front();
            }
        }

        [[nodiscard]] bool PushCommand(EditorCommand cmd, bool executeRedo)
        {
            assert(cmd.IsValid() && "Core::EditorCommand must provide both redo and undo callables");
            if (!cmd.IsValid()) return false;

            if (executeRedo)
                cmd.redo();

            redoStack.clear();
            if (m_Capacity > 0 && undoStack.size() == m_Capacity)
                undoStack.pop_front();

            undoStack.push_back(std::move(cmd));
            return true;
        }

    public:
        explicit CommandHistory(std::size_t capacity = 256)
            : m_Capacity(capacity == 0 ? 1 : capacity)
        {
        }

        CommandHistory(const CommandHistory&) = delete;
        CommandHistory& operator=(const CommandHistory&) = delete;
        CommandHistory(CommandHistory&&) = delete;
        CommandHistory& operator=(CommandHistory&&) = delete;

        [[nodiscard]] std::size_t Capacity() const noexcept { return m_Capacity; }
        [[nodiscard]] std::size_t UndoCount() const noexcept { return undoStack.size(); }
        [[nodiscard]] std::size_t RedoCount() const noexcept { return redoStack.size(); }
        [[nodiscard]] bool CanUndo() const noexcept { return !undoStack.empty(); }
        [[nodiscard]] bool CanRedo() const noexcept { return !redoStack.empty(); }

        void SetCapacity(std::size_t capacity)
        {
            m_Capacity = capacity == 0 ? 1 : capacity;
            TrimToCapacity(undoStack);
            TrimToCapacity(redoStack);
        }

        [[nodiscard]] bool Execute(EditorCommand cmd)
        {
            return PushCommand(std::move(cmd), true);
        }

        /// Push a command whose redo action has already been performed.
        /// Use when the action was applied externally (e.g. geometry operators)
        /// and only undo/redo replay should go through the command.
        [[nodiscard]] bool Record(EditorCommand cmd)
        {
            return PushCommand(std::move(cmd), false);
        }

        [[nodiscard]] bool Undo()
        {
            if (undoStack.empty()) return false;

            auto cmd = std::move(undoStack.back());
            undoStack.pop_back();

            cmd.undo();
            redoStack.push_back(std::move(cmd));
            return true;
        }

        [[nodiscard]] bool Redo()
        {
            if (redoStack.empty()) return false;

            auto cmd = std::move(redoStack.back());
            redoStack.pop_back();

            cmd.redo();
            if (m_Capacity > 0 && undoStack.size() == m_Capacity)
                undoStack.pop_front();
            undoStack.push_back(std::move(cmd));
            return true;
        }

        void Clear() noexcept
        {
            undoStack.clear();
            redoStack.clear();
        }
    };
}
