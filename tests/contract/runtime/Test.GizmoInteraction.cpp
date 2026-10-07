// RUNTIME-084 / UI-078 — contract coverage for the runtime transform-gizmo
// matrix drag session: world pivot, basis, hierarchy write rule, atomic TRS
// rejection, undo/redo of translate/rotate/scale batches. Snap and hit testing
// belong to the editor frontend (SandboxEditorGizmo).

#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.WorldHandle;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::GizmoBasisFallback;
using Extrinsic::Runtime::GizmoFrame;
using Extrinsic::Runtime::GizmoPivotMode;
using Extrinsic::Runtime::GizmoStatus;
using Extrinsic::Runtime::GizmoInteraction;
using Extrinsic::Runtime::GizmoMode;
using Extrinsic::Runtime::GizmoOrientation;

namespace Tf = Extrinsic::ECS::Components::Transform;
namespace Hi = Extrinsic::ECS::Components::Hierarchy;
namespace CullLocal = Extrinsic::ECS::Components::Culling::Local;
using Extrinsic::Runtime::EditorCommandHistory;
using Extrinsic::Runtime::EditorCommandHistoryStatus;

namespace
{
    EntityHandle MakeEntity(Registry& registry, const glm::vec3 position,
                            const glm::quat rotation = glm::quat{1.f, 0.f, 0.f, 0.f})
    {
        const EntityHandle entity = registry.Create();
        registry.Raw().emplace<Tf::Component>(entity, Tf::Component{
            .Position = position,
            .Rotation = rotation,
            .Scale = glm::vec3{1.f},
        });
        return entity;
    }
}

// --- Undo/redo per mode (ported from the retired ray adapter) ------------

TEST(GizmoInteraction, TranslateRotateScaleDragsCommitOneUndoableRedoableBatch)
{
    struct Case
    {
        GizmoMode Mode;
        glm::mat4 Delta;
        Tf::Component Expected;
    };
    const Case cases[] = {
        {GizmoMode::Translate, glm::translate(glm::mat4{1.f}, glm::vec3{3.f, 0.f, 0.f}),
         {.Position = {3.f, 0.f, 0.f}}},
        {GizmoMode::Rotate, glm::mat4_cast(glm::angleAxis(1.5f, glm::vec3{1.f, 0.f, 0.f})),
         {.Rotation = glm::angleAxis(1.5f, glm::vec3{1.f, 0.f, 0.f})}},
        {GizmoMode::Scale, glm::scale(glm::mat4{1.f}, glm::vec3{2.f, 1.f, 1.f}),
         {.Scale = {2.f, 1.f, 1.f}}},
    };
    for (const Case& c : cases)
    {
        Registry registry{};
        const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
        const EntityHandle selected[] = {entity};
        GizmoInteraction gizmo{};
        ASSERT_TRUE(gizmo.Begin(registry, selected, c.Mode, GizmoOrientation::Global,
                                GizmoPivotMode::WorldOrigins).Succeeded());
        EXPECT_EQ(gizmo.SessionMode(), c.Mode);
        ASSERT_TRUE(gizmo.Preview(registry, c.Delta * gizmo.SessionFrame().Matrix).Succeeded());
        const auto& preview = registry.Raw().get<Tf::Component>(entity);
        EXPECT_TRUE((registry.Raw().all_of<Tf::IsDirtyTag>(entity)));

        EditorCommandHistory history;
        ASSERT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
                  EditorCommandHistoryStatus::Applied);
        EXPECT_FALSE(gizmo.IsDragging());
        ASSERT_EQ(history.UndoCount(), 1u);
        EXPECT_EQ(history.Snapshot().UndoLabel, "Manipulate Transform");
        const auto expectNear = [&](const Tf::Component& expected)
        {
            for (int i = 0; i < 3; ++i)
            {
                EXPECT_NEAR(preview.Position[i], expected.Position[i], 1.0e-4f);
                EXPECT_NEAR(preview.Scale[i], expected.Scale[i], 1.0e-4f);
            }
            EXPECT_NEAR(std::abs(glm::dot(preview.Rotation, expected.Rotation)), 1.f, 1.0e-4f);
        };
        expectNear(c.Expected);
        ASSERT_EQ(history.Undo().Status, EditorCommandHistoryStatus::Undone);
        expectNear(Tf::Component{});
        ASSERT_EQ(history.Redo().Status, EditorCommandHistoryStatus::Redone);
        expectNear(c.Expected);
    }
}

