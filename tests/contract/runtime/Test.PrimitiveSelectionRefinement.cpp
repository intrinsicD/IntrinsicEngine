#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Geometry.Properties;

using Extrinsic::ECS::Components::GeometrySources::ConstSourceView;
using Extrinsic::ECS::Components::GeometrySources::Domain;
using Extrinsic::ECS::Components::GeometrySources::Edges;
using Extrinsic::ECS::Components::GeometrySources::Faces;
using Extrinsic::ECS::Components::GeometrySources::Halfedges;
using Extrinsic::ECS::Components::GeometrySources::Nodes;
using Extrinsic::ECS::Components::GeometrySources::Vertices;
using Extrinsic::Graphics::EncodeSelectionId;
using Extrinsic::Graphics::SelectionPrimitiveDomain;
using Extrinsic::Runtime::BuildSurfaceTriangleFaceMap;
using Extrinsic::Runtime::MeshPackBuffer;
using Extrinsic::Runtime::MeshPackStatus;
using Extrinsic::Runtime::PackMesh;
using Extrinsic::Runtime::kInvalidPrimitiveIndex;
using Extrinsic::Runtime::PrimitiveRefineRequest;
using Extrinsic::Runtime::PrimitiveRefineStatus;
using Extrinsic::Runtime::PrimitiveSelectionResult;
using Extrinsic::Runtime::RefinedPrimitiveKind;
using Extrinsic::Runtime::RefinePrimitiveSelection;

namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    void SetPositions(Geometry::PropertySet& set, const std::vector<glm::vec3>& positions)
    {
        set.Resize(positions.size());
        auto pos = set.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetU32(Geometry::PropertySet& set,
                std::string_view name,
                const std::vector<std::uint32_t>& values,
                std::size_t rows)
    {
        if (set.Size() != rows)
        {
            set.Resize(rows);
        }
        auto prop = set.GetOrAdd<std::uint32_t>(std::string{name}, 0u);
        prop.Vector() = values;
    }

    // Single CCW triangle (v0,v1,v2) with three halfedges, one face, three edges.
    //   h0: to_vertex=1, next=h1
    //   h1: to_vertex=2, next=h2
    //   h2: to_vertex=0, next=h0   (face ring vertices: 1,2,0)
    struct MeshScratch
    {
        Vertices VertexSource{};
        Edges EdgeSource{};
        Halfedges HalfedgeSource{};
        Faces FaceSource{};

        MeshScratch()
        {
            SetPositions(VertexSource.Properties, {
                {0.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f},
            });
            SetU32(EdgeSource.Properties, pn::kEdgeV0, {0u, 1u, 2u}, 3);
            SetU32(EdgeSource.Properties, pn::kEdgeV1, {1u, 2u, 0u}, 3);
            SetU32(HalfedgeSource.Properties, pn::kHalfedgeToVertex, {1u, 2u, 0u}, 3);
            SetU32(HalfedgeSource.Properties, pn::kHalfedgeNext, {1u, 2u, 0u}, 3);
            SetU32(HalfedgeSource.Properties, pn::kHalfedgeFace, {0u, 0u, 0u}, 3);
            SetU32(FaceSource.Properties, pn::kFaceHalfedge, {0u}, 1);
        }

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::Mesh;
            view.VertexSource = &VertexSource;
            view.EdgeSource = &EdgeSource;
            view.HalfedgeSource = &HalfedgeSource;
            view.FaceSource = &FaceSource;
            return view;
        }
    };

    // Mesh with one quad face (rows 0..3, fan-triangulated to 2 triangles) and
    // one triangle face (rows 4..6, 1 triangle). Surface triangle order is
    // [quad-tri-0, quad-tri-1, tri], so the gl_PrimitiveID -> face map is
    // [0, 0, 1] — the case where a raw payload would mis-resolve.
    struct MeshQuadTriScratch
    {
        Vertices VertexSource{};
        Halfedges HalfedgeSource{};
        Faces FaceSource{};

        MeshQuadTriScratch()
        {
            SetPositions(VertexSource.Properties, {
                {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                {5.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, {5.0f, 1.0f, 0.0f},
            });
            // h0..h3 = quad ring (face 0), h4..h6 = triangle ring (face 1).
            SetU32(HalfedgeSource.Properties, pn::kHalfedgeToVertex, {1u, 2u, 3u, 0u, 5u, 6u, 4u}, 7);
            SetU32(HalfedgeSource.Properties, pn::kHalfedgeNext, {1u, 2u, 3u, 0u, 5u, 6u, 4u}, 7);
            SetU32(HalfedgeSource.Properties, pn::kHalfedgeFace, {0u, 0u, 0u, 0u, 1u, 1u, 1u}, 7);
            SetU32(FaceSource.Properties, pn::kFaceHalfedge, {0u, 4u}, 2);
        }

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::Mesh;
            view.VertexSource = &VertexSource;
            view.HalfedgeSource = &HalfedgeSource;
            view.FaceSource = &FaceSource;
            return view;
        }
    };

    // Two nodes joined by one edge.
    struct GraphScratch
    {
        Nodes NodeSource{};
        Edges EdgeSource{};

        GraphScratch()
        {
            SetPositions(NodeSource.Properties, {
                {0.0f, 0.0f, 0.0f},
                {2.0f, 0.0f, 0.0f},
            });
            SetU32(EdgeSource.Properties, pn::kEdgeV0, {0u}, 1);
            SetU32(EdgeSource.Properties, pn::kEdgeV1, {1u}, 1);
        }

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::Graph;
            view.NodeSource = &NodeSource;
            view.EdgeSource = &EdgeSource;
            return view;
        }
    };

    struct CloudScratch
    {
        Vertices VertexSource{};

        CloudScratch()
        {
            SetPositions(VertexSource.Properties, {
                {0.0f, 0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f},
                {2.0f, 2.0f, 2.0f},
            });
        }

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::PointCloud;
            view.VertexSource = &VertexSource;
            return view;
        }
    };

    [[nodiscard]] PrimitiveRefineRequest BaseRequest() noexcept
    {
        PrimitiveRefineRequest request{};
        request.EntityId = 42u;
        request.StableId = 7u;
        request.EntityIsLive = true;
        request.LocalToWorld = glm::mat4(1.0f);
        return request;
    }
}

