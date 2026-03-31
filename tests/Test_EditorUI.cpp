#include <gtest/gtest.h>
#include <memory>

// This test avoids touching ImGui directly (requires Vulkan context).
// It validates linkability of the EditorUI module and exercises the
// SceneDirtyTracker state machine, which is pure CPU state.

import Graphics;
import ECS;
import Geometry;
import Runtime.Engine;
import Runtime.EditorUI;
import Runtime.PointCloudKMeans;
import Runtime.SceneSerializer;

using namespace Runtime;

// =========================================================================
// Symbol linkability
// =========================================================================

TEST(EditorUI, RegisterDefaultPanels_IsLinkable)
{
    auto* fn = &Runtime::EditorUI::RegisterDefaultPanels;
    ASSERT_NE(fn, nullptr);
}

TEST(EditorUI, GetSceneDirtyTracker_IsLinkable)
{
    auto* fn = &Runtime::EditorUI::GetSceneDirtyTracker;
    ASSERT_NE(fn, nullptr);
}

// =========================================================================
// SceneDirtyTracker state machine
// =========================================================================

TEST(EditorUISceneDirtyTracker, InitiallyClean)
{
    SceneDirtyTracker tracker;
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, MarkDirty_SetsDirtyState)
{
    SceneDirtyTracker tracker;
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, ClearDirty_ResetsState)
{
    SceneDirtyTracker tracker;
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());

    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, MarkDirty_IsIdempotent)
{
    SceneDirtyTracker tracker;
    tracker.MarkDirty();
    tracker.MarkDirty();
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());

    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, InitialPathIsEmpty)
{
    SceneDirtyTracker tracker;
    EXPECT_TRUE(tracker.GetCurrentPath().empty());
}

TEST(EditorUISceneDirtyTracker, SetCurrentPath_StoresPath)
{
    SceneDirtyTracker tracker;
    tracker.SetCurrentPath("scenes/test.json");
    EXPECT_EQ(tracker.GetCurrentPath(), "scenes/test.json");
}

TEST(EditorUISceneDirtyTracker, SetCurrentPath_Overwrites)
{
    SceneDirtyTracker tracker;
    tracker.SetCurrentPath("first.json");
    tracker.SetCurrentPath("second.json");
    EXPECT_EQ(tracker.GetCurrentPath(), "second.json");
}

TEST(EditorUISceneDirtyTracker, PathAndDirtyAreIndependent)
{
    SceneDirtyTracker tracker;

    tracker.SetCurrentPath("scene.json");
    EXPECT_FALSE(tracker.IsDirty());

    tracker.MarkDirty();
    EXPECT_EQ(tracker.GetCurrentPath(), "scene.json");

    tracker.ClearDirty();
    EXPECT_EQ(tracker.GetCurrentPath(), "scene.json");
    EXPECT_FALSE(tracker.IsDirty());
}

TEST(EditorUISceneDirtyTracker, ClearDirty_DoesNotClearPath)
{
    SceneDirtyTracker tracker;
    tracker.SetCurrentPath("my_scene.json");
    tracker.MarkDirty();
    tracker.ClearDirty();

    EXPECT_FALSE(tracker.IsDirty());
    EXPECT_EQ(tracker.GetCurrentPath(), "my_scene.json");
}

// =========================================================================
// Global tracker accessed via EditorUI
// =========================================================================

TEST(EditorUI, GlobalDirtyTracker_RoundTrip)
{
    auto& tracker = Runtime::EditorUI::GetSceneDirtyTracker();

    // Save original state to restore after test.
    const bool wasDirty = tracker.IsDirty();
    const auto origPath = tracker.GetCurrentPath();

    tracker.SetCurrentPath("test_roundtrip.json");
    tracker.MarkDirty();
    EXPECT_TRUE(tracker.IsDirty());
    EXPECT_EQ(tracker.GetCurrentPath(), "test_roundtrip.json");

    tracker.ClearDirty();
    EXPECT_FALSE(tracker.IsDirty());

    // Restore original state.
    tracker.SetCurrentPath(origPath);
    if (wasDirty) tracker.MarkDirty();
    else tracker.ClearDirty();
}

// =========================================================================
// Geometry Processing capability discovery
// =========================================================================

