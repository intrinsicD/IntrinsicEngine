#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include <entt/entity/registry.hpp>

import Core;

namespace
{
    struct TestComponent
    {
        int value = 0;

        friend bool operator==(const TestComponent& lhs, const TestComponent& rhs) = default;
    };
}

TEST(CoreCommands, ExecuteUndoRedoWithMoveOnlyCapture)
{
    Core::CommandHistory history;
    int observed = 0;

    auto payload = std::make_unique<int>(7);
    EXPECT_TRUE(history.Execute(Core::EditorCommand{
        .name = "Move-only capture",
        .redo = [ptr = std::move(payload), &observed]() { observed = *ptr; },
        .undo = [&observed]() { observed = 0; },
    }));

    EXPECT_EQ(observed, 7);
    EXPECT_TRUE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());

    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(observed, 0);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_TRUE(history.CanRedo());

    EXPECT_TRUE(history.Redo());
    EXPECT_EQ(observed, 7);
    EXPECT_TRUE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
}

TEST(CoreCommands, NewExecuteClearsRedoStack)
{
    Core::CommandHistory history;
    int value = 0;

    EXPECT_TRUE(history.Execute(Core::EditorCommand{
        .name = "Set one",
        .redo = [&value]() { value = 1; },
        .undo = [&value]() { value = 0; },
    }));

    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(history.CanRedo());

    EXPECT_TRUE(history.Execute(Core::EditorCommand{
        .name = "Set two",
        .redo = [&value]() { value = 2; },
        .undo = [&value]() { value = 0; },
    }));

    EXPECT_EQ(value, 2);
    EXPECT_TRUE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());
    EXPECT_FALSE(history.Redo());
}

TEST(CoreCommands, CapacityDropsOldestUndoHistory)
{
    Core::CommandHistory history{1};
    int value = 0;

    EXPECT_TRUE(history.Execute(Core::EditorCommand{
        .name = "Set one",
        .redo = [&value]() { value = 1; },
        .undo = [&value]() { value = 0; },
    }));

    EXPECT_TRUE(history.Execute(Core::EditorCommand{
        .name = "Set two",
        .redo = [&value]() { value = 2; },
        .undo = [&value]() { value = 1; },
    }));

    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_TRUE(history.CanUndo());

    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(value, 1);
    EXPECT_FALSE(history.CanUndo());
    EXPECT_TRUE(history.CanRedo());
}

TEST(CoreCommands, ComponentChangeCommandRestoresRegistryState)
{
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TestComponent>(entity, 1);
    Core::CommandHistory history;

    auto command = Core::MakeComponentChangeCommand(
        "Change value",
        &registry,
        entity,
        TestComponent{1},
        TestComponent{5});

    EXPECT_TRUE(history.Execute(std::move(command)));

    EXPECT_EQ(registry.get<TestComponent>(entity).value, 5);
    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(registry.get<TestComponent>(entity).value, 1);
    EXPECT_TRUE(history.Redo());
    EXPECT_EQ(registry.get<TestComponent>(entity).value, 5);
}

TEST(CoreCommands, RecordPushesWithoutExecutingRedo)
{
    Core::CommandHistory history;
    int value = 42;

    EXPECT_TRUE(history.Record(Core::EditorCommand{
        .name = "Already applied",
        .redo = [&value]() { value = 99; },
        .undo = [&value]() { value = 0; },
    }));

    // Record should NOT call redo — value stays at 42.
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(history.CanUndo());
    EXPECT_FALSE(history.CanRedo());

    // Undo should work normally.
    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(history.CanRedo());

    // Redo should re-apply the redo lambda.
    EXPECT_TRUE(history.Redo());
    EXPECT_EQ(value, 99);
}

TEST(CoreCommands, RecordClearsRedoStack)
{
    Core::CommandHistory history;
    int value = 0;

    EXPECT_TRUE(history.Execute(Core::EditorCommand{
        .name = "Set one",
        .redo = [&value]() { value = 1; },
        .undo = [&value]() { value = 0; },
    }));
    EXPECT_TRUE(history.Undo());
    EXPECT_TRUE(history.CanRedo());

    // Recording a new command should clear the redo stack.
    EXPECT_TRUE(history.Record(Core::EditorCommand{
        .name = "Recorded",
        .redo = [&value]() { value = 2; },
        .undo = [&value]() { value = 0; },
    }));

    EXPECT_FALSE(history.CanRedo());
    // Undo moved "Set one" to redo stack; Record cleared redo and added "Recorded".
    EXPECT_EQ(history.UndoCount(), 1u);
}

TEST(CoreCommands, RecordRespectsCapacity)
{
    Core::CommandHistory history{1};
    int value = 0;

    EXPECT_TRUE(history.Record(Core::EditorCommand{
        .name = "First",
        .redo = [&value]() { value = 1; },
        .undo = [&value]() { value = 0; },
    }));
    EXPECT_TRUE(history.Record(Core::EditorCommand{
        .name = "Second",
        .redo = [&value]() { value = 2; },
        .undo = [&value]() { value = 1; },
    }));

    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(value, 1);
    EXPECT_FALSE(history.CanUndo());
}

TEST(CoreCommands, ComponentChangeCommandFailsForDestroyedEntity)
{
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<TestComponent>(entity, 1);

    Core::CmdComponentChange<TestComponent> change{entity, TestComponent{1}, TestComponent{2}};
    registry.destroy(entity);

    EXPECT_FALSE(change.undo(registry));
    EXPECT_FALSE(change.redo(registry));
}