// ---- Mesh ------------------------------------------------------------------

TEST(PrimitiveSelectionRefinement, MeshFaceHintAnchorsFaceWithNearestVertexAndEdge)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 0u);
    request.HasLocalHit = true;
    // Hit sits just above the bottom edge near vertex 1: nearest vertex is 1,
    // nearest boundary edge is (0,1) = edge index 0.
    request.LocalHit = glm::vec3(0.8f, 0.02f, 0.0f);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_TRUE(result.Resolved());
    EXPECT_EQ(result.EntityId, 42u);
    EXPECT_EQ(result.StableId, 7u);
    EXPECT_EQ(result.Domain, Domain::Mesh);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Face);
    EXPECT_EQ(result.FaceId, 0u);
    EXPECT_EQ(result.VertexId, 1u);
    EXPECT_EQ(result.EdgeId, 0u); // edge (0,1).
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.WorldHit.x, 0.8f);
    EXPECT_FLOAT_EQ(result.WorldHit.y, 0.02f);
}

TEST(PrimitiveSelectionRefinement, MeshFaceHintWithoutAnchorReportsCentroidOnly)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Face);
    EXPECT_EQ(result.FaceId, 0u);
    EXPECT_EQ(result.VertexId, kInvalidPrimitiveIndex);
    EXPECT_EQ(result.EdgeId, kInvalidPrimitiveIndex);
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(result.LocalHit.y, 1.0f / 3.0f);
}

TEST(PrimitiveSelectionRefinement, MeshEdgeHintReturnsEdgeAndNearestEndpoint)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Edge, 1u); // edge (1,2).
    request.HasLocalHit = true;
    request.LocalHit = glm::vec3(0.1f, 0.9f, 0.0f); // near vertex 2.

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Edge);
    EXPECT_EQ(result.EdgeId, 1u);
    EXPECT_EQ(result.VertexId, 2u);
}