TEST(GizmoInteraction, GroupCommitUndoesAsOneBatchAndRefusesInterveningState)
{
    Registry registry{};
    const EntityHandle first = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle second = MakeEntity(registry, glm::vec3{10.f, 2.f, 0.f});
    const EntityHandle selected[] = {first, second};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    ASSERT_TRUE(gizmo.Preview(registry, glm::translate(glm::mat4{1.f}, glm::vec3{3.f, 0.f, 0.f}) *
                                            gizmo.SessionFrame().Matrix).Succeeded());
    EditorCommandHistory history;
    ASSERT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
              EditorCommandHistoryStatus::Applied);
    ASSERT_EQ(history.UndoCount(), 1u);
    EXPECT_NEAR(registry.Raw().get<Tf::Component>(first).Position.x, 3.f, 1.0e-4f);
    EXPECT_NEAR(registry.Raw().get<Tf::Component>(second).Position.x, 13.f, 1.0e-4f);

    ASSERT_EQ(history.Undo().Status, EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(registry.Raw().get<Tf::Component>(first).Position, glm::vec3(0.f));
    EXPECT_EQ(registry.Raw().get<Tf::Component>(second).Position, glm::vec3(10.f, 2.f, 0.f));

    // A foreign edit after redo makes the whole batch stale: no partial undo.
    ASSERT_EQ(history.Redo().Status, EditorCommandHistoryStatus::Redone);
    registry.Raw().get<Tf::Component>(first).Position.x = 99.f;
    const Tf::Component secondBefore = registry.Raw().get<Tf::Component>(second);
    EXPECT_EQ(history.Undo().Status, EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FLOAT_EQ(registry.Raw().get<Tf::Component>(first).Position.x, 99.f);
    EXPECT_EQ(registry.Raw().get<Tf::Component>(second).Position, secondBefore.Position);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
}

// --- Matrix session core (UI-078 slice 1) -----------------------------------

namespace
{
    constexpr float kTol = 1.0e-4f;

    glm::quat Rz(const float degrees)
    {
        return glm::angleAxis(glm::radians(degrees), glm::vec3{0.f, 0.f, 1.f});
    }

    void SetParent(Registry& registry, const EntityHandle child, const EntityHandle parent)
    {
        registry.Raw().emplace_or_replace<Hi::Component>(child, Hi::Component{.Parent = parent});
    }

    void ExpectVecNear(const glm::vec3 actual, const glm::vec3 expected, const float tol = kTol)
    {
        EXPECT_NEAR(actual.x, expected.x, tol);
        EXPECT_NEAR(actual.y, expected.y, tol);
        EXPECT_NEAR(actual.z, expected.z, tol);
    }

    // Same rotation regardless of quaternion sign.
    void ExpectSameRotation(const glm::quat actual, const glm::quat expected)
    {
        EXPECT_NEAR(std::abs(glm::dot(glm::normalize(actual), glm::normalize(expected))), 1.f, kTol);
    }

    void ExpectSameTransform(const Tf::Component& actual, const Tf::Component& expected)
    {
        EXPECT_EQ(actual.Position, expected.Position);
        EXPECT_EQ(actual.Rotation, expected.Rotation);
        EXPECT_EQ(actual.Scale, expected.Scale);
    }

    glm::mat4 Translation(const glm::vec3 v) { return glm::translate(glm::mat4{1.f}, v); }

    void ClearDirty(Registry& registry)
    {
        registry.Raw().clear<Tf::IsDirtyTag>();
    }
}

TEST(GizmoInteraction, WorldOriginPivotComposesParentChainAndIgnoresStaleWorldCache)
{
    Registry registry{};
    const EntityHandle parent = MakeEntity(registry, glm::vec3{10.f, 0.f, 0.f});
    const EntityHandle child = MakeEntity(registry, glm::vec3{2.f, 0.f, 0.f});
    SetParent(registry, child, parent);
    const EntityHandle other = MakeEntity(registry, glm::vec3{4.f, 0.f, 0.f});
    // A deliberately wrong cache: the gizmo must not read it.
    registry.Raw().emplace<Tf::WorldMatrix>(child, Tf::WorldMatrix{Translation({-100.f, 7.f, 3.f})});

    const EntityHandle selected[] = {child, other};
    const GizmoInteraction gizmo{};
    const GizmoFrame frame =
        gizmo.ComputeFrame(registry, selected, GizmoOrientation::Global, GizmoPivotMode::WorldOrigins);
    ASSERT_TRUE(frame.Available());
    ExpectVecNear(frame.Pivot, {8.f, 0.f, 0.f});
    ExpectVecNear(glm::vec3{frame.Matrix[3]}, {8.f, 0.f, 0.f});
}

TEST(GizmoInteraction, BoundsPivotAveragesWorldBoundsCentersWithOriginFallback)
{
    Registry registry{};
    const EntityHandle bounded = MakeEntity(registry, glm::vec3{0.f});
    registry.Raw().emplace<CullLocal::Bounds>(
        bounded, CullLocal::Bounds{.LocalBoundingAABB = {.Min = {1.f, -1.f, -1.f}, .Max = {3.f, 1.f, 1.f}}});
    const EntityHandle unbounded = MakeEntity(registry, glm::vec3{10.f, 0.f, 0.f});
    const GizmoInteraction gizmo{};

    const EntityHandle pair[] = {bounded, unbounded};
    GizmoFrame frame =
        gizmo.ComputeFrame(registry, pair, GizmoOrientation::Global, GizmoPivotMode::BoundsCenters);
    ASSERT_TRUE(frame.Available());
    ExpectVecNear(frame.Pivot, {6.f, 0.f, 0.f});
    frame = gizmo.ComputeFrame(registry, pair, GizmoOrientation::Global, GizmoPivotMode::WorldOrigins);
    ExpectVecNear(frame.Pivot, {5.f, 0.f, 0.f});

    // Default (invalid) bounds fall back to the origin.
    const EntityHandle invalidBounds = MakeEntity(registry, glm::vec3{0.f, 4.f, 0.f});
    registry.Raw().emplace<CullLocal::Bounds>(invalidBounds);
    const EntityHandle invalidOnly[] = {invalidBounds};
    frame = gizmo.ComputeFrame(registry, invalidOnly, GizmoOrientation::Global, GizmoPivotMode::BoundsCenters);
    ExpectVecNear(frame.Pivot, {0.f, 4.f, 0.f});

    // Bounds go through the fresh world matrix, including a rotated parent.
    const EntityHandle parent = MakeEntity(registry, glm::vec3{0.f, 5.f, 0.f}, Rz(90.f));
    const EntityHandle child = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, child, parent);
    registry.Raw().emplace<CullLocal::Bounds>(
        child, CullLocal::Bounds{.LocalBoundingAABB = {.Min = {0.5f, -0.5f, -0.5f}, .Max = {1.5f, 0.5f, 0.5f}}});
    const EntityHandle childOnly[] = {child};
    frame = gizmo.ComputeFrame(registry, childOnly, GizmoOrientation::Global, GizmoPivotMode::BoundsCenters);
    ExpectVecNear(frame.Pivot, {0.f, 6.f, 0.f});
}

