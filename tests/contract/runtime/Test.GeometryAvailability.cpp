#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

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

#include "../../../src/runtime/GeometryIntegration/Runtime.GeometryPositionCapture.hpp"

namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace G = Extrinsic::Graphics::Components;
namespace Runtime = Extrinsic::Runtime;

using Runtime::GeometryProcessingDetail::MeshSupport::CollectFiniteGeometryPositions;

TEST(GeometryPositionCapture, RejectsMissingWrongTypedEmptyAndMisSizedStorage)
{
    Geometry::PropertySet properties;
    EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "f:center"));
    (void)properties.GetOrAdd<float>("f:scalar");
    EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "f:scalar"));
    auto positions = properties.GetOrAdd<glm::vec3>("f:center");
    EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "f:center"));
    properties.Resize(2u);
    EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "f:scalar"));
    positions.Vector().resize(1u);
    EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "f:center"));
    positions.Vector().resize(3u);
    EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "f:center"));
}

TEST(GeometryPositionCapture, OwnsAllRowsInOrderWithoutInterpretingDeletion)
{
    Geometry::PropertySet properties;
    properties.Resize(3u);
    auto positions = properties.GetOrAdd<glm::vec3>("f:center");
    positions.Vector() = {{3, 2, 1}, {-0.0f, 0, 0},
                          {std::numeric_limits<float>::denorm_min(), 4, 5}};
    auto deleted = properties.GetOrAdd<bool>("f:deleted");
    deleted[1] = true;
    const auto snapshot = CollectFiniteGeometryPositions(properties, "f:center");
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(*snapshot, positions.Vector());
    ASSERT_EQ(snapshot->size(), 3u);
    positions[0] = {99, 99, 99};
    EXPECT_EQ(snapshot->front(), glm::vec3(3, 2, 1));
    EXPECT_TRUE(deleted[1]);
    EXPECT_EQ(properties.Size(), 3u);
}

TEST(GeometryPositionCapture, RejectsNonFiniteComponentsEvenInDeletedRows)
{
    Geometry::PropertySet properties;
    properties.Resize(2u);
    auto positions = properties.GetOrAdd<glm::vec3>("p:sample");
    auto deleted = properties.GetOrAdd<bool>("p:deleted");
    deleted[1] = true;
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity()})
    {
        for (int component = 0; component < 3; ++component)
        {
            positions[1] = glm::vec3(0);
            positions[1][component] = invalid;
            EXPECT_FALSE(CollectFiniteGeometryPositions(properties, "p:sample"));
            EXPECT_TRUE(deleted[1]);
            EXPECT_EQ(properties.Size(), 2u);
        }
    }
}

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

TEST(RuntimeGeometryAvailability, GraphSupportsUnifiedVertexHalfedgeAndEdgeSources)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto graph = MakeGraph();
    GS::PopulateFromGraph(registry, entity, graph);
    registry.emplace<G::RenderEdges>(entity);
    registry.emplace<G::RenderPoints>(entity);

    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Graph);
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Vertices));
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Halfedges));
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Edges));
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Edges).Ready());
    EXPECT_TRUE(Runtime::ResolveRenderLaneAvailability(
        availability, Runtime::GeometryRenderLane::Points).Ready());
    EXPECT_FALSE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshHalfedge));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphNode));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphHalfedge));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphEdge));
    EXPECT_EQ(Runtime::ResolveGeometryPropertySet(
                  availability,
                  Runtime::GeometryElementDomain::GraphNode),
              &availability.SourceView.VertexSource->Properties);
    EXPECT_EQ(Runtime::ResolveGeometryPropertySet(
                  availability,
                  Runtime::GeometryElementDomain::GraphHalfedge),
              &availability.SourceView.HalfedgeSource->Properties);
}

TEST(RuntimeGeometryAvailability, SharedSourceMatrixDoesNotReplaceGraphProvenance)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto graph = MakeGraph();
    GS::PopulateFromGraph(registry, entity, graph);

    registry.remove<GS::HasGraphTopology>(entity);
    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    EXPECT_EQ(availability.Sources.ExactDomain, GS::Domain::Unknown);
    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Unknown);
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Vertices));
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Halfedges));
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Edges));
    EXPECT_FALSE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphNode));
    EXPECT_FALSE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshVertex));
}

