// Test.HarmonicParameterization.cpp — GEOM-019 harmonic/Tutte fixed-boundary
// parameterization with explicit boundary policies and GEOM-018 diagnostics.
//
// Covers: square-fan harmonic-average placement, circle/arc-length boundary
// determinism, invalid topology and boundary-condition failure states, Tutte
// flip-free embedding via diagnostics, and that the existing LSCM API remains
// reachable.

#include <gtest/gtest.h>
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
    namespace Param = Geometry::Parameterization;

    // 4 corners + center, 4 fan triangles. One interior vertex (center = 4).
    Geometry::HalfedgeMesh::Mesh MakeSquareFan()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto c0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const auto c1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const auto c2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
        const auto c3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        const auto cc = mesh.AddVertex({0.5f, 0.5f, 0.0f});
        (void)mesh.AddTriangle(cc, c0, c1);
        (void)mesh.AddTriangle(cc, c1, c2);
        (void)mesh.AddTriangle(cc, c2, c3);
        (void)mesh.AddTriangle(cc, c3, c0);
        return mesh;
    }

    // Flat triangulated n x n grid in z = 0: a disk with one boundary loop.
    Geometry::HalfedgeMesh::Mesh MakeDiskGrid(int n)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<std::vector<Geometry::VertexHandle>> g(
            static_cast<std::size_t>(n + 1),
            std::vector<Geometry::VertexHandle>(static_cast<std::size_t>(n + 1)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                g[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                    mesh.AddVertex(glm::vec3(static_cast<float>(i) / static_cast<float>(n),
                                             static_cast<float>(j) / static_cast<float>(n), 0.0f));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                const auto a = g[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
                const auto b = g[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j)];
                const auto c = g[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j + 1)];
                const auto d = g[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)];
                (void)mesh.AddTriangle(a, b, c);
                (void)mesh.AddTriangle(a, c, d);
            }
        return mesh;
    }

}

TEST(HarmonicParameterization, SquareFanCenterAtHarmonicAverage)
{
    auto mesh = MakeSquareFan();

    // Custom boundary fixed to the corner positions; the lone interior vertex
    // must land at the centroid for both weightings.
    Param::HarmonicParams params;
    params.Boundary = Param::HarmonicBoundaryPolicy::Custom;
    params.PinnedVertices = {0u, 1u, 2u, 3u};
    params.PinnedUVs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (const Param::HarmonicWeightType w :
         {Param::HarmonicWeightType::Cotangent, Param::HarmonicWeightType::Uniform})
    {
        params.Weights = w;
        const auto result = Param::ComputeHarmonic(mesh, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->Status, Param::HarmonicStatus::Success);
        ASSERT_EQ(result->UVs.size(), mesh.VerticesSize());
        EXPECT_NEAR(result->UVs[4].x, 0.5f, 1e-4f);
        EXPECT_NEAR(result->UVs[4].y, 0.5f, 1e-4f);
        EXPECT_EQ(result->InteriorVertexCount, 1u);
        EXPECT_EQ(result->BoundaryVertexCount, 4u);
    }
}

TEST(HarmonicParameterization, CircleBoundaryIsOnCircleAndDeterministic)
{
    auto mesh = MakeDiskGrid(4);

    Param::HarmonicParams params;
    params.Boundary = Param::HarmonicBoundaryPolicy::Circle;
    params.Weights = Param::HarmonicWeightType::Cotangent;

    const auto a = Param::ComputeHarmonic(mesh, params);
    const auto b = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(a->Status, Param::HarmonicStatus::Success);

    // Boundary vertices lie on the unit circle (center 0.5,0.5; radius 0.5).
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v) || !mesh.IsBoundary(v))
            continue;
        const float r = glm::distance(a->UVs[i], glm::vec2(0.5f, 0.5f));
        EXPECT_NEAR(r, 0.5f, 1e-4f);
    }

    // Deterministic.
    ASSERT_TRUE(b.has_value());
    ASSERT_EQ(a->UVs.size(), b->UVs.size());
    for (std::size_t i = 0; i < a->UVs.size(); ++i)
    {
        EXPECT_EQ(a->UVs[i], b->UVs[i]);
    }
}

TEST(HarmonicParameterization, SquareArcLengthBoundaryProducesValidEmbedding)
{
    auto mesh = MakeDiskGrid(4);

    Param::HarmonicParams params;
    params.Boundary = Param::HarmonicBoundaryPolicy::Square;
    params.ArcLengthSpacing = true;
    params.Weights = Param::HarmonicWeightType::Uniform;

    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::Success);
    // Tutte (uniform) with a convex boundary is flip-free.
    EXPECT_EQ(result->Diagnostics.FlippedElementCount, 0u);
}

