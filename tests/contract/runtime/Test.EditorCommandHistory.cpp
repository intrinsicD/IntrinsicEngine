#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.SelectionController;

namespace Runtime = Extrinsic::Runtime;
namespace ECS = Extrinsic::ECS;
namespace ECSC = Extrinsic::ECS::Components;
namespace G = Extrinsic::Graphics::Components;

namespace
{
    [[nodiscard]] Runtime::EditorCommandRecord MakeValueCommand(
        int& value,
        const int next,
        const int previous,
        std::string label)
    {
        return Runtime::EditorCommandRecord{
            .Label = std::move(label),
            .Redo = [&value, next]()
            {
                value = next;
                return Runtime::EditorCommandHistoryStatus::Applied;
            },
            .Undo = [&value, previous]()
            {
                value = previous;
                return Runtime::EditorCommandHistoryStatus::Applied;
            },
            .Dirtying = true,
        };
    }

    [[nodiscard]] ECS::EntityHandle CreateEntity(ECS::Scene::Registry& registry)
    {
        return registry.Create();
    }
}

TEST(EditorCommandHistory, ExecuteRecordUndoRedoCapacityAndDirtyState)
{
    Runtime::EditorCommandHistory history{2u};
    int value = 0;

    auto first = history.Execute(MakeValueCommand(value, 1, 0, "First"));
    ASSERT_TRUE(first.Succeeded());
    EXPECT_EQ(first.Status, Runtime::EditorCommandHistoryStatus::Applied);
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.IsDirty());
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.Snapshot().UndoLabel, "First");

    history.MarkSaved("scene.extrinsic.json");
    EXPECT_FALSE(history.IsDirty());
    EXPECT_EQ(history.Snapshot().ActivePath, "scene.extrinsic.json");

    EXPECT_TRUE(history.Execute(MakeValueCommand(value, 2, 1, "Second")).Succeeded());
    EXPECT_TRUE(history.Execute(MakeValueCommand(value, 3, 2, "Third")).Succeeded());
    EXPECT_EQ(value, 3);
    EXPECT_EQ(history.UndoCount(), 2u);
    EXPECT_EQ(history.Snapshot().UndoLabel, "Third");

    auto undone = history.Undo();
    ASSERT_TRUE(undone.Succeeded());
    EXPECT_EQ(undone.Status, Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(history.CanRedo());
    EXPECT_EQ(history.Snapshot().RedoLabel, "Third");

    auto redone = history.Redo();
    ASSERT_TRUE(redone.Succeeded());
    EXPECT_EQ(redone.Status, Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_EQ(value, 3);

    history.ClearHistory();
    EXPECT_FALSE(history.CanUndo());
    EXPECT_TRUE(history.IsDirty());
    history.ResetDocument("loaded.extrinsic.json");
    EXPECT_FALSE(history.IsDirty());
    EXPECT_EQ(history.Snapshot().ActivePath, "loaded.extrinsic.json");
}

TEST(EditorCommandHistory, TransformAdapterAppliesUndoRedoAndRejectsStaleEntity)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle entity = CreateEntity(registry);
    ECSC::Transform::Component before{};
    before.Position = glm::vec3{0.0f, 0.0f, 0.0f};
    ECSC::Transform::Component after = before;
    after.Position = glm::vec3{4.0f, 5.0f, 6.0f};
    registry.Raw().emplace<ECSC::Transform::Component>(entity, before);

    Runtime::EditorCommandHistory history;
    const std::uint32_t stableId = Runtime::SelectionController::ToStableEntityId(entity);
    auto result = history.Execute(
        Runtime::MakeTransformEditCommand(
            Runtime::EditorTransformEditCommand{
                .Scene = &registry,
                .StableEntityId = stableId,
                .Before = before,
                .After = after,
                .Label = "Move Entity",
            }));
    ASSERT_TRUE(result.Succeeded());
    const auto* transform = registry.Raw().try_get<ECSC::Transform::Component>(entity);
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->Position, after.Position);
    EXPECT_TRUE(registry.Raw().all_of<ECSC::Transform::IsDirtyTag>(entity));

    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_EQ(registry.Raw().get<ECSC::Transform::Component>(entity).Position,
              before.Position);
    ASSERT_TRUE(history.Redo().Succeeded());
    EXPECT_EQ(registry.Raw().get<ECSC::Transform::Component>(entity).Position,
              after.Position);

    registry.Destroy(entity);
    EXPECT_EQ(history.Undo().Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
}

