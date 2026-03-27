#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <entt/entity/entity.hpp>
#include <memory>

import ECS;
import Runtime.Selection;
import Runtime.SelectionModule;
import Graphics;
import Geometry;

namespace
{
    [[nodiscard]] bool IsFiniteVec3(const glm::vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    enum class PrimitivePickDomain : uint32_t
    {
        SurfaceTriangle = 0u,
        LineSegment = 1u,
        Point = 2u,
    };

    [[nodiscard]] constexpr uint32_t EncodePrimitiveHint(PrimitivePickDomain domain, uint32_t index)
    {
        return (static_cast<uint32_t>(domain) << 30u) | (index & 0x3fffffffu);
    }
}

TEST(RuntimeSelection, RayFromNDC_IsSane)
{
    Graphics::CameraComponent cam{};
    cam.ViewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.ProjectionMatrix = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    const auto ray = Runtime::Selection::RayFromNDC(cam, glm::vec2(0.0f));

    EXPECT_TRUE(IsFiniteVec3(ray.Origin));
    EXPECT_TRUE(IsFiniteVec3(ray.Direction));

    const float len = glm::length(ray.Direction);
    EXPECT_NEAR(len, 1.0f, 1e-3f);
}

TEST(RuntimeSelection, SelectionRayMatchesGraphicsTopOriginScreenConvention)
{
    Graphics::CameraComponent cam{};
    cam.Position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.Fov = 60.0f;
    cam.AspectRatio = 16.0f / 9.0f;
    cam.Near = 0.1f;
    cam.Far = 1000.0f;
    Graphics::UpdateMatrices(cam, cam.AspectRatio);

    constexpr float width = 1920.0f;
    constexpr float height = 1080.0f;
    const glm::vec2 screen{width * 0.25f, height * 0.20f};

    const auto ndc = Graphics::ScreenToNDC(screen, width, height);
    const auto selectionRay = Runtime::Selection::RayFromNDC(cam, ndc);
    const auto graphicsRay = Graphics::RayFromScreen(cam, screen, width, height);

    EXPECT_NEAR(glm::distance(selectionRay.Origin, graphicsRay.Origin), 0.0f, 1e-4f);
    EXPECT_NEAR(glm::distance(selectionRay.Direction, graphicsRay.Direction), 0.0f, 1e-4f);
}

TEST(RuntimeSelectionModule, DefaultMouseButton_IsLmb)
{
    Runtime::SelectionModule module;
    EXPECT_EQ(module.GetConfig().MouseButton, 0);
}

TEST(RuntimeSelectionModule, PickerStartsAsBackground)
{
    Runtime::SelectionModule module;
    EXPECT_FALSE(static_cast<bool>(module.GetPicked().entity));
    EXPECT_TRUE(module.GetPicked().entity.is_background);
}

TEST(RuntimeSelection, PickCPU_PointCloudResolvesPointIndex)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("PointCloud");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.0f, 0.0f, 0.0f});
    cloud->AddPoint({0.0f, 0.0f, -1.0f});
    auto& pcd = reg.emplace<ECS::PointCloud::Data>(e);
    pcd.CloudRef = cloud;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 24.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.vertex_idx, 0u);
    EXPECT_FALSE(hit.PickedData.entity.is_background);
}

TEST(RuntimeSelection, PickCPU_GraphResolvesEdgeOrVertex)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("Graph");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex(glm::vec3(-0.5f, 0.0f, 0.0f));
    auto v1 = graph->AddVertex(glm::vec3(0.5f, 0.0f, 0.0f));
    static_cast<void>(graph->AddEdge(v0, v1));

    auto& gd = reg.emplace<ECS::Graph::Data>(e);
    gd.GraphRef = graph;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 24.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_TRUE(hit.PickedData.entity.edge_idx != Runtime::Selection::Picked::Entity::InvalidIndex ||
                hit.PickedData.entity.vertex_idx != Runtime::Selection::Picked::Entity::InvalidIndex);
}

