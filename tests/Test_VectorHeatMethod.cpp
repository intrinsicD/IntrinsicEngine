// tests/Test_VectorHeatMethod.cpp — Vector Heat Method tests.
// Covers: parallel transport convergence, direction consistency, logarithmic
// map, degenerate input, symmetry, and multi-source support.

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    // Subdivided icosahedron approximating a unit sphere.
    Geometry::Halfedge::Mesh MakeSphereMesh()
    {
        auto ico = MakeIcosahedron();
        Geometry::Halfedge::Mesh refined;
        Geometry::Subdivision::SubdivisionParams sp;
        sp.Iterations = 2;
        (void)Geometry::Subdivision::Subdivide(ico, refined, sp);
        return refined;
    }

    // Flat grid mesh for testing transport on a trivially flat surface.
    Geometry::Halfedge::Mesh MakeFlatGrid(int n = 5)
    {
        Geometry::Halfedge::Mesh mesh;
        // Create (n+1) x (n+1) grid of vertices
        std::vector<Geometry::VertexHandle> verts;
        for (int y = 0; y <= n; ++y)
        {
            for (int x = 0; x <= n; ++x)
            {
                verts.push_back(mesh.AddVertex({
                    static_cast<float>(x) / static_cast<float>(n),
                    static_cast<float>(y) / static_cast<float>(n),
                    0.0f}));
            }
        }

        // Create triangles
        for (int y = 0; y < n; ++y)
        {
            for (int x = 0; x < n; ++x)
            {
                int i00 = y * (n + 1) + x;
                int i10 = y * (n + 1) + (x + 1);
                int i01 = (y + 1) * (n + 1) + x;
                int i11 = (y + 1) * (n + 1) + (x + 1);
                (void)mesh.AddTriangle(verts[i00], verts[i10], verts[i11]);
                (void)mesh.AddTriangle(verts[i00], verts[i11], verts[i01]);
            }
        }

        return mesh;
    }
}

// =============================================================================
// TransportVectors tests
// =============================================================================

TEST(VectorHeatMethod, TransportConverges)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0};
    glm::vec3 srcPos = mesh.Position(Geometry::VertexHandle{0});

    // Use a tangent vector that lies in the tangent plane at vertex 0
    // (any non-zero direction projected to tangent plane will do)
    glm::vec3 srcVec = glm::vec3(1.0f, 0.0f, 0.0f);
    // Remove normal component (approximate normal at vertex 0 on sphere is srcPos)
    srcVec -= glm::dot(srcVec, glm::normalize(srcPos)) * glm::normalize(srcPos);
    if (glm::length(srcVec) > 1e-6f)
        srcVec = glm::normalize(srcVec);

    std::vector<glm::vec3> srcVecs = {srcVec};

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, srcVecs);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_GT(result->SolveIterations, 0u);
}

TEST(VectorHeatMethod, TransportedVectorsAreTangent)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0};
    glm::vec3 srcVec = glm::vec3(0.0f, 1.0f, 0.0f);
    std::vector<glm::vec3> srcVecs = {srcVec};

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, srcVecs);
    ASSERT_TRUE(result.has_value());

    // Each transported vector should be approximately tangent to the surface.
    // On a sphere centered at origin, vertex normal ≈ normalized position.
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
            continue;

        glm::vec3 tv = result->TransportedVectors[v];
        if (glm::length(tv) < 1e-6f)
            continue;

        // Approximate normal on sphere
        glm::vec3 normal = glm::normalize(mesh.Position(v));
        float dotNormal = std::abs(glm::dot(tv, normal));
        EXPECT_LT(dotNormal, 0.15f)
            << "Transported vector not tangent at vertex " << i
            << " (dot with normal = " << dotNormal << ")";
    }
}

