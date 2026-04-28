#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

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

TEST(CoreCommands, CompoundCommandExecutesInOrder)
{
    Core::CommandHistory history;
    std::vector<int> log;

    std::vector<Core::EditorCommand> subs;
    subs.push_back(Core::EditorCommand{
        .name = "A",
        .redo = [&log]() { log.push_back(1); },
        .undo = [&log]() { log.push_back(-1); },
    });
    subs.push_back(Core::EditorCommand{
        .name = "B",
        .redo = [&log]() { log.push_back(2); },
        .undo = [&log]() { log.push_back(-2); },
    });
    subs.push_back(Core::EditorCommand{
        .name = "C",
        .redo = [&log]() { log.push_back(3); },
        .undo = [&log]() { log.push_back(-3); },
    });

    auto compound = Core::MakeCompoundCommand("ABC", std::move(subs));
    EXPECT_TRUE(history.Execute(std::move(compound)));

    // Redo executes forward: 1, 2, 3
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[1], 2);
    EXPECT_EQ(log[2], 3);

    log.clear();
    EXPECT_TRUE(history.Undo());

    // Undo executes reverse: -3, -2, -1
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], -3);
    EXPECT_EQ(log[1], -2);
    EXPECT_EQ(log[2], -1);

    log.clear();
    EXPECT_TRUE(history.Redo());

    // Redo again: 1, 2, 3
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], 1);
    EXPECT_EQ(log[1], 2);
    EXPECT_EQ(log[2], 3);
}

TEST(CoreCommands, BatchComponentChangeRestoresAllEntities)
{
    entt::registry registry;
    const auto e1 = registry.create();
    const auto e2 = registry.create();
    const auto e3 = registry.create();
    registry.emplace<TestComponent>(e1, 10);
    registry.emplace<TestComponent>(e2, 20);
    registry.emplace<TestComponent>(e3, 30);

    Core::CommandHistory history;

    std::vector<std::pair<entt::entity, std::pair<TestComponent, TestComponent>>> changes;
    changes.push_back({e1, {TestComponent{10}, TestComponent{100}}});
    changes.push_back({e2, {TestComponent{20}, TestComponent{200}}});
    changes.push_back({e3, {TestComponent{30}, TestComponent{300}}});

    auto cmd = Core::MakeBatchComponentChangeCommand<TestComponent>(
        "Batch change", &registry, std::move(changes));

    EXPECT_TRUE(history.Execute(std::move(cmd)));
    EXPECT_EQ(registry.get<TestComponent>(e1).value, 100);
    EXPECT_EQ(registry.get<TestComponent>(e2).value, 200);
    EXPECT_EQ(registry.get<TestComponent>(e3).value, 300);

    EXPECT_TRUE(history.Undo());
    EXPECT_EQ(registry.get<TestComponent>(e1).value, 10);
    EXPECT_EQ(registry.get<TestComponent>(e2).value, 20);
    EXPECT_EQ(registry.get<TestComponent>(e3).value, 30);

    EXPECT_TRUE(history.Redo());
    EXPECT_EQ(registry.get<TestComponent>(e1).value, 100);
    EXPECT_EQ(registry.get<TestComponent>(e2).value, 200);
    EXPECT_EQ(registry.get<TestComponent>(e3).value, 300);
}

TEST(CoreCommands, CompoundCommandEmpty)
{
    Core::CommandHistory history;
    auto compound = Core::MakeCompoundCommand("Empty", {});

    // An empty compound command should still be valid and executable.
    EXPECT_TRUE(compound.IsValid());
    EXPECT_TRUE(history.Execute(std::move(compound)));
    EXPECT_TRUE(history.CanUndo());
    EXPECT_TRUE(history.Undo());
}
