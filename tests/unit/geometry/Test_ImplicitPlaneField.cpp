#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    Geometry::Halfedge::Mesh MakeQuadMesh()
    {
        Geometry::Halfedge::Mesh mesh;
        const auto v0 = mesh.AddVertex(glm::vec3(-1.0f, -1.0f, 0.0f));
        const auto v1 = mesh.AddVertex(glm::vec3( 1.0f, -1.0f, 0.0f));
        const auto v2 = mesh.AddVertex(glm::vec3( 1.0f,  1.0f, 0.0f));
        const auto v3 = mesh.AddVertex(glm::vec3(-1.0f,  1.0f, 0.0f));
        EXPECT_TRUE(mesh.AddQuad(v0, v1, v2, v3).has_value());
        return mesh;
    }
}

TEST(ImplicitPlaneField, BuildFromQuadProducesActiveLeavesAndSignedEvaluation)
{
    Geometry::Halfedge::Mesh mesh = MakeQuadMesh();

    Geometry::Implicit::BuildParams params;
    params.MaxDepth = 5;
    params.MinDepth = 1;
    params.BoundingBoxPadding = 0.2f;
    params.NarrowBandFactor = 4.0f;
    params.MaxPlaneErrorFraction = 0.05f;

    auto built = Geometry::Implicit::BuildPlaneField(mesh, params);
    ASSERT_TRUE(built.has_value());

    EXPECT_GT(built->Stats.NodeCount, 0u);
    EXPECT_GT(built->Stats.ActiveLeafCount, 0u);

    const auto onPlane = built->Field.Evaluate(glm::vec3(0.0f, 0.0f, 0.0f));
    const auto above = built->Field.Evaluate(glm::vec3(0.0f, 0.0f, 0.25f));
    const auto below = built->Field.Evaluate(glm::vec3(0.0f, 0.0f, -0.25f));

    ASSERT_TRUE(onPlane.has_value());
    ASSERT_TRUE(above.has_value());
    ASSERT_TRUE(below.has_value());

    EXPECT_NEAR(*onPlane, 0.0f, 2.5e-2f);
    EXPECT_GT(*above, 0.0f);
    EXPECT_LT(*below, 0.0f);
}

TEST(ImplicitPlaneField, ProjectSnapsToSurface)
{
    Geometry::Halfedge::Mesh mesh = MakeQuadMesh();

    Geometry::Implicit::BuildParams params;
    params.MaxDepth = 5;
    params.MinDepth = 1;
    params.BoundingBoxPadding = 0.2f;
    params.NarrowBandFactor = 4.0f;

    auto built = Geometry::Implicit::BuildPlaneField(mesh, params);
    ASSERT_TRUE(built.has_value());

    const glm::vec3 offSurface(0.3f, 0.3f, 0.5f);
    const auto projected = built->Field.Project(offSurface, 8, 1.0e-4f);
    ASSERT_TRUE(projected.has_value());

    const auto evalAtProjected = built->Field.Evaluate(*projected);
    ASSERT_TRUE(evalAtProjected.has_value());
    EXPECT_NEAR(*evalAtProjected, 0.0f, 1.0e-3f);
}

TEST(ImplicitPlaneField, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh empty;
    auto result = Geometry::Implicit::BuildPlaneField(empty);
    EXPECT_FALSE(result.has_value());
}

TEST(ImplicitPlaneField, CanSampleToDenseGrid)
{
    Geometry::Halfedge::Mesh mesh = MakeQuadMesh();

    Geometry::Implicit::BuildParams params;
    params.MaxDepth = 4;
    params.MinDepth = 1;
    params.BoundingBoxPadding = 0.2f;
    params.NarrowBandFactor = 4.0f;

    auto built = Geometry::Implicit::BuildPlaneField(mesh, params);
    ASSERT_TRUE(built.has_value());

    Geometry::Grid::GridDimensions dims;
    dims.NX = 8;
    dims.NY = 8;
    dims.NZ = 8;
    dims.Origin = glm::vec3(-1.5f, -1.5f, -1.0f);
    dims.Spacing = glm::vec3(3.0f / 8.0f, 3.0f / 8.0f, 2.0f / 8.0f);

    auto grid = Geometry::Implicit::SampleToDenseGrid(built->Field, dims);
    ASSERT_TRUE(grid.has_value());
    ASSERT_TRUE(grid->HasProperty("scalar"));

    auto scalar = grid->GetProperty<float>("scalar");
    ASSERT_TRUE(static_cast<bool>(scalar));

    const float centerValue = grid->At(scalar, 4u, 4u, 4u);
    EXPECT_TRUE(std::isfinite(centerValue));
}

TEST(ImplicitPlaneField, ExtractMeshProducesGeometry)
{
    Geometry::Halfedge::Mesh mesh = MakeQuadMesh();

    Geometry::Implicit::BuildParams params;
    params.MaxDepth = 4;
    params.MinDepth = 1;
    params.BoundingBoxPadding = 0.2f;
    params.NarrowBandFactor = 4.0f;

    auto built = Geometry::Implicit::BuildPlaneField(mesh, params);
    ASSERT_TRUE(built.has_value());

    Geometry::Grid::GridDimensions dims;
    dims.NX = 16;
    dims.NY = 16;
    dims.NZ = 16;
    dims.Origin = glm::vec3(-1.5f, -1.5f, -0.5f);
    dims.Spacing = glm::vec3(3.0f / 16.0f, 3.0f / 16.0f, 1.0f / 16.0f);

    auto extracted = Geometry::Implicit::ExtractMesh(built->Field, dims);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_GT(extracted->VertexCount(), 0u);
    EXPECT_GT(extracted->FaceCount(), 0u);
}
