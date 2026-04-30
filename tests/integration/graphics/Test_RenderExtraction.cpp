#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <entt/entity/fwd.hpp>

import Core.InplaceFunction;
import Core.Memory;
import RHI.Profiler;
import Runtime.RenderExtraction;
import Runtime.SceneManager;
import Graphics.Camera;
import Graphics.Components;
import Graphics.DebugDraw;
import Graphics.RenderPipeline;
import Geometry.Graph;
import Geometry.PointCloud;
import Geometry.PointCloudUtils;
import Geometry.HalfedgeMesh;
import Geometry.Handle;
import ECS;

TEST(RenderExtraction, FrameContext_DefaultStateIsUnprepared)
{
    Runtime::FrameContext frame{};
    EXPECT_EQ(frame.FrameNumber, 0u);
    EXPECT_EQ(frame.PreviousFrameNumber, Runtime::InvalidFrameNumber);
    EXPECT_EQ(frame.LastSubmittedTimelineValue, 0u);
    EXPECT_EQ(frame.SlotIndex, 0u);
    EXPECT_EQ(frame.FramesInFlight, Runtime::DefaultFrameContexts);
    EXPECT_FALSE(frame.Prepared);
    EXPECT_FALSE(frame.Submitted);
    EXPECT_FALSE(frame.ReusedSubmittedSlot);
    EXPECT_FALSE(frame.Viewport.IsValid());
    EXPECT_EQ(frame.GetPreparedRenderWorld(), nullptr);
    EXPECT_FALSE(frame.HasAllocators());
}

TEST(RenderExtraction, FrameContextRing_AllocatesPerSlotRenderAllocators)
{
    Runtime::FrameContextRing ring(2u);
    Runtime::FrameContext& frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 800, .Height = 600});
    EXPECT_TRUE(frame0.HasAllocators());

    Runtime::FrameContext& frame1 =
        ring.BeginFrame(1u, Runtime::RenderViewport{.Width = 800, .Height = 600});
    EXPECT_TRUE(frame1.HasAllocators());

    // Each slot has its own allocators (distinct pointers).
    EXPECT_NE(&frame0.GetRenderArena(), &frame1.GetRenderArena());
    EXPECT_NE(&frame0.GetRenderScope(), &frame1.GetRenderScope());
}

TEST(RenderExtraction, FrameContextRing_WrapAroundPreservesAllocatorPointers)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 800, .Height = 600};

    // First pass: frames 0 and 1.
    Runtime::FrameContext& first0 = ring.BeginFrame(0u, vp);
    auto* arena0 = &first0.GetRenderArena();
    auto* scope0 = &first0.GetRenderScope();
    Runtime::FrameContext& first1 = ring.BeginFrame(1u, vp);
    auto* arena1 = &first1.GetRenderArena();

    // Second pass: wrap around to slots 0 and 1 again.
    Runtime::FrameContext& second0 = ring.BeginFrame(2u, vp);
    Runtime::FrameContext& second1 = ring.BeginFrame(3u, vp);

    // Same slot returns the same allocator objects (stable pointers).
    EXPECT_EQ(&second0.GetRenderArena(), arena0);
    EXPECT_EQ(&second0.GetRenderScope(), scope0);
    EXPECT_EQ(&second1.GetRenderArena(), arena1);
}

TEST(RenderExtraction, FrameContextRing_ArenaContentIsolation)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 640, .Height = 480};

    Runtime::FrameContext& frame0 = ring.BeginFrame(0u, vp);
    Runtime::FrameContext& frame1 = ring.BeginFrame(1u, vp);

    // Allocate from slot 0's arena.
    auto& arena0 = frame0.GetRenderArena();
    auto r0 = arena0.Alloc(128, alignof(std::max_align_t));
    ASSERT_TRUE(r0.has_value());

    // Allocate from slot 1's arena — must not alias slot 0's allocation.
    auto& arena1 = frame1.GetRenderArena();
    auto r1 = arena1.Alloc(128, alignof(std::max_align_t));
    ASSERT_TRUE(r1.has_value());
    EXPECT_NE(*r0, *r1);
}

TEST(RenderExtraction, FrameContext_ResetPreparedStateClearsOwnedRenderWorld)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = Runtime::RenderWorld{
        .Alpha = 0.5,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 800, .Height = 600}),
        .World = sceneManager.CreateReadonlySnapshot(),
    };
    frame.Prepared = true;
    frame.Submitted = true;

    ASSERT_NE(frame.GetPreparedRenderWorld(), nullptr);
    EXPECT_EQ(frame.GetPreparedRenderWorld()->World.CommittedTick, 1u);

    frame.ResetPreparedState();

    EXPECT_EQ(frame.GetPreparedRenderWorld(), nullptr);
    EXPECT_FALSE(frame.Prepared);
    EXPECT_TRUE(frame.Submitted);
}

TEST(RenderExtraction, SanitizeFrameContextCount_ClampsToSupportedBounds)
{
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(0u), Runtime::MinFrameContexts);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(1u), Runtime::MinFrameContexts);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(2u), 2u);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(3u), 3u);
    EXPECT_EQ(Runtime::SanitizeFrameContextCount(99u), Runtime::MaxFrameContexts);
}

TEST(RenderExtraction, FrameContextRing_DefaultsToDoubleBuffering)
{
    Runtime::FrameContextRing ring;
    EXPECT_EQ(ring.GetFramesInFlight(), Runtime::DefaultFrameContexts);

    const Runtime::FrameContext& frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 1600, .Height = 900});

    EXPECT_EQ(frame0.FrameNumber, 0u);
    EXPECT_EQ(frame0.PreviousFrameNumber, Runtime::InvalidFrameNumber);
    EXPECT_EQ(frame0.SlotIndex, 0u);
    EXPECT_EQ(frame0.FramesInFlight, Runtime::DefaultFrameContexts);
    EXPECT_TRUE(frame0.Viewport.IsValid());
}

TEST(RenderExtraction, FrameContextRing_ReusesBoundedSlotsByFrameNumberModulo)
{
    Runtime::FrameContextRing ring(3u);

    Runtime::FrameContext& frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    const Runtime::FrameContext* frame0Address = &frame0;

    Runtime::FrameContext& frame1 =
        ring.BeginFrame(1u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    Runtime::FrameContext& frame2 =
        ring.BeginFrame(2u, Runtime::RenderViewport{.Width = 640, .Height = 480});
    frame0.Prepared = true;
    frame0.Submitted = true;
    frame0.LastSubmittedTimelineValue = 42u;
    frame0.PreparedRenderWorld = Runtime::RenderWorld{
        .Alpha = 0.25,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 640, .Height = 480}),
    };

    Runtime::FrameContext& frame3 =
        ring.BeginFrame(3u, Runtime::RenderViewport{.Width = 640, .Height = 480});

    EXPECT_EQ(frame0.SlotIndex, 0u);
    EXPECT_EQ(frame1.SlotIndex, 1u);
    EXPECT_EQ(frame2.SlotIndex, 2u);
    EXPECT_EQ(frame3.SlotIndex, 0u);
    EXPECT_EQ(frame3.PreviousFrameNumber, 0u);
    EXPECT_EQ(frame3.FramesInFlight, 3u);
    EXPECT_EQ(&frame3, frame0Address);
    EXPECT_FALSE(frame3.Prepared);
    EXPECT_FALSE(frame3.Submitted);
    EXPECT_TRUE(frame3.ReusedSubmittedSlot);
    EXPECT_EQ(frame3.LastSubmittedTimelineValue, 42u);
    EXPECT_EQ(frame3.GetPreparedRenderWorld(), nullptr);
}

TEST(RenderExtraction, FrameContextRing_DoesNotFlagReuseWhenPriorSlotWasNotSubmitted)
{
    Runtime::FrameContextRing ring(2u);

    Runtime::FrameContext& frame0 =
        ring.BeginFrame(0u, Runtime::RenderViewport{.Width = 800, .Height = 600});
    frame0.Prepared = true;
    frame0.Submitted = false;

    Runtime::FrameContext& frame2 =
        ring.BeginFrame(2u, Runtime::RenderViewport{.Width = 800, .Height = 600});

    EXPECT_FALSE(frame2.ReusedSubmittedSlot);
}