TEST(GizmoInteraction, GroupRotateAndScaleMoveOriginsAroundPivot)
{
    Registry registry{};
    const EntityHandle right = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle left = MakeEntity(registry, glm::vec3{-1.f, 0.f, 0.f});
    const EntityHandle selected[] = {right, left};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;

    ASSERT_TRUE(gizmo.Preview(registry, glm::mat4_cast(Rz(90.f)) * g0).Succeeded());
    ExpectVecNear(registry.Raw().get<Tf::Component>(right).Position, {0.f, 1.f, 0.f});
    ExpectVecNear(registry.Raw().get<Tf::Component>(left).Position, {0.f, -1.f, 0.f});
    ExpectSameRotation(registry.Raw().get<Tf::Component>(right).Rotation, Rz(90.f));
    ExpectSameRotation(registry.Raw().get<Tf::Component>(left).Rotation, Rz(90.f));

    // Every tick starts from the frozen start state, not the last preview.
    ASSERT_TRUE(gizmo.Preview(registry, g0 * glm::scale(glm::mat4{1.f}, {2.f, 1.f, 1.f})).Succeeded());
    ExpectVecNear(registry.Raw().get<Tf::Component>(right).Position, {2.f, 0.f, 0.f});
    ExpectVecNear(registry.Raw().get<Tf::Component>(left).Position, {-2.f, 0.f, 0.f});
    ExpectVecNear(registry.Raw().get<Tf::Component>(right).Scale, {2.f, 1.f, 1.f});
    ExpectSameRotation(registry.Raw().get<Tf::Component>(left).Rotation, glm::quat{1.f, 0.f, 0.f, 0.f});
}

TEST(GizmoInteraction, SelectedDescendantsMoveOnlyThroughSelectedAncestor)
{
    Registry registry{};
    const EntityHandle parent = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle child = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle middle = MakeEntity(registry, glm::vec3{0.f, 1.f, 0.f});
    const EntityHandle grandchild = MakeEntity(registry, glm::vec3{0.f, 0.f, 1.f});
    const EntityHandle unselectedChild = MakeEntity(registry, glm::vec3{0.f, 2.f, 0.f});
    SetParent(registry, child, parent);
    SetParent(registry, middle, parent);
    SetParent(registry, grandchild, middle);
    SetParent(registry, unselectedChild, parent);

    const Tf::Component childBefore = registry.Raw().get<Tf::Component>(child);
    const Tf::Component middleBefore = registry.Raw().get<Tf::Component>(middle);
    const Tf::Component grandchildBefore = registry.Raw().get<Tf::Component>(grandchild);
    const Tf::Component unselectedBefore = registry.Raw().get<Tf::Component>(unselectedChild);

    const EntityHandle selected[] = {grandchild, child, parent};
    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 5.f, 0.f}) * g0).Succeeded());
    ASSERT_TRUE(gizmo.Preview(registry, glm::mat4_cast(Rz(30.f)) * g0).Succeeded());

    EXPECT_TRUE(registry.Raw().all_of<Tf::IsDirtyTag>(parent));
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(child));
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(grandchild));
    ExpectSameTransform(registry.Raw().get<Tf::Component>(child), childBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(middle), middleBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(grandchild), grandchildBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(unselectedChild), unselectedBefore);
    ExpectSameRotation(registry.Raw().get<Tf::Component>(parent).Rotation, Rz(30.f));
}

TEST(GizmoInteraction, DuplicateAndPermutedSelectionsProduceIdenticalSessions)
{
    const std::vector<std::vector<int>> orders{{0, 1, 2}, {2, 0, 1}, {1, 1, 2, 0, 2}};
    std::vector<Tf::Component> reference{};
    for (const std::vector<int>& order : orders)
    {
        Registry registry{};
        const EntityHandle entities[] = {
            MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f}, Rz(10.f)),
            MakeEntity(registry, glm::vec3{0.f, 3.f, 0.f}, Rz(40.f)),
            MakeEntity(registry, glm::vec3{0.f, 0.f, -2.f}, Rz(70.f)),
        };
        std::vector<EntityHandle> selected{};
        for (const int index : order)
            selected.push_back(entities[index]);

        GizmoInteraction gizmo{};
        ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Local,
                                GizmoPivotMode::WorldOrigins).Succeeded());
        EXPECT_EQ(gizmo.SessionFrame().Primary, entities[0]);
        const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
        ASSERT_TRUE(gizmo.Preview(registry, Translation({1.f, 2.f, 3.f}) * glm::mat4_cast(Rz(25.f)) * g0)
                        .Succeeded());
        EditorCommandHistory history;
        ASSERT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
                  EditorCommandHistoryStatus::Applied);
        EXPECT_EQ(history.UndoCount(), 1u);

        std::vector<Tf::Component> result{};
        for (const EntityHandle entity : entities)
            result.push_back(registry.Raw().get<Tf::Component>(entity));
        if (reference.empty())
        {
            reference = result;
            continue;
        }
        for (std::size_t i = 0; i < result.size(); ++i)
            ExpectSameTransform(result[i], reference[i]);
    }
}

TEST(GizmoInteraction, LocalBasisUsesWorldRotationAndSignInvariantGroupMean)
{
    Registry registry{};
    const GizmoInteraction gizmo{};

    const EntityHandle parent = MakeEntity(registry, glm::vec3{0.f}, Rz(90.f));
    const EntityHandle child = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    SetParent(registry, child, parent);
    const EntityHandle childOnly[] = {child};
    GizmoFrame frame =
        gizmo.ComputeFrame(registry, childOnly, GizmoOrientation::Local, GizmoPivotMode::WorldOrigins);
    ASSERT_TRUE(frame.Available());
    EXPECT_EQ(frame.ActualOrientation, GizmoOrientation::Local);
    ExpectVecNear(frame.Basis[0], {0.f, 1.f, 0.f});
    ExpectVecNear(frame.Pivot, {0.f, 1.f, 0.f});

    const EntityHandle a = MakeEntity(registry, glm::vec3{0.f}, Rz(0.f));
    const EntityHandle b = MakeEntity(registry, glm::vec3{0.f}, Rz(90.f));
    const EntityHandle bNegated = MakeEntity(registry, glm::vec3{0.f}, -Rz(90.f));
    const float c45 = std::cos(glm::radians(45.f));
    for (const EntityHandle second : {b, bNegated})
    {
        const EntityHandle group[] = {a, second};
        frame = gizmo.ComputeFrame(registry, group, GizmoOrientation::Local, GizmoPivotMode::WorldOrigins);
        ASSERT_TRUE(frame.Available());
        EXPECT_EQ(frame.ActualOrientation, GizmoOrientation::Local);
        EXPECT_EQ(frame.BasisFallback, GizmoBasisFallback::None);
        ExpectVecNear(frame.Basis[0], {c45, c45, 0.f});
    }
}