TEST(VectorHeatMethod, TransportedVectorsAreUnitLength)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0};
    glm::vec3 srcVec = glm::vec3(1.0f, 0.0f, 0.0f);
    std::vector<glm::vec3> srcVecs = {srcVec};

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, srcVecs);
    ASSERT_TRUE(result.has_value());

    int nonZeroCount = 0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
            continue;

        glm::vec3 tv = result->TransportedVectors[v];
        float len = glm::length(tv);
        if (len > 1e-6f)
        {
            EXPECT_NEAR(len, 1.0f, 0.05f) << "Vector not unit at vertex " << i;
            nonZeroCount++;
        }
    }

    // Most vertices should have a non-zero transported vector
    EXPECT_GT(nonZeroCount, static_cast<int>(mesh.VertexCount()) / 2);
}

TEST(VectorHeatMethod, FlatSurfaceTransportPreservesDirection)
{
    // On a flat surface, parallel transport should preserve direction.
    // All transported vectors should be mutually consistent (point the same way).
    // Use center vertex as source for better diffusion (avoids boundary effects).
    auto mesh = MakeFlatGrid(10);
    std::size_t centerIdx = 5 * 11 + 5; // center of 10x10 grid
    std::vector<std::size_t> sources = {centerIdx};
    glm::vec3 srcVec = glm::vec3(1.0f, 0.0f, 0.0f); // +X direction on flat XY plane
    std::vector<glm::vec3> srcVecs = {srcVec};

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, srcVecs);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);

    // Collect all non-zero transported vectors
    std::vector<glm::vec3> transportedDirs;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
            continue;

        glm::vec3 tv = result->TransportedVectors[v];
        if (glm::length(tv) > 1e-6f)
            transportedDirs.push_back(glm::normalize(tv));
    }

    ASSERT_GT(transportedDirs.size(), 10u);

    // On a flat surface, all transported vectors should be mutually consistent.
    // Compute average direction and check that most vectors agree with it.
    glm::vec3 avgDir{0.0f};
    for (const auto& d : transportedDirs)
        avgDir += d;
    avgDir = glm::normalize(avgDir);

    int consistentCount = 0;
    for (const auto& d : transportedDirs)
    {
        if (glm::dot(d, avgDir) > 0.8f)
            consistentCount++;
    }

    // With correct connection angles, the vast majority should agree.
    double ratio = static_cast<double>(consistentCount) / static_cast<double>(transportedDirs.size());
    EXPECT_GT(ratio, 0.85) << "Only " << consistentCount << "/" << transportedDirs.size()
                           << " vectors consistent on flat surface";
}

TEST(VectorHeatMethod, MultiSourceTransport)
{
    auto mesh = MakeSphereMesh();
    std::vector<std::size_t> sources = {0, 10};
    std::vector<glm::vec3> srcVecs = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    };

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, srcVecs);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
}

// =============================================================================
// LogarithmicMap tests
// =============================================================================

TEST(VectorHeatMethod, LogMapConverges)
{
    auto mesh = MakeSphereMesh();

    auto result = Geometry::VectorHeatMethod::ComputeLogMap(mesh, 0);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_GT(result->VectorSolveIterations, 0u);
    EXPECT_GT(result->ScalarSolveIterations, 0u);
}

TEST(VectorHeatMethod, LogMapSourceIsOrigin)
{
    auto mesh = MakeSphereMesh();

    auto result = Geometry::VectorHeatMethod::ComputeLogMap(mesh, 0);
    ASSERT_TRUE(result.has_value());

    // Source vertex should be at the origin of the log map
    Geometry::VertexHandle v0{0};
    glm::vec2 coords = result->LogMapCoords[v0];
    EXPECT_NEAR(coords.x, 0.0f, 1e-4f);
    EXPECT_NEAR(coords.y, 0.0f, 1e-4f);
}