TEST(RenderExtraction, MakeRenderFrameInput_SanitizesAlphaAndCapturesSnapshotGeneration)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    camera.AspectRatio = 16.0f / 9.0f;

    const Runtime::RenderFrameInput input = Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1920, .Height = 1080},
        4.0);

    EXPECT_TRUE(input.IsValid());
    EXPECT_DOUBLE_EQ(input.Alpha, 1.0);
    EXPECT_EQ(input.World.CommittedTick, 2u);
    EXPECT_EQ(input.View.Camera.Position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(input.View.CameraPosition, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(input.View.Viewport.Width, 1920u);
    EXPECT_EQ(input.View.Viewport.Height, 1080u);
}

TEST(RenderExtraction, WorldSnapshot_DetectsAuthoritativeCommitDrift)
{
    Runtime::SceneManager sceneManager;

    const Runtime::WorldSnapshot snapshot = sceneManager.CreateReadonlySnapshot();
    EXPECT_TRUE(snapshot.IsValid());
    EXPECT_TRUE(snapshot.IsCurrent());
    EXPECT_FALSE(snapshot.HasCommitDrift());
    EXPECT_EQ(snapshot.GetCurrentCommittedTick(), 0u);

    sceneManager.CommitFixedTick();

    EXPECT_FALSE(snapshot.IsCurrent());
    EXPECT_TRUE(snapshot.HasCommitDrift());
    EXPECT_EQ(snapshot.CommittedTick, 0u);
    EXPECT_EQ(snapshot.GetCurrentCommittedTick(), 1u);
}

TEST(RenderExtraction, ExtractRenderWorld_CopiesImmutableFrameInputs)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(4.0f, 5.0f, 6.0f);
    camera.AspectRatio = 4.0f / 3.0f;

    const Runtime::RenderFrameInput input = Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1280, .Height = 720},
        0.25);

    camera.Position = glm::vec3(99.0f);

    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(input);

    EXPECT_TRUE(renderWorld.IsValid());
    EXPECT_DOUBLE_EQ(renderWorld.Alpha, 0.25);
    EXPECT_EQ(renderWorld.World.CommittedTick, 1u);
    EXPECT_EQ(renderWorld.View.Camera.Position, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(renderWorld.View.CameraForward, renderWorld.View.Camera.GetForward());
    EXPECT_EQ(renderWorld.View.Viewport.Width, 1280u);
    EXPECT_EQ(renderWorld.View.Viewport.Height, 720u);
    EXPECT_EQ(renderWorld.View.ViewProjectionMatrix,
              renderWorld.View.ProjectionMatrix * renderWorld.View.ViewMatrix);
}

TEST(RenderExtraction, ExtractRenderWorld_BuildsImmutablePickingPacketsFromAuthoritativeScene)
{
    Runtime::SceneManager sceneManager;
    auto& registry = sceneManager.GetRegistry();

    auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = mesh->AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
    const auto v1 = mesh->AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
    const auto v2 = mesh->AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(mesh->AddTriangle(v0, v1, v2).has_value());

    const entt::entity surfaceEntity = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(
        surfaceEntity,
        ECS::Components::Transform::Component{.Position = glm::vec3(1.0f, 2.0f, 3.0f)});
    registry.emplace<ECS::Components::Selection::PickID>(surfaceEntity, ECS::Components::Selection::PickID{.Value = 11u});
    auto& surface = registry.emplace<ECS::Surface::Component>(surfaceEntity, ECS::Surface::Component{
        .Geometry = Geometry::GeometryHandle{10u, 1u},
    });
    surface.CachedFaceColors = {0xFF102030u};
    surface.CachedVertexColors = {0xFF405060u, 0xFF708090u, 0xFFA0B0C0u};
    surface.UseNearestVertexColors = true;
    surface.CachedVertexLabels = {2u, 1u, 0u};
    surface.CachedCentroids = {
        ECS::Surface::Component::CentroidEntry{glm::vec3(1.0f, 0.0f, 0.0f), 0xFF112233u},
        ECS::Surface::Component::CentroidEntry{glm::vec3(0.0f, 1.0f, 0.0f), 0xFF445566u},
    };
    auto& meshData = registry.emplace<ECS::Mesh::Data>(surfaceEntity);
    meshData.MeshRef = mesh;

    const entt::entity lineEntity = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(
        lineEntity,
        ECS::Components::Transform::Component{.Position = glm::vec3(-1.0f, 0.0f, 0.0f)});
    registry.emplace<ECS::Components::Selection::PickID>(lineEntity, ECS::Components::Selection::PickID{.Value = 22u});
    auto& line = registry.emplace<ECS::Line::Component>(lineEntity);
    line.Geometry = Geometry::GeometryHandle{20u, 1u};
    line.EdgeView = Geometry::GeometryHandle{21u, 1u};
    line.EdgeCount = 4u;
    line.Width = 3.5f;
    line.HasPerEdgeColors = true;
    line.CachedEdgeColors = {0xFF010203u, 0xFF040506u, 0xFF070809u, 0xFF0A0B0Cu};
    line.SourceDomain = ECS::Line::Domain::GraphEdge;
    auto& graphData = registry.emplace<ECS::Graph::Data>(lineEntity);
    graphData.GraphRef = std::make_shared<Geometry::Graph::Graph>();
    graphData.CachedEdgeColors = {0xFF111213u, 0xFF141516u, 0xFF171819u, 0xFF1A1B1Cu};

    const entt::entity pointEntity = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(
        pointEntity,
        ECS::Components::Transform::Component{.Position = glm::vec3(0.0f, 5.0f, 0.0f)});
    registry.emplace<ECS::Components::Transform::WorldMatrix>(
        pointEntity,
        ECS::Components::Transform::WorldMatrix{
            .Matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)),
        });
    registry.emplace<ECS::Components::Selection::PickID>(pointEntity, ECS::Components::Selection::PickID{.Value = 33u});
    auto& point = registry.emplace<ECS::Point::Component>(pointEntity, ECS::Point::Component{
        .Geometry = Geometry::GeometryHandle{30u, 1u},
        .Size = 0.125f,
    });
    point.HasPerPointColors = true;
    point.HasPerPointRadii = true;
    point.HasPerPointNormals = true;
    point.SourceDomain = ECS::Point::Domain::CloudPoint;
    auto& cloudData = registry.emplace<ECS::PointCloud::Data>(pointEntity);
    cloudData.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>();
    cloudData.CachedColors = {0xFF212223u, 0xFF242526u};
    cloudData.CachedRadii = {0.25f, 0.5f};

    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 800, .Height = 600},
        0.5));

    ASSERT_TRUE(renderWorld.IsValid());
    ASSERT_EQ(renderWorld.SurfacePicking.size(), 1u);
    ASSERT_EQ(renderWorld.LinePicking.size(), 1u);
    ASSERT_EQ(renderWorld.PointPicking.size(), 1u);
    ASSERT_EQ(renderWorld.SurfaceDraws.size(), 1u);
    ASSERT_EQ(renderWorld.LineDraws.size(), 1u);
    ASSERT_EQ(renderWorld.PointDraws.size(), 1u);

    const auto& surfacePacket = renderWorld.SurfacePicking.front();
    EXPECT_EQ(surfacePacket.Geometry, Geometry::GeometryHandle(10u, 1u));
    EXPECT_EQ(surfacePacket.EntityId, 11u);
    EXPECT_EQ(surfacePacket.WorldMatrix[3], glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    EXPECT_EQ(surfacePacket.TriangleFaceIds.size(), 1u);
    EXPECT_EQ(surfacePacket.TriangleFaceIds.front(), 0u);

    const auto& linePacket = renderWorld.LinePicking.front();
    EXPECT_EQ(linePacket.Geometry, Geometry::GeometryHandle(20u, 1u));
    EXPECT_EQ(linePacket.EdgeView, Geometry::GeometryHandle(21u, 1u));
    EXPECT_EQ(linePacket.EntityId, 22u);
    EXPECT_FLOAT_EQ(linePacket.Width, 3.5f);
    EXPECT_EQ(linePacket.EdgeCount, 4u);
    EXPECT_EQ(linePacket.WorldMatrix[3], glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f));

    const auto& pointPacket = renderWorld.PointPicking.front();
    EXPECT_EQ(pointPacket.Geometry, Geometry::GeometryHandle(30u, 1u));
    EXPECT_EQ(pointPacket.EntityId, 33u);
    EXPECT_FLOAT_EQ(pointPacket.Size, 0.125f);
    EXPECT_EQ(pointPacket.WorldMatrix[3], glm::vec4(0.0f, 5.0f, 0.0f, 1.0f));

    const auto& surfaceDraw = renderWorld.SurfaceDraws.front();
    EXPECT_EQ(surfaceDraw.Geometry, Geometry::GeometryHandle(10u, 1u));
    ASSERT_EQ(surfaceDraw.FaceColors.size(), 1u);
    EXPECT_EQ(surfaceDraw.FaceColors.front(), 0xFF102030u);
    ASSERT_EQ(surfaceDraw.VertexColors.size(), 3u);
    EXPECT_TRUE(surfaceDraw.UseNearestVertexColors);
    EXPECT_EQ(surfaceDraw.VertexLabels, std::vector<uint32_t>({2u, 1u, 0u}));
    ASSERT_EQ(surfaceDraw.Centroids.size(), 2u);
    EXPECT_EQ(surfaceDraw.Centroids.front().PackedColor, 0xFF112233u);

    const auto& lineDraw = renderWorld.LineDraws.front();
    EXPECT_EQ(lineDraw.Geometry, Geometry::GeometryHandle(20u, 1u));
    EXPECT_EQ(lineDraw.EdgeView, Geometry::GeometryHandle(21u, 1u));
    EXPECT_EQ(lineDraw.EntityKey, static_cast<uint32_t>(lineEntity));
    EXPECT_EQ(lineDraw.EdgeCount, 4u);
    EXPECT_EQ(lineDraw.EdgeColors, graphData.CachedEdgeColors);
    EXPECT_EQ(lineDraw.WorldMatrix[3], glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f));

    const auto& pointDraw = renderWorld.PointDraws.front();
    EXPECT_EQ(pointDraw.Geometry, Geometry::GeometryHandle(30u, 1u));
    EXPECT_EQ(pointDraw.EntityKey, static_cast<uint32_t>(pointEntity));
    EXPECT_TRUE(pointDraw.HasPerPointNormals);
    EXPECT_EQ(pointDraw.Colors, cloudData.CachedColors);
    EXPECT_EQ(pointDraw.Radii, cloudData.CachedRadii);
    EXPECT_EQ(pointDraw.WorldMatrix[3], glm::vec4(0.0f, 5.0f, 0.0f, 1.0f));
}

