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
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GizmoFrameService;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.WorldHandle;

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
    CameraViewInput OrthoCameraInput()
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
        return input;
    }

    CameraViewSnapshot OrthoCamera(const Extrinsic::Core::Extent2D viewport)
    {
        return BuildCameraViewSnapshot(
            OrthoCameraInput(), viewport);
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

TEST(GizmoInteraction, DragTickTranslatesAlongAxisAndCommitsHistory)
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

    Extrinsic::Runtime::EditorCommandHistory history;
    const Extrinsic::Runtime::EditorCommandHistoryResult committed =
        gizmo.DragCommit(
            registry,
            Extrinsic::Runtime::DefaultWorldHandle,
            history);
    EXPECT_EQ(
        committed.Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Applied);
    EXPECT_FALSE(gizmo.IsDragging());
    ASSERT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.Snapshot().UndoLabel, "Manipulate Transform");

    ASSERT_EQ(
        history.Undo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(
        registry.Raw().get<Tf::Component>(entity).Position,
        glm::vec3(0.f));
    ASSERT_EQ(
        history.Redo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Redone);
    EXPECT_NEAR(
        registry.Raw().get<Tf::Component>(entity).Position.x,
        3.f,
        1.0e-4f);
}

TEST(GizmoInteraction,
     DragCommitCoalescesMultiSelectionAndRejectsInterveningState)
{
    Registry registry{};
    const EntityHandle first =
        MakeEntity(registry, glm::vec3{0.f});
    const EntityHandle second =
        MakeEntity(registry, glm::vec3{10.f, 2.f, 0.f});
    const EntityHandle selected[] = {first, second};

    GizmoInteraction gizmo{};
    const GizmoHitResult hit{
        .Hit = true,
        .Axis = GizmoAxis::X,
        .Entity = first,
    };
    const PickRay startRay{
        .Origin = {5.f, 0.f, 5.f},
        .Direction = {0.f, 0.f, -1.f},
    };
    const PickRay currentRay{
        .Origin = {8.f, 0.f, 5.f},
        .Direction = {0.f, 0.f, -1.f},
    };
    ASSERT_TRUE(
        gizmo.BeginDrag(
            registry, hit, startRay, selected));
    ASSERT_TRUE(gizmo.DragTick(registry, currentRay));

    Extrinsic::Runtime::EditorCommandHistory history;
    ASSERT_EQ(
        gizmo.DragCommit(
                 registry,
                 Extrinsic::Runtime::DefaultWorldHandle,
                 history)
            .Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Applied);
    ASSERT_EQ(history.UndoCount(), 1u);
    EXPECT_NEAR(
        registry.Raw().get<Tf::Component>(first).Position.x,
        3.f,
        1.0e-4f);
    EXPECT_NEAR(
        registry.Raw().get<Tf::Component>(second).Position.x,
        13.f,
        1.0e-4f);

    ASSERT_EQ(
        history.Undo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(
        registry.Raw().get<Tf::Component>(first).Position,
        glm::vec3(0.f));
    EXPECT_EQ(
        registry.Raw().get<Tf::Component>(second).Position,
        glm::vec3(10.f, 2.f, 0.f));

    ASSERT_EQ(
        history.Redo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Redone);
    auto& firstTransform =
        registry.Raw().get<Tf::Component>(first);
    const Tf::Component secondBeforeRejectedUndo =
        registry.Raw().get<Tf::Component>(second);
    firstTransform.Position.x = 99.f;

    EXPECT_EQ(
        history.Undo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::StaleEntity);
    EXPECT_FLOAT_EQ(firstTransform.Position.x, 99.f);
    EXPECT_EQ(
        registry.Raw().get<Tf::Component>(second).Position,
        secondBeforeRejectedUndo.Position);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
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

TEST(GizmoFrameService,
     SceneClearCancelsDragAndClearsPacketsAndScratch)
{
    Registry registry{};
    const EntityHandle entity =
        MakeEntity(registry, glm::vec3{1.f, 0.f, 0.f});
    registry.Raw().emplace<
        Extrinsic::ECS::Components::Selection::
            SelectableTag>(entity);
    Extrinsic::Runtime::SelectionController selection;
    ASSERT_TRUE(
        selection.SetSelectedEntity(registry, entity));

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Backend =
        Extrinsic::Core::Config::WindowBackend::Null;
    auto window =
        Extrinsic::Platform::CreateWindow(windowConfig);
    ASSERT_NE(window, nullptr);

    Extrinsic::Runtime::GizmoFrameService service;
    service.Interaction().Config().AxisLength = 2.5f;
    service.Interaction().SetMode(GizmoMode::Scale);
    service.Interaction().SetOrientation(
        GizmoOrientation::Local);
    service.DriveInputForFrame(
        Extrinsic::Runtime::GizmoFrameServiceInput{
            .Scene = registry,
            .Selection = selection,
            .Window = *window,
            .Viewport =
                Extrinsic::Platform::Extent2D{
                    .Width = 64,
                    .Height = 64,
                },
            .Camera = CameraViewInput{},
        });
    ASSERT_EQ(
        service.BuildRenderPackets(registry).size(),
        1u);

    GizmoHitResult hit{
        .Hit = true,
        .Axis = GizmoAxis::X,
        .Entity = entity,
    };
    const EntityHandle selected[] = {entity};
    const PickRay startRay{
        .Origin = {2.f, 0.f, 5.f},
        .Direction = {0.f, 0.f, -1.f},
    };
    const PickRay currentRay{
        .Origin = {3.f, 0.f, 5.f},
        .Direction = {0.f, 0.f, -1.f},
    };
    ASSERT_TRUE(
        service.Interaction().BeginDrag(
            registry, hit, startRay, selected));
    ASSERT_TRUE(
        service.Interaction().DragTick(
            registry, currentRay));
    EXPECT_FLOAT_EQ(
        registry.Raw()
            .get<Tf::Component>(entity)
            .Scale.x,
        2.f);

    service.ClearSceneState(&registry);

    EXPECT_FALSE(service.Interaction().IsDragging());
    EXPECT_FLOAT_EQ(
        registry.Raw()
            .get<Tf::Component>(entity)
            .Scale.x,
        1.f);
    EXPECT_TRUE(
        service.BuildRenderPackets(registry).empty());
    EXPECT_EQ(service.Interaction().Mode(),
              GizmoMode::Scale);
    EXPECT_EQ(service.Interaction().Orientation(),
              GizmoOrientation::Local);
    EXPECT_FLOAT_EQ(
        service.Interaction().Config().AxisLength,
        2.5f);
}

TEST(GizmoFrameService,
     FrameInputRequiresHistoryAndCommitsOneUndoableDrag)
{
    Registry registry{};
    const EntityHandle entity =
        MakeEntity(registry, glm::vec3{0.f});
    registry.Raw().emplace<
        Extrinsic::ECS::Components::Selection::
            SelectableTag>(entity);
    Extrinsic::Runtime::SelectionController selection;
    ASSERT_TRUE(
        selection.SetSelectedEntity(registry, entity));

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Backend =
        Extrinsic::Core::Config::WindowBackend::Null;
    windowConfig.Width = 800;
    windowConfig.Height = 600;
    Extrinsic::Platform::Backends::Null::NullWindow window{
        windowConfig};
    const Extrinsic::Platform::Extent2D viewport{
        .Width = 800,
        .Height = 600,
    };
    const CameraViewInput camera = OrthoCameraInput();
    Extrinsic::Runtime::GizmoFrameService service;

    const auto drive =
        [&](Extrinsic::Runtime::EditorCommandHistory* history)
        {
            service.DriveInputForFrame(
                Extrinsic::Runtime::GizmoFrameServiceInput{
                    .Scene = registry,
                    .World =
                        Extrinsic::Runtime::DefaultWorldHandle,
                    .Selection = selection,
                    .CommandHistory = history,
                    .Window = window,
                    .Viewport = viewport,
                    .Camera = camera,
                });
        };

    window.QueueCursor(450.0, 300.0);
    window.QueueMouseButton(0, true);
    window.PollEvents();
    drive(nullptr);
    EXPECT_FALSE(service.Interaction().IsDragging());

    window.QueueMouseButton(0, false);
    window.PollEvents();
    drive(nullptr);

    Extrinsic::Runtime::EditorCommandHistory history;
    window.QueueMouseButton(0, true);
    window.PollEvents();
    drive(&history);
    ASSERT_TRUE(service.Interaction().IsDragging());

    window.QueueCursor(550.0, 300.0);
    window.PollEvents();
    drive(&history);
    EXPECT_TRUE(service.Interaction().IsDragging());
    EXPECT_GT(
        registry.Raw().get<Tf::Component>(entity).Position.x,
        0.f);

    window.QueueMouseButton(0, false);
    window.PollEvents();
    drive(&history);
    EXPECT_FALSE(service.Interaction().IsDragging());
    ASSERT_EQ(history.UndoCount(), 1u);
    ASSERT_EQ(
        history.Undo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_EQ(
        registry.Raw().get<Tf::Component>(entity).Position,
        glm::vec3(0.f));
}

TEST(GizmoInteraction, DragTickRotatesAroundAxisAndCommitsHistory)
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

    Extrinsic::Runtime::EditorCommandHistory history;
    EXPECT_EQ(
        gizmo.DragCommit(
                 registry,
                 Extrinsic::Runtime::DefaultWorldHandle,
                 history)
            .Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Applied);
    ASSERT_EQ(history.UndoCount(), 1u);
    ASSERT_EQ(
        history.Undo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_NEAR(
        registry.Raw().get<Tf::Component>(entity).Rotation.w,
        1.f,
        1.0e-4f);
}

TEST(GizmoInteraction, DragTickScalesAlongAxisAndCommitsHistory)
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

    Extrinsic::Runtime::EditorCommandHistory history;
    EXPECT_EQ(
        gizmo.DragCommit(
                 registry,
                 Extrinsic::Runtime::DefaultWorldHandle,
                 history)
            .Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Applied);
    ASSERT_EQ(history.UndoCount(), 1u);
    ASSERT_EQ(
        history.Undo().Status,
        Extrinsic::Runtime::EditorCommandHistoryStatus::Undone);
    EXPECT_NEAR(
        registry.Raw().get<Tf::Component>(entity).Scale.x,
        1.f,
        1.0e-4f);
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
    EXPECT_EQ(packet.StableId, Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity));
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
