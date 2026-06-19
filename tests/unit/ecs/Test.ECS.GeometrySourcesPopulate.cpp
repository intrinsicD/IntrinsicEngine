#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.PointCloud;
import Geometry.Properties;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
namespace GeometrySources = Extrinsic::ECS::Components::GeometrySources;
namespace PropertyNames = GeometrySources::PropertyNames;

namespace
{
    constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    // Builds a two-triangle quad in the Z=0 plane:
    //   v3---v2
    //    |\  |
    //    | \ |
    //    |  \|
    //   v0---v1
    Geometry::HalfedgeMesh::Mesh MakeQuadMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
        const auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        (void)mesh.AddTriangle(v0, v1, v2);
        (void)mesh.AddTriangle(v0, v2, v3);
        return mesh;
    }
}

// =============================================================================
// PopulateFromMesh
// =============================================================================
TEST(ECSGeometrySourcesPopulate, PopulateFromMeshEmplacesAllFourDomains)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    auto mesh = MakeQuadMesh();
    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    EXPECT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Halfedges>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Faces>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Nodes>(entity));
}

TEST(ECSGeometrySourcesPopulate, PopulateFromMeshWritesCanonicalKeysAndPositions)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    auto mesh = MakeQuadMesh();
    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    auto& vComp = raw.get<GeometrySources::Vertices>(entity);
    ASSERT_TRUE(vComp.Properties.Exists(PropertyNames::kPosition));

    auto posProp = vComp.Properties.Get<glm::vec3>(PropertyNames::kPosition);
    ASSERT_EQ(posProp.Vector().size(), static_cast<std::size_t>(4));
    EXPECT_EQ(posProp.Vector()[0], glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(posProp.Vector()[1], glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(posProp.Vector()[2], glm::vec3(1.0f, 1.0f, 0.0f));
    EXPECT_EQ(posProp.Vector()[3], glm::vec3(0.0f, 1.0f, 0.0f));

    auto& eComp = raw.get<GeometrySources::Edges>(entity);
    ASSERT_TRUE(eComp.Properties.Exists(PropertyNames::kEdgeV0));
    ASSERT_TRUE(eComp.Properties.Exists(PropertyNames::kEdgeV1));
    auto v0Prop = eComp.Properties.Get<std::uint32_t>(PropertyNames::kEdgeV0);
    auto v1Prop = eComp.Properties.Get<std::uint32_t>(PropertyNames::kEdgeV1);
    EXPECT_EQ(v0Prop.Vector().size(), eComp.Properties.Size());
    EXPECT_EQ(v1Prop.Vector().size(), eComp.Properties.Size());
    for (std::size_t i = 0; i < v0Prop.Vector().size(); ++i)
        EXPECT_NE(v0Prop.Vector()[i], v1Prop.Vector()[i]) << "edge " << i << " is degenerate";

    auto& hComp = raw.get<GeometrySources::Halfedges>(entity);
    ASSERT_TRUE(hComp.Properties.Exists(PropertyNames::kHalfedgeToVertex));
    ASSERT_TRUE(hComp.Properties.Exists(PropertyNames::kHalfedgeNext));
    ASSERT_TRUE(hComp.Properties.Exists(PropertyNames::kHalfedgeFace));

    auto& fComp = raw.get<GeometrySources::Faces>(entity);
    ASSERT_TRUE(fComp.Properties.Exists(PropertyNames::kFaceHalfedge));
    auto heProp = fComp.Properties.Get<std::uint32_t>(PropertyNames::kFaceHalfedge);
    ASSERT_EQ(heProp.Vector().size(), static_cast<std::size_t>(2));
    EXPECT_NE(heProp.Vector()[0], kInvalidIndex);
    EXPECT_NE(heProp.Vector()[1], kInvalidIndex);
}

TEST(ECSGeometrySourcesPopulate, PopulateFromMeshAliveCountsMatchSourceMesh)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    auto mesh = MakeQuadMesh();
    const std::size_t expectedAliveV = mesh.VertexCount();
    const std::size_t expectedAliveE = mesh.EdgeCount();
    const std::size_t expectedAliveF = mesh.FaceCount();

    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    EXPECT_EQ(GeometrySources::VertexCount(raw.get<GeometrySources::Vertices>(entity)), expectedAliveV);
    EXPECT_EQ(GeometrySources::EdgeCount(raw.get<GeometrySources::Edges>(entity)), expectedAliveE);
    EXPECT_EQ(GeometrySources::FaceCount(raw.get<GeometrySources::Faces>(entity)), expectedAliveF);
}