TEST(RuntimeSelection, PickCPU_GraphReturnsExactNodeAndEdgeIndices)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("GraphExact");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    const auto v0 = graph->AddVertex(glm::vec3(-1.0f, 0.0f, 0.0f));
    const auto v1 = graph->AddVertex(glm::vec3( 1.0f, 0.0f, 0.0f));
    const auto edge = graph->AddEdge(v0, v1);
    ASSERT_TRUE(edge.has_value());

    auto& gd = reg.emplace<ECS::Graph::Data>(e);
    gd.GraphRef = graph;

    Runtime::Selection::PickRequest req{};
    req.PickRadiusPixels = 48.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    req.WorldRay = Geometry::Ray{{-1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    const auto vertexHit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(vertexHit.Entity, e);
    EXPECT_EQ(vertexHit.PickedData.entity.vertex_idx, static_cast<uint32_t>(v0.Index));

    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    const auto edgeHit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(edgeHit.Entity, e);
    EXPECT_EQ(edgeHit.PickedData.entity.edge_idx, static_cast<uint32_t>(edge->Index));
}

TEST(RuntimeSelection, PickCPU_MeshResolvesFaceIndex)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("Mesh");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f},
        { 0.0f,  1.0f, 0.0f},
    };
    collision->Indices = {0u, 1u, 2u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 24.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.face_idx, 0u);
    EXPECT_FALSE(hit.PickedData.entity.is_background);
}

TEST(RuntimeSelection, PickCPU_MeshUsesAttachedPrimitiveBVH)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshWithBVH");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f},
        { 0.0f,  1.0f, 0.0f},
    };
    collision->Indices = {0u, 1u, 2u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& primitiveBvh = reg.emplace<ECS::PrimitiveBVH::Data>(e);
    primitiveBvh.Source = ECS::PrimitiveBVH::SourceKind::MeshTriangles;
    primitiveBvh.Dirty = true;
    Graphics::Systems::PrimitiveBVHBuild::OnUpdate(reg);
    ASSERT_TRUE(primitiveBvh.HasValidBVH());

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 24.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.face_idx, 0u);
    EXPECT_FALSE(hit.PickedData.entity.is_background);
}

