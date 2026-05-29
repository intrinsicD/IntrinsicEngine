#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.GraphGeometryPacker;
import Geometry.Properties;

using Extrinsic::ECS::Components::GeometrySources::ConstSourceView;
using Extrinsic::ECS::Components::GeometrySources::Domain;
using Extrinsic::ECS::Components::GeometrySources::Edges;
using Extrinsic::ECS::Components::GeometrySources::Nodes;
using Extrinsic::Runtime::GraphPackBuffer;
using Extrinsic::Runtime::GraphPackResult;
using Extrinsic::Runtime::GraphPackStatus;
using Extrinsic::Runtime::GraphVertex;
using Extrinsic::Runtime::PackGraph;

namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    struct GraphScratch
    {
        Nodes NodeSource{};
        Edges EdgeSource{};

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::Graph;
            view.NodeSource = &NodeSource;
            view.EdgeSource = &EdgeSource;
            return view;
        }
    };

    void SetNodePositions(Nodes& n, const std::vector<glm::vec3>& positions)
    {
        n.Properties.Resize(positions.size());
        auto pos = n.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetEdges(Edges& e,
                  const std::vector<std::uint32_t>& v0,
                  const std::vector<std::uint32_t>& v1)
    {
        e.Properties.Resize(v0.size());
        auto p0 = e.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV0}, 0u);
        auto p1 = e.Properties.GetOrAdd<std::uint32_t>(std::string{pn::kEdgeV1}, 0u);
        p0.Vector() = v0;
        p1.Vector() = v1;
    }

    // Two nodes joined by one edge.
    GraphScratch BuildTwoNodeOneEdge()
    {
        GraphScratch g{};
        SetNodePositions(g.NodeSource, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
        });
        SetEdges(g.EdgeSource, {0u}, {1u});
        return g;
    }

    [[nodiscard]] GraphVertex ReadVertex(const GraphPackBuffer& buffer, std::size_t i)
    {
        GraphVertex v{};
        std::memcpy(&v, buffer.VertexBytes.data() + i * sizeof(GraphVertex), sizeof(GraphVertex));
        return v;
    }
}

TEST(GraphGeometryPacker, PointAndLineLanesPackNodesAndEdges)
{
    const GraphScratch g = BuildTwoNodeOneEdge();
    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), /*wantLines=*/true, /*wantPoints=*/true, buffer);

    ASSERT_EQ(result.Status, GraphPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 2u);
    EXPECT_TRUE(result.Upload->SurfaceIndices.empty());
    ASSERT_EQ(result.Upload->LineIndices.size(), 2u);
    EXPECT_EQ(result.Upload->LineIndices[0], 0u);
    EXPECT_EQ(result.Upload->LineIndices[1], 1u);

    const GraphVertex v0 = ReadVertex(buffer, 0);
    const GraphVertex v1 = ReadVertex(buffer, 1);
    EXPECT_FLOAT_EQ(v0.Px, 0.0f);
    EXPECT_FLOAT_EQ(v1.Px, 1.0f);
}

TEST(GraphGeometryPacker, PointOnlyLaneProducesNoLineIndices)
{
    const GraphScratch g = BuildTwoNodeOneEdge();
    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), /*wantLines=*/false, /*wantPoints=*/true, buffer);

    ASSERT_EQ(result.Status, GraphPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 2u);
    EXPECT_TRUE(result.Upload->LineIndices.empty());
}

TEST(GraphGeometryPacker, LineOnlyLanePacksEdges)
{
    const GraphScratch g = BuildTwoNodeOneEdge();
    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), /*wantLines=*/true, /*wantPoints=*/false, buffer);

    ASSERT_EQ(result.Status, GraphPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 2u);
    ASSERT_EQ(result.Upload->LineIndices.size(), 2u);
}

TEST(GraphGeometryPacker, EmptyEdgeSetIsValidForLineLane)
{
    GraphScratch g{};
    SetNodePositions(g.NodeSource, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    // EdgeSource has the property slots but zero rows.
    SetEdges(g.EdgeSource, {}, {});

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), /*wantLines=*/true, /*wantPoints=*/true, buffer);

    ASSERT_EQ(result.Status, GraphPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 2u);
    EXPECT_TRUE(result.Upload->LineIndices.empty());
}

TEST(GraphGeometryPacker, WrongDomainFailsClosed)
{
    GraphScratch g = BuildTwoNodeOneEdge();
    ConstSourceView view = g.View();
    view.ActiveDomain = Domain::Mesh;

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(view, true, true, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::WrongDomain);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(GraphGeometryPacker, NoRenderLaneFailsClosed)
{
    const GraphScratch g = BuildTwoNodeOneEdge();
    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), /*wantLines=*/false, /*wantPoints=*/false, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::NoRenderLane);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(GraphGeometryPacker, MissingNodePositionsFailsClosed)
{
    GraphScratch g{};
    // NodeSource present but no `v:position` slot.
    SetEdges(g.EdgeSource, {0u}, {0u});

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), true, true, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::MissingNodes);
}

TEST(GraphGeometryPacker, EmptyGraphFailsClosed)
{
    GraphScratch g{};
    SetNodePositions(g.NodeSource, {});
    SetEdges(g.EdgeSource, {}, {});

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), true, true, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::EmptyGraph);
}

TEST(GraphGeometryPacker, MissingEdgeTopologyFailsClosedForLineLane)
{
    GraphScratch g{};
    SetNodePositions(g.NodeSource, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    // EdgeSource present but no `e:v0`/`e:v1` slots.

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), /*wantLines=*/true, /*wantPoints=*/false, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::MissingEdgeTopology);
}

TEST(GraphGeometryPacker, OutOfRangeEdgeEndpointFailsClosed)
{
    GraphScratch g{};
    SetNodePositions(g.NodeSource, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    SetEdges(g.EdgeSource, {0u}, {7u}); // endpoint 7 out of range (2 nodes).

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), true, true, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::InvalidEdge);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(GraphGeometryPacker, NonFiniteNodePositionFailsClosed)
{
    GraphScratch g{};
    SetNodePositions(g.NodeSource, {
        {0.0f, 0.0f, 0.0f},
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
    });
    SetEdges(g.EdgeSource, {0u}, {1u});

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), true, true, buffer);
    EXPECT_EQ(result.Status, GraphPackStatus::NonFinitePosition);
}

TEST(GraphGeometryPacker, LocalSphereCoversNodeBounds)
{
    GraphScratch g{};
    SetNodePositions(g.NodeSource, {{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    SetEdges(g.EdgeSource, {0u}, {1u});

    GraphPackBuffer buffer{};
    const GraphPackResult result = PackGraph(g.View(), true, true, buffer);
    ASSERT_EQ(result.Status, GraphPackStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    const glm::vec4 sphere = result.Upload->LocalBounds.LocalSphere;
    EXPECT_FLOAT_EQ(sphere.x, 0.0f);
    EXPECT_FLOAT_EQ(sphere.w, 1.0f);
}


