#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.GeometryAvailability;

import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud;

namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace G = Extrinsic::Graphics::Components;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    Geometry::HalfedgeMesh::Mesh MakeTriangleMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);
        return mesh;
    }

    Geometry::Graph::Graph MakeGraph()
    {
        Geometry::Graph::Graph graph;
        const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
        (void)graph.AddEdge(v0, v1);
        return graph;
    }
}

TEST(RuntimeGeometryAvailability, MeshResolvesIndependentSurfaceEdgeAndPointLanes)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto mesh = MakeTriangleMesh();
    GS::PopulateFromMesh(registry, entity, mesh);
    registry.emplace<G::RenderSurface>(entity);
    registry.emplace<G::RenderEdges>(entity);
    registry.emplace<G::RenderPoints>(entity);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Mesh);
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Surface).Ready());
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Edges).Ready());
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Points).Ready());
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshVertex));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshEdge));
    EXPECT_FALSE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::PointCloudPoint));
}

TEST(RuntimeGeometryAvailability, GraphSupportsEdgeAndPointLanesWithoutHalfedgeSource)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto graph = MakeGraph();
    GS::PopulateFromGraph(registry, entity, graph);
    registry.emplace<G::RenderEdges>(entity);
    registry.emplace<G::RenderPoints>(entity);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Graph);
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Edges).Ready());
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Points).Ready());
    EXPECT_FALSE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshHalfedge));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphNode));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphEdge));
}

TEST(RuntimeGeometryAvailability, PointCloudSupportsPointsAndRejectsSurfaceAndEdges)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
    GS::PopulateFromCloud(registry, entity, cloud);
    registry.emplace<G::RenderSurface>(entity);
    registry.emplace<G::RenderEdges>(entity);
    registry.emplace<G::RenderPoints>(entity);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    const auto surface = Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Surface);
    const auto edges = Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Edges);
    const auto points = Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Points);

    EXPECT_FALSE(surface.Supported);
    EXPECT_EQ(surface.Status, Runtime::GeometryAvailabilityStatus::UnsupportedProvenance);
    EXPECT_FALSE(edges.Supported);
    EXPECT_EQ(edges.Status, Runtime::GeometryAvailabilityStatus::UnsupportedProvenance);
    EXPECT_TRUE(points.Ready());
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::PointCloudPoint));
}

TEST(RuntimeGeometryAvailability, MeshEdgeLaneCanDeriveFromSurfaceTopologyWithoutEdges)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto mesh = MakeTriangleMesh();
    GS::PopulateFromMesh(registry, entity, mesh);
    registry.remove<GS::Edges>(entity);
    registry.emplace<G::RenderEdges>(entity);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);
    const auto edges = Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Edges);

    EXPECT_EQ(availability.Sources.ExactDomain, GS::Domain::Unknown);
    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Mesh);
    EXPECT_FALSE(availability.Sources.Has(GS::SourceCapability::Edges));
    EXPECT_TRUE(edges.Ready());
}

TEST(RuntimeGeometryAvailability, PartialMeshCanSupportPointsWhileSurfaceReportsMissingHalfedges)
{
    entt::registry registry;
    const entt::entity entity = registry.create();

    GS::Vertices vertices{};
    vertices.Properties.GetOrAdd<glm::vec3>(
        std::string{GS::PropertyNames::kPosition},
        glm::vec3{})
        .Vector()
        .resize(3u, glm::vec3{0.0f, 0.0f, 0.0f});
    GS::Faces faces{};
    faces.Properties.GetOrAdd<std::uint32_t>(
        std::string{GS::PropertyNames::kFaceHalfedge},
        std::numeric_limits<std::uint32_t>::max())
        .Vector()
        .resize(1u, std::numeric_limits<std::uint32_t>::max());

    registry.emplace<GS::Vertices>(entity, std::move(vertices));
    registry.emplace<GS::Faces>(entity, std::move(faces));
    registry.emplace<GS::HasMeshTopology>(entity);
    registry.emplace<G::RenderSurface>(entity);
    registry.emplace<G::RenderPoints>(entity);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);
    const auto surface = Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Surface);
    const auto points = Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Points);

    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Mesh);
    EXPECT_FALSE(surface.Supported);
    EXPECT_EQ(surface.Status, Runtime::GeometryAvailabilityStatus::MissingHalfedgeSource);
    EXPECT_TRUE(points.Ready());
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshVertex));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshFace));
}
