// RUNTIME-084 — contract coverage for the runtime transform-gizmo interaction
// module: screen-space handle hit testing, axis-constrained translate/rotate/
// scale application against ECS authoring transforms, snap rounding, undo
// emission, and the frozen render-packet field set.

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Runtime.GizmoInteraction;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Graphics::BuildCameraViewSnapshot;
using Extrinsic::Graphics::CameraViewInput;
using Extrinsic::Graphics::CameraViewSnapshot;
using Extrinsic::Graphics::TransformGizmoRenderPacket;
using Extrinsic::Runtime::GizmoAxis;
using Extrinsic::Runtime::GizmoConfig;
using Extrinsic::Runtime::GizmoHitResult;
using Extrinsic::Runtime::GizmoInteraction;
using Extrinsic::Runtime::GizmoMode;
using Extrinsic::Runtime::GizmoModifier;
using Extrinsic::Runtime::GizmoOrientation;
using Extrinsic::Runtime::GizmoUndoStack;
using Extrinsic::Runtime::PickRay;
using Extrinsic::Runtime::TransformGizmoRenderPacketBuilder;

namespace Tf = Extrinsic::ECS::Components::Transform;

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

    // A centred orthographic camera looking down -Z. World (0,0,0) projects to
    // the viewport centre; +X projects to the right, +Y up. Orthographic so the
    // pixel mapping is linear and exact for the hit-test assertions.
    CameraViewSnapshot OrthoCamera(const Extrinsic::Core::Extent2D viewport)
    {
        CameraViewInput input{};
        input.View = glm::lookAt(glm::vec3{0.f, 0.f, 5.f}, glm::vec3{0.f}, glm::vec3{0.f, 1.f, 0.f});
        // Half-width 4 → world x in [-4, 4] maps to pixel x in [0, Width].
        input.Projection = glm::ortho(-4.f, 4.f, -3.f, 3.f, 0.1f, 100.f);
        input.Position = {0.f, 0.f, 5.f};
        input.Forward = {0.f, 0.f, -1.f};
        input.Up = {0.f, 1.f, 0.f};
        input.NearPlane = 0.1f;
        input.FarPlane = 100.f;
        input.Valid = true;
        return BuildCameraViewSnapshot(input, viewport);
    }
}

// --- Hit testing -----------------------------------------------------------

TEST(GizmoInteraction, HitTestResolvesXAxisAndRejectsOffAxisCursor)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle selected[] = {entity};

    const Extrinsic::Core::Extent2D viewport{.Width = 800, .Height = 600};
    const CameraViewSnapshot camera = OrthoCamera(viewport);
    ASSERT_TRUE(camera.Valid);

    GizmoInteraction gizmo{GizmoConfig{.HandlePickRadiusPixels = 8.f, .AxisLength = 1.f}};

    // Gizmo origin projects to (400, 300); the +X handle end (world (1,0,0))
    // projects to (500, 300). A cursor on that horizontal line resolves to X.
    const GizmoHitResult hit = gizmo.HitTest(registry, camera, glm::vec2{450.f, 300.f}, viewport, selected);
    EXPECT_TRUE(hit.Hit);
    EXPECT_EQ(hit.Axis, GizmoAxis::X);
    EXPECT_EQ(hit.Entity, entity);

    // A cursor well off every handle line (40px below the X handle, far from the
    // Y/Z handles too) is a background no-hit.
    const GizmoHitResult miss = gizmo.HitTest(registry, camera, glm::vec2{450.f, 340.f}, viewport, selected);
    EXPECT_FALSE(miss.Hit);
    EXPECT_EQ(miss.Axis, GizmoAxis::None);
}

TEST(GizmoInteraction, HitTestEmptySelectionIsNoHit)
{
    Registry registry{};
    const Extrinsic::Core::Extent2D viewport{.Width = 800, .Height = 600};
    const CameraViewSnapshot camera = OrthoCamera(viewport);

    GizmoInteraction gizmo{};
    const GizmoHitResult hit = gizmo.HitTest(registry, camera, glm::vec2{400.f, 300.f}, viewport, {});
    EXPECT_FALSE(hit.Hit);
}

// --- Drag application + undo emission --------------------------------------