TEST(PrimitiveSelectionRefinement, MeshPointHintResolvesVertex)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Point, 2u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(result.VertexId, 2u);
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.y, 1.0f);
}

TEST(PrimitiveSelectionRefinement, MeshEntityHintSelectsWholeEntity)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Entity);
    EXPECT_EQ(result.FaceId, kInvalidPrimitiveIndex);
    EXPECT_FALSE(result.HasHitPosition);
}

TEST(PrimitiveSelectionRefinement, TransformAppliesToWorldHit)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Point, 1u); // vertex 1 = (1,0,0).
    glm::mat4 transform(1.0f);
    transform[3] = glm::vec4(10.0f, 20.0f, 30.0f, 1.0f);
    request.LocalToWorld = transform;

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    ASSERT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 1.0f);
    EXPECT_FLOAT_EQ(result.WorldHit.x, 11.0f);
    EXPECT_FLOAT_EQ(result.WorldHit.y, 20.0f);
    EXPECT_FLOAT_EQ(result.WorldHit.z, 30.0f);
}

TEST(PrimitiveSelectionRefinement, MeshFaceIndexOutOfRangeFailsClosed)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 9u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::InvalidPrimitivePayload);
    EXPECT_FALSE(result.Resolved());
    EXPECT_EQ(result.FaceId, kInvalidPrimitiveIndex);
}

TEST(PrimitiveSelectionRefinement, MeshMissingPositionsFailsClosed)
{
    // Mesh-domain view whose vertex source carries no `v:position` property.
    Vertices emptyVertices{};
    Edges edges{};
    Halfedges halfedges{};
    Faces faces{};
    ConstSourceView view{};
    view.ActiveDomain = Domain::Mesh;
    view.VertexSource = &emptyVertices;
    view.EdgeSource = &edges;
    view.HalfedgeSource = &halfedges;
    view.FaceSource = &faces;

    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(view, request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::MissingGeometrySource);
}

TEST(PrimitiveSelectionRefinement, MeshFaceHintMapsTrianglePayloadToFaceRow)
{
    const MeshQuadTriScratch mesh;

    // Triangle 0 and triangle 1 are both fan steps of the quad (face 0); the
    // raw payload 1 used to be rejected (1 >= faceCount of 1) or mis-resolve.
    for (std::uint32_t triangle = 0; triangle <= 1u; ++triangle)
    {
        PrimitiveRefineRequest request = BaseRequest();
        request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, triangle);
        const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);
        EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success) << "triangle " << triangle;
        EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Face);
        EXPECT_EQ(result.FaceId, 0u) << "triangle " << triangle << " should resolve to the quad face";
    }

    // Triangle 2 is the standalone triangle face (face 1).
    {
        PrimitiveRefineRequest request = BaseRequest();
        request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 2u);
        const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);
        EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
        EXPECT_EQ(result.FaceId, 1u);
    }

    // Only three surface triangles exist; payload 3 is out of range.
    {
        PrimitiveRefineRequest request = BaseRequest();
        request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 3u);
        const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);
        EXPECT_EQ(result.Status, PrimitiveRefineStatus::InvalidPrimitivePayload);
    }
}

TEST(PrimitiveSelectionRefinement, SurfaceTriangleFaceMapMatchesPackMeshEmission)
{
    const MeshQuadTriScratch mesh;

    std::vector<std::uint32_t> triangleToFace;
    ASSERT_EQ(BuildSurfaceTriangleFaceMap(mesh.View(), triangleToFace), MeshPackStatus::Success);
    EXPECT_EQ(triangleToFace, (std::vector<std::uint32_t>{0u, 0u, 1u}));

    // The map must have exactly one entry per triangle PackMesh emits, so a
    // gl_PrimitiveID can never index outside it.
    MeshPackBuffer buffer{};
    const auto pack = PackMesh(mesh.View(), buffer);
    ASSERT_EQ(pack.Status, MeshPackStatus::Success);
    ASSERT_TRUE(pack.Upload.has_value());
    EXPECT_EQ(pack.Upload->SurfaceIndices.size(), triangleToFace.size() * 3u);
}