TEST(GizmoInteraction, DegenerateGroupMeanFallsBackToWorldBasisWithReason)
{
    Registry registry{};
    const EntityHandle a = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle b = MakeEntity(registry, glm::vec3{2.f, 0.f, 0.f},
                                      glm::angleAxis(3.14159265f, glm::vec3{1.f, 0.f, 0.f}));
    const EntityHandle group[] = {a, b};
    const GizmoInteraction gizmo{};
    const GizmoFrame frame =
        gizmo.ComputeFrame(registry, group, GizmoOrientation::Local, GizmoPivotMode::WorldOrigins);
    ASSERT_TRUE(frame.Available());
    EXPECT_EQ(frame.RequestedOrientation, GizmoOrientation::Local);
    EXPECT_EQ(frame.ActualOrientation, GizmoOrientation::Global);
    EXPECT_EQ(frame.BasisFallback, GizmoBasisFallback::MeanDegenerate);
    EXPECT_EQ(frame.Basis, glm::mat3{1.f});
}

TEST(GizmoInteraction, ShearResultIsRejectedWithoutPartialWrites)
{
    Registry registry{};
    const EntityHandle loose = MakeEntity(registry, glm::vec3{-2.f, 0.f, 0.f});
    const EntityHandle stretched = MakeEntity(registry, glm::vec3{2.f, 0.f, 0.f});
    registry.Raw().get<Tf::Component>(stretched).Scale = {2.f, 1.f, 1.f};
    const EntityHandle child = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, child, stretched);

    const EntityHandle selected[] = {loose, child};
    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    const glm::mat4 moved = Translation({0.f, 1.f, 0.f}) * g0;
    ASSERT_TRUE(gizmo.Preview(registry, moved).Succeeded());
    const Tf::Component looseAccepted = registry.Raw().get<Tf::Component>(loose);
    const Tf::Component childAccepted = registry.Raw().get<Tf::Component>(child);
    ClearDirty(registry);

    // Rotating the child under a non-uniformly scaled parent needs shear.
    const auto rejected = gizmo.Preview(registry, glm::mat4_cast(Rz(45.f)) * g0);
    EXPECT_EQ(rejected.Status, GizmoStatus::NonTrsResult);
    EXPECT_EQ(rejected.Entity, child);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(loose), looseAccepted);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(child), childAccepted);
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(loose));
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(child));
    EXPECT_EQ(gizmo.AcceptedGizmoMatrix(), moved);
    EXPECT_TRUE(gizmo.IsDragging());

    // The last valid preview is what commits.
    EditorCommandHistory history;
    ASSERT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
              EditorCommandHistoryStatus::Applied);
    ExpectVecNear(registry.Raw().get<Tf::Component>(loose).Position, {-2.f, 1.f, 0.f});
}

TEST(GizmoInteraction, NoOpAndMoveAndReturnLeaveExactTransformsAndNoHistory)
{
    Registry registry{};
    const EntityHandle parent = MakeEntity(registry, glm::vec3{3.f, 1.f, 0.f}, Rz(33.f));
    registry.Raw().get<Tf::Component>(parent).Scale = {1.5f, 1.5f, 1.5f};
    const EntityHandle child = MakeEntity(registry, glm::vec3{0.3f, -0.7f, 2.f}, Rz(-17.f));
    SetParent(registry, child, parent);
    const EntityHandle root = MakeEntity(registry, glm::vec3{-4.f, 0.f, 1.f}, Rz(51.f));
    // Mirrored: decomposition yields an equivalent but different TRS.
    const EntityHandle mirrored = MakeEntity(registry, glm::vec3{0.5f, 2.f, -1.f}, Rz(15.f));
    registry.Raw().get<Tf::Component>(mirrored).Scale = {-1.f, 1.f, 1.f};
    const Tf::Component childBefore = registry.Raw().get<Tf::Component>(child);
    const Tf::Component rootBefore = registry.Raw().get<Tf::Component>(root);
    const Tf::Component mirroredBefore = registry.Raw().get<Tf::Component>(mirrored);
    const EntityHandle selected[] = {child, root, mirrored};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Local,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(child));
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, g0).Succeeded());
    ASSERT_TRUE(gizmo.Preview(registry, g0).Succeeded());
    ExpectSameTransform(registry.Raw().get<Tf::Component>(child), childBefore);
    ASSERT_TRUE(gizmo.Preview(registry, glm::mat4_cast(Rz(20.f)) * Translation({1.f, 0.f, 0.f}) * g0).Succeeded());
    // Back near the start through the general (non-exact) path: Gt differs
    // from G0 by one float ulp of scale, so the bit-exact shortcut is skipped.
    const glm::mat4 nearStart = g0 * glm::scale(glm::mat4{1.f}, glm::vec3{1.f + 1.0e-7f});
    ASSERT_NE(nearStart, g0);
    ASSERT_TRUE(gizmo.Preview(registry, nearStart).Succeeded());

    EditorCommandHistory history;
    const auto committed = gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history);
    EXPECT_EQ(committed.Status, EditorCommandHistoryStatus::NoChange);
    EXPECT_EQ(history.UndoCount(), 0u);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(child), childBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(root), rootBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(mirrored), mirroredBefore);
    EXPECT_FALSE(gizmo.IsDragging());
}