TEST(EditorCommandHistory, SelectionAdapterRestoresSingleSelectionWithoutDirtyingDocument)
{
    ECS::Scene::Registry registry;
    Runtime::SelectionController selection;
    const ECS::EntityHandle first = CreateEntity(registry);
    const ECS::EntityHandle second = CreateEntity(registry);
    ASSERT_TRUE(selection.SetSelectedEntity(registry, first));

    Runtime::EditorCommandHistory history;
    history.MarkSaved("scene.extrinsic.json");
    const auto result = history.Execute(
        Runtime::MakeSelectionReplaceCommand(
            Runtime::EditorSelectionReplaceCommand{
                .Scene = &registry,
                .Selection = &selection,
                .BeforeStableEntityId = Runtime::SelectionController::ToStableEntityId(first),
                .AfterStableEntityId = Runtime::SelectionController::ToStableEntityId(second),
                .Label = "Select Second",
            }));
    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(selection.SelectedStableIds().size(), 1u);
    EXPECT_EQ(selection.SelectedStableIds().front(),
              Runtime::SelectionController::ToStableEntityId(second));
    EXPECT_FALSE(history.IsDirty());

    ASSERT_TRUE(history.Undo().Succeeded());
    ASSERT_EQ(selection.SelectedStableIds().size(), 1u);
    EXPECT_EQ(selection.SelectedStableIds().front(),
              Runtime::SelectionController::ToStableEntityId(first));
}

TEST(EditorCommandHistory, VisualizationSpatialAndPrimitiveAdaptersAreReversible)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle entity = CreateEntity(registry);
    const std::uint32_t stableId = Runtime::SelectionController::ToStableEntityId(entity);
    Runtime::EditorCommandHistory history;

    G::VisualizationConfig visualization{};
    visualization.Source = G::VisualizationConfig::ColorSource::UniformColor;
    visualization.Color = glm::vec4{0.2f, 0.4f, 0.6f, 1.0f};
    ASSERT_TRUE(history.Execute(
        Runtime::MakeVisualizationConfigCommand(
            Runtime::EditorVisualizationConfigCommand{
                .Scene = &registry,
                .StableEntityId = stableId,
                .Before = std::nullopt,
                .After = visualization,
            })).Succeeded());
    EXPECT_TRUE(registry.Raw().all_of<G::VisualizationConfig>(entity));
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_FALSE(registry.Raw().all_of<G::VisualizationConfig>(entity));

    ECSC::SpatialDebugBinding binding{};
    binding.RegistryKey = 42u;
    ASSERT_TRUE(history.Execute(
        Runtime::MakeSpatialDebugBindingCommand(
            Runtime::EditorSpatialDebugBindingCommand{
                .Scene = &registry,
                .StableEntityId = stableId,
                .Before = std::nullopt,
                .After = binding,
            })).Succeeded());
    EXPECT_TRUE(registry.Raw().all_of<ECSC::SpatialDebugBinding>(entity));
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_FALSE(registry.Raw().all_of<ECSC::SpatialDebugBinding>(entity));

    Runtime::MeshPrimitiveViewSettings stored{};
    auto setSettings = [&stored](std::uint32_t, Runtime::MeshPrimitiveViewSettings next)
    {
        stored = next;
    };
    auto clearSettings = [&stored](std::uint32_t)
    {
        stored = Runtime::MeshPrimitiveViewSettings{};
    };
    Runtime::MeshPrimitiveViewSettings enabled{};
    enabled.EnableEdgeView = true;
    ASSERT_TRUE(history.Execute(
        Runtime::MakePrimitiveViewSettingsCommand(
            Runtime::EditorPrimitiveViewSettingsCommand{
                .StableEntityId = stableId,
                .Before = Runtime::MeshPrimitiveViewSettings{},
                .After = enabled,
                .SetSettings = setSettings,
                .ClearSettings = clearSettings,
            })).Succeeded());
    EXPECT_TRUE(stored.EnableEdgeView);
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_FALSE(stored.AnyEnabled());
}