TEST(RenderExtraction, ExtractedRenderWorld_TracksWhenAuthoritativeSceneAdvancesPastExtraction)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 800, .Height = 600},
        0.5));

    ASSERT_TRUE(renderWorld.IsValid());
    EXPECT_TRUE(renderWorld.World.IsCurrent());
    EXPECT_FALSE(renderWorld.World.HasCommitDrift());

    sceneManager.CommitFixedTick();

    EXPECT_FALSE(renderWorld.World.IsCurrent());
    EXPECT_TRUE(renderWorld.World.HasCommitDrift());
    EXPECT_EQ(renderWorld.World.CommittedTick, 1u);
    EXPECT_EQ(renderWorld.World.GetCurrentCommittedTick(), 2u);
}

TEST(RenderExtraction, ExtractedRenderPacketsRemainStableAfterSceneMutation)
{
    Runtime::SceneManager sceneManager;
    auto& registry = sceneManager.GetRegistry();

    const entt::entity surfaceEntity = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(
        surfaceEntity,
        ECS::Components::Transform::Component{.Position = glm::vec3(1.0f, 0.0f, 0.0f)});
    registry.emplace<ECS::Components::Selection::PickID>(surfaceEntity, ECS::Components::Selection::PickID{.Value = 101u});
    auto& surface = registry.emplace<ECS::Surface::Component>(surfaceEntity, ECS::Surface::Component{
        .Geometry = Geometry::GeometryHandle{41u, 1u},
    });
    surface.CachedFaceColors = {0xFF010203u};

    const entt::entity lineEntity = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(
        lineEntity,
        ECS::Components::Transform::Component{.Position = glm::vec3(0.0f, 2.0f, 0.0f)});
    registry.emplace<ECS::Components::Selection::PickID>(lineEntity, ECS::Components::Selection::PickID{.Value = 202u});
    auto& line = registry.emplace<ECS::Line::Component>(lineEntity);
    line.Geometry = Geometry::GeometryHandle{51u, 1u};
    line.EdgeView = Geometry::GeometryHandle{52u, 1u};
    line.EdgeCount = 7u;
    line.Width = 2.5f;
    line.HasPerEdgeColors = true;
    line.CachedEdgeColors = {0xFF101112u, 0xFF131415u, 0xFF161718u, 0xFF191A1Bu,
                             0xFF1C1D1Eu, 0xFF1F2021u, 0xFF222324u};
    line.SourceDomain = ECS::Line::Domain::GraphEdge;
    auto& graphData = registry.emplace<ECS::Graph::Data>(lineEntity);
    graphData.GraphRef = std::make_shared<Geometry::Graph::Graph>();
    graphData.CachedEdgeColors = line.CachedEdgeColors;
    const std::vector<uint32_t> expectedLineColors = graphData.CachedEdgeColors;

    const entt::entity pointEntity = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(
        pointEntity,
        ECS::Components::Transform::Component{.Position = glm::vec3(0.0f, 0.0f, 4.0f)});
    registry.emplace<ECS::Components::Selection::PickID>(pointEntity, ECS::Components::Selection::PickID{.Value = 303u});
    auto& point = registry.emplace<ECS::Point::Component>(pointEntity);
    point.Geometry = Geometry::GeometryHandle{61u, 1u};
    point.HasPerPointColors = true;
    point.HasPerPointRadii = true;
    point.SourceDomain = ECS::Point::Domain::CloudPoint;
    auto& pointCloud = registry.emplace<ECS::PointCloud::Data>(pointEntity);
    pointCloud.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>();
    pointCloud.CachedColors = {0xFF252627u, 0xFF28292Au};
    pointCloud.CachedRadii = {0.2f, 0.4f};
    const std::vector<uint32_t> expectedPointColors = pointCloud.CachedColors;
    const std::vector<float> expectedPointRadii = pointCloud.CachedRadii;

    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    const Runtime::RenderWorld extracted = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1280, .Height = 720},
        0.5));

    ASSERT_EQ(extracted.SurfacePicking.size(), 1u);
    ASSERT_EQ(extracted.LinePicking.size(), 1u);
    ASSERT_EQ(extracted.PointPicking.size(), 1u);
    ASSERT_EQ(extracted.SurfaceDraws.size(), 1u);
    ASSERT_EQ(extracted.LineDraws.size(), 1u);
    ASSERT_EQ(extracted.PointDraws.size(), 1u);
    EXPECT_EQ(extracted.SurfacePicking.front().Geometry, Geometry::GeometryHandle(41u, 1u));
    EXPECT_EQ(extracted.LinePicking.front().Geometry, Geometry::GeometryHandle(51u, 1u));
    EXPECT_EQ(extracted.LinePicking.front().EdgeCount, 7u);
    EXPECT_EQ(extracted.SurfaceDraws.front().FaceColors, std::vector<uint32_t>({0xFF010203u}));
    EXPECT_EQ(extracted.LineDraws.front().EdgeColors, expectedLineColors);
    EXPECT_EQ(extracted.PointDraws.front().Colors, expectedPointColors);
    EXPECT_EQ(extracted.PointDraws.front().Radii, expectedPointRadii);

    // Mutate authoritative ECS after extraction.
    registry.get<ECS::Surface::Component>(surfaceEntity).Geometry = Geometry::GeometryHandle{};
    registry.get<ECS::Surface::Component>(surfaceEntity).CachedFaceColors = {0xFFFFFFFFu};
    registry.get<ECS::Line::Component>(lineEntity).EdgeCount = 0u;
    registry.get<ECS::Graph::Data>(lineEntity).CachedEdgeColors.clear();
    registry.get<ECS::PointCloud::Data>(pointEntity).CachedColors.clear();
    registry.get<ECS::PointCloud::Data>(pointEntity).CachedRadii.clear();
    const entt::entity latePoint = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(latePoint, ECS::Components::Transform::Component{});
    registry.emplace<ECS::Point::Component>(latePoint, ECS::Point::Component{
        .Geometry = Geometry::GeometryHandle{99u, 1u},
    });
    registry.emplace<ECS::PointCloud::Data>(latePoint, ECS::PointCloud::Data{
        .CloudRef = std::make_shared<Geometry::PointCloud::Cloud>()
    });

    // Extracted packets remain immutable snapshots and do not track live ECS mutations.
    ASSERT_EQ(extracted.SurfacePicking.size(), 1u);
    ASSERT_EQ(extracted.LinePicking.size(), 1u);
    ASSERT_EQ(extracted.PointPicking.size(), 1u);
    ASSERT_EQ(extracted.SurfaceDraws.size(), 1u);
    ASSERT_EQ(extracted.LineDraws.size(), 1u);
    ASSERT_EQ(extracted.PointDraws.size(), 1u);
    EXPECT_EQ(extracted.SurfacePicking.front().Geometry, Geometry::GeometryHandle(41u, 1u));
    EXPECT_EQ(extracted.LinePicking.front().Geometry, Geometry::GeometryHandle(51u, 1u));
    EXPECT_EQ(extracted.LinePicking.front().EdgeCount, 7u);
    EXPECT_EQ(extracted.SurfaceDraws.front().FaceColors, std::vector<uint32_t>({0xFF010203u}));
    EXPECT_EQ(extracted.LineDraws.front().EdgeColors, expectedLineColors);
    EXPECT_EQ(extracted.PointDraws.front().Colors, expectedPointColors);
    EXPECT_EQ(extracted.PointDraws.front().Radii, expectedPointRadii);
}

TEST(RenderExtraction, ExtractedSelectionWorkStateRemainsStableAfterMutation)
{
    Runtime::SceneManager sceneManager;
    auto& registry = sceneManager.GetRegistry();

    const entt::entity selectedEntity = registry.create();
    registry.emplace<ECS::Components::Selection::SelectedTag>(selectedEntity);

    sceneManager.CommitFixedTick();

    const Runtime::RenderWorld extracted = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        Graphics::CameraComponent{},
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1024, .Height = 768},
        0.0));

    EXPECT_TRUE(extracted.HasSelectionWork);

    registry.remove<ECS::Components::Selection::SelectedTag>(selectedEntity);
    EXPECT_FALSE(registry.any_of<ECS::Components::Selection::SelectedTag>(selectedEntity));

    // Extracted state is immutable and does not change with post-extraction ECS mutation.
    EXPECT_TRUE(extracted.HasSelectionWork);
}

TEST(RenderExtraction, ExtractedHtexPreviewPacketRemainsStableAfterMeshMutation)
{
    Runtime::SceneManager sceneManager;
    auto& registry = sceneManager.GetRegistry();

    auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = mesh->AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
    const auto v1 = mesh->AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
    const auto v2 = mesh->AddVertex(glm::vec3(0.0f, 1.0f, 0.0f));
    ASSERT_TRUE(mesh->AddTriangle(v0, v1, v2).has_value());

    const entt::entity meshEntity = registry.create();
    registry.emplace<ECS::Components::Selection::SelectedTag>(meshEntity);
    auto& meshData = registry.emplace<ECS::Mesh::Data>(meshEntity);
    meshData.MeshRef = mesh;
    meshData.KMeansResultRevision = 5u;
    meshData.KMeansCentroids = {glm::vec3(0.25f, 0.5f, 0.0f)};

    sceneManager.CommitFixedTick();

    const Runtime::RenderWorld extracted = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        Graphics::CameraComponent{},
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1024, .Height = 768},
        0.0));

    ASSERT_TRUE(extracted.HtexPatchPreview.has_value());
    ASSERT_TRUE(extracted.HtexPatchPreview->IsValid());
    EXPECT_EQ(extracted.HtexPatchPreview->SourceEntityId, static_cast<uint32_t>(meshEntity));
    EXPECT_EQ(extracted.HtexPatchPreview->KMeansResultRevision, 5u);
    ASSERT_EQ(extracted.HtexPatchPreview->KMeansCentroids.size(), 1u);

    const glm::vec3 extractedPosition = extracted.HtexPatchPreview->Mesh->Position(v0);
    mesh->Position(v0) = glm::vec3(99.0f, 98.0f, 97.0f);

    EXPECT_EQ(extracted.HtexPatchPreview->Mesh->Position(v0), extractedPosition);
}