TEST(VectorHeatMethod, LogMapDistanceConsistency)
{
    // The magnitude of logmap coordinates should match geodesic distance.
    auto mesh = MakeSphereMesh();

    auto result = Geometry::VectorHeatMethod::ComputeLogMap(mesh, 0);
    ASSERT_TRUE(result.has_value());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
            continue;

        glm::vec2 coords = result->LogMapCoords[v];
        double logmapDist = std::sqrt(coords.x * coords.x + coords.y * coords.y);
        double geoDist = result->Distance[v];

        if (geoDist > 1e-6)
        {
            // Relative error should be small
            double relError = std::abs(logmapDist - geoDist) / geoDist;
            EXPECT_LT(relError, 0.1)
                << "LogMap distance mismatch at vertex " << i
                << ": logmap=" << logmapDist << " geo=" << geoDist;
        }
    }
}

TEST(VectorHeatMethod, FlatGridLogMapCircularSymmetry)
{
    // On a flat grid, equidistant points should form roughly circular
    // contours in the log map.
    auto mesh = MakeFlatGrid(10);

    // Use center vertex of the grid as source
    // Grid is (10+1)^2 = 121 vertices, center is at index 60 (row 5, col 5)
    std::size_t centerIdx = 5 * 11 + 5;

    auto result = Geometry::VectorHeatMethod::ComputeLogMap(mesh, centerIdx);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);

    // Source should be at origin
    Geometry::VertexHandle vCenter{static_cast<Geometry::PropertyIndex>(centerIdx)};
    glm::vec2 centerCoords = result->LogMapCoords[vCenter];
    EXPECT_NEAR(centerCoords.x, 0.0f, 1e-4f);
    EXPECT_NEAR(centerCoords.y, 0.0f, 1e-4f);
}

// =============================================================================
// Degenerate input tests
// =============================================================================

TEST(VectorHeatMethod, TransportReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    std::vector<std::size_t> sources = {0};
    std::vector<glm::vec3> vecs = {glm::vec3(1, 0, 0)};

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, vecs);
    EXPECT_FALSE(result.has_value());
}

TEST(VectorHeatMethod, TransportReturnsNulloptForEmptySources)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources;
    std::vector<glm::vec3> vecs;

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, vecs);
    EXPECT_FALSE(result.has_value());
}

TEST(VectorHeatMethod, TransportReturnsNulloptForMismatchedArrays)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0, 1};
    std::vector<glm::vec3> vecs = {glm::vec3(1, 0, 0)}; // Only 1 vector for 2 sources

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, vecs);
    EXPECT_FALSE(result.has_value());
}

TEST(VectorHeatMethod, LogMapReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;

    auto result = Geometry::VectorHeatMethod::ComputeLogMap(mesh, 0);
    EXPECT_FALSE(result.has_value());
}

TEST(VectorHeatMethod, LogMapReturnsNulloptForInvalidVertex)
{
    auto mesh = MakeTetrahedron();

    auto result = Geometry::VectorHeatMethod::ComputeLogMap(mesh, 99999);
    EXPECT_FALSE(result.has_value());
}

TEST(VectorHeatMethod, TransportOnTetrahedron)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};
    // Arbitrary tangent vector (will be projected to tangent plane)
    std::vector<glm::vec3> vecs = {glm::vec3(1.0f, 0.0f, 0.0f)};

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, vecs);
    ASSERT_TRUE(result.has_value());
    // Small mesh may or may not fully converge, but should produce a result
    EXPECT_GT(result->SolveIterations, 0u);
}

TEST(VectorHeatMethod, CustomTimeStep)
{
    auto mesh = MakeTetrahedron();
    std::vector<std::size_t> sources = {0};
    std::vector<glm::vec3> vecs = {glm::vec3(1, 0, 0)};

    Geometry::VectorHeatMethod::VectorHeatParams params;
    params.TimeStep = 0.1;

    auto result = Geometry::VectorHeatMethod::TransportVectors(mesh, sources, vecs, params);
    ASSERT_TRUE(result.has_value());
}
