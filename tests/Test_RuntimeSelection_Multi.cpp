#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>

import ECS;
import Runtime.Selection;

namespace
{
    struct DummyScene
    {
        ECS::Scene Inner;
    };
}

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