TEST(HarmonicParameterization, TutteEmbeddingHasNoFlippedTriangles)
{
    auto mesh = MakeDiskGrid(5);

    Param::HarmonicParams params;
    params.Boundary = Param::HarmonicBoundaryPolicy::Circle;
    params.Weights = Param::HarmonicWeightType::Uniform;

    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::Success);
    EXPECT_EQ(result->Diagnostics.FlippedElementCount, 0u);
    EXPECT_EQ(result->Diagnostics.Status, Param::ParameterizationDiagnosticsStatus::Success);
    EXPECT_EQ(result->Diagnostics.EvaluatedFaceCount, mesh.FaceCount());
    EXPECT_TRUE(std::isfinite(result->Diagnostics.MeanConformalDistortion));
    EXPECT_TRUE(std::isfinite(result->Diagnostics.MeanAreaDistortion));
}

// --- Invalid topology ---

TEST(HarmonicParameterization, ClosedMeshIsNotDiskTopology)
{
    auto mesh = MakeCube(); // closed, no boundary
    const auto result = Param::ComputeHarmonic(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::NotDiskTopology);
}

TEST(HarmonicParameterization, MultipleBoundaryLoopsAreNotDiskTopology)
{
    auto mesh = MakeCube();
    mesh.DeleteFace(Geometry::FaceHandle{0u});
    mesh.DeleteFace(Geometry::FaceHandle{2u});
    const auto quality = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(quality.has_value());
    ASSERT_EQ(quality->BoundaryLoopCount, 2u);

    const auto result = Param::ComputeHarmonic(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::NotDiskTopology);
}

TEST(HarmonicParameterization, PuncturedGenusOneMeshIsNotDiskTopology)
{
    auto mesh = MakePuncturedTorus();
    const auto quality = Geometry::MeshQuality::ComputeQuality(mesh);
    ASSERT_TRUE(quality.has_value());
    ASSERT_EQ(quality->BoundaryLoopCount, 1u);
    ASSERT_EQ(quality->EulerCharacteristic, -1);

    const auto result = Param::ComputeHarmonic(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::NotDiskTopology);
}

TEST(HarmonicParameterization, FewerThanThreeVerticesAreReported)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    (void)mesh.AddVertex({0.0f, 0.0f, 0.0f});
    (void)mesh.AddVertex({1.0f, 0.0f, 0.0f});

    const auto result = Param::ComputeHarmonic(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::InsufficientVertices);
}

TEST(HarmonicParameterization, NonTriangleMeshIsRejected)
{
    auto mesh = MakeSingleQuad(); // one quad face
    const auto result = Param::ComputeHarmonic(mesh);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::NotTriangleMesh);
}

// --- Invalid boundary conditions ---

TEST(HarmonicParameterization, MismatchedBoundaryArrayIsRejected)
{
    auto mesh = MakeSquareFan();
    Param::HarmonicParams params;
    params.PinnedVertices = {0u};
    params.PinnedUVs = {}; // size mismatch
    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::MismatchedBoundaryArray);
}

TEST(HarmonicParameterization, DuplicatePinIsRejected)
{
    auto mesh = MakeSquareFan();
    Param::HarmonicParams params;
    params.PinnedVertices = {0u, 0u};
    params.PinnedUVs = {{0.0f, 0.0f}, {0.0f, 0.0f}};
    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::InvalidPins);
}

TEST(HarmonicParameterization, DeletedPinIsRejected)
{
    auto mesh = MakeSquareFan();
    mesh.DeleteVertex(Geometry::VertexHandle{0u});

    Param::HarmonicParams params;
    params.PinnedVertices = {0u};
    params.PinnedUVs = {{0.0f, 0.0f}};
    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::InvalidPins);
}

TEST(HarmonicParameterization, NonFinitePinUvIsRejected)
{
    auto mesh = MakeSquareFan();
    Param::HarmonicParams params;
    params.PinnedVertices = {0u};
    params.PinnedUVs = {{std::numeric_limits<float>::quiet_NaN(), 0.0f}};
    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::NonFiniteBoundaryUV);
}

TEST(HarmonicParameterization, CustomBoundaryNotFullyPinnedIsRejected)
{
    auto mesh = MakeSquareFan();
    Param::HarmonicParams params;
    params.Boundary = Param::HarmonicBoundaryPolicy::Custom;
    params.PinnedVertices = {0u, 1u}; // boundary loop has 4 vertices
    params.PinnedUVs = {{0.0f, 0.0f}, {1.0f, 0.0f}};
    const auto result = Param::ComputeHarmonic(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, Param::HarmonicStatus::InvalidPins);
}

// --- LSCM remains reachable (not replaced by GEOM-019) ---

TEST(HarmonicParameterization, LscmApiRemainsReachable)
{
    auto mesh = MakeDiskGrid(4);
    const auto lscm = Geometry::Parameterization::ComputeLSCM(
        mesh, Geometry::Parameterization::ParameterizationParams{});
    ASSERT_TRUE(lscm.has_value());
    EXPECT_EQ(lscm->UVs.size(), mesh.VerticesSize());
}