TEST(RuntimeSelection, PickCPU_MeshUsesAuthoritativeVertexAndEdgeIndices)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshAuthoritative");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    // Two triangles with duplicated per-triangle vertices. Collision-space vertex
    // indices differ from the authoritative quad topology on purpose.
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f}, {-1.0f,  1.0f, 0.0f},
    };
    collision->Indices = {0u, 1u, 2u, 3u, 4u, 5u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({-1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddQuad(v0, v1, v2, v3).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{-0.98f, 0.98f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    // Top-left source-mesh vertex is v3 (index 3). The old collision-soup path
    // would have reported duplicated triangle-soup vertex 5 here.
    EXPECT_EQ(hit.PickedData.entity.vertex_idx, 3u);
    EXPECT_EQ(hit.PickedData.entity.face_idx, 0u);
    EXPECT_NE(hit.PickedData.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);
}

TEST(RuntimeSelection, PickCPU_MeshQuadFaceIndexIsStableAcrossTriangulation)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshQuad");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f}, {-1.0f,  1.0f, 0.0f}
    };
    collision->Indices = {0u, 1u, 2u, 0u, 2u, 3u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({-1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddQuad(v0, v1, v2, v3).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{-0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 24.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.face_idx, 0u);
}

TEST(RuntimeSelection, PickCPU_MeshDataWithoutColliderUsesAuthoritativeMeshFallback)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshDataOnlyCpuFallback");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({-1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddQuad(v0, v1, v2, v3).has_value());

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{-0.98f, 0.98f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.face_idx, 0u);
    EXPECT_EQ(hit.PickedData.entity.vertex_idx, 3u);
    EXPECT_NE(hit.PickedData.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshDataWithoutColliderUsesCpuFallback)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshDataOnlyGpuCpuFallback");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);
    reg.emplace<ECS::Surface::Component>(e);

    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({-1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddQuad(v0, v1, v2, v3).has_value());

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{-0.98f, 0.98f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene, e, EncodePrimitiveHint(PrimitivePickDomain::SurfaceTriangle, 1u), Runtime::Selection::ElementMode::Vertex, &req);
    EXPECT_EQ(picked.entity.face_idx, 0u);
    EXPECT_EQ(picked.entity.vertex_idx, 3u);
    EXPECT_NE(picked.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);
}

TEST(RuntimeSelection, PickCPU_MeshVertexIndexUsesLocalKdTreeUnderWorldTransform)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshTransformKdTree");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto& transform = reg.get<ECS::Components::Transform::Component>(e);
    transform.Position = glm::vec3(10.0f, -2.0f, 0.0f);
    reg.get<ECS::Components::Transform::WorldMatrix>(e).Matrix = ECS::Components::Transform::GetMatrix(transform);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    collision->SourceMesh = sourceMesh;
    collision->Positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    collision->Indices = {0u, 1u, 2u};
    collision->LocalVertexLookupPoints = collision->Positions;
    collision->LocalVertexLookupIndices = {0u, 1u, 2u};
    ASSERT_TRUE(collision->LocalVertexKdTree.BuildFromPoints(collision->LocalVertexLookupPoints).has_value());
    collision->LocalAABB = Geometry::AABB{glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(10.5f, -1.5f, 0.0f);
    collider.WorldOBB.Extents = glm::vec3(0.5f, 0.5f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{10.01f, -1.99f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickCPU(scene, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.vertex_idx, 0u);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshUsesAuthoritativeFaceAndVertex)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuRefine");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f}, { 1.0f,  1.0f, 0.0f}, {-1.0f,  1.0f, 0.0f},
    };
    collision->Indices = {0u, 1u, 2u, 3u, 4u, 5u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({-1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddQuad(v0, v1, v2, v3).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{-0.98f, 0.98f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto pickedFace = Runtime::Selection::ResolveGpuSubElementPick(
        scene, e, 1u, Runtime::Selection::ElementMode::Face, &req);
    EXPECT_EQ(pickedFace.entity.face_idx, 0u);

    const auto pickedVertex = Runtime::Selection::ResolveGpuSubElementPick(
        scene, e, 0u, Runtime::Selection::ElementMode::Vertex, &req);
    EXPECT_EQ(pickedVertex.entity.face_idx, 0u);
    EXPECT_EQ(pickedVertex.entity.vertex_idx, 3u);
    EXPECT_NE(pickedVertex.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshSurfacePrimitiveDoesNotFallbackToWholeMeshRaycast)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuFaceAuthoritative");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);
    reg.emplace<ECS::Surface::Component>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->Positions = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f}, { 0.0f,  1.0f, 0.0f},
        { 4.0f, -1.0f, 0.0f}, { 6.0f, -1.0f, 0.0f}, { 5.0f,  1.0f, 0.0f},
    };
    collision->Indices = {0u, 1u, 2u, 3u, 4u, 5u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(6.0f, 1.0f, 0.0f)};

    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 0.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({ 4.0f, -1.0f, 0.0f});
    const auto v4 = sourceMesh->AddVertex({ 6.0f, -1.0f, 0.0f});
    const auto v5 = sourceMesh->AddVertex({ 5.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    ASSERT_TRUE(sourceMesh->AddTriangle(v3, v4, v5).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(2.5f, 0.0f, 0.0f);
    collider.WorldOBB.Extents = glm::vec3(3.5f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.1f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::SurfaceTriangle, 1u),
        Runtime::Selection::ElementMode::Vertex,
        &req);

    EXPECT_EQ(picked.entity.face_idx, 1u);
    EXPECT_GE(picked.entity.vertex_idx, 3u);
    EXPECT_LE(picked.entity.vertex_idx, 5u);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshEdgeUsesClosestEdgeOnFace)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuEdgeRefine");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({-1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddQuad(v0, v1, v2, v3).has_value());
    collision->SourceMesh = sourceMesh;
    collision->Positions = {{-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
    collision->Indices = {0u, 1u, 2u, 0u, 2u, 3u};
    collision->LocalAABB = Geometry::AABB{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f)};

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.99f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene, e, 0u, Runtime::Selection::ElementMode::Edge, &req);
    EXPECT_EQ(picked.entity.face_idx, 0u);
    EXPECT_NE(picked.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);

    const Geometry::EdgeHandle edge{static_cast<Geometry::PropertyIndex>(picked.entity.edge_idx)};
    ASSERT_TRUE(sourceMesh->IsValid(edge));
    const auto halfedge = sourceMesh->Halfedge(edge, 0);
    const auto a = sourceMesh->FromVertex(halfedge).Index;
    const auto b = sourceMesh->ToVertex(halfedge).Index;
    EXPECT_TRUE((a == v2.Index && b == v3.Index) || (a == v3.Index && b == v2.Index));
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshMixedSurfacePointFallbackUsesEncodedPointPrimitive)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuMixedPointFallback");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;

    reg.emplace<ECS::Surface::Component>(e);
    reg.emplace<ECS::Point::Component>(e);

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::Point, static_cast<uint32_t>(v2.Index)),
        Runtime::Selection::ElementMode::Vertex,
        nullptr);

    EXPECT_EQ(picked.entity.id, e);
    EXPECT_EQ(picked.entity.vertex_idx, static_cast<uint32_t>(v2.Index));
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshMixedSurfaceLineFallbackUsesEncodedLinePrimitive)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuMixedLineFallback");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;

    reg.emplace<ECS::Surface::Component>(e);
    reg.emplace<ECS::Line::Component>(e);

    const auto halfedge = sourceMesh->FindHalfedge(v1, v2);
    ASSERT_TRUE(halfedge.has_value());
    const auto edge = sourceMesh->Edge(*halfedge);
    ASSERT_TRUE(sourceMesh->IsValid(edge));

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::LineSegment, static_cast<uint32_t>(edge.Index)),
        Runtime::Selection::ElementMode::Edge,
        nullptr);

    EXPECT_EQ(picked.entity.id, e);
    EXPECT_EQ(picked.entity.edge_idx, static_cast<uint32_t>(edge.Index));
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_ContractGuaranteesRequestedModeIndex)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuContractGuarantees");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 0.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;

    reg.emplace<ECS::Surface::Component>(e);
    reg.emplace<ECS::Line::Component>(e);
    reg.emplace<ECS::Point::Component>(e);

    const auto halfedge = sourceMesh->FindHalfedge(v1, v2);
    ASSERT_TRUE(halfedge.has_value());
    const auto edge = sourceMesh->Edge(*halfedge);
    ASSERT_TRUE(sourceMesh->IsValid(edge));

    const auto facePick = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::SurfaceTriangle, 0u),
        Runtime::Selection::ElementMode::Face,
        nullptr);
    EXPECT_EQ(facePick.entity.id, e);
    EXPECT_EQ(facePick.entity.face_idx, 0u);

    const auto edgePick = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::LineSegment, static_cast<uint32_t>(edge.Index)),
        Runtime::Selection::ElementMode::Edge,
        nullptr);
    EXPECT_EQ(edgePick.entity.id, e);
    EXPECT_EQ(edgePick.entity.edge_idx, static_cast<uint32_t>(edge.Index));

    const auto vertexPick = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::Point, static_cast<uint32_t>(v2.Index)),
        Runtime::Selection::ElementMode::Vertex,
        nullptr);
    EXPECT_EQ(vertexPick.entity.id, e);
    EXPECT_EQ(vertexPick.entity.vertex_idx, static_cast<uint32_t>(v2.Index));
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_ContractUnsupportedModeStaysUnresolvedWithoutRefinement)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuContractUnsupported");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 0.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;

    reg.emplace<ECS::Surface::Component>(e);
    reg.emplace<ECS::Line::Component>(e);
    reg.emplace<ECS::Point::Component>(e);

    const auto halfedge = sourceMesh->FindHalfedge(v1, v2);
    ASSERT_TRUE(halfedge.has_value());
    const auto edge = sourceMesh->Edge(*halfedge);
    ASSERT_TRUE(sourceMesh->IsValid(edge));

    const auto surfaceAsEdge = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::SurfaceTriangle, 0u),
        Runtime::Selection::ElementMode::Edge,
        nullptr);
    EXPECT_EQ(surfaceAsEdge.entity.id, e);
    EXPECT_EQ(surfaceAsEdge.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);

    const auto surfaceAsVertex = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::SurfaceTriangle, 0u),
        Runtime::Selection::ElementMode::Vertex,
        nullptr);
    EXPECT_EQ(surfaceAsVertex.entity.id, e);
    EXPECT_EQ(surfaceAsVertex.entity.vertex_idx, Runtime::Selection::Picked::Entity::InvalidIndex);

    const auto lineAsFace = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::LineSegment, static_cast<uint32_t>(edge.Index)),
        Runtime::Selection::ElementMode::Face,
        nullptr);
    EXPECT_EQ(lineAsFace.entity.id, e);
    EXPECT_EQ(lineAsFace.entity.face_idx, Runtime::Selection::Picked::Entity::InvalidIndex);

    const auto pointAsEdge = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::Point, static_cast<uint32_t>(v2.Index)),
        Runtime::Selection::ElementMode::Edge,
        nullptr);
    EXPECT_EQ(pointAsEdge.entity.id, e);
    EXPECT_EQ(pointAsEdge.entity.edge_idx, Runtime::Selection::Picked::Entity::InvalidIndex);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshLinePrimitiveRefinesNearestEndpointVertex)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuLineRefine");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(0.0f, 0.0f, 0.0f);
    collider.WorldOBB.Extents = glm::vec3(1.0f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    reg.emplace<ECS::Line::Component>(e);

    const auto halfedge = sourceMesh->FindHalfedge(v1, v2);
    ASSERT_TRUE(halfedge.has_value());
    const auto edge = sourceMesh->Edge(*halfedge);
    ASSERT_TRUE(sourceMesh->IsValid(edge));

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.95f, 0.8f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::LineSegment, static_cast<uint32_t>(edge.Index)),
        Runtime::Selection::ElementMode::Vertex,
        &req);

    EXPECT_EQ(picked.entity.id, e);
    EXPECT_EQ(picked.entity.edge_idx, static_cast<uint32_t>(edge.Index));
    EXPECT_EQ(picked.entity.vertex_idx, static_cast<uint32_t>(v2.Index));
    EXPECT_NEAR(picked.spaces.World.x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(picked.spaces.World.y, 0.8f, 1.0e-3f);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_MeshLinePrimitiveDoesNotFallbackToWholeMeshRaycast)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("MeshGpuLineAuthoritative");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    auto sourceMesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = sourceMesh->AddVertex({-1.0f, -1.0f, 0.0f});
    const auto v1 = sourceMesh->AddVertex({ 1.0f, -1.0f, 0.0f});
    const auto v2 = sourceMesh->AddVertex({ 1.0f,  1.0f, 0.0f});
    const auto v3 = sourceMesh->AddVertex({ 4.0f, -1.0f, 0.0f});
    const auto v4 = sourceMesh->AddVertex({ 6.0f, -1.0f, 0.0f});
    const auto v5 = sourceMesh->AddVertex({ 6.0f,  1.0f, 0.0f});
    ASSERT_TRUE(sourceMesh->AddTriangle(v0, v1, v2).has_value());
    ASSERT_TRUE(sourceMesh->AddTriangle(v3, v4, v5).has_value());
    collision->SourceMesh = sourceMesh;

    auto& collider = reg.emplace<ECS::MeshCollider::Component>(e);
    collider.CollisionRef = collision;
    collider.WorldOBB.Center = glm::vec3(2.5f, 0.0f, 0.0f);
    collider.WorldOBB.Extents = glm::vec3(3.5f, 1.0f, 0.01f);

    auto& meshData = reg.emplace<ECS::Mesh::Data>(e);
    meshData.MeshRef = sourceMesh;

    reg.emplace<ECS::Line::Component>(e);

    const auto halfedge = sourceMesh->FindHalfedge(v4, v5);
    ASSERT_TRUE(halfedge.has_value());
    const auto edge = sourceMesh->Edge(*halfedge);
    ASSERT_TRUE(sourceMesh->IsValid(edge));

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.9f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 128.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene,
        e,
        EncodePrimitiveHint(PrimitivePickDomain::LineSegment, static_cast<uint32_t>(edge.Index)),
        Runtime::Selection::ElementMode::Vertex,
        &req);

    EXPECT_EQ(picked.entity.edge_idx, static_cast<uint32_t>(edge.Index));
    EXPECT_GE(picked.entity.vertex_idx, 4u);
    EXPECT_LE(picked.entity.vertex_idx, 5u);
    EXPECT_NEAR(picked.spaces.World.x, 6.0f, 1.0e-4f);
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_GraphVertexUsesClosestNodeNotPrimitive)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("GraphGpuVertexRefine");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    const auto v0 = graph->AddVertex(glm::vec3(-1.0f, 0.0f, 0.0f));
    const auto v1 = graph->AddVertex(glm::vec3( 1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(graph->AddEdge(v0, v1).has_value());

    auto& gd = reg.emplace<ECS::Graph::Data>(e);
    gd.GraphRef = graph;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{-1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 48.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene, e, static_cast<uint32_t>(v1.Index), Runtime::Selection::ElementMode::Vertex, &req);
    EXPECT_EQ(picked.entity.vertex_idx, static_cast<uint32_t>(v0.Index));
    EXPECT_EQ(picked.entity.edge_idx, 0u);
}

TEST(RuntimeSelection, PickCPU_GraphPopulatesNearestEdgeAndVertexFromSingleHit)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("GraphFullTuple");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    const auto v0 = graph->AddVertex(glm::vec3(-1.0f, 0.0f, 0.0f));
    const auto v1 = graph->AddVertex(glm::vec3( 1.0f, 0.0f, 0.0f));
    const auto edge = graph->AddEdge(v0, v1);
    ASSERT_TRUE(edge.has_value());

    auto& gd = reg.emplace<ECS::Graph::Data>(e);
    gd.GraphRef = graph;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 48.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto hit = Runtime::Selection::PickEntityCPU(scene, e, req);
    EXPECT_EQ(hit.Entity, e);
    EXPECT_EQ(hit.PickedData.entity.edge_idx, static_cast<uint32_t>(edge->Index));
    EXPECT_TRUE(hit.PickedData.entity.vertex_idx == static_cast<uint32_t>(v0.Index) ||
                hit.PickedData.entity.vertex_idx == static_cast<uint32_t>(v1.Index));
}