TEST(GizmoInteraction, DragTickTranslatesAlongAxisAndCommitEmitsUndoRecord)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle selected[] = {entity};

    GizmoInteraction gizmo{};
    GizmoHitResult hit{};
    hit.Hit = true;
    hit.Axis = GizmoAxis::X;
    hit.Entity = entity;

    // Pick ray closest point on the X axis is at param 2.
    const PickRay startRay{.Origin = {2.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    ASSERT_TRUE(gizmo.BeginDrag(registry, hit, startRay, selected));
    EXPECT_TRUE(gizmo.IsDragging());

    // Move the ray so its closest point on the X axis is at param 5 → +3 delta.
    const PickRay currentRay{.Origin = {5.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));

    const auto& transform = registry.Raw().get<Tf::Component>(entity);
    EXPECT_NEAR(transform.Position.x, 3.f, 1.0e-4f);
    EXPECT_NEAR(transform.Position.y, 0.f, 1.0e-4f);
    EXPECT_NEAR(transform.Position.z, 0.f, 1.0e-4f);
    EXPECT_TRUE((registry.Raw().all_of<Tf::IsDirtyTag>(entity)));

    GizmoUndoStack undo{};
    EXPECT_EQ(gizmo.DragCommit(registry, undo), 1u);
    EXPECT_FALSE(gizmo.IsDragging());
    ASSERT_EQ(undo.Size(), 1u);
    EXPECT_EQ(undo.Back().Entity, entity);
    EXPECT_NEAR(undo.Back().BeforePosition.x, 0.f, 1.0e-4f);
    EXPECT_NEAR(undo.Back().AfterPosition.x, 3.f, 1.0e-4f);
    EXPECT_NEAR(undo.Back().BeforeScale.x, 1.f, 1.0e-4f);
    EXPECT_NEAR(undo.Back().AfterScale.x, 1.f, 1.0e-4f);
}

TEST(GizmoInteraction, DragCancelRestoresBeforeTransform)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    const EntityHandle selected[] = {entity};

    GizmoInteraction gizmo{};
    GizmoHitResult hit{};
    hit.Hit = true;
    hit.Axis = GizmoAxis::X;
    hit.Entity = entity;

    const PickRay startRay{.Origin = {2.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    ASSERT_TRUE(gizmo.BeginDrag(registry, hit, startRay, selected));
    const PickRay currentRay{.Origin = {6.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));
    EXPECT_GT(registry.Raw().get<Tf::Component>(entity).Position.x, 1.f);

    gizmo.DragCancel(registry);
    EXPECT_FALSE(gizmo.IsDragging());
    const auto& restored = registry.Raw().get<Tf::Component>(entity);
    EXPECT_NEAR(restored.Position.x, 1.f, 1.0e-4f);
    EXPECT_NEAR(restored.Scale.x, 1.f, 1.0e-4f);
    EXPECT_NEAR(restored.Rotation.w, 1.f, 1.0e-4f);
}

TEST(GizmoInteraction, DragTickRotatesAroundAxisAndCommitEmitsUndoRecord)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle selected[] = {entity};

    GizmoHitResult hit{};
    hit.Hit = true;
    hit.Axis = GizmoAxis::X;
    hit.Entity = entity;
    const PickRay startRay{.Origin = {2.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    const PickRay currentRay{.Origin = {5.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};

    GizmoInteraction gizmo{};
    gizmo.SetMode(GizmoMode::Rotate);
    ASSERT_TRUE(gizmo.BeginDrag(registry, hit, startRay, selected));
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));

    const auto& transform = registry.Raw().get<Tf::Component>(entity);
    EXPECT_NEAR(transform.Position.x, 0.f, 1.0e-4f);
    EXPECT_NEAR(transform.Rotation.w, std::cos(1.5f), 1.0e-4f);
    EXPECT_NEAR(transform.Rotation.x, std::sin(1.5f), 1.0e-4f);

    GizmoUndoStack undo{};
    EXPECT_EQ(gizmo.DragCommit(registry, undo), 1u);
    ASSERT_EQ(undo.Size(), 1u);
    EXPECT_NEAR(undo.Back().BeforeRotation.w, 1.f, 1.0e-4f);
    EXPECT_NEAR(undo.Back().AfterRotation.w, std::cos(1.5f), 1.0e-4f);
}

TEST(GizmoInteraction, DragTickScalesAlongAxisAndCommitEmitsUndoRecord)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle selected[] = {entity};

    GizmoHitResult hit{};
    hit.Hit = true;
    hit.Axis = GizmoAxis::X;
    hit.Entity = entity;
    const PickRay startRay{.Origin = {2.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    const PickRay currentRay{.Origin = {3.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};

    GizmoInteraction gizmo{};
    gizmo.SetMode(GizmoMode::Scale);
    ASSERT_TRUE(gizmo.BeginDrag(registry, hit, startRay, selected));
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));

    const auto& transform = registry.Raw().get<Tf::Component>(entity);
    EXPECT_NEAR(transform.Scale.x, 2.f, 1.0e-4f);
    EXPECT_NEAR(transform.Scale.y, 1.f, 1.0e-4f);
    EXPECT_NEAR(transform.Scale.z, 1.f, 1.0e-4f);

    GizmoUndoStack undo{};
    EXPECT_EQ(gizmo.DragCommit(registry, undo), 1u);
    ASSERT_EQ(undo.Size(), 1u);
    EXPECT_NEAR(undo.Back().BeforeScale.x, 1.f, 1.0e-4f);
    EXPECT_NEAR(undo.Back().AfterScale.x, 2.f, 1.0e-4f);
}

TEST(GizmoInteraction, DragModeIsLatchedWhenToolbarModeChangesMidDrag)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle selected[] = {entity};

    GizmoHitResult hit{};
    hit.Hit = true;
    hit.Axis = GizmoAxis::X;
    hit.Entity = entity;
    const PickRay startRay{.Origin = {2.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    const PickRay currentRay{.Origin = {5.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};

    GizmoInteraction gizmo{};
    ASSERT_TRUE(gizmo.BeginDrag(registry, hit, startRay, selected));
    gizmo.SetMode(GizmoMode::Rotate);
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));

    const auto& transform = registry.Raw().get<Tf::Component>(entity);
    EXPECT_NEAR(transform.Position.x, 3.f, 1.0e-4f);
    EXPECT_NEAR(transform.Rotation.w, 1.f, 1.0e-4f);
}

// --- Snap rounding ---------------------------------------------------------

TEST(GizmoInteraction, SnapModifierRoundsTranslationToStep)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle selected[] = {entity};

    GizmoInteraction gizmo{GizmoConfig{.HandlePickRadiusPixels = 8.f, .AxisLength = 1.f, .TranslateSnapStep = 1.f}};
    GizmoHitResult hit{};
    hit.Hit = true;
    hit.Axis = GizmoAxis::X;
    hit.Entity = entity;

    const PickRay startRay{.Origin = {2.f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    ASSERT_TRUE(gizmo.BeginDrag(registry, hit, startRay, selected));

    // Delta would be +3.4; with snap step 1.0 it rounds to +3.0.
    gizmo.SetModifierMask(static_cast<std::uint32_t>(GizmoModifier::Snap));
    const PickRay currentRay{.Origin = {5.4f, 0.f, 5.f}, .Direction = {0.f, 0.f, -1.f}};
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));
    EXPECT_NEAR(registry.Raw().get<Tf::Component>(entity).Position.x, 3.f, 1.0e-4f);

    // Clearing the modifier applies the raw delta.
    gizmo.SetModifierMask(0u);
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));
    EXPECT_NEAR(registry.Raw().get<Tf::Component>(entity).Position.x, 3.4f, 1.0e-4f);
}

