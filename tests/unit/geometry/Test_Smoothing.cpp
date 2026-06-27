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

// =============================================================================
// Bilateral denoiser — full two-stage DenoiseBilateral
// =============================================================================

namespace
{
    // RMS distance between current and a reference set of positions.
    double RmsToReference(const Geometry::HalfedgeMesh::Mesh& mesh,
                          const std::vector<glm::vec3>& reference)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (mesh.IsDeleted(v)) continue;
            glm::vec3 d = mesh.Position(v) - reference[i];
            sum += glm::dot(d, d);
            ++count;
        }
        return count ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
    }

    std::vector<glm::vec3> CapturePositions(const Geometry::HalfedgeMesh::Mesh& mesh)
    {
        std::vector<glm::vec3> out(mesh.VerticesSize());
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
            out[i] = mesh.Position(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)});
        return out;
    }

    // Assert every vertex position (from startIdx on) is byte-identical to a
    // captured reference — used to prove the fail-closed "mesh unmodified"
    // contract. Component-wise so it compiles without glm ostream support.
    void ExpectPositionsUnchanged(const Geometry::HalfedgeMesh::Mesh& mesh,
                                  const std::vector<glm::vec3>& before,
                                  std::size_t startIdx = 0)
    {
        for (std::size_t i = startIdx; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            glm::vec3 p = mesh.Position(v);
            EXPECT_EQ(p.x, before[i].x) << "vertex " << i << " moved";
            EXPECT_EQ(p.y, before[i].y) << "vertex " << i << " moved";
            EXPECT_EQ(p.z, before[i].z) << "vertex " << i << " moved";
        }
    }

    // A square grid folded along x=0 into a "roof"/crease: z = slope*|x|.
    // Two planar halves meeting at a sharp feature edge along the y axis.
    Geometry::HalfedgeMesh::Mesh MakeCreaseGrid(int n, float cell, float slope)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<std::vector<Geometry::VertexHandle>> id(
            n + 1, std::vector<Geometry::VertexHandle>(n + 1));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
            {
                float x = (static_cast<float>(i) - n / 2.0f) * cell;
                float y = (static_cast<float>(j) - n / 2.0f) * cell;
                id[i][j] = mesh.AddVertex({x, y, slope * std::fabs(x)});
            }
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                (void)mesh.AddTriangle(id[i][j], id[i + 1][j], id[i + 1][j + 1]);
                (void)mesh.AddTriangle(id[i][j], id[i + 1][j + 1], id[i][j + 1]);
            }
        return mesh;
    }
}

TEST(Smoothing, DenoiseBilateralCleanMeshNearIdentity)
{
    auto mesh = MakeDenseMesh();
    auto before = CapturePositions(mesh);
    const double meanEdge = Geometry::MeshUtils::MeanEdgeLength(mesh);

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.NormalIterations = 5;
    params.VertexIterations = 10;

    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    ASSERT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::Success);

    double maxDisp = 0.0;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v)) continue;
        maxDisp = std::max(maxDisp,
                           static_cast<double>(glm::length(mesh.Position(v) - before[i])));
    }
    // Denoising an already-clean surface must barely move any vertex.
    EXPECT_LT(maxDisp, 0.1 * meanEdge) << "Clean mesh moved too much";
}

TEST(Smoothing, DenoiseBilateralReducesNoise)
{
    auto clean = MakeDenseMesh();
    auto cleanPos = CapturePositions(clean);

    // Inject deterministic noise along each vertex normal.
    auto noisy = MakeDenseMesh();
    DeterministicNoise noise(0xD1CE5EEDULL);
    for (std::size_t i = 0; i < noisy.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (noisy.IsDeleted(v)) continue;
        glm::vec3 nrm = Geometry::MeshUtils::VertexNormal(noisy, v);
        noisy.Position(v) = noisy.Position(v) + (0.05f * static_cast<float>(noise.Next())) * nrm;
    }
    const double rmsNoisy = RmsToReference(noisy, cleanPos);

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.NormalIterations = 5;
    params.VertexIterations = 10;

    auto result = Geometry::Smoothing::DenoiseBilateral(noisy, params);
    ASSERT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::Success);

    const double rmsDenoised = RmsToReference(noisy, cleanPos);
    EXPECT_LT(rmsDenoised, rmsNoisy)
        << "RMS to clean surface should shrink after denoising (" << rmsNoisy
        << " -> " << rmsDenoised << ")";
}