TEST(GizmoInteraction, ManyPreviewTicksCommitExactlyOneUndoableBatch)
{
    Registry registry{};
    const EntityHandle a = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f}, Rz(12.f));
    const EntityHandle b = MakeEntity(registry, glm::vec3{-1.f, 2.f, 0.f});
    const Tf::Component aBefore = registry.Raw().get<Tf::Component>(a);
    const Tf::Component bBefore = registry.Raw().get<Tf::Component>(b);
    const EntityHandle selected[] = {a, b};

    EditorCommandHistory history;
    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    for (int tick = 1; tick <= 100; ++tick)
    {
        const glm::mat4 gt = Translation({0.01f * tick, 0.f, 0.f}) * glm::mat4_cast(Rz(0.9f * tick)) * g0;
        ASSERT_TRUE(gizmo.Preview(registry, gt).Succeeded()) << tick;
    }
    EXPECT_EQ(history.UndoCount(), 0u);
    const Tf::Component aAfter = registry.Raw().get<Tf::Component>(a);
    const Tf::Component bAfter = registry.Raw().get<Tf::Component>(b);

    ASSERT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
              EditorCommandHistoryStatus::Applied);
    ASSERT_EQ(history.UndoCount(), 1u);
    ASSERT_EQ(history.Undo().Status, EditorCommandHistoryStatus::Undone);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(a), aBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(b), bBefore);
    ASSERT_EQ(history.Redo().Status, EditorCommandHistoryStatus::Redone);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(a), aAfter);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(b), bAfter);
}

TEST(GizmoInteraction, CancelRestoresExactOriginalTransformsWithoutHistory)
{
    Registry registry{};
    const EntityHandle a = MakeEntity(registry, glm::vec3{0.1f, 0.2f, 0.3f}, glm::normalize(glm::quat{0.9f, 0.1f, 0.2f, 0.3f}));
    registry.Raw().get<Tf::Component>(a).Scale = {0.7f, 1.3f, 2.1f};
    const EntityHandle b = MakeEntity(registry, glm::vec3{-5.f, 1.f, 0.f}, Rz(77.f));
    const Tf::Component aBefore = registry.Raw().get<Tf::Component>(a);
    const Tf::Component bBefore = registry.Raw().get<Tf::Component>(b);
    const EntityHandle selected[] = {a, b};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, glm::mat4_cast(Rz(40.f)) * g0).Succeeded());
    ASSERT_TRUE(gizmo.Preview(registry, g0 * glm::scale(glm::mat4{1.f}, glm::vec3{1.7f})).Succeeded());
    gizmo.DragCancel(registry);

    EXPECT_FALSE(gizmo.IsDragging());
    ExpectSameTransform(registry.Raw().get<Tf::Component>(a), aBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(b), bBefore);
    EXPECT_TRUE(registry.Raw().all_of<Tf::IsDirtyTag>(a));
    EditorCommandHistory history;
    EXPECT_EQ(history.UndoCount(), 0u);
}

TEST(GizmoInteraction, SessionIsFrozenAndRejectsStaleState)
{
    Registry registry{};
    const EntityHandle a = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle b = MakeEntity(registry, glm::vec3{3.f, 0.f, 0.f});
    std::vector<EntityHandle> selected{a, b};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(a));
    EXPECT_EQ(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Local,
                          GizmoPivotMode::BoundsCenters).Status,
              GizmoStatus::SessionActive);

    // A later selection change does not reinterpret the running session.
    selected.pop_back();
    EXPECT_EQ(gizmo.SessionMode(), GizmoMode::Translate);
    ExpectVecNear(gizmo.SessionFrame().Pivot, {2.f, 0.f, 0.f});
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 1.f, 0.f}) * g0).Succeeded());
    ExpectVecNear(registry.Raw().get<Tf::Component>(b).Position, {3.f, 1.f, 0.f});

    Registry other{};
    EXPECT_EQ(gizmo.Preview(other, g0).Status, GizmoStatus::StaleSession);

    // A foreign edit to a target is a conflict, not silently absorbed.
    registry.Raw().get<Tf::Component>(a).Position.z = 9.f;
    const Tf::Component bAccepted = registry.Raw().get<Tf::Component>(b);
    const auto stale = gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0);
    EXPECT_EQ(stale.Status, GizmoStatus::StaleSession);
    EXPECT_EQ(stale.Entity, a);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(b), bAccepted);

    // Reparenting a target is stale as well.
    registry.Raw().get<Tf::Component>(a).Position.z = 0.f;
    ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0).Succeeded());
    SetParent(registry, b, a);
    EXPECT_EQ(gizmo.Preview(registry, g0).Status, GizmoStatus::StaleSession);
    gizmo.DragCancel(registry);
    EXPECT_EQ(gizmo.Preview(registry, g0).Status, GizmoStatus::NoSession);
}

TEST(GizmoInteraction, BeginRejectsInvalidSelectionAndHierarchyWithoutSession)
{
    Registry registry{};
    GizmoInteraction gizmo{};
    const auto begin = [&](std::span<const EntityHandle> selected)
    {
        return gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                           GizmoPivotMode::WorldOrigins);
    };

    EXPECT_EQ(begin({}).Status, GizmoStatus::EmptySelection);

    const EntityHandle bare = registry.Create();
    const EntityHandle bareOnly[] = {bare};
    EXPECT_EQ(begin(bareOnly).Status, GizmoStatus::InvalidEntity);

    const EntityHandle x = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle y = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, x, y);
    SetParent(registry, y, x);
    const EntityHandle cyclic[] = {x};
    EXPECT_EQ(begin(cyclic).Status, GizmoStatus::BrokenHierarchy);

    const EntityHandle flat = MakeEntity(registry, glm::vec3{0.f});
    registry.Raw().get<Tf::Component>(flat).Scale = {1.f, 0.f, 1.f};
    const EntityHandle flatChild = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, flatChild, flat);
    const EntityHandle singular[] = {flatChild};
    const auto refused = begin(singular);
    EXPECT_EQ(refused.Status, GizmoStatus::SingularParent);
    EXPECT_EQ(refused.Entity, flatChild);
    EXPECT_FALSE(gizmo.IsDragging());
}

// --- Review regressions (UI-078 slice 1) -----------------------------------

TEST(GizmoInteraction, FailedCommitRollsBackOwnedPreviewsWithoutHistory)
{
    Registry registry{};
    const EntityHandle a = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle b = MakeEntity(registry, glm::vec3{3.f, 0.f, 0.f}, Rz(20.f));
    const Tf::Component bBefore = registry.Raw().get<Tf::Component>(b);
    const EntityHandle selected[] = {a, b};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0).Succeeded());

    // A foreign edit to A makes the commit fail; B must not keep its preview.
    registry.Raw().get<Tf::Component>(a).Position.z = 9.f;
    const Tf::Component aForeign = registry.Raw().get<Tf::Component>(a);

    EditorCommandHistory history;
    const auto committed = gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history);
    EXPECT_EQ(committed.Status, EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_FALSE(gizmo.IsDragging());
    ExpectSameTransform(registry.Raw().get<Tf::Component>(b), bBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(a), aForeign);
}

