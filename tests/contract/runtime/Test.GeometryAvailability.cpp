#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Runtime.GeometryAvailability;

import Geometry.Properties;
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

// ============================================================================
// RUNTIME-192 — canonical geometry-property reference and catalog.
//
// These pin the one vocabulary that replaces the duplicated
// progressive/editor/bake property-domain and value-kind enums.
// ============================================================================

TEST(RuntimeGeometryProperty, ValueKindFilterTreatsNulloptAsUnconstrained)
{
    using Runtime::MatchesGeometryPropertyValueKind;
    using Kind = Geometry::PropertyValueKind;

    // std::nullopt is the "any kind" constraint that the retired
    // ProgressivePropertyValueKind::Any enumerator used to express.
    EXPECT_TRUE(MatchesGeometryPropertyValueKind(std::nullopt, Kind::Float));
    EXPECT_TRUE(MatchesGeometryPropertyValueKind(std::nullopt, Kind::Vec3));
    EXPECT_TRUE(MatchesGeometryPropertyValueKind(std::nullopt, Kind::Unknown));

    EXPECT_TRUE(MatchesGeometryPropertyValueKind(Kind::Float, Kind::Float));
    EXPECT_FALSE(MatchesGeometryPropertyValueKind(Kind::Float, Kind::Double));
    EXPECT_FALSE(MatchesGeometryPropertyValueKind(Kind::Vec3, Kind::Vec4));
}

TEST(RuntimeGeometryProperty, RefComparesOnDomainNameAndValueKind)
{
    const Runtime::GeometryPropertyRef base{
        .Domain = Runtime::GeometryElementDomain::MeshVertex,
        .Name = "v:quality",
        .ValueKind = Geometry::PropertyValueKind::Float,
    };

    EXPECT_EQ(base, base);
    EXPECT_TRUE(base.HasName());

    Runtime::GeometryPropertyRef otherDomain = base;
    otherDomain.Domain = Runtime::GeometryElementDomain::MeshFace;
    EXPECT_NE(base, otherDomain);

    Runtime::GeometryPropertyRef otherName = base;
    otherName.Name = "v:other";
    EXPECT_NE(base, otherName);

    Runtime::GeometryPropertyRef otherKind = base;
    otherKind.ValueKind = Geometry::PropertyValueKind::Double;
    EXPECT_NE(base, otherKind);

    EXPECT_FALSE(Runtime::GeometryPropertyRef{}.HasName());
}

TEST(RuntimeGeometryProperty, CatalogSnapshotIsDeterministicAndCarriesIdentity)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto mesh = MakeTriangleMesh();

    // Insert out of alphabetical order so the sort is actually exercised.
    auto zebra = mesh.VertexProperties().Add<float>("v:zebra", 0.0f);
    auto alpha = mesh.VertexProperties().Add<glm::vec3>("v:alpha", glm::vec3{0.0f});
    ASSERT_TRUE(zebra.IsValid());
    ASSERT_TRUE(alpha.IsValid());

    GS::PopulateFromMesh(registry, entity, mesh);
    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    const auto snapshot =
        Runtime::BuildGeometryPropertyCatalogSnapshot(availability, 77u, 5u);

    EXPECT_EQ(snapshot.SourceStableId, 77u);
    EXPECT_EQ(snapshot.SourceGeneration, 5u);
    EXPECT_FALSE(snapshot.Empty());

    // Deterministic order: (domain, name) ascending, independent of the order
    // the properties were added in.
    for (std::size_t i = 1; i < snapshot.Entries.size(); ++i)
    {
        const auto& prev = snapshot.Entries[i - 1].Ref;
        const auto& cur = snapshot.Entries[i].Ref;
        const bool ordered = std::tie(prev.Domain, prev.Name)
                           < std::tie(cur.Domain, cur.Name);
        EXPECT_TRUE(ordered)
            << "entry " << i << " out of order: " << prev.Name
            << " then " << cur.Name;
    }

    // Rebuilding the same source yields an identical snapshot.
    const auto again =
        Runtime::BuildGeometryPropertyCatalogSnapshot(availability, 77u, 5u);
    ASSERT_EQ(again.Entries.size(), snapshot.Entries.size());
    for (std::size_t i = 0; i < snapshot.Entries.size(); ++i)
        EXPECT_EQ(again.Entries[i].Ref, snapshot.Entries[i].Ref);

    const auto* zebraEntry = Runtime::FindGeometryPropertyCatalogEntry(
        snapshot, Runtime::GeometryElementDomain::MeshVertex, "v:zebra");
    ASSERT_NE(zebraEntry, nullptr);
    EXPECT_EQ(zebraEntry->Ref.ValueKind, Geometry::PropertyValueKind::Float);
    EXPECT_EQ(zebraEntry->ElementCount, mesh.VertexProperties().Size());
    EXPECT_EQ(zebraEntry->PropertyGeneration, 5u);

    const auto* alphaEntry = Runtime::FindGeometryPropertyCatalogEntry(
        snapshot, Runtime::GeometryElementDomain::MeshVertex, "v:alpha");
    ASSERT_NE(alphaEntry, nullptr);
    EXPECT_EQ(alphaEntry->Ref.ValueKind, Geometry::PropertyValueKind::Vec3);

    // Wrong domain and missing name resolve to no entry rather than a false hit.
    EXPECT_EQ(Runtime::FindGeometryPropertyCatalogEntry(
                  snapshot, Runtime::GeometryElementDomain::MeshFace, "v:zebra"),
              nullptr);
    EXPECT_EQ(Runtime::FindGeometryPropertyCatalogEntry(
                  snapshot, Runtime::GeometryElementDomain::MeshVertex, "v:absent"),
              nullptr);
}