TEST(ECSGeometrySourcesPopulate, PopulateFromMeshCopiesUserDefinedVertexProperty)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    auto mesh = MakeQuadMesh();
    {
        auto userProp = mesh.VertexProperties().GetOrAdd<float>("v:user_radius", 0.0f);
        userProp.Vector().resize(mesh.VerticesSize(), 0.0f);
        for (std::size_t i = 0; i < userProp.Vector().size(); ++i)
            userProp.Vector()[i] = 0.5f + static_cast<float>(i);
    }

    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    auto& vComp = raw.get<GeometrySources::Vertices>(entity);
    ASSERT_TRUE(vComp.Properties.Exists("v:user_radius"));
    auto userProp = vComp.Properties.Get<float>("v:user_radius");
    ASSERT_EQ(userProp.Vector().size(), static_cast<std::size_t>(4));
    EXPECT_FLOAT_EQ(userProp.Vector()[0], 0.5f);
    EXPECT_FLOAT_EQ(userProp.Vector()[1], 1.5f);
    EXPECT_FLOAT_EQ(userProp.Vector()[2], 2.5f);
    EXPECT_FLOAT_EQ(userProp.Vector()[3], 3.5f);
}

TEST(ECSGeometrySourcesPopulate, PopulateFromMeshYieldsMeshDomain)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    auto mesh = MakeQuadMesh();
    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::Mesh);
    EXPECT_EQ(view.VerticesAlive(), static_cast<std::size_t>(4));
    EXPECT_GT(view.EdgesAlive(), static_cast<std::size_t>(0));
    EXPECT_GT(view.HalfedgesTotal(), static_cast<std::size_t>(0));
    EXPECT_EQ(view.FacesAlive(), static_cast<std::size_t>(2));
}

TEST(ECSGeometrySourcesPopulate, MeshAvailabilitySeparatesProvenanceFromSourceCapabilities)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    auto mesh = MakeQuadMesh();
    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    const auto availability = GeometrySources::BuildSourceAvailability(view);

    EXPECT_EQ(availability.ExactDomain, GeometrySources::Domain::Mesh);
    EXPECT_EQ(availability.ProvenanceDomain, GeometrySources::Domain::Mesh);
    EXPECT_TRUE(availability.HasMeshProvenance());
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::VertexPoints));
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::Edges));
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::Halfedges));
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::Faces));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::NodePoints));
    EXPECT_TRUE(availability.HasPointSource());
    EXPECT_EQ(availability.VertexPointCount, static_cast<std::size_t>(4));
    EXPECT_EQ(availability.FaceCount, static_cast<std::size_t>(2));
}

// =============================================================================
// PopulateFromGraph
// =============================================================================
TEST(ECSGeometrySourcesPopulate, PopulateFromGraphEmplacesNodesAndEdges)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::Graph::Graph graph;
    const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = graph.AddVertex({0.0f, 1.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);
    (void)graph.AddEdge(v1, v2);

    GeometrySources::PopulateFromGraph(raw, entity, graph);

    EXPECT_TRUE(raw.all_of<GeometrySources::Nodes>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::HasGraphTopology>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Faces>(entity));
}