TEST(GizmoInteraction, CommitAndCancelOnForeignRegistryWriteNothingAndKeepSession)
{
    Registry registry{};
    Registry other{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle twin = MakeEntity(other, glm::vec3{5.f, 5.f, 5.f}, Rz(60.f));
    ASSERT_EQ(entity, twin); // Same-numbered handles in different registries.
    const Tf::Component entityBefore = registry.Raw().get<Tf::Component>(entity);
    const Tf::Component twinBefore = other.Raw().get<Tf::Component>(twin);
    const EntityHandle selected[] = {entity};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0).Succeeded());

    EXPECT_EQ(gizmo.DragCancel(other).Status, GizmoStatus::StaleSession);
    EditorCommandHistory history;
    EXPECT_FALSE(gizmo.DragCommit(other, Extrinsic::Runtime::DefaultWorldHandle, history).Succeeded());
    EXPECT_EQ(history.UndoCount(), 0u);
    ExpectSameTransform(other.Raw().get<Tf::Component>(twin), twinBefore);
    EXPECT_FALSE(other.Raw().all_of<Tf::IsDirtyTag>(twin));
    EXPECT_TRUE(gizmo.IsDragging());

    // The owning registry can still be restored.
    EXPECT_TRUE(gizmo.DragCancel(registry).Succeeded());
    ExpectSameTransform(registry.Raw().get<Tf::Component>(entity), entityBefore);
    EXPECT_FALSE(gizmo.IsDragging());
}

TEST(GizmoInteraction, ReparentingBeforeCommitIsRejectedWithoutHistory)
{
    Registry registry{};
    const EntityHandle a = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle b = MakeEntity(registry, glm::vec3{3.f, 0.f, 0.f});
    const EntityHandle newParent = MakeEntity(registry, glm::vec3{0.f, 0.f, 7.f});
    const Tf::Component aBefore = registry.Raw().get<Tf::Component>(a);
    const EntityHandle selected[] = {a, b};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0).Succeeded());
    SetParent(registry, b, newParent);

    EditorCommandHistory history;
    const auto committed = gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history);
    EXPECT_EQ(committed.Status, EditorCommandHistoryStatus::StaleEntity);
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_FALSE(gizmo.IsDragging());
    ExpectSameTransform(registry.Raw().get<Tf::Component>(a), aBefore);
}

TEST(GizmoInteraction, SelectedDescendantDeletedOrReparentedDuringDragIsConflict)
{
    for (const bool destroy : {true, false})
    {
        Registry registry{};
        const EntityHandle parent = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
        const EntityHandle child = MakeEntity(registry, glm::vec3{0.f, 1.f, 0.f});
        SetParent(registry, child, parent);
        const EntityHandle selected[] = {parent, child};

        GizmoInteraction gizmo{};
        ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                                GizmoPivotMode::WorldOrigins).Succeeded());
        const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
        ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0).Succeeded());

        if (destroy)
            registry.Destroy(child);
        else
            registry.Raw().remove<Hi::Component>(child); // Moved out of its selected parent.
        const Tf::Component parentAccepted = registry.Raw().get<Tf::Component>(parent);
        const auto stale = gizmo.Preview(registry, Translation({0.f, 3.f, 0.f}) * g0);
        EXPECT_EQ(stale.Status, GizmoStatus::StaleSession) << destroy;
        EXPECT_EQ(stale.Entity, child) << destroy;
        ExpectSameTransform(registry.Raw().get<Tf::Component>(parent), parentAccepted);
    }
}

TEST(GizmoInteraction, PerspectiveGizmoMatrixIsRejectedWithoutWrites)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const Tf::Component before = registry.Raw().get<Tf::Component>(entity);
    const EntityHandle selected[] = {entity};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Scale, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    ASSERT_EQ(gizmo.SessionFrame().Matrix, glm::mat4{1.f});
    glm::mat4 gt{1.f};
    gt[0][0] = 10000.f;
    gt[0][3] = 0.5f; // Perspective row entry.
    const auto rejected = gizmo.Preview(registry, gt);
    EXPECT_EQ(rejected.Status, GizmoStatus::NonTrsResult);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(entity), before);
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(entity));
    EXPECT_EQ(gizmo.AcceptedGizmoMatrix(), glm::mat4{1.f});
    gizmo.DragCancel(registry);

    // A tiny entity scale must not shrink the perspective entry below the
    // check: the affine form of Gt itself is validated.
    const EntityHandle thin = MakeEntity(registry, glm::vec3{0.f});
    registry.Raw().get<Tf::Component>(thin).Scale = {1.0e-6f, 1.f, 1.f};
    const Tf::Component thinBefore = registry.Raw().get<Tf::Component>(thin);
    ClearDirty(registry);
    const EntityHandle thinOnly[] = {thin};
    ASSERT_TRUE(gizmo.Begin(registry, thinOnly, GizmoMode::Scale, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    ASSERT_EQ(gizmo.SessionFrame().Matrix, glm::mat4{1.f});
    EXPECT_EQ(gizmo.Preview(registry, gt).Status, GizmoStatus::NonTrsResult);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(thin), thinBefore);
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(thin));
}