TEST(RuntimeGeometryAvailability, GraphMarkerKeepsProvenanceForMixedSourceMatrix)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto graph = MakeGraph();
    GS::PopulateFromGraph(registry, entity, graph);

    registry.emplace<GS::Faces>(entity);
    const auto availability = Runtime::BuildGeometryAvailability(registry, entity);

    EXPECT_EQ(availability.Sources.ExactDomain, GS::Domain::Unknown);
    EXPECT_EQ(availability.Sources.ProvenanceDomain, GS::Domain::Graph);
    EXPECT_TRUE(availability.Sources.Has(GS::SourceCapability::Faces));
    EXPECT_TRUE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::GraphNode));
    EXPECT_FALSE(Runtime::SupportsGeometryElementDomain(
        availability, Runtime::GeometryElementDomain::MeshFace));
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

TEST(RuntimeGeometryProperty, GraphHalfedgePropertiesUseCanonicalDomain)
{
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto graph = MakeGraph();
    graph.HalfedgeProperties()
        .GetOrAdd<float>("h:weight", 0.0f)
        .Vector() = {1.0f, 2.0f};
    GS::PopulateFromGraph(registry, entity, graph);

    const auto availability =
        Runtime::BuildGeometryAvailability(registry, entity);
    const auto* halfedges = Runtime::ResolveGeometryPropertySet(
        availability,
        Runtime::GeometryElementDomain::GraphHalfedge);
    ASSERT_NE(halfedges, nullptr);
    EXPECT_EQ(halfedges, &availability.SourceView.HalfedgeSource->Properties);
    EXPECT_EQ(Runtime::ResolveGeometryElementCount(
                  availability,
                  Runtime::GeometryElementDomain::GraphHalfedge),
              2u);
    EXPECT_EQ(Runtime::ToString(
                  Runtime::GeometryElementDomain::GraphHalfedge),
              "GraphHalfedge");

    const auto snapshot =
        Runtime::BuildGeometryPropertyCatalogSnapshot(availability);
    const auto* entry = Runtime::FindGeometryPropertyCatalogEntry(
        snapshot,
        Runtime::GeometryElementDomain::GraphHalfedge,
        "h:weight");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Ref.ValueKind, Geometry::PropertyValueKind::Float);
    EXPECT_EQ(entry->ElementCount, 2u);
    EXPECT_TRUE(Runtime::ResolveGeometryProperty(
                    availability,
                    Runtime::GeometryElementDomain::GraphHalfedge,
                    "h:weight",
                    Geometry::PropertyValueKind::Float,
                    2u)
                    .Resolved());
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


TEST(RuntimeGeometryProperty, StructuralProtectionUsesDomainIdentityNotPrefixes)
{
    using D = Runtime::GeometryElementDomain;
    EXPECT_TRUE(Runtime::IsTopologyProperty(D::MeshFace, "f:connectivity"));
    EXPECT_TRUE(Runtime::IsTopologyProperty(D::MeshHalfedge, "h:next"));
    EXPECT_TRUE(Runtime::IsTopologyProperty(D::GraphEdge, "e:v0"));
    EXPECT_TRUE(Runtime::IsTopologyProperty(D::PointCloudPoint, "v:deleted"));
    EXPECT_FALSE(Runtime::IsTopologyProperty(D::MeshVertex, "f:connectivity"));
    EXPECT_FALSE(Runtime::IsTopologyProperty(D::MeshFace, "v:position"));
    EXPECT_FALSE(Runtime::IsTopologyProperty(D::PointCloudPoint, "v:connectivity"));
    EXPECT_FALSE(Runtime::IsTopologyProperty(D::MeshFace, "f:temperature"));
}

