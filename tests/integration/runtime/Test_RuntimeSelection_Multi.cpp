#include <gtest/gtest.h>

#include <algorithm>
#include <ostream>
#include <vector>
#include <entt/entity/registry.hpp>

import ECS;
import Runtime.Selection;
import Runtime.SelectionModule;

namespace
{
    struct SelectionModeCase
    {
        const char* Name{};
        std::size_t EntityCount{};
        std::vector<std::size_t> InitiallySelected{};
        std::size_t TargetIndex{};
        bool TargetBackground{};
        Runtime::Selection::PickMode Mode{};
        std::vector<std::size_t> ExpectedSelected{};
    };

    class RuntimeSelectionModeParameterized : public ::testing::TestWithParam<SelectionModeCase>
    {
    };

    [[nodiscard]] bool ContainsIndex(const std::vector<std::size_t>& indices, std::size_t index)
    {
        return std::find(indices.begin(), indices.end(), index) != indices.end();
    }

    void PrintTo(const SelectionModeCase& testCase, std::ostream* os)
    {
        *os << testCase.Name;
    }
}

TEST_P(RuntimeSelectionModeParameterized, ApplySelectionUpdatesSelectionState)
{
    const auto& testCase = GetParam();
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    std::vector<entt::entity> entities;
    entities.reserve(testCase.EntityCount);
    for (std::size_t i = 0; i < testCase.EntityCount; ++i)
    {
        const entt::entity entity = scene.CreateEntity("Selectable");
        reg.emplace<ECS::Components::Selection::SelectableTag>(entity);
        entities.push_back(entity);
    }

    for (const std::size_t index : testCase.InitiallySelected)
        reg.emplace<ECS::Components::Selection::SelectedTag>(entities[index]);

    const entt::entity target = testCase.TargetBackground ? entt::null : entities[testCase.TargetIndex];
    Runtime::Selection::ApplySelection(scene, target, testCase.Mode);

    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        EXPECT_EQ(reg.all_of<ECS::Components::Selection::SelectedTag>(entities[i]),
                  ContainsIndex(testCase.ExpectedSelected, i))
            << "case=" << testCase.Name << " entityIndex=" << i;
    }
}

INSTANTIATE_TEST_SUITE_P(
    RuntimeSelection,
    RuntimeSelectionModeParameterized,
    ::testing::Values(
        SelectionModeCase{
            .Name = "SingleClick_Add_DoesNotDeselectOthers",
            .EntityCount = 2u,
            .InitiallySelected = {0u},
            .TargetIndex = 1u,
            .TargetBackground = false,
            .Mode = Runtime::Selection::PickMode::Add,
            .ExpectedSelected = {0u, 1u}},
        SelectionModeCase{
            .Name = "ShiftClick_Toggle_DeselectsWhenAlreadySelected",
            .EntityCount = 1u,
            .InitiallySelected = {0u},
            .TargetIndex = 0u,
            .TargetBackground = false,
            .Mode = Runtime::Selection::PickMode::Toggle,
            .ExpectedSelected = {}},
        SelectionModeCase{
            .Name = "BackgroundClick_Replace_ClearsAll",
            .EntityCount = 2u,
            .InitiallySelected = {0u, 1u},
            .TargetIndex = 0u,
            .TargetBackground = true,
            .Mode = Runtime::Selection::PickMode::Replace,
            .ExpectedSelected = {}},
        SelectionModeCase{
            .Name = "CtrlClick_Toggle_AddsSecondEntity",
            .EntityCount = 3u,
            .InitiallySelected = {0u},
            .TargetIndex = 1u,
            .TargetBackground = false,
            .Mode = Runtime::Selection::PickMode::Toggle,
            .ExpectedSelected = {0u, 1u}},
        SelectionModeCase{
            .Name = "CtrlClick_Toggle_RemovesFromMultiSelection",
            .EntityCount = 2u,
            .InitiallySelected = {0u, 1u},
            .TargetIndex = 0u,
            .TargetBackground = false,
            .Mode = Runtime::Selection::PickMode::Toggle,
            .ExpectedSelected = {1u}},
        SelectionModeCase{
            .Name = "PlainClick_Replace_ClearsMultiSelection",
            .EntityCount = 3u,
            .InitiallySelected = {0u, 1u},
            .TargetIndex = 2u,
            .TargetBackground = false,
            .Mode = Runtime::Selection::PickMode::Replace,
            .ExpectedSelected = {2u}}),
    [](const ::testing::TestParamInfo<SelectionModeCase>& info)
    {
        return info.param.Name;
    });

// --- Multi-entity selection tests (F4: Hierarchy Multi-Select) ---


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
