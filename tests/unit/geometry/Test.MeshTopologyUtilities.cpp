#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeTwoComponentMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a0 = mesh.AddVertex({0.0F, 0.0F, 0.0F});
        const auto a1 = mesh.AddVertex({1.0F, 0.0F, 0.0F});
        const auto a2 = mesh.AddVertex({0.0F, 1.0F, 0.0F});
        const auto b0 = mesh.AddVertex({10.0F, 0.0F, 0.0F});
        const auto b1 = mesh.AddVertex({11.0F, 0.0F, 0.0F});
        const auto b2 = mesh.AddVertex({10.0F, 1.0F, 0.0F});
        (void)mesh.AddTriangle(a0, a1, a2);
        (void)mesh.AddTriangle(b0, b1, b2);
        return mesh;
    }
}

TEST(MeshTopologyUtilities, TriangulateFansPolygonFace)
{
    auto mesh = MakeSingleQuad();
    const auto triangles = mesh.Triangulate(Geometry::FaceHandle{0u});
    ASSERT_TRUE(triangles.has_value());
    EXPECT_EQ(*triangles, 2u);
    EXPECT_EQ(mesh.FaceCount(), 2u);

    for (const Geometry::FaceHandle face : mesh.LiveFaces())
    {
        EXPECT_EQ(mesh.Valence(face), 3u);
    }
}

TEST(MeshTopologyUtilities, ConnectedComponentsPublishLabelsAndSplitMeshes)
{
    auto mesh = MakeTwoComponentMesh();
    const auto components = Geometry::MeshRepair::ComputeConnectedComponents(mesh);
    ASSERT_TRUE(components.has_value());
    EXPECT_EQ(components->ComponentCount, 2u);

    const auto vertexLabels = mesh.VertexProperties().Get<std::uint32_t>("v:component");
    const auto faceLabels = mesh.FaceProperties().Get<std::uint32_t>("f:component");
    ASSERT_TRUE(vertexLabels.IsValid());
    ASSERT_TRUE(faceLabels.IsValid());
    EXPECT_EQ(vertexLabels.Vector().size(), mesh.VerticesSize());
    EXPECT_EQ(faceLabels.Vector().size(), mesh.FacesSize());

    const auto split = Geometry::MeshRepair::SplitIntoComponents(mesh);
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->size(), 2u);
    EXPECT_EQ((*split)[0].FaceCount(), 1u);
    EXPECT_EQ((*split)[1].FaceCount(), 1u);
}

TEST(MeshTopologyUtilities, TriangleAdjacencyAndNearestFaceAreDeterministic)
{
    const auto mesh = MakeTwoTriangleSquare();
    const auto adjacency = Geometry::MeshUtils::BuildTriangleAdjacencyIndices(mesh);
    ASSERT_TRUE(adjacency.has_value());
    ASSERT_EQ(adjacency->size(), 12u);

    const auto nearest = Geometry::MeshUtils::NearestFace(mesh, {0.2F, 0.2F, 1.0F});
    EXPECT_TRUE(nearest.Found);
    EXPECT_TRUE(nearest.Face.IsValid());
    EXPECT_NEAR(nearest.SquaredDistance, 1.0F, 1.0e-5F);
    EXPECT_NEAR(nearest.Point.z, 0.0F, 1.0e-6F);
}

TEST(MeshTopologyUtilities, UpdateEdgeLengthsPublishesCanonicalCache)
{
    auto mesh = MakeTwoTriangleSquare();
    const auto lengths = mesh.UpdateEdgeLengths();
    ASSERT_TRUE(lengths.IsValid());
    EXPECT_EQ(lengths.Vector().size(), mesh.EdgesSize());

    for (const Geometry::EdgeHandle edge : mesh.LiveEdges())
    {
        EXPECT_NEAR(lengths[edge], mesh.EdgeLength(edge), 1.0e-12);
    }

    const auto cached = mesh.EdgeProperties().Get<double>("e:length");
    ASSERT_TRUE(cached.IsValid());
    EXPECT_EQ(cached.Vector().size(), mesh.EdgesSize());
}
