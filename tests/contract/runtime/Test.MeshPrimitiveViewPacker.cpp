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
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Geometry.Properties;

using Extrinsic::ECS::Components::GeometrySources::ConstSourceView;
using Extrinsic::ECS::Components::GeometrySources::Domain;
using Extrinsic::ECS::Components::GeometrySources::Edges;
using Extrinsic::ECS::Components::GeometrySources::Vertices;
using Extrinsic::Runtime::MeshPrimitiveVertex;
using Extrinsic::Runtime::MeshPrimitiveViewBuffer;
using Extrinsic::Runtime::MeshPrimitiveViewResult;
using Extrinsic::Runtime::MeshPrimitiveViewSettings;
using Extrinsic::Runtime::MeshPrimitiveViewStatus;
using Extrinsic::Runtime::PackMeshEdgeView;
using Extrinsic::Runtime::PackMeshVertexView;

namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    // Minimal mesh-domain scratch. A `Domain::Mesh` entity also owns Halfedges
    // and Faces, but the primitive-view packers read only `Vertices` (positions,
    // shared by both views) and `Edges` (line endpoints for the edge view), so
    // the scratch carries just those two PropertySets.
    struct MeshScratch
    {
        Vertices VertexSource{};
        Edges EdgeSource{};

        [[nodiscard]] ConstSourceView View() const noexcept
        {
            ConstSourceView view{};
            view.ActiveDomain = Domain::Mesh;
            view.VertexSource = &VertexSource;
            view.EdgeSource = &EdgeSource;
            return view;
        }
    };

    void SetPositions(Vertices& v, const std::vector<glm::vec3>& positions)
    {
        v.Properties.Resize(positions.size());
        auto pos = v.Properties.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
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

    // A single triangle: three vertices, three edges.
    MeshScratch BuildTriangle()
    {
        MeshScratch m{};
        SetPositions(m.VertexSource, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });
        SetEdges(m.EdgeSource, {0u, 1u, 2u}, {1u, 2u, 0u});
        return m;
    }

    [[nodiscard]] MeshPrimitiveVertex ReadVertex(const MeshPrimitiveViewBuffer& buffer, std::size_t i)
    {
        MeshPrimitiveVertex v{};
        std::memcpy(&v, buffer.VertexBytes.data() + i * sizeof(MeshPrimitiveVertex), sizeof(MeshPrimitiveVertex));
        return v;
    }
}

TEST(MeshPrimitiveViewSettingsTest, AnyEnabledReflectsFlags)
{
    EXPECT_FALSE((MeshPrimitiveViewSettings{}).AnyEnabled());
    EXPECT_TRUE((MeshPrimitiveViewSettings{.EnableEdgeView = true}).AnyEnabled());
    EXPECT_TRUE((MeshPrimitiveViewSettings{.EnableVertexView = true}).AnyEnabled());
    EXPECT_TRUE((MeshPrimitiveViewSettings{.EnableEdgeView = true, .EnableVertexView = true}).AnyEnabled());
}

TEST(MeshPrimitiveViewPacker, EdgeViewPacksVerticesAndLineIndices)
{
    const MeshScratch m = BuildTriangle();
    MeshPrimitiveViewBuffer buffer{};
    const MeshPrimitiveViewResult result = PackMeshEdgeView(m.View(), buffer);

    ASSERT_EQ(result.Status, MeshPrimitiveViewStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 3u);
    EXPECT_TRUE(result.Upload->SurfaceIndices.empty());
    ASSERT_EQ(result.Upload->LineIndices.size(), 6u);
    EXPECT_EQ(result.Upload->LineIndices[0], 0u);
    EXPECT_EQ(result.Upload->LineIndices[1], 1u);
    EXPECT_EQ(result.Upload->LineIndices[4], 2u);
    EXPECT_EQ(result.Upload->LineIndices[5], 0u);

    const MeshPrimitiveVertex v1 = ReadVertex(buffer, 1);
    EXPECT_FLOAT_EQ(v1.Px, 1.0f);
}

TEST(MeshPrimitiveViewPacker, VertexViewPacksPointsWithoutIndices)
{
    const MeshScratch m = BuildTriangle();
    MeshPrimitiveViewBuffer buffer{};
    const MeshPrimitiveViewResult result = PackMeshVertexView(m.View(), buffer);

    ASSERT_EQ(result.Status, MeshPrimitiveViewStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 3u);
    EXPECT_TRUE(result.Upload->SurfaceIndices.empty());
    EXPECT_TRUE(result.Upload->LineIndices.empty());

    const MeshPrimitiveVertex v2 = ReadVertex(buffer, 2);
    EXPECT_FLOAT_EQ(v2.Py, 1.0f);
}