TEST(RuntimeSelection, ResolveGpuSubElementPick_PointCloudAcceptsPrimitiveZero)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("PointCloudGpuPrimitiveZero");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.0f, 0.0f, 0.0f});
    cloud->AddPoint({1.0f, 0.0f, 0.0f});
    auto& pcd = reg.emplace<ECS::PointCloud::Data>(e);
    pcd.CloudRef = cloud;

    Runtime::Selection::PickRequest req{};
    req.WorldRay = Geometry::Ray{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    req.PickRadiusPixels = 24.0f;
    req.CameraFovYRadians = glm::radians(45.0f);
    req.ViewportHeightPixels = 900.0f;

    const auto picked = Runtime::Selection::ResolveGpuSubElementPick(
        scene, e, 0u, Runtime::Selection::ElementMode::Vertex, &req);
    EXPECT_EQ(picked.entity.vertex_idx, 0u);
    EXPECT_FALSE(picked.entity.is_background);
}

TEST(RuntimeSelectionModule, SelectionChangedEventRefreshesPickedEntity)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity e = scene.CreateEntity("Selected");
    reg.emplace<ECS::Components::Selection::SelectableTag>(e);

    Runtime::SelectionModule module;
    module.ConnectToScene(scene);

    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);
    scene.GetDispatcher().update();

    EXPECT_EQ(module.GetPicked().entity.id, e);
    EXPECT_FALSE(module.GetPicked().entity.is_background);
}