TEST(GeometryScalarPublication, ConvertsOnlyLiveRowsAndPreservesExactUndoStorage)
{
    Geometry::PropertySet properties;
    properties.Resize(3);
    const Runtime::GeometryPropertyRef ref{Runtime::GeometryElementDomain::MeshFace,
        "arbitrary", Geometry::PropertyValueKind::UInt64};
    properties.GetOrAdd<std::uint64_t>(ref.Name).Vector() = {9, UINT64_MAX, 8};
    const auto before = Runtime::CaptureGeometryScalarProperty(properties, ref);
    auto after = before;
    const std::vector<std::uint32_t> slots{0,2};
    const std::vector<double> values{1, std::numeric_limits<double>::quiet_NaN(), 2};
    ASSERT_TRUE(Runtime::PrepareGeometryScalarProperty(after, ref.ValueKind, 3, slots, values));
    Runtime::ApplyGeometryScalarProperty(properties, ref, after);
    EXPECT_EQ(properties.Get<std::uint64_t>(ref.Name).Vector(),
        (std::vector<std::uint64_t>{1, UINT64_MAX, 2}));
    Runtime::ApplyGeometryScalarProperty(properties, ref, before);
    EXPECT_EQ(properties.Get<std::uint64_t>(ref.Name).Vector(),
        (std::vector<std::uint64_t>{9, UINT64_MAX, 8}));
}

TEST(GeometryScalarPublication, RejectsInexactNonfiniteAndOutOfRangeBeforePublication)
{
    using K = Geometry::PropertyValueKind;
    const std::vector<std::uint32_t> slots{0};
    for (const auto [kind, value] : std::vector<std::pair<K,double>>{
        {K::Bool,2}, {K::Int32,0.5}, {K::Int32,2147483648.0}, {K::UInt32,-1},
        {K::UInt64,18446744073709551616.0}, {K::Float,16777217.0},
        {K::Double,std::numeric_limits<double>::infinity()},
        {K::Float,std::numeric_limits<double>::quiet_NaN()}})
    {
        SCOPED_TRACE(int(kind));
        Runtime::GeometryScalarPropertySnapshot state;
        const std::vector<double> values{value};
        EXPECT_FALSE(Runtime::PrepareGeometryScalarProperty(state,kind,1,slots,values));
        EXPECT_FALSE(state.Exists);
    }
    for (const auto kind : {K::Bool,K::Int32,K::UInt32,K::UInt64,K::Float,K::Double})
    {
        Geometry::PropertySet properties;
        properties.Resize(1);
        Runtime::GeometryScalarPropertySnapshot state;
        const std::vector<double> values{1};
        ASSERT_TRUE(Runtime::PrepareGeometryScalarProperty(state,kind,1,slots,values));
        const Runtime::GeometryPropertyRef ref{Runtime::GeometryElementDomain::MeshFace,"field",kind};
        Runtime::ApplyGeometryScalarProperty(properties,ref,state);
        EXPECT_EQ(Runtime::DetectGeometryPropertyValueKind(properties,ref.Name),kind);
    }
}

TEST(GeometryScalarPublication, SentinelPolicyAndSnapshotEqualityAreExplicit)
{
    Geometry::PropertySet properties;
    properties.Resize(2);
    Runtime::GeometryPropertyRef ref{Runtime::GeometryElementDomain::MeshVertex,"distance",Geometry::PropertyValueKind::Double};
    properties.GetOrAdd<double>(ref.Name).Vector()={0,std::numeric_limits<double>::quiet_NaN()};
    const auto before=Runtime::CaptureGeometryScalarProperty(properties,ref);
    auto after=before;
    EXPECT_TRUE(Runtime::SameGeometryScalarPropertySnapshot(before,after));
    EXPECT_EQ(Runtime::GeometryScalarPropertySize(before),2u);
    const std::vector<std::uint32_t> slots{0};
    const std::vector<double> values{std::numeric_limits<double>::infinity(),0};
    EXPECT_FALSE(Runtime::PrepareGeometryScalarProperty(after,ref.ValueKind,2,slots,values));
    EXPECT_TRUE(Runtime::SameGeometryScalarPropertySnapshot(before,after));
    ASSERT_TRUE(Runtime::PrepareGeometryScalarProperty(after,ref.ValueKind,2,slots,values,
        Runtime::GeometryScalarNonfinitePolicy::AllowInfinity));
    EXPECT_FALSE(Runtime::SameGeometryScalarPropertySnapshot(before,after));
    Runtime::ApplyGeometryScalarProperty(properties,ref,after);
    Runtime::ApplyGeometryScalarProperty(properties,ref,before);
    EXPECT_TRUE(Runtime::SameGeometryScalarPropertySnapshot(before,
        Runtime::CaptureGeometryScalarProperty(properties,ref)));
}