// --- Render packet field set ------------------------------------------------

TEST(GizmoInteraction, RenderPacketBuilderMapsOnlyFrozenFields)
{
    Registry registry{};
    const EntityHandle entity = MakeEntity(registry, glm::vec3{2.f, 3.f, 4.f});
    const EntityHandle selected[] = {entity};

    TransformGizmoRenderPacketBuilder builder{};
    const auto translatePackets =
        builder.Build(registry, selected, GizmoMode::Translate, GizmoOrientation::Global, 1.5f);

    ASSERT_EQ(translatePackets.size(), 1u);
    const TransformGizmoRenderPacket& packet = translatePackets[0];
    EXPECT_EQ(packet.StableId, static_cast<std::uint32_t>(entity));
    EXPECT_NEAR(packet.AxisLength, 1.5f, 1.0e-4f);
    EXPECT_NEAR(packet.Transform[3].x, 2.f, 1.0e-4f);
    EXPECT_NEAR(packet.Transform[3].y, 3.f, 1.0e-4f);
    EXPECT_NEAR(packet.Transform[3].z, 4.f, 1.0e-4f);
    // Global orientation → identity rotation columns.
    EXPECT_NEAR(packet.Transform[0].x, 1.f, 1.0e-4f);
    EXPECT_NEAR(packet.Transform[1].y, 1.f, 1.0e-4f);
    EXPECT_NEAR(packet.Transform[2].z, 1.f, 1.0e-4f);
    EXPECT_TRUE(packet.ShowTranslate);
    EXPECT_FALSE(packet.ShowRotate);
    EXPECT_FALSE(packet.ShowScale);

    // Mode visibility is the only thing that changes for rotate.
    const auto rotatePackets =
        builder.Build(registry, selected, GizmoMode::Rotate, GizmoOrientation::Global, 1.f);
    ASSERT_EQ(rotatePackets.size(), 1u);
    EXPECT_FALSE(rotatePackets[0].ShowTranslate);
    EXPECT_TRUE(rotatePackets[0].ShowRotate);
    EXPECT_FALSE(rotatePackets[0].ShowScale);
}
