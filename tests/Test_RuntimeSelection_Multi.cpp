#include <gtest/gtest.h>

#include <vector>
#include <entt/entity/registry.hpp>

import ECS;
import Runtime.Selection;
import Runtime.SelectionModule;

TEST(RuntimeSelection, SingleClick_Add_DoesNotDeselectOthers)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);

    // Pretend A already selected.
    reg.emplace<ECS::Components::Selection::SelectedTag>(a);

    // Single click on B => Add (no deselection)
    Runtime::Selection::ApplySelection(scene, b, Runtime::Selection::PickMode::Add);

    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
}

TEST(RuntimeSelection, ShiftClick_Toggle_DeselectsWhenAlreadySelected)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectedTag>(a);

    // Shift-click => Toggle => deselect
    Runtime::Selection::ApplySelection(scene, a, Runtime::Selection::PickMode::Toggle);

    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
}

TEST(RuntimeSelection, BackgroundClick_Replace_ClearsAll)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);

    reg.emplace<ECS::Components::Selection::SelectedTag>(a);
    reg.emplace<ECS::Components::Selection::SelectedTag>(b);

    Runtime::Selection::ApplySelection(scene, entt::null, Runtime::Selection::PickMode::Replace);

    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
}

// --- Multi-entity selection tests (F4: Hierarchy Multi-Select) ---

TEST(RuntimeSelection, CtrlClick_Toggle_AddsSecondEntity)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");
    const entt::entity c = scene.CreateEntity("C");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);
    reg.emplace<ECS::Components::Selection::SelectableTag>(c);

    // Select A via Replace.
    Runtime::Selection::ApplySelection(scene, a, Runtime::Selection::PickMode::Replace);
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));

    // Ctrl+click B => Toggle (adds B, keeps A).
    Runtime::Selection::ApplySelection(scene, b, Runtime::Selection::PickMode::Toggle);
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(c));
}

TEST(RuntimeSelection, CtrlClick_Toggle_RemovesFromMultiSelection)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);

    // Both selected.
    reg.emplace<ECS::Components::Selection::SelectedTag>(a);
    reg.emplace<ECS::Components::Selection::SelectedTag>(b);

    // Ctrl+click A => toggles A off, B stays.
    Runtime::Selection::ApplySelection(scene, a, Runtime::Selection::PickMode::Toggle);
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
}

TEST(RuntimeSelection, PlainClick_Replace_ClearsMultiSelection)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");
    const entt::entity c = scene.CreateEntity("C");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);
    reg.emplace<ECS::Components::Selection::SelectableTag>(c);

    // A and B selected.
    reg.emplace<ECS::Components::Selection::SelectedTag>(a);
    reg.emplace<ECS::Components::Selection::SelectedTag>(b);

    // Plain click on C => Replace clears A and B, selects C.
    Runtime::Selection::ApplySelection(scene, c, Runtime::Selection::PickMode::Replace);
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(c));
}

TEST(RuntimeSelection, RangeSelect_ReplaceThenAdd_SelectsContiguousRange)
{
    // Simulates the Shift+click range selection pattern:
    // Replace first entity, then Add the rest of the range.
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");
    const entt::entity c = scene.CreateEntity("C");
    const entt::entity d = scene.CreateEntity("D");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);
    reg.emplace<ECS::Components::Selection::SelectableTag>(c);
    reg.emplace<ECS::Components::Selection::SelectableTag>(d);

    // Simulate range select B-D: Replace B, then Add C, Add D.
    Runtime::Selection::ApplySelection(scene, b, Runtime::Selection::PickMode::Replace);
    Runtime::Selection::ApplySelection(scene, c, Runtime::Selection::PickMode::Add);
    Runtime::Selection::ApplySelection(scene, d, Runtime::Selection::PickMode::Add);

    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(c));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(d));
}

TEST(RuntimeSelectionModule, GetSelectedEntities_ReturnsAllSelected)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    const entt::entity b = scene.CreateEntity("B");
    const entt::entity c = scene.CreateEntity("C");

    reg.emplace<ECS::Components::Selection::SelectableTag>(a);
    reg.emplace<ECS::Components::Selection::SelectableTag>(b);
    reg.emplace<ECS::Components::Selection::SelectableTag>(c);

    reg.emplace<ECS::Components::Selection::SelectedTag>(a);
    reg.emplace<ECS::Components::Selection::SelectedTag>(c);

    Runtime::SelectionModule module;
    auto selected = module.GetSelectedEntities(scene);

    ASSERT_EQ(selected.size(), 2u);
    // Both a and c should be present (order may vary).
    bool hasA = std::find(selected.begin(), selected.end(), a) != selected.end();
    bool hasC = std::find(selected.begin(), selected.end(), c) != selected.end();
    EXPECT_TRUE(hasA);
    EXPECT_TRUE(hasC);
}

TEST(RuntimeSelectionModule, GetSelectedEntities_EmptyWhenNoneSelected)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity a = scene.CreateEntity("A");
    reg.emplace<ECS::Components::Selection::SelectableTag>(a);

    Runtime::SelectionModule module;
    auto selected = module.GetSelectedEntities(scene);

    EXPECT_TRUE(selected.empty());
}