TEST(RuntimeGeometryProperty, ResolutionReportsEveryFailureModeDistinctly)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto mesh = MakeTriangleMesh();
    auto quality = mesh.VertexProperties().Add<float>("v:quality", 1.0f);
    ASSERT_TRUE(quality.IsValid());
    GS::PopulateFromMesh(registry, entity, mesh);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);
    const std::size_t vertexCount = mesh.VertexProperties().Size();
    using Status = Runtime::GeometryPropertyResolutionStatus;
    using Kind = Geometry::PropertyValueKind;
    using Domain = Runtime::GeometryElementDomain;

    // Resolved, with the observed generation echoed back for revalidation.
    const auto ok = Runtime::ResolveGeometryProperty(
        availability, Domain::MeshVertex, "v:quality", Kind::Float,
        vertexCount, /*requireFiniteValues=*/true, /*observedSourceGeneration=*/9u);
    EXPECT_EQ(ok.Status, Status::Resolved);
    EXPECT_TRUE(ok.Resolved());
    EXPECT_EQ(ok.ResolvedValueKind, Kind::Float);
    EXPECT_EQ(ok.ElementCount, vertexCount);
    EXPECT_EQ(ok.ObservedSourceGeneration, 9u);

    // Unconstrained kind still resolves.
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:quality").Status,
              Status::Resolved);

    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::Unknown, "v:quality").Status,
              Status::UnsupportedDomain);
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "").Status,
              Status::MissingName);
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:absent").Status,
              Status::MissingProperty);
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:quality", Kind::Vec3).Status,
              Status::ValueKindMismatch);
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:quality", std::nullopt,
                  vertexCount + 1u).Status,
              Status::ElementCountMismatch);

    // A point-cloud domain is not resolvable on a mesh source.
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::PointCloudPoint, "v:quality").Status,
              Status::UnsupportedDomain);

    EXPECT_EQ(Runtime::ToString(Status::Resolved), "Resolved");
    EXPECT_EQ(Runtime::ToString(Status::NonFiniteValues), "NonFiniteValues");
}

TEST(RuntimeGeometryProperty, NonFiniteValuesAreOptInAndDetectedPerKind)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto mesh = MakeTriangleMesh();

    auto scalar = mesh.VertexProperties().Add<float>("v:scalar", 0.0f);
    auto vector = mesh.VertexProperties().Add<glm::vec3>("v:vector", glm::vec3{0.0f});
    auto label = mesh.VertexProperties().Add<std::uint32_t>("v:label", 0u);
    ASSERT_TRUE(scalar.IsValid());
    ASSERT_TRUE(vector.IsValid());
    ASSERT_TRUE(label.IsValid());

    scalar[0] = std::numeric_limits<float>::quiet_NaN();
    vector[1] = glm::vec3{0.0f, std::numeric_limits<float>::infinity(), 0.0f};

    GS::PopulateFromMesh(registry, entity, mesh);
    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);
    using Status = Runtime::GeometryPropertyResolutionStatus;
    using Domain = Runtime::GeometryElementDomain;

    // Opt-in: without the flag a non-finite property still resolves, so
    // consumers that do not care never pay the O(n) scan.
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:scalar").Status,
              Status::Resolved);

    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:scalar", std::nullopt,
                  std::nullopt, /*requireFiniteValues=*/true).Status,
              Status::NonFiniteValues);
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:vector", std::nullopt,
                  std::nullopt, /*requireFiniteValues=*/true).Status,
              Status::NonFiniteValues);

    // Integral kinds cannot be non-finite and must not be rejected.
    EXPECT_EQ(Runtime::ResolveGeometryProperty(
                  availability, Domain::MeshVertex, "v:label", std::nullopt,
                  std::nullopt, /*requireFiniteValues=*/true).Status,
              Status::Resolved);

    const auto& properties = mesh.VertexProperties();
    EXPECT_FALSE(Runtime::GeometryPropertyValuesAreFinite(properties, "v:scalar"));
    EXPECT_FALSE(Runtime::GeometryPropertyValuesAreFinite(properties, "v:vector"));
    EXPECT_TRUE(Runtime::GeometryPropertyValuesAreFinite(properties, "v:label"));
    // A missing property is reported by resolution, not by this predicate.
    EXPECT_TRUE(Runtime::GeometryPropertyValuesAreFinite(properties, "v:absent"));
}

TEST(RuntimeGeometryProperty, RefOverloadTreatsUnknownKindAsUnconstrained)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto mesh = MakeTriangleMesh();
    auto quality = mesh.VertexProperties().Add<float>("v:quality", 1.0f);
    ASSERT_TRUE(quality.IsValid());
    GS::PopulateFromMesh(registry, entity, mesh);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);
    using Status = Runtime::GeometryPropertyResolutionStatus;

    // An authoring recipe that names a property without pinning its kind
    // resolves against whatever kind the source actually has.
    const Runtime::GeometryPropertyRef unconstrained{
        .Domain = Runtime::GeometryElementDomain::MeshVertex,
        .Name = "v:quality",
    };
    const auto resolved =
        Runtime::ResolveGeometryProperty(availability, unconstrained);
    EXPECT_EQ(resolved.Status, Status::Resolved);
    EXPECT_EQ(resolved.ResolvedValueKind, Geometry::PropertyValueKind::Float);

    const Runtime::GeometryPropertyRef wrongKind{
        .Domain = Runtime::GeometryElementDomain::MeshVertex,
        .Name = "v:quality",
        .ValueKind = Geometry::PropertyValueKind::Vec4,
    };
    EXPECT_EQ(Runtime::ResolveGeometryProperty(availability, wrongKind).Status,
              Status::ValueKindMismatch);
}