TEST(Smoothing, DenoiseBilateralPreservesFeatureBetterThanUniform)
{
    // Build a creased surface, add the same fixed-seed normal-direction noise to
    // two copies, then denoise one bilaterally and smooth the other with the
    // uniform Laplacian to comparable (or greater) overall smoothing.
    auto clean = MakeCreaseGrid(20, 0.4f, 0.6f);
    auto cleanPos = CapturePositions(clean);

    auto addNoise = [](Geometry::HalfedgeMesh::Mesh& m)
    {
        DeterministicNoise noise(0xFEA70511ULL);
        for (std::size_t i = 0; i < m.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(v)) continue;
            glm::vec3 p = m.Position(v);
            p.z += 0.05f * static_cast<float>(noise.Next());
            m.Position(v) = p;
        }
    };

    auto bilateral = MakeCreaseGrid(20, 0.4f, 0.6f);
    auto uniform = MakeCreaseGrid(20, 0.4f, 0.6f);
    addNoise(bilateral);
    addNoise(uniform);

    Geometry::Smoothing::BilateralDenoiseParams bp;
    bp.NormalIterations = 5;
    bp.VertexIterations = 15;
    auto br = Geometry::Smoothing::DenoiseBilateral(bilateral, bp);
    ASSERT_EQ(br.Status, Geometry::Smoothing::DenoiseStatus::Success);

    Geometry::Smoothing::SmoothingParams up;
    up.Iterations = 15;
    up.Lambda = 0.5;
    up.PreserveBoundary = true;
    auto ur = Geometry::Smoothing::UniformLaplacian(uniform, up);
    ASSERT_TRUE(ur.has_value());

    // Error measured only along the crease band (|x| ~ 0): the sharp feature.
    auto creaseError = [&](const Geometry::HalfedgeMesh::Mesh& m)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < m.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(v)) continue;
            if (std::fabs(cleanPos[i].x) < 0.1f)
            {
                float dz = m.Position(v).z - cleanPos[i].z;
                sum += static_cast<double>(dz) * dz;
                ++count;
            }
        }
        return count ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
    };

    const double bilateralFeature = creaseError(bilateral);
    const double uniformFeature = creaseError(uniform);
    // Uniform Laplacian rounds the crease; the bilateral denoiser keeps it sharp.
    EXPECT_LT(bilateralFeature, uniformFeature)
        << "bilateral=" << bilateralFeature << " uniform=" << uniformFeature;
    // And it is at least as smooth overall (not feature-preservation by inaction).
    EXPECT_LE(RmsToReference(bilateral, cleanPos), RmsToReference(uniform, cleanPos));
}

TEST(Smoothing, DenoiseBilateralIsDeterministic)
{
    auto build = []()
    {
        auto m = MakeDenseMesh();
        DeterministicNoise noise(0x0BADBEEFULL);
        for (std::size_t i = 0; i < m.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (m.IsDeleted(v)) continue;
            glm::vec3 d{static_cast<float>(noise.Next()),
                        static_cast<float>(noise.Next()),
                        static_cast<float>(noise.Next())};
            m.Position(v) = m.Position(v) + 0.03f * d;
        }
        return m;
    };

    auto meshA = build();
    auto meshB = build();

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.NormalIterations = 4;
    params.VertexIterations = 8;

    auto ra = Geometry::Smoothing::DenoiseBilateral(meshA, params);
    auto rb = Geometry::Smoothing::DenoiseBilateral(meshB, params);
    ASSERT_EQ(ra.Status, Geometry::Smoothing::DenoiseStatus::Success);
    ASSERT_EQ(rb.Status, Geometry::Smoothing::DenoiseStatus::Success);

    for (std::size_t i = 0; i < meshA.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        glm::vec3 a = meshA.Position(v);
        glm::vec3 b = meshB.Position(v);
        EXPECT_EQ(a.x, b.x);
        EXPECT_EQ(a.y, b.y);
        EXPECT_EQ(a.z, b.z);
    }
    EXPECT_EQ(ra.MovedVertexCount, rb.MovedVertexCount);
    EXPECT_EQ(ra.PinnedBoundaryVertexCount, rb.PinnedBoundaryVertexCount);
    EXPECT_EQ(ra.VertexIterationsPerformed, rb.VertexIterationsPerformed);
}