TEST(RenderExtraction, RenderPassContext_ExposesReadonlyExtractedPackets)
{
    using SurfaceSpan = decltype(std::declval<Graphics::RenderPassContext>().SurfaceDrawPackets);
    using LineSpan = decltype(std::declval<Graphics::RenderPassContext>().LineDrawPackets);
    using PointSpan = decltype(std::declval<Graphics::RenderPassContext>().PointDrawPackets);
    using HtexPtr = decltype(std::declval<Graphics::RenderPassContext>().HtexPatchPreview);

    static_assert(std::is_same_v<SurfaceSpan, std::span<const Graphics::SurfaceDrawPacket>>);
    static_assert(std::is_same_v<LineSpan, std::span<const Graphics::LineDrawPacket>>);
    static_assert(std::is_same_v<PointSpan, std::span<const Graphics::PointDrawPacket>>);
    static_assert(std::is_same_v<HtexPtr, const Graphics::HtexPatchPreviewPacket*>);
    SUCCEED();
}

TEST(RenderExtraction, FrameContext_OwnsPreparedRenderWorldAfterMove)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(7.0f, 8.0f, 9.0f);

    Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 1024, .Height = 768},
        0.75));

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = std::move(renderWorld);
    frame.Prepared = true;

    const Runtime::RenderWorld* owned = frame.GetPreparedRenderWorld();
    ASSERT_NE(owned, nullptr);
    EXPECT_DOUBLE_EQ(owned->Alpha, 0.75);
    EXPECT_EQ(owned->View.Camera.Position, glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_EQ(owned->View.CameraPosition, glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_EQ(owned->World.CommittedTick, 1u);
}

TEST(RenderExtraction, LightEnvironmentPacket_DefaultsMatchPreviousHardcodedValues)
{
    // Verify that LightEnvironmentPacket defaults produce the same lighting
    // as the previously hardcoded shader constants (direction (1,1,1) normalised,
    // white light, 0.1 ambient).
    Graphics::LightEnvironmentPacket pkt{};
    const glm::vec3 expectedDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_NEAR(pkt.LightDirection.x, expectedDir.x, 1e-5f);
    EXPECT_NEAR(pkt.LightDirection.y, expectedDir.y, 1e-5f);
    EXPECT_NEAR(pkt.LightDirection.z, expectedDir.z, 1e-5f);
    EXPECT_FLOAT_EQ(pkt.LightIntensity, 1.0f);
    EXPECT_EQ(pkt.LightColor, glm::vec3(1.0f));
    EXPECT_EQ(pkt.AmbientColor, glm::vec3(1.0f));
    EXPECT_FLOAT_EQ(pkt.AmbientIntensity, 0.1f);
    EXPECT_FALSE(pkt.Shadows.Enabled);
    EXPECT_EQ(pkt.Shadows.CascadeCount, Graphics::ShadowParams::MaxCascades);
    EXPECT_FLOAT_EQ(pkt.Shadows.CascadeSplits[0], 0.10f);
    EXPECT_FLOAT_EQ(pkt.Shadows.CascadeSplits[1], 0.25f);
    EXPECT_FLOAT_EQ(pkt.Shadows.CascadeSplits[2], 0.55f);
    EXPECT_FLOAT_EQ(pkt.Shadows.CascadeSplits[3], 1.00f);
    EXPECT_FLOAT_EQ(pkt.Shadows.DepthBias, 0.0015f);
    EXPECT_FLOAT_EQ(pkt.Shadows.NormalBias, 0.0025f);
    EXPECT_FLOAT_EQ(pkt.Shadows.PcfFilterRadius, 1.5f);
    EXPECT_FLOAT_EQ(pkt.Shadows.SplitLambda, 0.85f);
}

TEST(RenderExtraction, ComputeCascadeSplitDistances_PracticalSchemeProducesMonotonicNormalizedSplits)
{
    const auto splits = Graphics::ComputeCascadeSplitDistances(
        0.1f,
        100.0f,
        Graphics::ShadowParams::MaxCascades,
        0.85f);

    EXPECT_GE(splits[0], 0.0f);
    EXPECT_LE(splits[3], 1.0f);
    EXPECT_LE(splits[0], splits[1]);
    EXPECT_LE(splits[1], splits[2]);
    EXPECT_LE(splits[2], splits[3]);
    EXPECT_FLOAT_EQ(splits[3], 1.0f);
}

TEST(RenderExtraction, ComputeCascadeSplitDistances_HandlesDegenerateAndOutOfRangeInputs)
{
    const auto splits = Graphics::ComputeCascadeSplitDistances(
        0.0f,   // invalid near
        0.0f,   // invalid far
        0u,     // invalid cascade count
        2.0f);  // invalid lambda

    EXPECT_FLOAT_EQ(splits[0], 1.0f);
    EXPECT_FLOAT_EQ(splits[1], 1.0f);
    EXPECT_FLOAT_EQ(splits[2], 1.0f);
    EXPECT_FLOAT_EQ(splits[3], 1.0f);
}

// =========================================================================
// A2 — CSM: Focused tests that shadow pass produces non-trivial depth
// for a known scene.  CPU-side validation of ComputeCascadeViewProjections:
// project known world-space geometry through cascade light-VP matrices and
// verify the resulting NDC depth is in the valid (0,1) range and varies
// across vertices (i.e. the shadow map would contain non-trivial depth).
// =========================================================================

namespace
{
    // Helper: project a world-space point through a cascade VP matrix and
    // return the resulting NDC depth (Vulkan convention: z in [0,1]).
    float ProjectShadowDepth(const glm::mat4& cascadeVP, const glm::vec3& worldPos)
    {
        glm::vec4 clip = cascadeVP * glm::vec4(worldPos, 1.0f);
        return clip.z / clip.w;
    }

    // Configurable test camera for CSM tests.
    struct ShadowTestCamera
    {
        glm::mat4 View;
        glm::mat4 Proj;
        float Near = 0.1f;
        float Far  = 100.0f;

        static ShadowTestCamera Create(const glm::vec3& eye, const glm::vec3& center,
                                       const glm::vec3& up,
                                       float fovDeg = 60.0f, float aspect = 16.0f / 9.0f,
                                       float near = 0.1f, float far = 100.0f)
        {
            ShadowTestCamera cam;
            cam.Near = near;
            cam.Far  = far;
            cam.View = glm::lookAt(eye, center, up);
            cam.Proj = glm::perspective(glm::radians(fovDeg), aspect, near, far);
            cam.Proj[1][1] *= -1.0f; // Vulkan Y-flip
            return cam;
        }

        static ShadowTestCamera LookingAtOrigin(float fovDeg = 60.0f, float aspect = 16.0f / 9.0f,
                                                 float near = 0.1f, float far = 100.0f)
        {
            return Create(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                          glm::vec3(0.0f, 1.0f, 0.0f), fovDeg, aspect, near, far);
        }
    };

    // Verify every element of a cascade VP matrix is finite (no NaN/inf).
    void ExpectFiniteMatrix(const glm::mat4& m, uint32_t cascadeIndex)
    {
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                EXPECT_TRUE(std::isfinite(m[col][row]))
                    << "Cascade " << cascadeIndex
                    << " has non-finite value at [" << col << "][" << row << "]";
    }
}

TEST(RenderExtraction, CascadeVPs_ProduceNonTrivialDepthForKnownScene)
{
    // A camera looking at a unit cube at the origin, lit by a directional
    // light from the upper-right.
    const auto cam = ShadowTestCamera::LookingAtOrigin();
    const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, Graphics::ShadowParams::MaxCascades, 0.85f);

    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits,
        Graphics::ShadowParams::MaxCascades, 2048u,
        cam.Near, cam.Far);

    // Unit cube vertices at origin — the standard test scene.
    constexpr glm::vec3 cubeVerts[] = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f},
    };

    // The cube is close to the camera (within the first few cascades).
    // At least one cascade must produce non-trivial, in-range depth values.
    bool anyNonTrivialCascade = false;

    for (uint32_t c = 0; c < Graphics::ShadowParams::MaxCascades; ++c)
    {
        const glm::mat4& vp = cascadeVPs[c];

        // Cascade VP must not be identity (degenerate).
        EXPECT_NE(vp, glm::mat4(1.0f)) << "Cascade " << c << " is identity (degenerate)";
        ExpectFiniteMatrix(vp, c);

        float minDepth = std::numeric_limits<float>::max();
        float maxDepth = std::numeric_limits<float>::lowest();
        int inRangeCount = 0;

        for (const auto& v : cubeVerts)
        {
            const float d = ProjectShadowDepth(vp, v);
            minDepth = std::min(minDepth, d);
            maxDepth = std::max(maxDepth, d);
            if (d > 0.0f && d < 1.0f)
                ++inRangeCount;
        }

        // "Non-trivial" means: at least 2 vertices in range AND depth
        // varies by more than 1% of the [0,1] range.  A nearly-flat
        // depth (e.g. all at 0.500x) would indicate a degenerate ortho.
        if (inRangeCount >= 2 && (maxDepth - minDepth) > 0.01f)
            anyNonTrivialCascade = true;
    }

    EXPECT_TRUE(anyNonTrivialCascade)
        << "No cascade produced non-trivial depth for a unit cube at origin";
}

