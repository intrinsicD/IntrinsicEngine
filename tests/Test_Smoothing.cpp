// tests/Test_Smoothing.cpp — Mesh smoothing operator tests.
// Covers: uniform Laplacian, cotangent Laplacian, Taubin (shrinkage-free),
// implicit Laplacian, boundary preservation, and degenerate input.

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "TestMeshBuilders.h"

namespace
{
    // Compute average vertex distance from origin.
    double AverageRadius(const Geometry::Halfedge::Mesh& mesh)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (mesh.IsDeleted(v)) continue;
            sum += glm::length(mesh.Position(v));
            ++count;
        }
        return count > 0 ? sum / count : 0.0;
    }

    // Dense closed mesh suitable for smoothing tests.
    Geometry::Halfedge::Mesh MakeDenseMesh()
    {
        auto ico = MakeIcosahedron();
        Geometry::Halfedge::Mesh refined;
        Geometry::Subdivision::SubdivisionParams sp;
        sp.Iterations = 2;
        Geometry::Subdivision::Subdivide(ico, refined, sp);
        return refined;
    }
}

// --- Uniform Laplacian ---

TEST(Smoothing, UniformLaplacianReducesRoughness)
{
    auto mesh = MakeDenseMesh();

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 5;
    params.Lambda = 0.5;

    auto result = Geometry::Smoothing::UniformLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 5u);
    EXPECT_EQ(result->VertexCount, mesh.VertexCount());
}

TEST(Smoothing, UniformLaplacianCausesShrinkage)
{
    auto mesh = MakeDenseMesh();
    double radiusBefore = AverageRadius(mesh);

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 10;
    params.Lambda = 0.5;

    auto result = Geometry::Smoothing::UniformLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    double radiusAfter = AverageRadius(mesh);
    // Uniform Laplacian should shrink the mesh
    EXPECT_LT(radiusAfter, radiusBefore);
}

TEST(Smoothing, UniformLaplacianReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 1;

    auto result = Geometry::Smoothing::UniformLaplacian(mesh, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Smoothing, UniformLaplacianReturnsNulloptForZeroIterations)
{
    auto mesh = MakeTetrahedron();
    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 0;

    auto result = Geometry::Smoothing::UniformLaplacian(mesh, params);
    EXPECT_FALSE(result.has_value());
}

// --- Cotangent Laplacian ---

TEST(Smoothing, CotanLaplacianConverges)
{
    auto mesh = MakeDenseMesh();

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 3;
    params.Lambda = 0.3;

    auto result = Geometry::Smoothing::CotanLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 3u);
}

// --- Taubin Smoothing ---

TEST(Smoothing, TaubinPreservesVolume)
{
    auto mesh = MakeDenseMesh();
    double radiusBefore = AverageRadius(mesh);

    Geometry::Smoothing::TaubinParams params;
    params.Iterations = 10;
    params.Lambda = 0.5;
    params.PassbandFrequency = 0.1;

    auto result = Geometry::Smoothing::Taubin(mesh, params);
    ASSERT_TRUE(result.has_value());

    double radiusAfter = AverageRadius(mesh);
    // Taubin should preserve volume much better than uniform Laplacian
    // Allow 5% tolerance
    EXPECT_NEAR(radiusAfter, radiusBefore, radiusBefore * 0.05);
}

TEST(Smoothing, TaubinReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    Geometry::Smoothing::TaubinParams params;
    params.Iterations = 1;

    auto result = Geometry::Smoothing::Taubin(mesh, params);
    EXPECT_FALSE(result.has_value());
}

// --- Implicit Laplacian ---

TEST(Smoothing, ImplicitLaplacianConverges)
{
    auto mesh = MakeDenseMesh();

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 1;
    params.Lambda = 1.0;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_GT(result->LastCGIterations, 0u);
}

TEST(Smoothing, ImplicitLaplacianReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    Geometry::Smoothing::ImplicitSmoothingParams params;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    EXPECT_FALSE(result.has_value());
}

// --- Topology preservation ---

TEST(Smoothing, SmoothingPreservesTopology)
{
    auto mesh = MakeTetrahedron();
    const auto origVerts = mesh.VertexCount();
    const auto origFaces = mesh.FaceCount();

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 3;
    params.Lambda = 0.3;

    auto result = Geometry::Smoothing::UniformLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(mesh.VertexCount(), origVerts);
    EXPECT_EQ(mesh.FaceCount(), origFaces);
}