TEST(EditorUIProcessing, SupportedDomainsExposePointSetAndSurfaceCapabilities)
{
    using Runtime::EditorUI::GeometryProcessingAlgorithm;
    using Runtime::EditorUI::GeometryProcessingDomain;

    const auto kmeans = Runtime::EditorUI::GetSupportedDomains(GeometryProcessingAlgorithm::KMeans);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(kmeans, GeometryProcessingDomain::MeshVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(kmeans, GeometryProcessingDomain::GraphVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(kmeans, GeometryProcessingDomain::PointCloudPoints));
    EXPECT_FALSE(Runtime::EditorUI::HasAnyDomain(kmeans, GeometryProcessingDomain::MeshEdges));

    const auto smoothing = Runtime::EditorUI::GetSupportedDomains(GeometryProcessingAlgorithm::Smoothing);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(smoothing, GeometryProcessingDomain::MeshVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(smoothing, GeometryProcessingDomain::MeshEdges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(smoothing, GeometryProcessingDomain::MeshHalfedges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(smoothing, GeometryProcessingDomain::MeshFaces));
    EXPECT_FALSE(Runtime::EditorUI::HasAnyDomain(smoothing, GeometryProcessingDomain::GraphVertices));
}

TEST(EditorUIProcessing, MeshEntityReportsExplicitTopologyDomains)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();
    const auto entity = reg.create();

    auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = mesh->AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh->AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh->AddVertex({0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh->AddTriangle(v0, v1, v2).has_value());

    ECS::Mesh::Data meshData{};
    meshData.MeshRef = mesh;
    reg.emplace<ECS::Mesh::Data>(entity, meshData);
    reg.emplace<ECS::Surface::Component>(entity);

    ECS::MeshCollider::Component collider{};
    collider.CollisionRef = std::make_shared<Graphics::GeometryCollisionData>();
    reg.emplace<ECS::MeshCollider::Component>(entity, std::move(collider));

    const auto caps = Runtime::EditorUI::GetGeometryProcessingCapabilities(reg, entity);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshEdges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshHalfedges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshFaces));
    EXPECT_TRUE(caps.HasEditableSurfaceMesh);
    EXPECT_FALSE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphVertices));
    EXPECT_FALSE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::PointCloudPoints));
}

TEST(EditorUIProcessing, ColliderBackedMeshFallbackReportsTopologyDomainsWithoutMeshComponent)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();
    const auto entity = reg.create();

    auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
    const auto v0 = mesh->AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = mesh->AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = mesh->AddVertex({0.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh->AddTriangle(v0, v1, v2).has_value());

    reg.emplace<ECS::Surface::Component>(entity);

    auto collision = std::make_shared<Graphics::GeometryCollisionData>();
    collision->SourceMesh = mesh;
    collision->LocalAABB = Geometry::AABB{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};

    ECS::MeshCollider::Component collider{};
    collider.CollisionRef = std::move(collision);
    reg.emplace<ECS::MeshCollider::Component>(entity, std::move(collider));

    const auto caps = Runtime::EditorUI::GetGeometryProcessingCapabilities(reg, entity);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshEdges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshHalfedges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshFaces));
    EXPECT_TRUE(caps.HasEditableSurfaceMesh);
}

TEST(EditorUIProcessing, GraphAndPointCloudEntitiesReportPointDomains)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const auto graphEntity = reg.create();
    auto graph = std::make_shared<Geometry::Graph::Graph>();
    const auto gv0 = graph->AddVertex({0.0f, 0.0f, 0.0f});
    const auto gv1 = graph->AddVertex({1.0f, 0.0f, 0.0f});
    static_cast<void>(graph->AddEdge(gv0, gv1));

    ECS::Graph::Data graphData{};
    graphData.GraphRef = graph;
    reg.emplace<ECS::Graph::Data>(graphEntity, graphData);

    const auto graphCaps = Runtime::EditorUI::GetGeometryProcessingCapabilities(reg, graphEntity);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(graphCaps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(graphCaps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphEdges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(graphCaps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphHalfedges));
    EXPECT_FALSE(Runtime::EditorUI::HasAnyDomain(graphCaps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshVertices));

    const auto cloudEntity = reg.create();
    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.0f, 0.0f, 0.0f});
    cloud->AddPoint({1.0f, 0.0f, 0.0f});

    ECS::PointCloud::Data pointCloudData{};
    pointCloudData.CloudRef = cloud;
    reg.emplace<ECS::PointCloud::Data>(cloudEntity, pointCloudData);

    const auto cloudCaps = Runtime::EditorUI::GetGeometryProcessingCapabilities(reg, cloudEntity);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(cloudCaps.Domains, Runtime::EditorUI::GeometryProcessingDomain::PointCloudPoints));
    EXPECT_FALSE(Runtime::EditorUI::HasAnyDomain(cloudCaps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshVertices));
}