TEST(RenderExtraction, CascadeVPs_AllCascadesAreDistinct)
{
    const auto cam = ShadowTestCamera::LookingAtOrigin();
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, Graphics::ShadowParams::MaxCascades, 0.85f);

    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits,
        Graphics::ShadowParams::MaxCascades, 2048u,
        cam.Near, cam.Far);

    // Each cascade covers a different frustum slice, so their VP matrices
    // must differ (different orthographic extents or offsets).
    for (uint32_t i = 0; i < Graphics::ShadowParams::MaxCascades; ++i)
    {
        for (uint32_t j = i + 1; j < Graphics::ShadowParams::MaxCascades; ++j)
        {
            EXPECT_NE(cascadeVPs[i], cascadeVPs[j])
                << "Cascades " << i << " and " << j << " have identical VP matrices";
        }
    }
}

TEST(RenderExtraction, CascadeVPs_LightAlignedWithUpVector)
{
    // When light direction is nearly aligned with the world up vector,
    // the lookAt fallback to (0,0,1) up must still produce valid matrices.
    const auto cam = ShadowTestCamera::LookingAtOrigin();
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.001f, 1.0f, 0.001f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, 2u, 0.5f);

    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits, 2u, 2048u,
        cam.Near, cam.Far);

    for (uint32_t c = 0; c < 2u; ++c)
    {
        EXPECT_NE(cascadeVPs[c], glm::mat4(1.0f))
            << "Cascade " << c << " is degenerate with up-aligned light";
        ExpectFiniteMatrix(cascadeVPs[c], c);
    }
}

TEST(RenderExtraction, CascadeVPs_SingleCascade)
{
    const auto cam = ShadowTestCamera::LookingAtOrigin();
    const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, 1u, 0.85f);

    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits, 1u, 2048u,
        cam.Near, cam.Far);

    // Only cascade 0 should be populated; the rest remain identity.
    EXPECT_NE(cascadeVPs[0], glm::mat4(1.0f));
    ExpectFiniteMatrix(cascadeVPs[0], 0);
    for (uint32_t c = 1; c < Graphics::ShadowParams::MaxCascades; ++c)
        EXPECT_EQ(cascadeVPs[c], glm::mat4(1.0f))
            << "Cascade " << c << " should be identity when only 1 cascade is active";

    // Project a point visible from the camera and verify meaningful depth.
    const float d = ProjectShadowDepth(cascadeVPs[0], glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(std::isfinite(d)) << "Depth for origin is not finite";
}

TEST(RenderExtraction, CascadeVPs_ExtremeNearFarRatio)
{
    // Near/far ratio of 10^5 — exercises the logarithmic split path
    // which can produce very skewed cascade partitions.
    const auto cam = ShadowTestCamera::LookingAtOrigin(60.0f, 16.0f / 9.0f, 0.001f, 10000.0f);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 0.5f, 0.7f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, Graphics::ShadowParams::MaxCascades, 0.85f);

    // Splits must still be monotonic and normalized.
    for (uint32_t i = 0; i < Graphics::ShadowParams::MaxCascades; ++i)
    {
        EXPECT_GE(splits[i], 0.0f);
        EXPECT_LE(splits[i], 1.0f);
        if (i > 0)
            EXPECT_GE(splits[i], splits[i - 1]);
    }

    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits,
        Graphics::ShadowParams::MaxCascades, 2048u,
        cam.Near, cam.Far);

    for (uint32_t c = 0; c < Graphics::ShadowParams::MaxCascades; ++c)
    {
        EXPECT_NE(cascadeVPs[c], glm::mat4(1.0f))
            << "Cascade " << c << " is identity under extreme near/far ratio";
        ExpectFiniteMatrix(cascadeVPs[c], c);
    }
}

TEST(RenderExtraction, CascadeVPs_RotatedCamera)
{
    // Camera looking sideways (+X) instead of the typical -Z to exercise
    // frustum corner unprojection with a non-trivial view matrix.
    const auto cam = ShadowTestCamera::Create(
        glm::vec3(-5.0f, 1.0f, 0.0f),  // eye: left of origin
        glm::vec3(10.0f, 0.0f, 0.0f),  // center: looking right
        glm::vec3(0.0f, 1.0f, 0.0f),
        70.0f, 2.0f, 0.1f, 50.0f);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.2f, 1.0f, -0.3f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, Graphics::ShadowParams::MaxCascades, 0.85f);

    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits,
        Graphics::ShadowParams::MaxCascades, 2048u,
        cam.Near, cam.Far);

    // All cascades must be non-degenerate and finite.
    for (uint32_t c = 0; c < Graphics::ShadowParams::MaxCascades; ++c)
    {
        EXPECT_NE(cascadeVPs[c], glm::mat4(1.0f))
            << "Cascade " << c << " is identity with rotated camera";
        ExpectFiniteMatrix(cascadeVPs[c], c);
    }

    // A point along the camera's view direction should produce valid depth.
    const float d = ProjectShadowDepth(cascadeVPs[0], glm::vec3(0.0f, 0.5f, 0.0f));
    EXPECT_TRUE(std::isfinite(d));
}

TEST(RenderExtraction, CascadeVPs_ZeroResolutionDisablesTexelSnapping)
{
    // When cascadeResolution is 0, the texel-snapping code path is skipped.
    // The resulting matrices must still be valid and non-degenerate.
    const auto cam = ShadowTestCamera::LookingAtOrigin();
    const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

    const auto splits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, 2u, 0.85f);

    const auto noSnap = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits, 2u, 0u, cam.Near, cam.Far);

    const auto withSnap = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, splits, 2u, 2048u, cam.Near, cam.Far);

    for (uint32_t c = 0; c < 2u; ++c)
    {
        EXPECT_NE(noSnap[c], glm::mat4(1.0f))
            << "Cascade " << c << " is identity with resolution=0";
        ExpectFiniteMatrix(noSnap[c], c);
        ExpectFiniteMatrix(withSnap[c], c);
    }
}

TEST(RenderExtraction, CascadeVPs_EndToEnd_ProducesValidGpuPayload)
{
    // Full CPU-side shadow pipeline: splits -> VP matrices -> packed GPU data.
    // This mirrors the runtime path: ComputeCascadeSplitDistances ->
    // ComputeCascadeViewProjections -> PackShadowCascadeData.
    const auto cam = ShadowTestCamera::LookingAtOrigin(45.0f, 1.0f, 0.5f, 200.0f);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.3f, 0.8f, 0.5f));

    Graphics::ShadowParams params{};
    params.Enabled = true;
    params.CascadeCount = 3u;
    params.SplitLambda = 0.75f;
    params.DepthBias = 0.002f;
    params.NormalBias = 0.003f;
    params.PcfFilterRadius = 2.5f;

    // Step 1: Compute split distances.
    params.CascadeSplits = Graphics::ComputeCascadeSplitDistances(
        cam.Near, cam.Far, params.CascadeCount, params.SplitLambda);

    for (uint32_t i = 0; i < params.CascadeCount; ++i)
    {
        EXPECT_GE(params.CascadeSplits[i], 0.0f);
        EXPECT_LE(params.CascadeSplits[i], 1.0f);
        if (i > 0)
            EXPECT_GE(params.CascadeSplits[i], params.CascadeSplits[i - 1])
                << "Split " << i << " is not monotonically increasing";
    }

    // Step 2: Compute cascade VP matrices.
    const auto cascadeVPs = Graphics::ComputeCascadeViewProjections(
        cam.View, cam.Proj, lightDir, params.CascadeSplits,
        params.CascadeCount, 2048u, cam.Near, cam.Far);

    // Step 3: Pack into GPU-ready struct.
    const Graphics::ShadowCascadeData packed = Graphics::PackShadowCascadeData(
        params, cascadeVPs);

    EXPECT_EQ(packed.CascadeCount, 3u);
    EXPECT_FLOAT_EQ(packed.DepthBias, 0.002f);
    EXPECT_FLOAT_EQ(packed.NormalBias, 0.003f);
    EXPECT_FLOAT_EQ(packed.PcfFilterRadius, 2.5f);

    // Packed matrices must match computed matrices for active cascades.
    for (uint32_t c = 0; c < params.CascadeCount; ++c)
        EXPECT_EQ(packed.LightViewProjection[c], cascadeVPs[c])
            << "Packed cascade " << c << " matrix differs from computed";

    // Unused cascade slot (index 3) should be identity.
    EXPECT_EQ(packed.LightViewProjection[3], glm::mat4(1.0f));

    // Project scene geometry through each active cascade and verify at least
    // one cascade produces meaningful depth values.
    const glm::vec3 scenePoints[] = {
        {0.0f, 0.0f, 0.0f},     // origin
        {0.0f, 1.0f, 0.0f},     // above origin
        {2.0f, 0.0f, -3.0f},    // offset in view
        {-1.0f, 0.5f, -10.0f},  // farther away
    };

    bool anyProducesDepth = false;
    for (uint32_t c = 0; c < packed.CascadeCount; ++c)
    {
        int validCount = 0;
        for (const auto& p : scenePoints)
        {
            const float d = ProjectShadowDepth(packed.LightViewProjection[c], p);
            if (std::isfinite(d) && d > 0.0f && d < 1.0f)
                ++validCount;
        }
        if (validCount >= 2)
            anyProducesDepth = true;
    }
    EXPECT_TRUE(anyProducesDepth)
        << "No cascade in the packed data produces valid depth for known scene points";
}