TEST(GizmoInteraction, LargeTranslationDoesNotHideShear)
{
    Registry registry{};
    const EntityHandle loose = MakeEntity(registry, glm::vec3{1.0e6f - 4.f, 0.f, 0.f});
    const EntityHandle stretched = MakeEntity(registry, glm::vec3{1.0e6f, 0.f, 0.f});
    registry.Raw().get<Tf::Component>(stretched).Scale = {2.f, 1.f, 1.f};
    const EntityHandle child = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, child, stretched);
    const Tf::Component looseBefore = registry.Raw().get<Tf::Component>(loose);
    const Tf::Component childBefore = registry.Raw().get<Tf::Component>(child);
    const EntityHandle selected[] = {loose, child};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
    const glm::mat4 rotated = Translation(gizmo.SessionFrame().Pivot) * glm::mat4_cast(Rz(45.f)) *
                              Translation(-gizmo.SessionFrame().Pivot) * g0;
    const auto rejected = gizmo.Preview(registry, rotated);
    EXPECT_EQ(rejected.Status, GizmoStatus::NonTrsResult);
    EXPECT_EQ(rejected.Entity, child);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(loose), looseBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(child), childBefore);
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(loose));

    // A pure translation at the same magnitude is still storable.
    EXPECT_TRUE(gizmo.Preview(registry, Translation({0.f, 8.f, 0.f}) * g0).Succeeded());
}

TEST(GizmoInteraction, NearlySingularParentIsRejectedWithReasonAndNoWrites)
{
    Registry registry{};
    const EntityHandle loose = MakeEntity(registry, glm::vec3{-2.f, 0.f, 0.f});
    const Tf::Component looseBefore = registry.Raw().get<Tf::Component>(loose);

    // det(parent) = 1e-9: refused at Begin, nothing written.
    const EntityHandle flat = MakeEntity(registry, glm::vec3{2.f, 0.f, 0.f});
    registry.Raw().get<Tf::Component>(flat).Scale = {1.f, 1.0e-9f, 1.f};
    const EntityHandle flatChild = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, flatChild, flat);
    const EntityHandle refusedSelection[] = {loose, flatChild};
    GizmoInteraction gizmo{};
    const auto refused = gizmo.Begin(registry, refusedSelection, GizmoMode::Rotate, GizmoOrientation::Global,
                                     GizmoPivotMode::WorldOrigins);
    EXPECT_EQ(refused.Status, GizmoStatus::SingularParent);
    EXPECT_EQ(refused.Entity, flatChild);
    EXPECT_FALSE(gizmo.IsDragging());

    // det(parent) = 1e-3: invertible, but rotating the child would need shear.
    const EntityHandle thin = MakeEntity(registry, glm::vec3{2.f, 0.f, 0.f});
    registry.Raw().get<Tf::Component>(thin).Scale = {1.f, 1.0e-3f, 1.f};
    const EntityHandle thinChild = MakeEntity(registry, glm::vec3{0.f});
    SetParent(registry, thinChild, thin);
    const Tf::Component thinChildBefore = registry.Raw().get<Tf::Component>(thinChild);
    const EntityHandle selected[] = {loose, thinChild};
    ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global,
                            GizmoPivotMode::WorldOrigins).Succeeded());
    const auto rejected = gizmo.Preview(registry, glm::mat4_cast(Rz(30.f)) * gizmo.SessionFrame().Matrix);
    EXPECT_EQ(rejected.Status, GizmoStatus::NonTrsResult);
    EXPECT_EQ(rejected.Entity, thinChild);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(loose), looseBefore);
    ExpectSameTransform(registry.Raw().get<Tf::Component>(thinChild), thinChildBefore);
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(loose));
    EXPECT_FALSE(registry.Raw().all_of<Tf::IsDirtyTag>(thinChild));
}

TEST(GizmoInteraction, RealMoveAtLargePositionIsCommittedAndUndoable)
{
    // At x = 1e6 one float ULP is 0.0625; 0.3125 is five ULP and must count as a move.
    for (const float move : {1.f, 0.3125f})
    {
        Registry registry{};
        const EntityHandle far = MakeEntity(registry, glm::vec3{1.0e6f, 0.f, 0.f});
        const Tf::Component before = registry.Raw().get<Tf::Component>(far);
        const EntityHandle selected[] = {far};

        GizmoInteraction gizmo{};
        ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                                GizmoPivotMode::WorldOrigins).Succeeded());
        ASSERT_TRUE(gizmo.Preview(registry, Translation({move, 0.f, 0.f}) * gizmo.SessionFrame().Matrix)
                        .Succeeded());

        EditorCommandHistory history;
        ASSERT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
                  EditorCommandHistoryStatus::Applied) << move;
        ASSERT_EQ(history.UndoCount(), 1u) << move;
        EXPECT_EQ(registry.Raw().get<Tf::Component>(far).Position.x, 1.0e6f + move);
        ASSERT_EQ(history.Undo().Status, EditorCommandHistoryStatus::Undone);
        ExpectSameTransform(registry.Raw().get<Tf::Component>(far), before);
    }
}

TEST(GizmoInteraction, ForeignChangeToSelectedDescendantTransformIsConflict)
{
    for (const bool removeComponent : {false, true})
    {
        Registry registry{};
        const EntityHandle parent = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
        const EntityHandle child = MakeEntity(registry, glm::vec3{0.f, 1.f, 0.f});
        SetParent(registry, child, parent);
        const Tf::Component parentBefore = registry.Raw().get<Tf::Component>(parent);
        const EntityHandle selected[] = {parent, child};

        GizmoInteraction gizmo{};
        ASSERT_TRUE(gizmo.Begin(registry, selected, GizmoMode::Translate, GizmoOrientation::Global,
                                GizmoPivotMode::WorldOrigins).Succeeded());
        const glm::mat4 g0 = gizmo.SessionFrame().Matrix;
        ASSERT_TRUE(gizmo.Preview(registry, Translation({0.f, 2.f, 0.f}) * g0).Succeeded());

        if (removeComponent)
            registry.Raw().remove<Tf::Component>(child);
        else
            registry.Raw().get<Tf::Component>(child).Position.z = 5.f;
        const auto stale = gizmo.Preview(registry, Translation({0.f, 3.f, 0.f}) * g0);
        EXPECT_EQ(stale.Status, GizmoStatus::StaleSession) << removeComponent;
        EXPECT_EQ(stale.Entity, child) << removeComponent;

        EditorCommandHistory history;
        EXPECT_EQ(gizmo.DragCommit(registry, Extrinsic::Runtime::DefaultWorldHandle, history).Status,
                  EditorCommandHistoryStatus::StaleEntity) << removeComponent;
        EXPECT_EQ(history.UndoCount(), 0u);
        ExpectSameTransform(registry.Raw().get<Tf::Component>(parent), parentBefore);
    }
}

