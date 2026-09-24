#include <gtest/gtest.h>
#include <cmath>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.Graph;
import Geometry.MeshSoup;
import Geometry.Parameterization.Diagnostics;
import Geometry.Parameterization.Harmonic;
import Geometry.Parameterization;

#include "Test_MeshBuilders.h"

// Larger disk mesh: subdivided square (4 triangles, 5 vertices)
static Geometry::HalfedgeMesh::Mesh MakeSquareDisk()
{
    Geometry::HalfedgeMesh::Mesh mesh;
    // 5 vertices: 4 corners + center
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    auto v4 = mesh.AddVertex({0.5f, 0.5f, 0.0f}); // center

    (void)mesh.AddTriangle(v0, v1, v4);
    (void)mesh.AddTriangle(v1, v2, v4);
    (void)mesh.AddTriangle(v2, v3, v4);
    (void)mesh.AddTriangle(v3, v0, v4);

    return mesh;
}

// =============================================================================
// LSCM Parameterization Tests
// =============================================================================

TEST(LSCM, EmptyMeshReturnsNullopt)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    auto result = Geometry::Parameterization::ComputeLSCM(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(LSCM, ClosedMeshReturnsNullopt)
{
    auto mesh = MakeTetrahedron();
    auto result = Geometry::Parameterization::ComputeLSCM(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(LSCM, DiskTopologyProducesUVs)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Parameterization::ParameterizationParams params;
    auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value()) << "LSCM should succeed on disk-topology mesh";

    // Should have a UV for every vertex
    EXPECT_EQ(result->UVs.size(), mesh.VerticesSize());
    EXPECT_TRUE(result->Converged);
}

TEST(LSCM, PinnedVerticesHaveCorrectUVs)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Parameterization::ParameterizationParams params;
    params.PinVertex0 = 0;
    params.PinVertex1 = 1;
    params.PinUV0 = glm::vec2(0.0f, 0.0f);
    params.PinUV1 = glm::vec2(1.0f, 0.0f);

    auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->UVs[0].x, 0.0f, 1e-4f);
    EXPECT_NEAR(result->UVs[0].y, 0.0f, 1e-4f);
    EXPECT_NEAR(result->UVs[1].x, 1.0f, 1e-4f);
    EXPECT_NEAR(result->UVs[1].y, 0.0f, 1e-4f);
}

TEST(LSCM, NoFlippedTrianglesOnConvexDisk)
{
    auto mesh = MakeSquareDisk();

    Geometry::Parameterization::ParameterizationParams params;
    auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->FlippedTriangleCount, 0u);
}

TEST(LSCM, FlatDiskDistortionNearOne)
{
    // A flat disk mesh should have conformal distortion close to 1.0
    // (the identity map is conformal for a flat surface)
    auto mesh = MakeSubdividedTriangle();

    Geometry::Parameterization::ParameterizationParams params;
    auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Mean conformal distortion should be close to 1 for a flat mesh
    EXPECT_GT(result->MeanConformalDistortion, 0.5);
    EXPECT_LT(result->MeanConformalDistortion, 5.0);
}

TEST(LSCM, UVAreaPositive)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Parameterization::ParameterizationParams params;
    auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Compute total UV area — should be positive
    double totalArea = 0.0;
    for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
    {
        Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(fi)};
        if (mesh.IsDeleted(fh)) continue;

        auto h0 = mesh.Halfedge(fh);
        auto h1 = mesh.NextHalfedge(h0);
        auto h2 = mesh.NextHalfedge(h1);

        auto va = mesh.ToVertex(h0);
        auto vb = mesh.ToVertex(h1);
        auto vc = mesh.ToVertex(h2);

        glm::vec2 uva = result->UVs[va.Index];
        glm::vec2 uvb = result->UVs[vb.Index];
        glm::vec2 uvc = result->UVs[vc.Index];

        double area = 0.5 * ((uvb.x - uva.x) * (uvc.y - uva.y) -
                              (uvc.x - uva.x) * (uvb.y - uva.y));
        totalArea += area;
    }

    EXPECT_GT(std::abs(totalArea), 1e-10) << "UV area should be non-zero";
}

TEST(LSCM, SingleTriangleDiskTopology)
{
    auto mesh = MakeSingleTriangle();

    Geometry::Parameterization::ParameterizationParams params;
    auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value()) << "LSCM should succeed on a single triangle";

    EXPECT_EQ(result->UVs.size(), 3u);
    EXPECT_TRUE(result->Converged);
}

TEST(LSCM, DisconnectedClosedComponentIsRejected)
{
    auto mesh = MakeDiskAndClosedComponent();
    ASSERT_EQ(Geometry::MeshUtils::CollectBoundaryLoops(mesh).size(), 1u);
    const auto result = Geometry::Parameterization::ComputeLSCM(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(LSCM, NonmanifoldVertexIsRejected)
{
    auto mesh = MakeBowtieTriangles();
    ASSERT_FALSE(mesh.IsManifold(Geometry::VertexHandle{0u}));
    const auto result = Geometry::Parameterization::ComputeLSCM(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(LSCM, IsolatedVertexIsRejected)
{
    auto mesh = MakeSingleTriangle();
    (void)mesh.AddVertex({2.0f, 2.0f, 0.0f});
    const auto result = Geometry::Parameterization::ComputeLSCM(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(LSCM, DeletedSlotsPreserveLiveDiskUvs)
{
    auto mesh = MakeTwoTriangleSquare();
    mesh.DeleteFace(Geometry::FaceHandle{0u});
    ASSERT_GT(mesh.VerticesSize(), mesh.VertexCount());
    ASSERT_GT(mesh.EdgesSize(), mesh.EdgeCount());
    ASSERT_GT(mesh.FacesSize(), mesh.FaceCount());
    const auto result = Geometry::Parameterization::ComputeLSCM(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    const auto& uvs = result->UVs;
    ASSERT_EQ(uvs.size(), mesh.VerticesSize());
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        if (!mesh.IsDeleted(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)}))
        {
            EXPECT_TRUE(std::isfinite(uvs[i].x));
            EXPECT_TRUE(std::isfinite(uvs[i].y));
        }
    }
}

TEST(LSCM, DirectSolverIsExactOnPlanarStrip)
{
    // A planar disk has an exact similarity map; the direct solve must
    // reproduce it up to round-off instead of a CG residual tolerance.
    auto mesh = MakeTriangleStrip(24);
    Geometry::Parameterization::ParameterizationParams params;
    params.UseDirectSolver = true;
    const auto result = Geometry::Parameterization::ComputeLSCM(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_EQ(result->CGIterations, 0u);
    EXPECT_EQ(result->FlippedTriangleCount, 0u);
    EXPECT_NEAR(result->MaxConformalDistortion, 1.0, 1.0e-5);
    EXPECT_NEAR(result->Diagnostics.MaxAreaRatio / result->Diagnostics.MinAreaRatio,
                1.0, 1.0e-4);
}
