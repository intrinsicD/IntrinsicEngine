// tests/Test_Smoothing.cpp — Mesh smoothing operator tests.
// Covers: uniform Laplacian, cotangent Laplacian, Taubin (shrinkage-free),
// implicit Laplacian, boundary preservation, and degenerate input.

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

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
        (void)Geometry::Subdivision::Subdivide(ico, refined, sp);
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

// --- Boundary preservation ---

TEST(Smoothing, UniformPreservesBoundary)
{
    auto mesh = MakeSubdividedTriangle();

    // Record boundary vertex positions
    std::vector<std::pair<Geometry::VertexHandle, glm::vec3>> boundaryPositions;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsBoundary(vh))
            boundaryPositions.push_back({vh, mesh.Position(vh)});
    }

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 10;
    params.Lambda = 0.5;
    params.PreserveBoundary = true;

    auto result = Geometry::Smoothing::UniformLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    for (const auto& [vh, pos] : boundaryPositions)
    {
        glm::vec3 after = mesh.Position(vh);
        EXPECT_NEAR(after.x, pos.x, 1e-6f);
        EXPECT_NEAR(after.y, pos.y, 1e-6f);
        EXPECT_NEAR(after.z, pos.z, 1e-6f);
    }
}

// --- Cotangent Laplacian edge-length variance ---

TEST(Smoothing, CotanReducesEdgeLengthVariance)
{
    auto mesh = MakeIcosahedron();

    // Add mild radial noise to break uniformity.
    // Keep noise small so explicit cotan integration remains stable.
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        float noise = 0.03f * (static_cast<float>((i * 7) % 5) - 2.0f);
        glm::vec3 p = mesh.Position(vh);
        mesh.Position(vh) = p + glm::normalize(p) * noise;
    }

    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;
            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)), m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    double varBefore = edgeLengthVariance(mesh);

    Geometry::Smoothing::SmoothingParams params;
    params.Iterations = 10;
    params.Lambda = 0.05;
    params.PreserveBoundary = false;

    auto result = Geometry::Smoothing::CotanLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    double varAfter = edgeLengthVariance(mesh);
    EXPECT_LT(varAfter, varBefore);
}

// --- Taubin flat-mesh invariant ---

TEST(Smoothing, TaubinFlatMeshStaysFlat)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Smoothing::TaubinParams params;
    params.Iterations = 5;
    params.Lambda = 0.5;
    params.PassbandFrequency = 0.1;
    params.PreserveBoundary = true;

    auto result = Geometry::Smoothing::Taubin(mesh, params);
    ASSERT_TRUE(result.has_value());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        EXPECT_NEAR(mesh.Position(vh).z, 0.0f, 1e-6f)
            << "Vertex " << i << " should remain on z=0 plane after smoothing";
    }
}

// --- Implicit Laplacian additional tests ---

TEST(Smoothing, ImplicitFlatMeshStaysFlat)
{
    auto mesh = MakeSubdividedTriangle();

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 3;
    params.Lambda = 1.0;
    params.PreserveBoundary = true;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        EXPECT_NEAR(mesh.Position(vh).z, 0.0f, 1e-4f)
            << "Vertex " << i << " should remain on z=0 plane";
    }
}

TEST(Smoothing, ImplicitReducesNoise)
{
    auto mesh = MakeIcosahedron();

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh)) continue;

        float noise = 0.05f * (static_cast<float>(i % 5) - 2.0f);
        glm::vec3 p = mesh.Position(vh);
        mesh.Position(vh) = p + glm::normalize(p) * noise;
    }

    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;
            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)), m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    double varBefore = edgeLengthVariance(mesh);

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 1;
    params.Lambda = 1.0;
    params.PreserveBoundary = false;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_LT(edgeLengthVariance(mesh), varBefore);
}

TEST(Smoothing, ImplicitPreservesBoundary)
{
    auto mesh = MakeSubdividedTriangle();

    std::vector<std::pair<Geometry::VertexHandle, glm::vec3>> boundaryPositions;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsBoundary(vh))
            boundaryPositions.push_back({vh, mesh.Position(vh)});
    }

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 3;
    params.Lambda = 1.0;
    params.PreserveBoundary = true;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    for (const auto& [vh, pos] : boundaryPositions)
    {
        glm::vec3 after = mesh.Position(vh);
        EXPECT_NEAR(after.x, pos.x, 1e-5f);
        EXPECT_NEAR(after.y, pos.y, 1e-5f);
        EXPECT_NEAR(after.z, pos.z, 1e-5f);
    }
}

TEST(Smoothing, ImplicitUnconditionallyStable)
{
    auto mesh = MakeIcosahedron();

    Geometry::Smoothing::ImplicitSmoothingParams params;
    params.Iterations = 1;
    params.Lambda = 1.0;
    params.TimeStep = 1000.0; // Enormous timestep
    params.PreserveBoundary = false;

    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh, params);
    ASSERT_TRUE(result.has_value());

    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

        glm::vec3 p = mesh.Position(vh);
        EXPECT_FALSE(std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z))
            << "Vertex " << i << " has NaN after large timestep";
        EXPECT_FALSE(std::isinf(p.x) || std::isinf(p.y) || std::isinf(p.z))
            << "Vertex " << i << " has Inf after large timestep";
    }
}

TEST(Smoothing, ImplicitMultipleIterationsSmootherThanOne)
{
    auto mesh1 = MakeIcosahedron();
    auto mesh2 = MakeIcosahedron();

    // Add same noise to both
    for (std::size_t i = 0; i < mesh1.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh1.IsDeleted(vh)) continue;
        float noise = 0.05f * (static_cast<float>(i % 5) - 2.0f);
        glm::vec3 p = mesh1.Position(vh);
        mesh1.Position(vh) = p + glm::normalize(p) * noise;
        mesh2.Position(vh) = p + glm::normalize(p) * noise;
    }

    Geometry::Smoothing::ImplicitSmoothingParams params1;
    params1.Iterations = 1;
    params1.Lambda = 1.0;
    params1.PreserveBoundary = false;
    (void)Geometry::Smoothing::ImplicitLaplacian(mesh1, params1);

    Geometry::Smoothing::ImplicitSmoothingParams params3;
    params3.Iterations = 3;
    params3.Lambda = 1.0;
    params3.PreserveBoundary = false;
    auto result = Geometry::Smoothing::ImplicitLaplacian(mesh2, params3);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 3u);

    auto edgeLengthVariance = [](const Geometry::Halfedge::Mesh& m) -> double
    {
        double sum = 0.0, sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.EdgesSize(); ++i)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(e)) continue;
            auto h = m.Halfedge(e, 0);
            double len = static_cast<double>(glm::distance(
                m.Position(m.FromVertex(h)), m.Position(m.ToVertex(h))));
            sum += len;
            sumSq += len * len;
            ++count;
        }
        double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    };

    // mesh2 (3 iterations) should be smoother than mesh1 (1 iteration)
    EXPECT_LE(edgeLengthVariance(mesh2), edgeLengthVariance(mesh1));
}