TEST(RenderExtraction, PackShadowCascadeData_SanitizesAndPacksForGpuConsumption)
{
    Graphics::ShadowParams shadows{};
    shadows.CascadeCount = 0u; // invalid, should clamp to 1
    shadows.CascadeSplits = {0.6f, 0.2f, 1.4f, 0.7f}; // non-monotonic and out of range
    shadows.DepthBias = 0.003f;
    shadows.NormalBias = 0.004f;
    shadows.PcfFilterRadius = 2.0f;

    const std::array<glm::mat4, 2> matrices{
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)),
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 2.0f, 2.0f))
    };

    const Graphics::ShadowCascadeData packed = Graphics::PackShadowCascadeData(shadows, matrices);

    EXPECT_EQ(packed.CascadeCount, 1u);
    EXPECT_FLOAT_EQ(packed.DepthBias, 0.003f);
    EXPECT_FLOAT_EQ(packed.NormalBias, 0.004f);
    EXPECT_FLOAT_EQ(packed.PcfFilterRadius, 2.0f);
    EXPECT_FLOAT_EQ(packed.SplitDistances[0], 0.6f);
    EXPECT_FLOAT_EQ(packed.SplitDistances[1], 0.6f);
    EXPECT_FLOAT_EQ(packed.SplitDistances[2], 1.0f);
    EXPECT_FLOAT_EQ(packed.SplitDistances[3], 1.0f);
    EXPECT_EQ(packed.LightViewProjection[0], matrices[0]);
    EXPECT_EQ(packed.LightViewProjection[1], matrices[1]);
    EXPECT_EQ(packed.LightViewProjection[2], glm::mat4(1.0f));
    EXPECT_EQ(packed.LightViewProjection[3], glm::mat4(1.0f));
}

TEST(RenderExtraction, ExtractRenderWorld_PopulatesDefaultLightEnvironmentPacket)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        Graphics::CameraComponent{},
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 800, .Height = 600},
        0.0));

    EXPECT_TRUE(renderWorld.IsValid());
    const auto& light = renderWorld.Lighting;
    EXPECT_FLOAT_EQ(light.LightIntensity, 1.0f);
    EXPECT_FLOAT_EQ(light.AmbientIntensity, 0.1f);
    EXPECT_EQ(light.LightColor, glm::vec3(1.0f));
    EXPECT_EQ(light.AmbientColor, glm::vec3(1.0f));
}

TEST(RenderExtraction, MakeRenderViewPacket_CapturesDerivedCameraState)
{
    Graphics::CameraComponent camera{};
    camera.Position = glm::vec3(-2.0f, 3.0f, 5.0f);
    camera.Fov = 60.0f;
    camera.AspectRatio = 21.0f / 9.0f;
    camera.Near = 0.25f;
    camera.Far = 250.0f;
    camera.Orientation = glm::normalize(glm::quat(glm::vec3(0.2f, -0.35f, 0.1f)));
    Graphics::UpdateMatrices(camera, camera.AspectRatio);

    const Runtime::RenderViewPacket view =
        Runtime::MakeRenderViewPacket(camera, Runtime::RenderViewport{.Width = 3440, .Height = 1440});

    EXPECT_TRUE(view.IsValid());
    EXPECT_EQ(view.Camera.Position, camera.Position);
    EXPECT_EQ(view.CameraPosition, camera.Position);
    EXPECT_EQ(view.CameraForward, camera.GetForward());
    EXPECT_EQ(view.ViewMatrix, camera.ViewMatrix);
    EXPECT_EQ(view.ProjectionMatrix, camera.ProjectionMatrix);
    EXPECT_EQ(view.ViewProjectionMatrix, camera.ProjectionMatrix * camera.ViewMatrix);
    EXPECT_EQ(view.NearPlane, camera.Near);
    EXPECT_EQ(view.FarPlane, camera.Far);
    EXPECT_EQ(view.AspectRatio, camera.AspectRatio);
    EXPECT_EQ(view.VerticalFieldOfViewDegrees, camera.Fov);
    EXPECT_EQ(view.Viewport.Width, 3440u);
    EXPECT_EQ(view.Viewport.Height, 1440u);
}

// =============================================================================
// EditorOverlayPacket
// =============================================================================

TEST(RenderExtraction, EditorOverlayPacket_DefaultHasNoDrawData)
{
    const Graphics::EditorOverlayPacket packet{};
    EXPECT_FALSE(packet.HasDrawData);
}

TEST(RenderExtraction, RenderWorld_DefaultEditorOverlayHasNoDrawData)
{
    Runtime::RenderWorld world{};
    EXPECT_FALSE(world.EditorOverlay.HasDrawData);
}

TEST(RenderExtraction, EditorOverlayPacket_PresentInExtractedRenderWorld)
{
    // ExtractRenderWorld produces an EditorOverlayPacket with HasDrawData=false
    // by default (the overlay is set by the orchestrator after extraction).
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();
    Graphics::CameraComponent camera{};
    camera.AspectRatio = 16.0f / 9.0f;
    Graphics::UpdateMatrices(camera, camera.AspectRatio);

    const Runtime::RenderFrameInput input{
        .Alpha = 0.0,
        .View = Runtime::MakeRenderViewPacket(camera, Runtime::RenderViewport{.Width = 800, .Height = 600}),
        .World = sceneManager.CreateReadonlySnapshot(),
    };
    Runtime::RenderWorld world = Runtime::ExtractRenderWorld(input);
    EXPECT_FALSE(world.EditorOverlay.HasDrawData);

    // Simulating what PrepareEditorOverlay does — set HasDrawData after extraction.
    world.EditorOverlay = Graphics::EditorOverlayPacket{.HasDrawData = true};
    EXPECT_TRUE(world.EditorOverlay.HasDrawData);
}

TEST(RenderExtraction, FrameContext_PreparedRenderWorldCarriesEditorOverlay)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = Runtime::RenderWorld{
        .Alpha = 0.5,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 800, .Height = 600}),
        .World = sceneManager.CreateReadonlySnapshot(),
        .EditorOverlay = Graphics::EditorOverlayPacket{.HasDrawData = true},
    };
    frame.Prepared = true;

    const Runtime::RenderWorld* prepared = frame.GetPreparedRenderWorld();
    ASSERT_NE(prepared, nullptr);
    EXPECT_TRUE(prepared->EditorOverlay.HasDrawData);
}

// --------------------------------------------------------------------------
// DebugDrawTriangles extraction packet
// --------------------------------------------------------------------------

TEST(RenderExtraction, RenderWorld_DebugDrawTrianglesDefaultEmpty)
{
    Runtime::RenderWorld world{};
    EXPECT_TRUE(world.DebugDrawTriangles.empty());
}

TEST(RenderExtraction, RenderWorld_DebugDrawTrianglesCarriesPopulatedData)
{
    Runtime::RenderWorld world{};

    const uint32_t kBlue = Graphics::DebugDraw::PackColor(40, 120, 255, 160);
    const glm::vec3 a{0.0f, 0.0f, 0.0f};
    const glm::vec3 b{1.0f, 0.0f, 0.0f};
    const glm::vec3 c{0.0f, 1.0f, 0.0f};
    const glm::vec3 n{0.0f, 0.0f, 1.0f};

    world.DebugDrawTriangles.push_back({a, kBlue, n, 0.0f});
    world.DebugDrawTriangles.push_back({b, kBlue, n, 0.0f});
    world.DebugDrawTriangles.push_back({c, kBlue, n, 0.0f});

    ASSERT_EQ(world.DebugDrawTriangles.size(), 3u);
    EXPECT_EQ(world.DebugDrawTriangles[0].Position, a);
    EXPECT_EQ(world.DebugDrawTriangles[1].Position, b);
    EXPECT_EQ(world.DebugDrawTriangles[2].Position, c);
    EXPECT_EQ(world.DebugDrawTriangles[0].Color, kBlue);
    EXPECT_EQ(world.DebugDrawTriangles[0].Normal, n);
}

TEST(RenderExtraction, RenderWorld_DebugDrawTrianglesSurviveMove)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Runtime::RenderWorld world{
        .Alpha = 0.5,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 800, .Height = 600}),
        .World = sceneManager.CreateReadonlySnapshot(),
    };

    const uint32_t kRed = Graphics::DebugDraw::PackColor(255, 0, 0, 255);
    world.DebugDrawTriangles.push_back({{1,2,3}, kRed, {0,0,1}, 0.0f});
    world.DebugDrawTriangles.push_back({{4,5,6}, kRed, {0,0,1}, 0.0f});
    world.DebugDrawTriangles.push_back({{7,8,9}, kRed, {0,0,1}, 0.0f});

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = std::move(world);
    frame.Prepared = true;

    const Runtime::RenderWorld* prepared = frame.GetPreparedRenderWorld();
    ASSERT_NE(prepared, nullptr);
    ASSERT_EQ(prepared->DebugDrawTriangles.size(), 3u);
    EXPECT_EQ(prepared->DebugDrawTriangles[0].Position, glm::vec3(1, 2, 3));
    EXPECT_EQ(prepared->DebugDrawTriangles[2].Position, glm::vec3(7, 8, 9));
}