// --- Exact rotation snap for the editor frontend (UI-078 3c) ----------------

namespace
{
    // G0 at (1,2,3) with a tilted basis; `turn` applied about world `axis`.
    GizmoFrame TiltedFrame()
    {
        GizmoFrame frame{};
        frame.Pivot = {1.f, 2.f, 3.f};
        frame.Basis = glm::mat3_cast(glm::angleAxis(0.4f, glm::normalize(glm::vec3{1.f, 1.f, 0.f})));
        frame.Matrix = glm::translate(glm::mat4{1.f}, frame.Pivot) * glm::mat4{frame.Basis};
        return frame;
    }
    glm::mat4 Turned(const GizmoFrame& frame, const float degrees, const glm::vec3 axis)
    {
        return glm::translate(glm::mat4{1.f}, frame.Pivot) *
               glm::mat4_cast(glm::angleAxis(glm::radians(degrees), glm::normalize(axis))) *
               glm::mat4{frame.Basis};
    }
    // Same full transform: rotation (either quaternion sign) and the pivot.
    void ExpectSameRotation(const glm::mat4& actual, const glm::mat4& expected)
    {
        const glm::quat a = glm::quat_cast(glm::mat3{actual});
        const glm::quat e = glm::quat_cast(glm::mat3{expected});
        EXPECT_NEAR(std::abs(glm::dot(a, e)), 1.f, 1.0e-6f) << "rotation differs";
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                EXPECT_NEAR(actual[c][r], expected[c][r], 1.0e-5f) << c << "," << r;
        ExpectVecNear(glm::vec3{actual[3]}, glm::vec3{expected[3]}, 1.0e-5f);
    }
}

TEST(GizmoInteraction, SnapGizmoRotationKeepsTheSignedAngleForStepsThatDoNotDivide360)
{
    const GizmoFrame frame = TiltedFrame();
    const float step = glm::radians(100.f);
    const glm::vec3 axis{1.f, 2.f, 3.f};
    // ImGuizmo can snap to +-200 degrees (beyond 180) with a 100-degree step; with drift.
    for (const float degrees : {200.f, -200.f, 100.f, -100.f})
    {
        SCOPED_TRACE(degrees);
        const float drift = degrees > 0.f ? 0.03f : -0.03f;
        ExpectSameRotation(Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, degrees + drift, axis), step),
                           Turned(frame, degrees, axis));
    }
}

TEST(GizmoInteraction, SnapGizmoRotationKeepsTheAxisOfTinySteps)
{
    const GizmoFrame frame = TiltedFrame();
    const float step = glm::radians(0.01f);
    for (const glm::vec3 axis : {glm::vec3{1.f, 0.f, 0.f}, glm::vec3{0.f, 1.f, 0.f}})
    {
        SCOPED_TRACE(axis.x);
        ExpectSameRotation(Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, 0.02f, axis), step),
                           Turned(frame, 0.02f, axis));
        const glm::mat4 snapped = Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, 0.02f, axis), step);
        const glm::quat delta = glm::quat_cast(glm::mat3{snapped} * glm::transpose(frame.Basis));
        EXPECT_NEAR(std::abs(delta.z), 0.f, 1.0e-7f) << "no Z rotation";
        EXPECT_NEAR(std::abs(axis.x > 0.f ? delta.x : delta.y), std::sin(glm::radians(0.01f)), 1.0e-6f);
    }
    // Below half a step, or no rotation at all: exactly G0.
    EXPECT_EQ(Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, 0.004f, {1.f, 0.f, 0.f}), step), frame.Matrix);
    EXPECT_EQ(Extrinsic::Runtime::SnapGizmoRotation(frame, frame.Matrix, step), frame.Matrix);
}

TEST(GizmoInteraction, SnapGizmoRotationKeepsLargeStepsThatAreNotAWholeTurn)
{
    const GizmoFrame frame = TiltedFrame();
    const float step = glm::radians(270.f);
    const glm::vec3 axis{1.f, 2.f, 3.f};
    for (const float degrees : {270.f, -270.f})
    {
        SCOPED_TRACE(degrees);
        ExpectSameRotation(Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, degrees, axis), step),
                           Turned(frame, degrees, axis));
    }
    // Snapping to a whole turn (355 degrees on a 120-degree step) is no turn: exactly G0.
    EXPECT_EQ(Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, 355.f, axis), glm::radians(120.f)),
              frame.Matrix);
}

TEST(GizmoInteraction, SnapGizmoRotationKeepsTheSmallestConfiguredStep)
{
    // 0.001 degrees (~1.7e-5 rad) is the `sandbox.gizmo` floor; one such step must
    // survive on a tilted basis and on the identity basis (where only Z turns).
    const float step = glm::radians(0.001f);
    GizmoFrame identity{};
    identity.Pivot = {1000.f, 0.f, 0.f};
    identity.Matrix = glm::translate(glm::mat4{1.f}, identity.Pivot);
    const std::array<std::pair<GizmoFrame, glm::vec3>, 2> cases{{
        {TiltedFrame(), glm::normalize(glm::vec3{1.f, 2.f, 3.f})},
        {identity, glm::vec3{0.f, 0.f, 1.f}},
    }};
    for (const auto& [frame, axis] : cases)
    {
        for (const float sign : {1.f, -1.f})
        {
            SCOPED_TRACE(sign * axis.z);
            const glm::mat4 snapped =
                Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, sign * 0.001f, axis), step);
            // The signed angle about `axis`, from the vector part (exact for tiny turns).
            glm::quat delta = glm::quat_cast(glm::mat3{snapped} * glm::transpose(frame.Basis));
            if (delta.w < 0.f)
                delta = -delta;
            EXPECT_NEAR(2.f * glm::dot(glm::vec3{delta.x, delta.y, delta.z}, axis), sign * step, 2.0e-7f);
            // Below half a step: exactly G0.
            EXPECT_EQ(Extrinsic::Runtime::SnapGizmoRotation(frame, Turned(frame, sign * 0.0004f, axis), step),
                      frame.Matrix);
        }
    }
}
