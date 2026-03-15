// Tests for sub-element selection mode (vertex/edge/face selection).
// Validates SubElementSelection state management and SelectionModule
// element-mode integration.

#include <gtest/gtest.h>
#include <set>
#include <entt/entity/registry.hpp>

import ECS;
import Runtime.Selection;
import Runtime.SelectionModule;

namespace
{
    [[nodiscard]] bool IsNullEntity(entt::entity entity)
    {
        return entity == entt::null;
    }
}

// =========================================================================
// SubElementSelection unit tests
// =========================================================================

TEST(ElementSelection, SubElementSelection_DefaultIsEmpty)
{
    Runtime::Selection::SubElementSelection sub;
    EXPECT_TRUE(sub.Empty());
    EXPECT_TRUE(IsNullEntity(sub.Entity));
}

TEST(ElementSelection, SubElementSelection_ClearResetsAll)
{
    Runtime::Selection::SubElementSelection sub;
    sub.Entity = static_cast<entt::entity>(42);
    sub.SelectedVertices.insert(0);
    sub.SelectedEdges.insert(1);
    sub.SelectedFaces.insert(2);

    EXPECT_FALSE(sub.Empty());

    sub.Clear();
    EXPECT_TRUE(sub.Empty());
    EXPECT_TRUE(IsNullEntity(sub.Entity));
}

TEST(ElementSelection, SubElementSelection_EmptyChecksAllSets)
{
    Runtime::Selection::SubElementSelection sub;

    sub.SelectedVertices.insert(5);
    EXPECT_FALSE(sub.Empty());

    sub.SelectedVertices.clear();
    sub.SelectedEdges.insert(3);
    EXPECT_FALSE(sub.Empty());

    sub.SelectedEdges.clear();
    sub.SelectedFaces.insert(7);
    EXPECT_FALSE(sub.Empty());

    sub.SelectedFaces.clear();
    EXPECT_TRUE(sub.Empty());
}

// =========================================================================
// ElementMode enum tests
// =========================================================================

TEST(ElementSelection, ElementMode_HasAllFourValues)
{
    EXPECT_EQ(static_cast<int>(Runtime::Selection::ElementMode::Entity), 0);
    EXPECT_EQ(static_cast<int>(Runtime::Selection::ElementMode::Vertex), 1);
    EXPECT_EQ(static_cast<int>(Runtime::Selection::ElementMode::Edge), 2);
    EXPECT_EQ(static_cast<int>(Runtime::Selection::ElementMode::Face), 3);
}

// =========================================================================
// SelectionModule sub-element integration
// =========================================================================

TEST(ElementSelection, SelectionModule_DefaultIsEntityMode)
{
    Runtime::SelectionModule module;
    EXPECT_EQ(module.GetConfig().ElementMode, Runtime::Selection::ElementMode::Entity);
    EXPECT_TRUE(module.GetSubElementSelection().Empty());
}

TEST(ElementSelection, SelectionModule_ClearSubElementSelection)
{
    Runtime::SelectionModule module;
    auto& sub = module.GetSubElementSelection();
    sub.Entity = static_cast<entt::entity>(1);
    sub.SelectedVertices.insert(0);
    sub.SelectedVertices.insert(1);

    EXPECT_FALSE(sub.Empty());

    module.ClearSubElementSelection();
    EXPECT_TRUE(sub.Empty());
    EXPECT_TRUE(IsNullEntity(sub.Entity));
}

TEST(ElementSelection, SelectionModule_ClearSelectionAlsoClearsSubElements)
{
    Runtime::SelectionModule module;
    ECS::Scene scene;

    module.ConnectToScene(scene);

    auto& sub = module.GetSubElementSelection();
    sub.Entity = static_cast<entt::entity>(1);
    sub.SelectedVertices.insert(5);

    module.ClearSelection(scene);
    EXPECT_TRUE(sub.Empty());
}

TEST(ElementSelection, SubElementSelection_MultipleVertices)
{
    Runtime::Selection::SubElementSelection sub;
    sub.Entity = static_cast<entt::entity>(10);

    sub.SelectedVertices.insert(0);
    sub.SelectedVertices.insert(5);
    sub.SelectedVertices.insert(12);

    EXPECT_EQ(sub.SelectedVertices.size(), 3u);
    EXPECT_TRUE(sub.SelectedVertices.count(0));
    EXPECT_TRUE(sub.SelectedVertices.count(5));
    EXPECT_TRUE(sub.SelectedVertices.count(12));
    EXPECT_FALSE(sub.SelectedVertices.count(3));
}

TEST(ElementSelection, SubElementSelection_ToggleVertex)
{
    Runtime::Selection::SubElementSelection sub;
    sub.Entity = static_cast<entt::entity>(10);

    // Add vertex
    sub.SelectedVertices.insert(5);
    EXPECT_EQ(sub.SelectedVertices.size(), 1u);

    // Toggle same vertex removes it
    if (auto it = sub.SelectedVertices.find(5); it != sub.SelectedVertices.end())
        sub.SelectedVertices.erase(it);
    else
        sub.SelectedVertices.insert(5);

    EXPECT_TRUE(sub.SelectedVertices.empty());

    // Toggle again adds it back
    if (auto it = sub.SelectedVertices.find(5); it != sub.SelectedVertices.end())
        sub.SelectedVertices.erase(it);
    else
        sub.SelectedVertices.insert(5);

    EXPECT_EQ(sub.SelectedVertices.size(), 1u);
    EXPECT_TRUE(sub.SelectedVertices.count(5));
}
