// tests/Test_Geodesic.cpp — Heat method geodesic distance tests.
// Covers: convergence, source vertex distance, symmetry, degenerate input,
// and multi-source support.

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

namespace
{
    // Subdivided icosahedron approximating a unit sphere.
    Geometry::Halfedge::Mesh MakeSphereMesh()
    {
        auto ico = MakeIcosahedron();
        Geometry::Halfedge::Mesh refined;
        Geometry::Subdivision::SubdivisionParams sp;
        sp.Iterations = 2;
        Geometry::Subdivision::Subdivide(ico, refined, sp);
        return refined;
    }
}

TEST(Geodesic, SourceVertexHasZeroDistance)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);

    // Source vertex distance should be 0
    Geometry::VertexHandle v0{0};
    double d = result->DistanceProperty.Get(v0);
    EXPECT_NEAR(d, 0.0, 1e-6);
}

TEST(Geodesic, AllDistancesNonNegative)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsDeleted(v))
            EXPECT_GE(result->DistanceProperty.Get(v), 0.0);
    }
}

TEST(Geodesic, DistanceIncreasesWithTopologicalDistance)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);

    // Source has distance 0, all other vertices are 1-ring neighbors
    // and should have positive distance
    Geometry::VertexHandle v0{0};
    double d0 = result->DistanceProperty.Get(v0);
    EXPECT_NEAR(d0, 0.0, 1e-6);

    for (std::size_t i = 1; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsDeleted(v))
            EXPECT_GT(result->DistanceProperty.Get(v), d0);
    }
}

TEST(Geodesic, MultipleSourceVertices)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0, 1};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);

    // Both source vertices should have distance 0
    for (auto src : sources)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(src)};
        EXPECT_NEAR(result->DistanceProperty.Get(v), 0.0, 1e-6);
    }
}

TEST(Geodesic, ReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    EXPECT_FALSE(result.has_value());
}

TEST(Geodesic, ReturnsNulloptForEmptySources)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources;

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    EXPECT_FALSE(result.has_value());
}

TEST(Geodesic, SolverIterationsReported)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->HeatSolveIterations, 0u);
    EXPECT_GT(result->PoissonSolveIterations, 0u);
}

TEST(Geodesic, CustomTimeStep)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};

    Geometry::Geodesic::GeodesicParams params;
    params.TimeStep = 0.1; // Explicit time step

    auto result = Geometry::Geodesic::ComputeDistance(mesh, sources, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
}