TEST(ECSGeometrySourcesPopulate, PopulateFromGraphWritesCanonicalKeysAndPositions)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::Graph::Graph graph;
    const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex({1.0f, 2.0f, 3.0f});
    (void)graph.AddEdge(v0, v1);

    GeometrySources::PopulateFromGraph(raw, entity, graph);

    auto& nComp = raw.get<GeometrySources::Nodes>(entity);
    ASSERT_TRUE(nComp.Properties.Exists(PropertyNames::kPosition));
    auto posProp = nComp.Properties.Get<glm::vec3>(PropertyNames::kPosition);
    ASSERT_EQ(posProp.Vector().size(), static_cast<std::size_t>(2));
    EXPECT_EQ(posProp.Vector()[0], glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(posProp.Vector()[1], glm::vec3(1.0f, 2.0f, 3.0f));

    auto& eComp = raw.get<GeometrySources::Edges>(entity);
    ASSERT_TRUE(eComp.Properties.Exists(PropertyNames::kEdgeV0));
    ASSERT_TRUE(eComp.Properties.Exists(PropertyNames::kEdgeV1));
    auto v0Prop = eComp.Properties.Get<std::uint32_t>(PropertyNames::kEdgeV0);
    auto v1Prop = eComp.Properties.Get<std::uint32_t>(PropertyNames::kEdgeV1);
    ASSERT_EQ(v0Prop.Vector().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(v1Prop.Vector().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(v0Prop.Vector()[0], static_cast<std::uint32_t>(0));
    EXPECT_EQ(v1Prop.Vector()[0], static_cast<std::uint32_t>(1));
}

TEST(ECSGeometrySourcesPopulate, PopulateFromGraphYieldsGraphDomain)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::Graph::Graph graph;
    const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);

    GeometrySources::PopulateFromGraph(raw, entity, graph);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::Graph);
    EXPECT_EQ(view.NodesAlive(), static_cast<std::size_t>(2));
    EXPECT_EQ(view.EdgesAlive(), static_cast<std::size_t>(1));
}

TEST(ECSGeometrySourcesPopulate, GraphAvailabilityReportsNodesAndEdgesWithoutHalfedgeSource)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::Graph::Graph graph;
    const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);

    GeometrySources::PopulateFromGraph(raw, entity, graph);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    const auto availability = GeometrySources::BuildSourceAvailability(view);

    EXPECT_EQ(availability.ExactDomain, GeometrySources::Domain::Graph);
    EXPECT_EQ(availability.ProvenanceDomain, GeometrySources::Domain::Graph);
    EXPECT_TRUE(availability.HasGraphProvenance());
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::NodePoints));
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::Edges));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::VertexPoints));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::Halfedges));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::Faces));
    EXPECT_TRUE(availability.HasPointSource());
    EXPECT_EQ(availability.NodePointCount, static_cast<std::size_t>(2));
    EXPECT_EQ(availability.EdgeCount, static_cast<std::size_t>(1));
}

// =============================================================================
// PopulateFromCloud
// =============================================================================
TEST(ECSGeometrySourcesPopulate, PopulateFromCloudEmplacesOnlyVertices)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
    (void)cloud.AddPoint({1.0f, 0.0f, 0.0f});
    (void)cloud.AddPoint({0.0f, 1.0f, 0.0f});

    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    EXPECT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Halfedges>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Faces>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Nodes>(entity));
}

TEST(ECSGeometrySourcesPopulate, PopulateFromCloudWritesCanonicalPositionsAndNormals)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::PointCloud::Cloud cloud;
    cloud.EnableNormals();
    const auto p0 = cloud.AddPoint({0.0f, 0.0f, 0.0f});
    const auto p1 = cloud.AddPoint({1.0f, 2.0f, 3.0f});
    cloud.Normal(p0) = glm::vec3(0.0f, 0.0f, 1.0f);
    cloud.Normal(p1) = glm::vec3(1.0f, 0.0f, 0.0f);
    ASSERT_TRUE(cloud.HasNormals());

    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    auto& vComp = raw.get<GeometrySources::Vertices>(entity);
    ASSERT_TRUE(vComp.Properties.Exists(PropertyNames::kPosition));
    ASSERT_TRUE(vComp.Properties.Exists(PropertyNames::kNormal));

    auto posProp = vComp.Properties.Get<glm::vec3>(PropertyNames::kPosition);
    ASSERT_EQ(posProp.Vector().size(), static_cast<std::size_t>(2));
    EXPECT_EQ(posProp.Vector()[0], glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(posProp.Vector()[1], glm::vec3(1.0f, 2.0f, 3.0f));

    auto normProp = vComp.Properties.Get<glm::vec3>(PropertyNames::kNormal);
    ASSERT_EQ(normProp.Vector().size(), static_cast<std::size_t>(2));
    EXPECT_EQ(normProp.Vector()[0], glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(normProp.Vector()[1], glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(ECSGeometrySourcesPopulate, PopulateFromCloudSkipsNormalsWhenSourceLacksThem)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
    ASSERT_FALSE(cloud.HasNormals());

    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    auto& vComp = raw.get<GeometrySources::Vertices>(entity);
    EXPECT_TRUE(vComp.Properties.Exists(PropertyNames::kPosition));
    EXPECT_FALSE(vComp.Properties.Exists(PropertyNames::kNormal));
}

TEST(ECSGeometrySourcesPopulate, PopulateFromCloudYieldsPointCloudDomain)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
    (void)cloud.AddPoint({1.0f, 0.0f, 0.0f});

    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::PointCloud);
    EXPECT_EQ(view.VerticesAlive(), static_cast<std::size_t>(2));
}