TEST(Smoothing, DenoiseBilateralFailsClosedOnEmptyMesh)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    Geometry::Smoothing::BilateralDenoiseParams params;
    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::EmptyMesh);
}

TEST(Smoothing, DenoiseBilateralFailsClosedOnNonManifoldInput)
{
    // Bowtie: two triangles sharing only vertex v2 -> v2 is non-manifold.
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({1.5f, 1.0f, 0.0f});
    auto v4 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v2, v3, v4);
    ASSERT_FALSE(mesh.IsManifold(v2)) << "test precondition: v2 must be non-manifold";

    auto before = CapturePositions(mesh);
    Geometry::Smoothing::BilateralDenoiseParams params;
    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::NonManifoldInput);

    ExpectPositionsUnchanged(mesh, before);
}

TEST(Smoothing, DenoiseBilateralFailsClosedOnDegenerateGeometry)
{
    // A single zero-area (collinear) triangle: no usable faces remain.
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);

    auto before = CapturePositions(mesh);
    Geometry::Smoothing::BilateralDenoiseParams params;
    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::DegenerateGeometry);
    EXPECT_EQ(result.DegenerateFaceCount, 1u);

    ExpectPositionsUnchanged(mesh, before);
}

TEST(Smoothing, DenoiseBilateralFailsClosedOnNonFiniteInput)
{
    auto mesh = MakeTetrahedron();
    Geometry::VertexHandle bad{0};
    mesh.Position(bad) = glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);

    // Capture finite vertices for the unchanged check.
    auto before = CapturePositions(mesh);

    Geometry::Smoothing::BilateralDenoiseParams params;
    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::NonFiniteInput);

    // Every finite vertex must be untouched (the NaN one stays NaN).
    ExpectPositionsUnchanged(mesh, before, /*startIdx=*/1);
}

TEST(Smoothing, DenoiseBilateralFailsClosedOnNonFiniteSigma)
{
    auto mesh = MakeDenseMesh();
    auto before = CapturePositions(mesh);

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.SigmaRange = std::numeric_limits<double>::infinity();

    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    EXPECT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::InvalidParams);

    ExpectPositionsUnchanged(mesh, before);
}

TEST(Smoothing, DenoiseBilateralPreservesBoundary)
{
    auto mesh = MakeCreaseGrid(8, 0.4f, 0.6f);

    std::vector<std::pair<Geometry::VertexHandle, glm::vec3>> boundaryPositions;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsBoundary(vh))
            boundaryPositions.push_back({vh, mesh.Position(vh)});
    }
    ASSERT_FALSE(boundaryPositions.empty());

    Geometry::Smoothing::BilateralDenoiseParams params;
    params.PreserveBoundary = true;
    params.NormalIterations = 5;
    params.VertexIterations = 10;

    auto result = Geometry::Smoothing::DenoiseBilateral(mesh, params);
    ASSERT_EQ(result.Status, Geometry::Smoothing::DenoiseStatus::Success);

    for (const auto& [vh, pos] : boundaryPositions)
    {
        glm::vec3 after = mesh.Position(vh);
        EXPECT_EQ(after.x, pos.x);
        EXPECT_EQ(after.y, pos.y);
        EXPECT_EQ(after.z, pos.z);
    }
}