// ---- Graph -----------------------------------------------------------------

TEST(PrimitiveSelectionRefinement, GraphEdgeHintReturnsEdgeAndNearestEndpoint)
{
    const GraphScratch graph;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Edge, 0u);
    request.HasLocalHit = true;
    request.LocalHit = glm::vec3(1.8f, 0.0f, 0.0f); // near node 1 = (2,0,0).

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(graph.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Domain, Domain::Graph);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Edge);
    EXPECT_EQ(result.EdgeId, 0u);
    EXPECT_EQ(result.VertexId, 1u);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 1.8f);
}

TEST(PrimitiveSelectionRefinement, GraphPointHintReturnsNode)
{
    const GraphScratch graph;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Point, 1u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(graph.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(result.VertexId, 1u);
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 2.0f);
}

TEST(PrimitiveSelectionRefinement, GraphFaceHintIsUnsupported)
{
    const GraphScratch graph;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(graph.View(), request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::UnsupportedDomain);
}

// ---- Point cloud -----------------------------------------------------------

TEST(PrimitiveSelectionRefinement, PointCloudPointHintReturnsPoint)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Point, 2u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result.Domain, Domain::PointCloud);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Point);
    EXPECT_EQ(result.PointId, 2u);
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.z, 2.0f);
}

TEST(PrimitiveSelectionRefinement, PointCloudPointIndexOutOfRangeFailsClosed)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Point, 5u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::InvalidPrimitivePayload);
    EXPECT_EQ(result.PointId, kInvalidPrimitiveIndex);
}

TEST(PrimitiveSelectionRefinement, PointCloudEdgeHintIsUnsupported)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Edge, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::UnsupportedDomain);
}

// ---- CPU ray fallback (missing hint) ---------------------------------------

TEST(PrimitiveSelectionRefinement, PointCloudMissingHintRayFallbackResolvesNearestPoint)
{
    const CloudScratch cloud; // points (0,0,0),(1,1,1),(2,2,2).
    PrimitiveRefineRequest request = BaseRequest();
    // Default Hint is the all-zero (`None` domain) "no hit" encoding.
    request.HasPickRay = true;
    request.RayOrigin = glm::vec3(1.0f, 1.0f, -5.0f);
    request.RayDirection = glm::vec3(0.0f, 0.0f, 1.0f); // line x=1, y=1.
    request.FallbackRadius = 0.5f;

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_TRUE(result.Resolved());
    EXPECT_EQ(result.Domain, Domain::PointCloud);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Point);
    EXPECT_EQ(result.PointId, 1u); // (1,1,1) lies on the ray.
    EXPECT_EQ(result.VertexId, kInvalidPrimitiveIndex);
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 1.0f);
    EXPECT_FLOAT_EQ(result.LocalHit.y, 1.0f);
    EXPECT_FLOAT_EQ(result.LocalHit.z, 1.0f);
}

TEST(PrimitiveSelectionRefinement, RayFallbackOutsideRadiusMissesClosed)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    request.HasPickRay = true;
    request.RayOrigin = glm::vec3(10.0f, 10.0f, -5.0f);
    request.RayDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    request.FallbackRadius = 0.5f; // every point is far from this ray.

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::CpuFallbackMiss);
    EXPECT_FALSE(result.Resolved());
    EXPECT_EQ(result.PointId, kInvalidPrimitiveIndex);
    EXPECT_FALSE(result.HasHitPosition);
}

TEST(PrimitiveSelectionRefinement, MissingHintWithoutRayIsUnsupported)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    // None-domain hint and HasPickRay left false: no fallback is configured, so
    // the deterministic fail-closed outcome is preserved.
    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::UnsupportedDomain);
    EXPECT_FALSE(result.Resolved());
}