TEST(ECSGeometrySourcesPopulate, PointCloudAvailabilityReportsVertexPointSource)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
    (void)cloud.AddPoint({1.0f, 0.0f, 0.0f});

    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    const auto availability = GeometrySources::BuildSourceAvailability(view);

    EXPECT_EQ(availability.ExactDomain, GeometrySources::Domain::PointCloud);
    EXPECT_EQ(availability.ProvenanceDomain, GeometrySources::Domain::PointCloud);
    EXPECT_TRUE(availability.HasPointCloudProvenance());
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::VertexPoints));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::NodePoints));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::Edges));
    EXPECT_TRUE(availability.HasPointSource());
    EXPECT_EQ(availability.VertexPointCount, static_cast<std::size_t>(2));
    EXPECT_EQ(availability.PointCount(), static_cast<std::size_t>(2));
}

TEST(ECSGeometrySourcesPopulate, PartialMeshMarkedEntityKeepsMeshProvenanceWithoutMissingSources)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    GeometrySources::Vertices vertices{};
    vertices.Properties.GetOrAdd<glm::vec3>(
        std::string{PropertyNames::kPosition},
        glm::vec3{})
        .Vector()
        .resize(3u, glm::vec3{1.0f, 0.0f, 0.0f});
    GeometrySources::Faces faces{};
    faces.Properties.GetOrAdd<std::uint32_t>(
        std::string{PropertyNames::kFaceHalfedge},
        kInvalidIndex)
        .Vector()
        .resize(1u, kInvalidIndex);

    raw.emplace<GeometrySources::Vertices>(entity, std::move(vertices));
    raw.emplace<GeometrySources::Faces>(entity, std::move(faces));
    raw.emplace<GeometrySources::HasMeshTopology>(entity);

    const auto view = GeometrySources::BuildConstView(raw, entity);
    const auto availability = GeometrySources::BuildSourceAvailability(view);

    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::Unknown);
    EXPECT_EQ(availability.ExactDomain, GeometrySources::Domain::Unknown);
    EXPECT_EQ(availability.ProvenanceDomain, GeometrySources::Domain::Mesh);
    EXPECT_TRUE(availability.HasMeshProvenance());
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::VertexPoints));
    EXPECT_TRUE(availability.Has(GeometrySources::SourceCapability::Faces));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::Edges));
    EXPECT_FALSE(availability.Has(GeometrySources::SourceCapability::Halfedges));
}

// =============================================================================
// Cross-cutting: ownership survives source destruction
// =============================================================================
TEST(ECSGeometrySourcesPopulate, ECSComponentSurvivesSourceMeshDestruction)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    {
        auto mesh = MakeQuadMesh();
        GeometrySources::PopulateFromMesh(raw, entity, mesh);
    }
    // The source mesh is now destroyed; the ECS-owned PropertySets must
    // still resolve canonical data because PopulateFromMesh copies them.

    auto& vComp = raw.get<GeometrySources::Vertices>(entity);
    ASSERT_TRUE(vComp.Properties.Exists(PropertyNames::kPosition));
    auto posProp = vComp.Properties.Get<glm::vec3>(PropertyNames::kPosition);
    EXPECT_EQ(posProp.Vector().size(), static_cast<std::size_t>(4));
    EXPECT_EQ(posProp.Vector()[2], glm::vec3(1.0f, 1.0f, 0.0f));
}