TEST(EditorCommandHistory, CompoundCommandRollsBackAppliedCommandsOnFailure)
{
    std::vector<int> order;
    Runtime::EditorCommandRecord first{
        .Label = "First",
        .Redo = [&order]()
        {
            order.push_back(1);
            return Runtime::EditorCommandHistoryStatus::Applied;
        },
        .Undo = [&order]()
        {
            order.push_back(-1);
            return Runtime::EditorCommandHistoryStatus::Applied;
        },
    };
    Runtime::EditorCommandRecord failing{
        .Label = "Fail",
        .Redo = []()
        {
            return Runtime::EditorCommandHistoryStatus::CommandFailed;
        },
        .Undo = []()
        {
            return Runtime::EditorCommandHistoryStatus::Applied;
        },
    };

    Runtime::EditorCommandHistory history;
    auto failed = history.Execute(
        Runtime::MakeCompoundEditorCommand("Compound", {first, failing}));
    EXPECT_EQ(failed.Status, Runtime::EditorCommandHistoryStatus::CommandFailed);
    EXPECT_TRUE(order == std::vector<int>({1, -1}));
    EXPECT_FALSE(history.CanUndo());

    order.clear();
    Runtime::EditorCommandRecord second{
        .Label = "Second",
        .Redo = [&order]()
        {
            order.push_back(2);
            return Runtime::EditorCommandHistoryStatus::Applied;
        },
        .Undo = [&order]()
        {
            order.push_back(-2);
            return Runtime::EditorCommandHistoryStatus::Applied;
        },
    };
    ASSERT_TRUE(history.Execute(
        Runtime::MakeCompoundEditorCommand("Compound", {first, second})).Succeeded());
    EXPECT_TRUE(order == std::vector<int>({1, 2}));
    ASSERT_TRUE(history.Undo().Succeeded());
    EXPECT_TRUE(order == std::vector<int>({1, 2, -2, -1}));
}

TEST(EditorCommandHistory, HierarchyDeletePlanDefinesRecursiveDeleteAndOrphanPolicies)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle root = CreateEntity(registry);
    const ECS::EntityHandle child = CreateEntity(registry);
    const ECS::EntityHandle grandchild = CreateEntity(registry);
    const ECS::EntityHandle sibling = CreateEntity(registry);

    registry.Raw().emplace<ECSC::Hierarchy::Component>(
        root,
        ECSC::Hierarchy::Component{
            .FirstChild = child,
            .ChildCount = 2u,
        });
    registry.Raw().emplace<ECSC::Hierarchy::Component>(
        child,
        ECSC::Hierarchy::Component{
            .Parent = root,
            .FirstChild = grandchild,
            .NextSibling = sibling,
            .ChildCount = 1u,
        });
    registry.Raw().emplace<ECSC::Hierarchy::Component>(
        grandchild,
        ECSC::Hierarchy::Component{
            .Parent = child,
        });
    registry.Raw().emplace<ECSC::Hierarchy::Component>(
        sibling,
        ECSC::Hierarchy::Component{
            .Parent = root,
            .PrevSibling = child,
        });

    const std::uint32_t rootId = Runtime::SelectionController::ToStableEntityId(root);
    const std::uint32_t childId = Runtime::SelectionController::ToStableEntityId(child);
    const std::uint32_t grandchildId =
        Runtime::SelectionController::ToStableEntityId(grandchild);
    const std::uint32_t siblingId =
        Runtime::SelectionController::ToStableEntityId(sibling);

    const Runtime::EditorHierarchyDeletePlan recursive =
        Runtime::BuildHierarchyDeletePlan(
            registry,
            rootId,
            Runtime::EditorHierarchyDeletePolicy::DeleteDescendants);
    EXPECT_EQ(recursive.Status, Runtime::EditorCommandHistoryStatus::Applied);
    EXPECT_EQ(recursive.DeletedStableIds,
              std::vector<std::uint32_t>(
                  {rootId, childId, grandchildId, siblingId}));
    EXPECT_TRUE(recursive.OrphanedStableIds.empty());

    const Runtime::EditorHierarchyDeletePlan orphan =
        Runtime::BuildHierarchyDeletePlan(
            registry,
            rootId,
            Runtime::EditorHierarchyDeletePolicy::OrphanDescendants);
    EXPECT_EQ(orphan.DeletedStableIds, std::vector<std::uint32_t>({rootId}));
    EXPECT_EQ(orphan.OrphanedStableIds,
              std::vector<std::uint32_t>(
                  {childId, grandchildId, siblingId}));

    registry.Destroy(root);
    EXPECT_EQ(Runtime::BuildHierarchyDeletePlan(
                  registry,
                  rootId,
                  Runtime::EditorHierarchyDeletePolicy::DeleteDescendants).Status,
              Runtime::EditorCommandHistoryStatus::StaleEntity);
}

