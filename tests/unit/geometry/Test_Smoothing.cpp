// tests/Test_Smoothing.cpp — Mesh smoothing operator tests.
// Covers: uniform Laplacian, cotangent Laplacian, Taubin (shrinkage-free),
// implicit Laplacian, boundary preservation, and degenerate input.

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    // Compute average vertex distance from origin.
    double AverageRadius(const Geometry::HalfedgeMesh::Mesh& mesh)
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
    Geometry::HalfedgeMesh::Mesh MakeDenseMesh()
    {
        auto ico = MakeIcosahedron();
        Geometry::HalfedgeMesh::Mesh refined;
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
    Geometry::HalfedgeMesh::Mesh mesh;
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
    Geometry::HalfedgeMesh::Mesh mesh;
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
    Geometry::HalfedgeMesh::Mesh mesh;
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

    auto edgeLengthVariance = [](const Geometry::HalfedgeMesh::Mesh& m) -> double
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

    auto edgeLengthVariance = [](const Geometry::HalfedgeMesh::Mesh& m) -> double
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

    auto edgeLengthVariance = [](const Geometry::HalfedgeMesh::Mesh& m) -> double
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

// =============================================================================
// Bilateral denoiser — Stage 1 (face-normal bilateral filtering)
// =============================================================================

namespace
{
    // Deterministic Gaussian-ish noise via a fixed-seed LCG (no <random> so the
    // sequence is identical across platforms/runs).
    struct DeterministicNoise
    {
        std::uint64_t state;
        explicit DeterministicNoise(std::uint64_t seed) : state(seed) {}
        // Returns a value in [-1, 1].
        double Next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            // Use the high bits; map to [-1, 1].
            const std::uint32_t hi = static_cast<std::uint32_t>(state >> 33);
            return (static_cast<double>(hi) / 2147483647.0) - 1.0;
        }
    };

    // Reference unit face normal from the canonical Newell area vector.
    glm::dvec3 RawFaceNormal(const Geometry::HalfedgeMesh::Mesh& mesh,
                             Geometry::FaceHandle f)
    {
        const glm::dvec3 a = Geometry::MeshUtils::FaceAreaVector(mesh, f);
        const double len = glm::length(a);
        return len > 0.0 ? a / len : glm::dvec3(0.0);
    }
}

TEST(Smoothing, FilterFaceNormalsFlatMeshIsIdentity)
{
    auto mesh = MakeSubdividedTriangle(); // planar, all faces share one normal

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.NormalIterations = 8;

    std::vector<glm::vec3> filtered;
    auto result = Geometry::Smoothing::FilterFaceNormals(mesh, params, filtered);
    ASSERT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::Success);
    ASSERT_EQ(filtered.size(), mesh.FacesSize());
    EXPECT_EQ(result.ProcessedFaceCount, mesh.FaceCount());

    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(f)) continue;
        glm::dvec3 raw = RawFaceNormal(mesh, f);
        glm::dvec3 got = glm::dvec3(filtered[i]);
        EXPECT_GT(glm::dot(raw, got), 1.0 - 1e-6)
            << "Filtered normal on a flat mesh must equal the raw face normal";
    }
}

TEST(Smoothing, FilterFaceNormalsCleanMeshNearIdentity)
{
    // A smooth (subdivided) closed mesh: filtered normals stay very close to the
    // raw face normals — bilateral filtering should not distort a clean surface.
    auto mesh = MakeDenseMesh();

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.NormalIterations = 3;

    std::vector<glm::vec3> filtered;
    auto result = Geometry::Smoothing::FilterFaceNormals(mesh, params, filtered);
    ASSERT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::Success);

    double minDot = 1.0;
    for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
    {
        Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(f)) continue;
        glm::dvec3 raw = RawFaceNormal(mesh, f);
        glm::dvec3 got = glm::dvec3(filtered[i]);
        minDot = std::min(minDot, glm::dot(raw, got));
    }
    // Every filtered normal stays within a small angle of the original.
    EXPECT_GT(minDot, 0.9) << "Clean-surface normals drifted too far";
}

TEST(Smoothing, FilterFaceNormalsIsDeterministic)
{
    auto makeNoisy = []()
    {
        auto mesh = MakeDenseMesh();
        DeterministicNoise noise(0x9E3779B97F4A7C15ULL);
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
            if (mesh.IsDeleted(vh)) continue;
            glm::vec3 d{static_cast<float>(noise.Next()),
                        static_cast<float>(noise.Next()),
                        static_cast<float>(noise.Next())};
            mesh.Position(vh) = mesh.Position(vh) + 0.03f * d;
        }
        return mesh;
    };

    auto meshA = makeNoisy();
    auto meshB = makeNoisy();

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.NormalIterations = 5;

    std::vector<glm::vec3> a, b;
    auto ra = Geometry::Smoothing::FilterFaceNormals(meshA, params, a);
    auto rb = Geometry::Smoothing::FilterFaceNormals(meshB, params, b);
    ASSERT_EQ(ra.Status, Geometry::Smoothing::DenoiseStatus::Success);
    ASSERT_EQ(rb.Status, Geometry::Smoothing::DenoiseStatus::Success);
    ASSERT_EQ(a.size(), b.size());

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].x, b[i].x);
        EXPECT_EQ(a[i].y, b[i].y);
        EXPECT_EQ(a[i].z, b[i].z);
    }
    EXPECT_EQ(ra.ProcessedFaceCount, rb.ProcessedFaceCount);
    EXPECT_EQ(ra.SigmaSpatialUsed, rb.SigmaSpatialUsed);
    EXPECT_EQ(ra.SigmaRangeUsed, rb.SigmaRangeUsed);
}

TEST(Smoothing, FilterFaceNormalsFailsClosedOnEmptyMesh)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    Geometry::Smoothing::BilateralDenoiseParams params;

    std::vector<glm::vec3> filtered;
    auto result = Geometry::Smoothing::FilterFaceNormals(mesh, params, filtered);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::EmptyMesh);
    EXPECT_TRUE(filtered.empty());
}

TEST(Smoothing, FilterFaceNormalsFailsClosedOnNonFiniteSigma)
{
    auto mesh = MakeDenseMesh();
    Geometry::Smoothing::BilateralDenoiseParams params;
    params.SigmaSpatial = std::numeric_limits<double>::quiet_NaN();

    std::vector<glm::vec3> filtered;
    auto result = Geometry::Smoothing::FilterFaceNormals(mesh, params, filtered);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::InvalidParams);
}
