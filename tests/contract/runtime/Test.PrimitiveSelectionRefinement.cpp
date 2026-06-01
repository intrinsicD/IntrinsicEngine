#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.SelectionSystem;
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