TEST(PrimitiveSelectionRefinement, DegenerateRayFallbackMissesClosed)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    request.HasPickRay = true;
    request.RayOrigin = glm::vec3(1.0f, 1.0f, 1.0f); // sits exactly on point 1.
    request.RayDirection = glm::vec3(0.0f, 0.0f, 0.0f); // degenerate.
    request.FallbackRadius = 1.0f;

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::CpuFallbackMiss);
}

TEST(PrimitiveSelectionRefinement, RayFallbackReportsTransformedWorldHit)
{
    const CloudScratch cloud;
    PrimitiveRefineRequest request = BaseRequest();
    request.HasPickRay = true;
    request.RayOrigin = glm::vec3(1.0f, 1.0f, -5.0f);
    request.RayDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    request.FallbackRadius = 0.5f;
    glm::mat4 transform(1.0f);
    transform[3] = glm::vec4(10.0f, 20.0f, 30.0f, 1.0f);
    request.LocalToWorld = transform;

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(cloud.View(), request);

    ASSERT_EQ(result.Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_EQ(result.PointId, 1u);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 1.0f);
    EXPECT_FLOAT_EQ(result.WorldHit.x, 11.0f);
    EXPECT_FLOAT_EQ(result.WorldHit.y, 21.0f);
    EXPECT_FLOAT_EQ(result.WorldHit.z, 31.0f);
}

TEST(PrimitiveSelectionRefinement, MeshMissingHintRayFallbackResolvesNearestVertex)
{
    const MeshScratch mesh; // vertices (0,0,0),(1,0,0),(0,1,0).
    PrimitiveRefineRequest request = BaseRequest();
    request.HasPickRay = true;
    request.RayOrigin = glm::vec3(1.0f, 0.0f, -5.0f);
    request.RayDirection = glm::vec3(0.0f, 0.0f, 1.0f); // line x=1, y=0 -> vertex 1.
    request.FallbackRadius = 0.5f;

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_EQ(result.Domain, Domain::Mesh);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(result.VertexId, 1u);
    EXPECT_EQ(result.PointId, kInvalidPrimitiveIndex);
    EXPECT_TRUE(result.HasHitPosition);
    EXPECT_FLOAT_EQ(result.LocalHit.x, 1.0f);
}

TEST(PrimitiveSelectionRefinement, GraphMissingHintRayFallbackResolvesNearestNode)
{
    const GraphScratch graph; // nodes (0,0,0),(2,0,0).
    PrimitiveRefineRequest request = BaseRequest();
    request.HasPickRay = true;
    request.RayOrigin = glm::vec3(2.0f, 0.0f, -5.0f);
    request.RayDirection = glm::vec3(0.0f, 0.0f, 1.0f); // line x=2 -> node 1.
    request.FallbackRadius = 0.5f;

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(graph.View(), request);

    EXPECT_EQ(result.Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_EQ(result.Domain, Domain::Graph);
    EXPECT_EQ(result.Kind, RefinedPrimitiveKind::Vertex); // graph node -> Vertex.
    EXPECT_EQ(result.VertexId, 1u);
}

// ---- Cross-cutting ---------------------------------------------------------

TEST(PrimitiveSelectionRefinement, StaleEntityIsRejectedBeforeGeometry)
{
    const MeshScratch mesh;
    PrimitiveRefineRequest request = BaseRequest();
    request.EntityIsLive = false;
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Face, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(mesh.View(), request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::StaleEntity);
    EXPECT_FALSE(result.Resolved());
    EXPECT_EQ(result.EntityId, 42u);
}

TEST(PrimitiveSelectionRefinement, NoneDomainIsUnsupported)
{
    ConstSourceView view{};
    view.ActiveDomain = Domain::None;
    PrimitiveRefineRequest request = BaseRequest();
    request.Hint = EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0u);

    const PrimitiveSelectionResult result = RefinePrimitiveSelection(view, request);
    EXPECT_EQ(result.Status, PrimitiveRefineStatus::UnsupportedDomain);
}