// ---------------------------------------------------------------------------
// Extraction-time interaction snapshots
// ---------------------------------------------------------------------------

TEST(RenderExtraction, RenderWorld_PickRequestDefaultIsNotPending)
{
    Runtime::RenderWorld world{};
    EXPECT_FALSE(world.PickRequest.Pending);
    EXPECT_EQ(world.PickRequest.X, 0u);
    EXPECT_EQ(world.PickRequest.Y, 0u);
}

TEST(RenderExtraction, RenderWorld_DebugViewDefaultIsDisabled)
{
    Runtime::RenderWorld world{};
    EXPECT_FALSE(world.DebugView.Enabled);
    EXPECT_FALSE(world.DebugView.ShowInViewport);
    EXPECT_FALSE(world.DebugView.DisableCulling);
    EXPECT_FLOAT_EQ(world.DebugView.DepthNear, 0.1f);
    EXPECT_FLOAT_EQ(world.DebugView.DepthFar, 1000.0f);
}

TEST(RenderExtraction, RenderWorld_GpuSceneDefaultIsUnavailable)
{
    Runtime::RenderWorld world{};
    EXPECT_FALSE(world.GpuScene.Available);
    EXPECT_EQ(world.GpuScene.ActiveCountApprox, 0u);
}

TEST(RenderExtraction, RenderWorld_ExtractionSnapshotsCarryThroughFrameContext)
{
    Runtime::SceneManager sceneManager;
    sceneManager.CommitFixedTick();

    Runtime::RenderWorld world{
        .Alpha = 0.5,
        .View = Runtime::MakeRenderViewPacket(Graphics::CameraComponent{},
                                              Runtime::RenderViewport{.Width = 800, .Height = 600}),
        .World = sceneManager.CreateReadonlySnapshot(),
    };
    world.PickRequest = Graphics::PickRequestSnapshot{.Pending = true, .X = 42, .Y = 99};
    world.DebugView = Graphics::DebugViewSnapshot{.Enabled = true, .ShowInViewport = true};
    world.GpuScene = Graphics::GpuSceneSnapshot{.Available = true, .ActiveCountApprox = 128};

    Runtime::FrameContext frame{};
    frame.PreparedRenderWorld = std::move(world);
    frame.Prepared = true;

    const Runtime::RenderWorld* prepared = frame.GetPreparedRenderWorld();
    ASSERT_NE(prepared, nullptr);
    EXPECT_TRUE(prepared->PickRequest.Pending);
    EXPECT_EQ(prepared->PickRequest.X, 42u);
    EXPECT_EQ(prepared->PickRequest.Y, 99u);
    EXPECT_TRUE(prepared->DebugView.Enabled);
    EXPECT_TRUE(prepared->DebugView.ShowInViewport);
    EXPECT_TRUE(prepared->GpuScene.Available);
    EXPECT_EQ(prepared->GpuScene.ActiveCountApprox, 128u);
}

// ---------------------------------------------------------------------------
// InvalidateAfterResize
// ---------------------------------------------------------------------------

TEST(RenderExtraction, FrameContextRing_InvalidateAfterResize_ClearsSubmittedStateOnAllSlots)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 800, .Height = 600};

    // Simulate two submitted frames.
    Runtime::FrameContext& frame0 = ring.BeginFrame(0u, vp);
    frame0.Prepared = true;
    frame0.Submitted = true;
    frame0.LastSubmittedTimelineValue = 10u;

    Runtime::FrameContext& frame1 = ring.BeginFrame(1u, vp);
    frame1.Prepared = true;
    frame1.Submitted = true;
    frame1.LastSubmittedTimelineValue = 11u;

    // Invalidate simulates what happens after the GPU has been drained during resize.
    ring.InvalidateAfterResize();

    EXPECT_FALSE(frame0.Submitted);
    EXPECT_FALSE(frame0.ReusedSubmittedSlot);
    EXPECT_EQ(frame0.LastSubmittedTimelineValue, 0u);
    EXPECT_FALSE(frame0.Prepared);
    EXPECT_EQ(frame0.GetPreparedRenderWorld(), nullptr);

    EXPECT_FALSE(frame1.Submitted);
    EXPECT_FALSE(frame1.ReusedSubmittedSlot);
    EXPECT_EQ(frame1.LastSubmittedTimelineValue, 0u);
    EXPECT_FALSE(frame1.Prepared);
    EXPECT_EQ(frame1.GetPreparedRenderWorld(), nullptr);
}

TEST(RenderExtraction, FrameContextRing_InvalidateAfterResize_NextBeginFrameDoesNotFlagReuse)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 1024, .Height = 768};

    // Simulate a submitted frame then invalidate (resize).
    Runtime::FrameContext& frame0 = ring.BeginFrame(0u, vp);
    frame0.Submitted = true;
    frame0.LastSubmittedTimelineValue = 5u;

    ring.InvalidateAfterResize();

    // Next BeginFrame wraps to the same slot. Since InvalidateAfterResize
    // cleared Submitted, BeginFrame should NOT flag ReusedSubmittedSlot.
    Runtime::FrameContext& frame2 = ring.BeginFrame(2u, vp);
    EXPECT_FALSE(frame2.ReusedSubmittedSlot);
    EXPECT_EQ(frame2.LastSubmittedTimelineValue, 0u);
}

TEST(RenderExtraction, FrameContextRing_InvalidateAfterResize_PreservesAllocators)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 640, .Height = 480};

    Runtime::FrameContext& frame0 = ring.BeginFrame(0u, vp);
    ASSERT_TRUE(frame0.HasAllocators());
    const auto* arena0 = &frame0.GetRenderArena();
    const auto* scope0 = &frame0.GetRenderScope();

    ring.InvalidateAfterResize();

    // Allocators survive invalidation — they are reused, not destroyed.
    EXPECT_TRUE(frame0.HasAllocators());
    EXPECT_EQ(&frame0.GetRenderArena(), arena0);
    EXPECT_EQ(&frame0.GetRenderScope(), scope0);
}

// ---------------------------------------------------------------------------
// FrameContext deferred deletion queue (B4.9)
// ---------------------------------------------------------------------------

TEST(RenderExtraction, FrameContext_DeferDeletion_CallbackFiresOnFlush)
{
    Runtime::FrameContext frame{};
    int counter = 0;
    frame.DeferDeletion([&counter]() { ++counter; });
    frame.DeferDeletion([&counter]() { counter += 10; });

    EXPECT_EQ(counter, 0);
    frame.FlushDeferredDeletions();
    EXPECT_EQ(counter, 11);
}

TEST(RenderExtraction, FrameContext_FlushDeferredDeletions_ClearsQueue)
{
    Runtime::FrameContext frame{};
    int counter = 0;
    frame.DeferDeletion([&counter]() { ++counter; });

    frame.FlushDeferredDeletions();
    EXPECT_EQ(counter, 1);

    // Second flush should be a no-op (queue cleared).
    frame.FlushDeferredDeletions();
    EXPECT_EQ(counter, 1);
}

TEST(RenderExtraction, FrameContext_FlushDeferredDeletions_EmptyQueueIsNoOp)
{
    Runtime::FrameContext frame{};
    // Should not crash or assert.
    frame.FlushDeferredDeletions();
}

TEST(RenderExtraction, FrameContext_DeferredDeletions_IgnoreEmptyCallbacks)
{
    Runtime::FrameContext frame{};
    int counter = 0;

    Core::InplaceFunction<void()> empty{};
    frame.DeferDeletion(std::move(empty));
    frame.DeferDeletion([&counter]() { ++counter; });

    frame.FlushDeferredDeletions();
    EXPECT_EQ(counter, 1);
}

TEST(RenderExtraction, FrameContext_DeferredDeletions_ExecuteInOrder)
{
    Runtime::FrameContext frame{};
    std::vector<int> order;
    frame.DeferDeletion([&order]() { order.push_back(1); });
    frame.DeferDeletion([&order]() { order.push_back(2); });
    frame.DeferDeletion([&order]() { order.push_back(3); });

    frame.FlushDeferredDeletions();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(RenderExtraction, FrameContextRing_InvalidateAfterResize_FlushesDeferredDeletions)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 640, .Height = 480};

    Runtime::FrameContext& frame0 = ring.BeginFrame(0u, vp);
    int counter = 0;
    frame0.DeferDeletion([&counter]() { ++counter; });

    Runtime::FrameContext& frame1 = ring.BeginFrame(1u, vp);
    frame1.DeferDeletion([&counter]() { counter += 10; });

    // Simulate previously-submitted slots: InvalidateAfterResize flushes
    // callbacks only for submitted frame-contexts.
    frame0.Submitted = true;
    frame1.Submitted = true;

    EXPECT_EQ(counter, 0);
    ring.InvalidateAfterResize();
    EXPECT_EQ(counter, 11);
}

TEST(RenderExtraction, FrameContextRing_InvalidateAfterResize_UnsubmittedSlotsClearDeferredDeletionsWithoutInvocation)
{
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 640, .Height = 480};

    Runtime::FrameContext& frame0 = ring.BeginFrame(0u, vp);
    int counter = 0;
    frame0.DeferDeletion([&counter]() { ++counter; });
    ASSERT_FALSE(frame0.Submitted);

    ring.InvalidateAfterResize();
    EXPECT_EQ(counter, 0);

    Runtime::FrameContext& frame0Again = ring.BeginFrame(2u, vp);
    frame0Again.FlushDeferredDeletions();
    EXPECT_EQ(counter, 0);
}