TEST(MeshPrimitiveViewPacker, EdgeViewWithZeroEdgesIsValid)
{
    MeshScratch m{};
    SetPositions(m.VertexSource, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    // Edges PropertySet has the slots but zero rows (isolated vertices).
    SetEdges(m.EdgeSource, {}, {});

    MeshPrimitiveViewBuffer buffer{};
    const MeshPrimitiveViewResult result = PackMeshEdgeView(m.View(), buffer);

    ASSERT_EQ(result.Status, MeshPrimitiveViewStatus::Success);
    ASSERT_TRUE(result.Upload.has_value());
    EXPECT_EQ(result.Upload->VertexCount, 2u);
    EXPECT_TRUE(result.Upload->LineIndices.empty());
}

TEST(MeshPrimitiveViewPacker, WrongDomainFailsClosed)
{
    MeshScratch m = BuildTriangle();
    ConstSourceView view = m.View();
    view.ActiveDomain = Domain::Graph;

    MeshPrimitiveViewBuffer buffer{};
    EXPECT_EQ(PackMeshEdgeView(view, buffer).Status, MeshPrimitiveViewStatus::WrongDomain);
    EXPECT_EQ(PackMeshVertexView(view, buffer).Status, MeshPrimitiveViewStatus::WrongDomain);
}

TEST(MeshPrimitiveViewPacker, MissingPositionsFailsClosed)
{
    MeshScratch m{};
    // VertexSource present but no `v:position` slot.
    SetEdges(m.EdgeSource, {0u}, {0u});

    MeshPrimitiveViewBuffer buffer{};
    EXPECT_EQ(PackMeshEdgeView(m.View(), buffer).Status, MeshPrimitiveViewStatus::MissingPositions);
    EXPECT_EQ(PackMeshVertexView(m.View(), buffer).Status, MeshPrimitiveViewStatus::MissingPositions);
}

TEST(MeshPrimitiveViewPacker, EmptyMeshFailsClosed)
{
    MeshScratch m{};
    SetPositions(m.VertexSource, {});
    SetEdges(m.EdgeSource, {}, {});

    MeshPrimitiveViewBuffer buffer{};
    EXPECT_EQ(PackMeshEdgeView(m.View(), buffer).Status, MeshPrimitiveViewStatus::EmptyMesh);
    EXPECT_EQ(PackMeshVertexView(m.View(), buffer).Status, MeshPrimitiveViewStatus::EmptyMesh);
}

TEST(MeshPrimitiveViewPacker, MissingEdgeTopologyFailsClosedForEdgeView)
{
    MeshScratch m{};
    SetPositions(m.VertexSource, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    // EdgeSource present but no `e:v0`/`e:v1` slots.

    MeshPrimitiveViewBuffer buffer{};
    const MeshPrimitiveViewResult result = PackMeshEdgeView(m.View(), buffer);
    EXPECT_EQ(result.Status, MeshPrimitiveViewStatus::MissingEdgeTopology);
    EXPECT_FALSE(result.Upload.has_value());

    // The vertex view does not consult edges and still succeeds.
    EXPECT_EQ(PackMeshVertexView(m.View(), buffer).Status, MeshPrimitiveViewStatus::Success);
}

TEST(MeshPrimitiveViewPacker, OutOfRangeEdgeEndpointFailsClosed)
{
    MeshScratch m{};
    SetPositions(m.VertexSource, {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    SetEdges(m.EdgeSource, {0u}, {9u}); // endpoint 9 out of range (2 vertices).

    MeshPrimitiveViewBuffer buffer{};
    const MeshPrimitiveViewResult result = PackMeshEdgeView(m.View(), buffer);
    EXPECT_EQ(result.Status, MeshPrimitiveViewStatus::InvalidEdge);
    EXPECT_FALSE(result.Upload.has_value());
}

TEST(MeshPrimitiveViewPacker, NonFinitePositionFailsClosed)
{
    MeshScratch m{};
    SetPositions(m.VertexSource, {
        {0.0f, 0.0f, 0.0f},
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
    });
    SetEdges(m.EdgeSource, {0u}, {1u});

    MeshPrimitiveViewBuffer buffer{};
    EXPECT_EQ(PackMeshEdgeView(m.View(), buffer).Status, MeshPrimitiveViewStatus::NonFinitePosition);
    EXPECT_EQ(PackMeshVertexView(m.View(), buffer).Status, MeshPrimitiveViewStatus::NonFinitePosition);
}

TEST(MeshPrimitiveViewPacker, LocalSphereCoversVertexBounds)
{
    MeshScratch m{};
    SetPositions(m.VertexSource, {{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    SetEdges(m.EdgeSource, {0u}, {1u});

    MeshPrimitiveViewBuffer buffer{};
    const MeshPrimitiveViewResult edge = PackMeshEdgeView(m.View(), buffer);
    ASSERT_EQ(edge.Status, MeshPrimitiveViewStatus::Success);
    ASSERT_TRUE(edge.Upload.has_value());
    EXPECT_FLOAT_EQ(edge.Upload->LocalBounds.LocalSphere.x, 0.0f);
    EXPECT_FLOAT_EQ(edge.Upload->LocalBounds.LocalSphere.w, 1.0f);

    const MeshPrimitiveViewResult vertex = PackMeshVertexView(m.View(), buffer);
    ASSERT_EQ(vertex.Status, MeshPrimitiveViewStatus::Success);
    ASSERT_TRUE(vertex.Upload.has_value());
    EXPECT_FLOAT_EQ(vertex.Upload->LocalBounds.LocalSphere.w, 1.0f);
}