TEST(EditorCommandHistory, CorruptHierarchyDeletePlanFailsWithoutPartialMutation)
{
    ECS::Scene::Registry registry;
    const ECS::EntityHandle root = CreateEntity(registry);
    const ECS::EntityHandle child = CreateEntity(registry);
    const ECS::EntityHandle danglingSibling = CreateEntity(registry);
    registry.Destroy(danglingSibling);

    registry.Raw().emplace<ECSC::Hierarchy::Component>(
        root,
        ECSC::Hierarchy::Component{
            .FirstChild = child,
            .ChildCount = 2u,
        });
    registry.Raw().emplace<ECSC::Hierarchy::Component>(
        child,
        ECSC::Hierarchy::Component{
            .Parent = root,
            .NextSibling = danglingSibling,
        });

    const ECSC::Hierarchy::Component rootBefore =
        registry.Raw().get<ECSC::Hierarchy::Component>(root);
    const ECSC::Hierarchy::Component childBefore =
        registry.Raw().get<ECSC::Hierarchy::Component>(child);
    const std::uint32_t rootId =
        Runtime::SelectionController::ToStableEntityId(root);

    for (const Runtime::EditorHierarchyDeletePolicy policy :
         {Runtime::EditorHierarchyDeletePolicy::DeleteDescendants,
          Runtime::EditorHierarchyDeletePolicy::OrphanDescendants})
    {
        const Runtime::EditorHierarchyDeletePlan plan =
            Runtime::BuildHierarchyDeletePlan(registry, rootId, policy);
        EXPECT_EQ(plan.Status, Runtime::EditorCommandHistoryStatus::CommandFailed);
        EXPECT_TRUE(plan.DeletedStableIds.empty());
        EXPECT_TRUE(plan.OrphanedStableIds.empty());
    }

    EXPECT_TRUE(registry.Raw().valid(root));
    EXPECT_TRUE(registry.Raw().valid(child));
    const ECSC::Hierarchy::Component& rootAfter =
        registry.Raw().get<ECSC::Hierarchy::Component>(root);
    const ECSC::Hierarchy::Component& childAfter =
        registry.Raw().get<ECSC::Hierarchy::Component>(child);
    EXPECT_EQ(rootAfter.FirstChild, rootBefore.FirstChild);
    EXPECT_EQ(rootAfter.ChildCount, rootBefore.ChildCount);
    EXPECT_EQ(childAfter.Parent, childBefore.Parent);
    EXPECT_EQ(childAfter.NextSibling, childBefore.NextSibling);
}