TEST(RenderExtraction, FrameContext_DeferredDeletions_DropNeverSubmittedSlotOnReuse)
{
    // Deferred deletions on a never-submitted slot do not have a valid GPU
    // ownership state. The ring defensively drops them when the slot is reused.
    Runtime::FrameContextRing ring(2u);
    constexpr Runtime::RenderViewport vp{.Width = 800, .Height = 600};

    Runtime::FrameContext& slot0_frame0 = ring.BeginFrame(0u, vp);
    int counter = 0;
    slot0_frame0.DeferDeletion([&counter]() { ++counter; });

    // Advance past slot 1 back to slot 0.
    [[maybe_unused]] auto& slot1 = ring.BeginFrame(1u, vp);
    Runtime::FrameContext& slot0_frame2 = ring.BeginFrame(2u, vp);

    // The callback was dropped, not invoked.
    EXPECT_EQ(counter, 0);

    // Explicit flush has nothing left to execute for the unsubmitted slot.
    slot0_frame2.FlushDeferredDeletions();
    EXPECT_EQ(counter, 0);
}

TEST(RenderExtraction, FrameContext_DeferredDeletions_ReentrancySafe)
{
    // A callback that enqueues another deletion during flush must not
    // trigger iterator invalidation (swap-and-drain pattern).
    Runtime::FrameContext frame{};
    int counter = 0;

    frame.DeferDeletion([&frame, &counter]() {
        ++counter;
        // Re-entrant: enqueue a second deletion from inside the flush.
        frame.DeferDeletion([&counter]() { counter += 100; });
    });

    frame.FlushDeferredDeletions();
    // Only the first callback ran; the re-entrant one is deferred.
    EXPECT_EQ(counter, 1);

    // Second flush drains the re-entrant callback.
    frame.FlushDeferredDeletions();
    EXPECT_EQ(counter, 101);
}

TEST(RenderExtraction, FrameContext_DeferredDeletions_MoveOnlyCapture)
{
    // Verify that move-only captures work through InplaceFunction.
    Runtime::FrameContext frame{};
    bool deleted = false;

    auto ptr = std::make_unique<int>(42);
    frame.DeferDeletion([p = std::move(ptr), &deleted]() {
        EXPECT_EQ(*p, 42);
        deleted = true;
    });

    EXPECT_FALSE(deleted);
    frame.FlushDeferredDeletions();
    EXPECT_TRUE(deleted);
}

// FrameContext GPU profiling ownership (B4.9)
// -------------------------------------------------------------------------

TEST(RenderExtraction, FrameContext_ResolvedGpuProfile_DefaultIsNullopt)
{
    Runtime::FrameContext frame{};
    EXPECT_FALSE(frame.ResolvedGpuProfile.has_value());
}

TEST(RenderExtraction, FrameContext_ResolvedGpuProfile_StoresAndConsumes)
{
    Runtime::FrameContext frame{};

    RHI::GpuTimestampFrame profile{};
    profile.FrameNumber = 42;
    profile.GpuFrameTimeNs = 16'000'000;
    profile.ScopeCount = 2;
    profile.ScopeNames = {"SurfacePass", "LinePass"};
    profile.ScopeDurationsNs = {8'000'000, 4'000'000};

    frame.ResolvedGpuProfile = std::move(profile);
    ASSERT_TRUE(frame.ResolvedGpuProfile.has_value());
    EXPECT_EQ(frame.ResolvedGpuProfile->FrameNumber, 42u);
    EXPECT_EQ(frame.ResolvedGpuProfile->GpuFrameTimeNs, 16'000'000u);
    EXPECT_EQ(frame.ResolvedGpuProfile->ScopeCount, 2u);
    EXPECT_EQ(frame.ResolvedGpuProfile->ScopeNames.size(), 2u);

    // Consuming resets the optional.
    frame.ResolvedGpuProfile.reset();
    EXPECT_FALSE(frame.ResolvedGpuProfile.has_value());
}

TEST(RenderExtraction, FrameContext_ResolvedGpuProfile_SurvivesSlotReuseUntilConsumed)
{
    // Verify that a stored GPU profile on a FrameContext slot persists
    // across ring reuse so the orchestrator can consume it in BeginFrame.
    Runtime::FrameContextRing ring(2u);
    const Runtime::RenderViewport vp{.Width = 800, .Height = 600};

    Runtime::FrameContext& slot0 = ring.BeginFrame(0u, vp);
    RHI::GpuTimestampFrame profile{};
    profile.FrameNumber = 7;
    profile.GpuFrameTimeNs = 5'000'000;
    slot0.ResolvedGpuProfile = std::move(profile);

    // Advance to the next slot.
    (void)ring.BeginFrame(1u, vp);

    // Reuse slot 0 — the profile should still be there.
    Runtime::FrameContext& slot0_reused = ring.BeginFrame(2u, vp);
    ASSERT_TRUE(slot0_reused.ResolvedGpuProfile.has_value());
    EXPECT_EQ(slot0_reused.ResolvedGpuProfile->FrameNumber, 7u);
    EXPECT_EQ(slot0_reused.ResolvedGpuProfile->GpuFrameTimeNs, 5'000'000u);
}

TEST(RenderExtraction, FrameContextRing_InvalidateAfterResize_ClearsResolvedGpuProfile)
{
    Runtime::FrameContextRing ring(2u);
    const Runtime::RenderViewport vp{.Width = 800, .Height = 600};

    Runtime::FrameContext& slot0 = ring.BeginFrame(0u, vp);
    RHI::GpuTimestampFrame profile{};
    profile.FrameNumber = 99;
    profile.GpuFrameTimeNs = 10'000'000;
    slot0.ResolvedGpuProfile = std::move(profile);

    Runtime::FrameContext& slot1 = ring.BeginFrame(1u, vp);
    RHI::GpuTimestampFrame profile1{};
    profile1.FrameNumber = 100;
    slot1.ResolvedGpuProfile = std::move(profile1);

    // Simulate resize — all stale profiling data should be cleared.
    ring.InvalidateAfterResize();

    Runtime::FrameContext& slot0_post = ring.BeginFrame(2u, vp);
    EXPECT_FALSE(slot0_post.ResolvedGpuProfile.has_value());

    Runtime::FrameContext& slot1_post = ring.BeginFrame(3u, vp);
    EXPECT_FALSE(slot1_post.ResolvedGpuProfile.has_value());
}

TEST(RenderExtraction, ExtractRenderWorld_SelectionOutlineIncludesMeshLineAndPointOverlays)
{
    Runtime::SceneManager sceneManager;
    auto& registry = sceneManager.GetRegistry();

    const entt::entity root = registry.create();
    registry.emplace<ECS::Components::Selection::SelectedTag>(root);
    registry.emplace<ECS::Components::Selection::HoveredTag>(root);
    registry.emplace<ECS::Components::Selection::PickID>(root, ECS::Components::Selection::PickID{.Value = 900u});

    const entt::entity meshLineOverlay = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(meshLineOverlay);
    registry.emplace<ECS::Components::Selection::PickID>(meshLineOverlay, ECS::Components::Selection::PickID{.Value = 901u});
    auto& line = registry.emplace<ECS::Line::Component>(meshLineOverlay);
    line.SourceDomain = ECS::Line::Domain::MeshEdge;
    line.Geometry = Geometry::GeometryHandle{101u, 1u};
    line.EdgeView = Geometry::GeometryHandle{102u, 1u};
    line.EdgeCount = 3u;
    ECS::Components::Hierarchy::Attach(registry, meshLineOverlay, root);

    const entt::entity meshPointOverlay = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(meshPointOverlay);
    registry.emplace<ECS::Components::Selection::PickID>(meshPointOverlay, ECS::Components::Selection::PickID{.Value = 902u});
    auto& point = registry.emplace<ECS::Point::Component>(meshPointOverlay);
    point.SourceDomain = ECS::Point::Domain::MeshVertex;
    point.Geometry = Geometry::GeometryHandle{103u, 1u};
    point.Mode = Geometry::PointCloud::RenderMode::Sphere;
    ECS::Components::Hierarchy::Attach(registry, meshPointOverlay, root);

    sceneManager.CommitFixedTick();

    Graphics::CameraComponent camera{};
    const Runtime::RenderWorld renderWorld = Runtime::ExtractRenderWorld(Runtime::MakeRenderFrameInput(
        camera,
        sceneManager.CreateReadonlySnapshot(),
        Runtime::RenderViewport{.Width = 640, .Height = 480},
        0.0));

    EXPECT_TRUE(renderWorld.IsValid());
    ASSERT_EQ(renderWorld.LinePicking.size(), 1u);
    EXPECT_EQ(renderWorld.LinePicking.front().EntityId, 901u);
    ASSERT_EQ(renderWorld.PointPicking.size(), 1u);
    EXPECT_EQ(renderWorld.PointPicking.front().EntityId, 902u);
    EXPECT_EQ(renderWorld.SelectionOutline.SelectedPickIds.size(), 2u);
    EXPECT_EQ(renderWorld.SelectionOutline.SelectedPickIds[0], 901u);
    EXPECT_EQ(renderWorld.SelectionOutline.SelectedPickIds[1], 902u);
    EXPECT_TRUE(renderWorld.SelectionOutline.HoveredPickId == 901u ||
                renderWorld.SelectionOutline.HoveredPickId == 902u);
}