// =============================================================================
// Re-population across domains drops stale GeometrySources components
// =============================================================================
TEST(ECSGeometrySourcesPopulate, MeshToCloudRePopulationDropsMeshTopology)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    {
        auto mesh = MakeQuadMesh();
        GeometrySources::PopulateFromMesh(raw, entity, mesh);
    }
    ASSERT_TRUE(raw.all_of<GeometrySources::Edges>(entity));
    ASSERT_TRUE(raw.all_of<GeometrySources::Halfedges>(entity));
    ASSERT_TRUE(raw.all_of<GeometrySources::Faces>(entity));

    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({0.0f, 0.0f, 0.0f});
    (void)cloud.AddPoint({1.0f, 0.0f, 0.0f});
    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    EXPECT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Halfedges>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Faces>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Nodes>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::HasMeshTopology>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::HasGraphTopology>(entity));

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::PointCloud);
    EXPECT_EQ(view.VerticesAlive(), static_cast<std::size_t>(2));
}

TEST(ECSGeometrySourcesPopulate, GraphToCloudRePopulationDropsGraphState)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    {
        Geometry::Graph::Graph graph;
        const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
        const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
        (void)graph.AddEdge(v0, v1);
        GeometrySources::PopulateFromGraph(raw, entity, graph);
    }
    ASSERT_TRUE(raw.all_of<GeometrySources::Nodes>(entity));
    ASSERT_TRUE(raw.all_of<GeometrySources::Edges>(entity));
    ASSERT_TRUE(raw.all_of<GeometrySources::HasGraphTopology>(entity));

    Geometry::PointCloud::Cloud cloud;
    (void)cloud.AddPoint({2.0f, 2.0f, 2.0f});
    GeometrySources::PopulateFromCloud(raw, entity, cloud);

    EXPECT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Nodes>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::HasGraphTopology>(entity));

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::PointCloud);
    EXPECT_EQ(view.VerticesAlive(), static_cast<std::size_t>(1));
}

TEST(ECSGeometrySourcesPopulate, MeshToGraphRePopulationDropsMeshFacesAndHalfedges)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    {
        auto mesh = MakeQuadMesh();
        GeometrySources::PopulateFromMesh(raw, entity, mesh);
    }
    ASSERT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));
    ASSERT_TRUE(raw.all_of<GeometrySources::Halfedges>(entity));
    ASSERT_TRUE(raw.all_of<GeometrySources::Faces>(entity));

    Geometry::Graph::Graph graph;
    const auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = graph.AddVertex({2.0f, 0.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);
    (void)graph.AddEdge(v1, v2);
    GeometrySources::PopulateFromGraph(raw, entity, graph);

    EXPECT_TRUE(raw.all_of<GeometrySources::Nodes>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::HasGraphTopology>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Halfedges>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Faces>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::HasMeshTopology>(entity));

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::Graph);
    EXPECT_EQ(view.NodesAlive(), static_cast<std::size_t>(3));
    EXPECT_EQ(view.EdgesAlive(), static_cast<std::size_t>(2));
}

TEST(ECSGeometrySourcesPopulate, CloudToMeshRePopulationProducesMeshDomain)
{
    Registry scene;
    auto& raw = scene.Raw();
    const EntityHandle entity = scene.Create();

    {
        Geometry::PointCloud::Cloud cloud;
        (void)cloud.AddPoint({5.0f, 5.0f, 5.0f});
        GeometrySources::PopulateFromCloud(raw, entity, cloud);
    }
    ASSERT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));

    auto mesh = MakeQuadMesh();
    GeometrySources::PopulateFromMesh(raw, entity, mesh);

    EXPECT_TRUE(raw.all_of<GeometrySources::Vertices>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Edges>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Halfedges>(entity));
    EXPECT_TRUE(raw.all_of<GeometrySources::Faces>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::Nodes>(entity));
    EXPECT_FALSE(raw.all_of<GeometrySources::HasGraphTopology>(entity));

    // Vertex data must be the mesh's, not the prior point cloud's.
    auto& vComp = raw.get<GeometrySources::Vertices>(entity);
    auto posProp = vComp.Properties.Get<glm::vec3>(PropertyNames::kPosition);
    ASSERT_EQ(posProp.Vector().size(), static_cast<std::size_t>(4));
    EXPECT_EQ(posProp.Vector()[0], glm::vec3(0.0f, 0.0f, 0.0f));

    const auto view = GeometrySources::BuildConstView(raw, entity);
    EXPECT_EQ(view.ActiveDomain, GeometrySources::Domain::Mesh);
}