TEST(EditorUIProcessing, MixedGeometryEntityEnumeratesAllKMeansDomainsInStableOrder)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();
    const auto entity = reg.create();

    auto mesh = std::make_shared<Geometry::Halfedge::Mesh>();
    static_cast<void>(mesh->AddVertex({0.0f, 0.0f, 0.0f}));
    static_cast<void>(mesh->AddVertex({1.0f, 0.0f, 0.0f}));
    static_cast<void>(mesh->AddVertex({0.0f, 1.0f, 0.0f}));
    ECS::Mesh::Data meshData{};
    meshData.MeshRef = mesh;
    reg.emplace<ECS::Mesh::Data>(entity, meshData);
    reg.emplace<ECS::Surface::Component>(entity);

    ECS::MeshCollider::Component collider{};
    collider.CollisionRef = std::make_shared<Graphics::GeometryCollisionData>();
    reg.emplace<ECS::MeshCollider::Component>(entity, std::move(collider));

    auto graph = std::make_shared<Geometry::Graph::Graph>();
    const auto g0 = graph->AddVertex({0.0f, 0.0f, 0.0f});
    const auto g1 = graph->AddVertex({1.0f, 0.0f, 0.0f});
    static_cast<void>(graph->AddEdge(g0, g1));
    ECS::Graph::Data graphData{};
    graphData.GraphRef = graph;
    reg.emplace<ECS::Graph::Data>(entity, graphData);

    auto cloud = std::make_shared<Geometry::PointCloud::Cloud>();
    cloud->AddPoint({0.0f, 0.0f, 0.0f});
    cloud->AddPoint({1.0f, 0.0f, 0.0f});
    ECS::PointCloud::Data pointCloudData{};
    pointCloudData.CloudRef = cloud;
    reg.emplace<ECS::PointCloud::Data>(entity, pointCloudData);

    const auto caps = Runtime::EditorUI::GetGeometryProcessingCapabilities(reg, entity);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshEdges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshHalfedges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshFaces));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphEdges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphHalfedges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(caps.Domains, Runtime::EditorUI::GeometryProcessingDomain::PointCloudPoints));
    EXPECT_TRUE(caps.HasEditableSurfaceMesh);

    const auto domains = Runtime::EditorUI::GetAvailableKMeansDomains(reg, entity);
    ASSERT_EQ(domains.size(), 3u);
    EXPECT_EQ(domains[0], Runtime::PointCloudKMeans::Domain::MeshVertices);
    EXPECT_EQ(domains[1], Runtime::PointCloudKMeans::Domain::GraphVertices);
    EXPECT_EQ(domains[2], Runtime::PointCloudKMeans::Domain::PointCloudPoints);

    const auto entries = Runtime::EditorUI::ResolveGeometryProcessingEntries(reg, entity);
    ASSERT_EQ(entries.size(), 7u);
    EXPECT_EQ(entries[0].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::KMeans);
    EXPECT_EQ(entries[1].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::NormalEstimation);
    EXPECT_EQ(entries[2].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::Remeshing);
    EXPECT_EQ(entries[3].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::Simplification);
    EXPECT_EQ(entries[4].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::Smoothing);
    EXPECT_EQ(entries[5].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::Subdivision);
    EXPECT_EQ(entries[6].Algorithm, Runtime::EditorUI::GeometryProcessingAlgorithm::Repair);
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(entries[0].Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(entries[0].Domains, Runtime::EditorUI::GeometryProcessingDomain::GraphVertices));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(entries[0].Domains, Runtime::EditorUI::GeometryProcessingDomain::PointCloudPoints));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(entries[1].Domains, Runtime::EditorUI::GeometryProcessingDomain::PointCloudPoints));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(entries[2].Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshHalfedges));
    EXPECT_TRUE(Runtime::EditorUI::HasAnyDomain(entries[2].Domains, Runtime::EditorUI::GeometryProcessingDomain::MeshFaces));
}

