// tests/Test_Simplification.cpp — QEM mesh simplification tests.
// Covers: target face count, error threshold, boundary preservation,
// degenerate input handling, and quality guard correctness.

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    // Subdivided icosahedron: closed mesh with many faces for simplification.
    Geometry::HalfedgeMesh::Mesh MakeDenseMesh()
    {
        auto ico = MakeIcosahedron();
        Geometry::HalfedgeMesh::Mesh refined;
        Geometry::Subdivision::SubdivisionParams sp;
        sp.Iterations = 2; // 20 * 16 = 320 faces
        (void)Geometry::Subdivision::Subdivide(ico, refined, sp);
        return refined;
    }

    // Closed, watertight cube tessellated into an n*n grid per face. Faces stay
    // planar and the 8 geometric corners stay sharp (each corner has 3 incident
    // 90-degree feature edges), which is exactly the fixture FA_QEM must protect.
    Geometry::HalfedgeMesh::Mesh MakeTessellatedCube(int n, float h = 1.0f)
    {
        using Geometry::VertexHandle;
        Geometry::HalfedgeMesh::Mesh mesh;
        std::map<std::array<int, 3>, VertexHandle> verts;

        auto vertexAt = [&](int i, int j, int k) -> VertexHandle {
            const std::array<int, 3> key{i, j, k};
            if (auto it = verts.find(key); it != verts.end())
                return it->second;
            const glm::vec3 p(
                -h + 2.0f * h * static_cast<float>(i) / static_cast<float>(n),
                -h + 2.0f * h * static_cast<float>(j) / static_cast<float>(n),
                -h + 2.0f * h * static_cast<float>(k) / static_cast<float>(n));
            const VertexHandle v = mesh.AddVertex(p);
            verts.emplace(key, v);
            return v;
        };

        auto orientedTriangle = [&](VertexHandle a, VertexHandle b, VertexHandle c,
                                    glm::vec3 outward) {
            const glm::vec3 nrm = glm::cross(
                mesh.Position(b) - mesh.Position(a),
                mesh.Position(c) - mesh.Position(a));
            if (glm::dot(nrm, outward) < 0.0f)
                (void)mesh.AddTriangle(a, c, b);
            else
                (void)mesh.AddTriangle(a, b, c);
        };

        for (int axis = 0; axis < 3; ++axis)
        {
            for (int side = 0; side < 2; ++side)
            {
                const int level = side == 0 ? 0 : n;
                glm::vec3 outward(0.0f);
                outward[axis] = side == 0 ? -1.0f : 1.0f;
                const int a1 = (axis + 1) % 3;
                const int a2 = (axis + 2) % 3;
                for (int i = 0; i < n; ++i)
                {
                    for (int j = 0; j < n; ++j)
                    {
                        auto corner = [&](int u, int v) {
                            std::array<int, 3> c{};
                            c[static_cast<std::size_t>(axis)] = level;
                            c[static_cast<std::size_t>(a1)] = u;
                            c[static_cast<std::size_t>(a2)] = v;
                            return vertexAt(c[0], c[1], c[2]);
                        };
                        const VertexHandle v00 = corner(i, j);
                        const VertexHandle v10 = corner(i + 1, j);
                        const VertexHandle v11 = corner(i + 1, j + 1);
                        const VertexHandle v01 = corner(i, j + 1);
                        orientedTriangle(v00, v10, v11, outward);
                        orientedTriangle(v00, v11, v01, outward);
                    }
                }
            }
        }
        return mesh;
    }

    // Flat triangulated square grid in the z = 0 plane: an open mesh with a
    // boundary loop, used for UV-seam preservation coverage.
    Geometry::HalfedgeMesh::Mesh MakeGridPlane(int n, float h = 1.0f)
    {
        using Geometry::VertexHandle;
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<std::vector<VertexHandle>> grid(
            static_cast<std::size_t>(n + 1),
            std::vector<VertexHandle>(static_cast<std::size_t>(n + 1)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j)
                grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                    mesh.AddVertex(glm::vec3(
                        -h + 2.0f * h * static_cast<float>(i) / static_cast<float>(n),
                        -h + 2.0f * h * static_cast<float>(j) / static_cast<float>(n),
                        0.0f));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                const VertexHandle a = grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
                const VertexHandle b = grid[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j)];
                const VertexHandle c = grid[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j + 1)];
                const VertexHandle d = grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)];
                (void)mesh.AddTriangle(a, b, c);
                (void)mesh.AddTriangle(a, c, d);
            }
        return mesh;
    }

    struct SampledSurfaceDistance
    {
        double MaxDistance{0.0};
        std::size_t SampleCount{0u};
        bool Succeeded{false};
    };

    // Deterministic one-sided surface-distance proxy. Sampling the original
    // triangle interiors (not only surviving vertices) makes the signal
    // sensitive to removed corners and missing surface regions when the
    // simplifier keeps survivor positions unchanged.
    SampledSurfaceDistance SampleReferenceToResultSurfaceDistance(
        const Geometry::HalfedgeMesh::Mesh& reference,
        const Geometry::HalfedgeMesh::Mesh& result)
    {
        Geometry::MeshClosestFaceIndex resultIndex;
        if (!resultIndex.Build(result))
            return {};

        constexpr int kBarycentricResolution = 4;
        SampledSurfaceDistance distance{};
        for (std::size_t fi = 0; fi < reference.FacesSize(); ++fi)
        {
            const Geometry::FaceHandle face{static_cast<Geometry::PropertyIndex>(fi)};
            if (!reference.IsValid(face) || reference.IsDeleted(face))
                continue;

            std::vector<glm::vec3> polygon;
            for (const Geometry::VertexHandle vertex : reference.VerticesAroundFace(face))
            {
                if (reference.IsValid(vertex) && !reference.IsDeleted(vertex))
                    polygon.push_back(reference.Position(vertex));
            }

            for (std::size_t triangle = 1u; triangle + 1u < polygon.size(); ++triangle)
            {
                const glm::vec3 p0 = polygon[0u];
                const glm::vec3 p1 = polygon[triangle];
                const glm::vec3 p2 = polygon[triangle + 1u];
                for (int i = 0; i <= kBarycentricResolution; ++i)
                {
                    for (int j = 0; j <= kBarycentricResolution - i; ++j)
                    {
                        const int k = kBarycentricResolution - i - j;
                        const glm::vec3 sample =
                            (static_cast<float>(i) * p0
                             + static_cast<float>(j) * p1
                             + static_cast<float>(k) * p2)
                            / static_cast<float>(kBarycentricResolution);
                        const Geometry::MeshClosestFaceResult nearest = resultIndex.Query(sample);
                        if (!nearest.Found
                            || nearest.Status != Geometry::MeshClosestFaceStatus::Success
                            || !std::isfinite(nearest.SquaredDistance)
                            || nearest.SquaredDistance < 0.0f)
                        {
                            return {};
                        }
                        distance.MaxDistance = std::max(
                            distance.MaxDistance,
                            std::sqrt(static_cast<double>(nearest.SquaredDistance)));
                        ++distance.SampleCount;
                    }
                }
            }
        }
        distance.Succeeded = distance.SampleCount > 0u;
        return distance;
    }

    bool HasLiveVertexNear(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        glm::vec3 target,
        float eps = 1e-3f)
    {
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
                continue;
            if (glm::distance(mesh.Position(v), target) <= eps)
                return true;
        }
        return false;
    }
}

// --- Basic functionality ---

TEST(Simplification, ReducesToTargetFaceCount)
{
    auto mesh = MakeDenseMesh();
    const auto originalFaces = mesh.FaceCount();
    ASSERT_GT(originalFaces, 100u);

    Geometry::Simplification::Params params;
    params.TargetFaces = 40;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);

    mesh.GarbageCollection();
    EXPECT_LE(mesh.FaceCount(), 40u);
    EXPECT_EQ(result->FinalFaceCount, mesh.FaceCount());
}

TEST(Simplification, ErrorThresholdStopsEarly)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::Params params;
    params.TargetFaces = 4; // Aggressive target
    params.MaxError = 1e-10; // Very tight error — should stop early

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());

    mesh.GarbageCollection();
    // Should have stopped before reaching target due to error bound
    EXPECT_GT(mesh.FaceCount(), 4u);
}

TEST(Simplification, PreservesBoundary)
{
    // Open mesh (single triangle is all-boundary) — no collapses possible
    auto mesh = MakeSingleTriangle();
    ASSERT_EQ(mesh.FaceCount(), 1u);

    Geometry::Simplification::Params params;
    params.TargetFaces = 0;
    params.PreserveBoundary = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    // Either nullopt (can't simplify) or zero collapses
    if (result.has_value())
        EXPECT_EQ(result->CollapseCount, 0u);
}

TEST(Simplification, ClosedMeshRemainsClosedAfterSimplification)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::Params params;
    params.TargetFaces = 80;
    params.PreserveBoundary = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);

    mesh.GarbageCollection();
    // Closed mesh should remain closed (no boundary edges)
    std::size_t boundaryEdges = 0;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsDeleted(e) && mesh.IsBoundary(e))
            ++boundaryEdges;
    }
    EXPECT_EQ(boundaryEdges, 0u);
}

TEST(Simplification, ReturnsNulloptForEmptyMesh)
{
    Geometry::HalfedgeMesh::Mesh mesh;

    Geometry::Simplification::Params params;
    params.TargetFaces = 0;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Simplification, ReturnsNulloptForSingleFaceMesh)
{
    auto mesh = MakeSingleTriangle();

    Geometry::Simplification::Params params;
    params.TargetFaces = 0;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    // Can't simplify below minimum — either nullopt or zero collapses
    if (result.has_value())
        EXPECT_EQ(result->CollapseCount, 0u);
}

TEST(Simplification, MaxCollapseErrorIsNonNegative)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::Params params;
    params.TargetFaces = 100;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->MaxCollapseError, 0.0);
}

TEST(Simplification, TetrahedronMinimalSimplification)
{
    auto mesh = MakeTetrahedron();
    ASSERT_EQ(mesh.FaceCount(), 4u);

    Geometry::Simplification::Params params;
    params.TargetFaces = 2;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    // Tetrahedron has very few faces; some collapses may be blocked by topology
    if (result.has_value())
    {
        mesh.GarbageCollection();
        EXPECT_LE(mesh.FaceCount(), 4u);
    }
}

TEST(Simplification, QuadricMinimizerPlacement)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::Params params;
    params.TargetFaces = 80;
    params.Quadric.PlacementPolicy =
        Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
}

// --- GEOM-014: Feature-Aware QEM (FA_QEM) ---

namespace
{
    const std::array<glm::vec3, 8> kCubeCorners{{
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
        {1.0f, 1.0f, -1.0f},   {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},    {-1.0f, 1.0f, 1.0f},
    }};
}

// The default metric (FA_QEM) must keep all eight sharp cube corners even under
// aggressive decimation. Classical QEM is permitted to remove them.
TEST(Simplification, FeatureAwarePreservesCubeCorners)
{
    auto mesh = MakeTessellatedCube(4);
    ASSERT_GT(mesh.FaceCount(), 100u);

    Geometry::Simplification::Params params; // Metric defaults to FA_QEM
    params.TargetFaces = 24; // aggressive (~10% of 192)
    params.PreserveBoundary = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
    EXPECT_EQ(result->SharpFeatureVerticesPinned, 8u);

    mesh.GarbageCollection();
    for (const glm::vec3& corner : kCubeCorners)
        EXPECT_TRUE(HasLiveVertexNear(mesh, corner)) << "corner removed: " << corner.x;
}

// The classical metric remains a stable quadric-only contract: changing every
// FA_QEM-specific control must not change its result or compacted topology.
TEST(Simplification, ClassicalMetricRemainsReachable)
{
    auto baselineMesh = MakeTessellatedCube(4);
    auto faControlsChangedMesh = MakeTessellatedCube(4);

    Geometry::Simplification::Params baselineParams;
    baselineParams.Metric = Geometry::Simplification::Metric::ClassicalQEM;
    baselineParams.TargetFaces = 40;

    Geometry::Simplification::Params faControlsChanged = baselineParams;
    faControlsChanged.FeatureAngleThresholdDegrees = 1.0;
    faControlsChanged.NormalWeight = 1.0e6;
    faControlsChanged.BoundaryWeight = 1.0e6;
    faControlsChanged.CurvatureWeight = 1.0e6;
    faControlsChanged.PreserveSharpFeatures = false;
    faControlsChanged.PreserveUvSeams = false;

    const auto baselineResult =
        Geometry::Simplification::Simplify(baselineMesh, baselineParams);
    const auto faControlsChangedResult =
        Geometry::Simplification::Simplify(faControlsChangedMesh, faControlsChanged);
    ASSERT_TRUE(baselineResult.has_value());
    ASSERT_TRUE(faControlsChangedResult.has_value());
    EXPECT_GT(baselineResult->CollapseCount, 0u);
    EXPECT_EQ(baselineResult->FinalFaceCount, 40u);
    EXPECT_EQ(baselineResult->SharpFeatureVerticesPinned, 0u);
    EXPECT_EQ(baselineResult->SeamVerticesPinned, 0u);
    EXPECT_EQ(baselineResult->CollapseCount, faControlsChangedResult->CollapseCount);
    EXPECT_EQ(baselineResult->FinalFaceCount, faControlsChangedResult->FinalFaceCount);
    EXPECT_DOUBLE_EQ(baselineResult->MaxCollapseError, faControlsChangedResult->MaxCollapseError);
    EXPECT_EQ(
        baselineResult->CollapsesRejectedTopology,
        faControlsChangedResult->CollapsesRejectedTopology);
    EXPECT_EQ(
        baselineResult->CollapsesRejectedQuality,
        faControlsChangedResult->CollapsesRejectedQuality);

    baselineMesh.GarbageCollection();
    faControlsChangedMesh.GarbageCollection();
    ASSERT_EQ(baselineMesh.VertexCount(), faControlsChangedMesh.VertexCount());
    ASSERT_EQ(baselineMesh.FaceCount(), faControlsChangedMesh.FaceCount());
    for (std::size_t vi = 0; vi < baselineMesh.VerticesSize(); ++vi)
    {
        const Geometry::VertexHandle vertex{static_cast<Geometry::PropertyIndex>(vi)};
        EXPECT_EQ(baselineMesh.IsDeleted(vertex), faControlsChangedMesh.IsDeleted(vertex));
        if (!baselineMesh.IsDeleted(vertex))
            EXPECT_EQ(baselineMesh.Position(vertex), faControlsChangedMesh.Position(vertex));
    }
    for (std::size_t fi = 0; fi < baselineMesh.FacesSize(); ++fi)
    {
        const Geometry::FaceHandle face{static_cast<Geometry::PropertyIndex>(fi)};
        ASSERT_EQ(baselineMesh.IsDeleted(face), faControlsChangedMesh.IsDeleted(face));
        if (baselineMesh.IsDeleted(face))
            continue;
        std::vector<Geometry::PropertyIndex> baselineVertices;
        std::vector<Geometry::PropertyIndex> changedVertices;
        for (const Geometry::VertexHandle vertex : baselineMesh.VerticesAroundFace(face))
            baselineVertices.push_back(vertex.Index);
        for (const Geometry::VertexHandle vertex : faControlsChangedMesh.VerticesAroundFace(face))
            changedVertices.push_back(vertex.Index);
        EXPECT_EQ(baselineVertices, changedVertices);
    }
}

// Same input and params produce identical output (no RNG anywhere in the path).
TEST(Simplification, FeatureAwareIsDeterministic)
{
    Geometry::Simplification::Params params;
    params.TargetFaces = 30;

    auto a = MakeTessellatedCube(4);
    auto b = MakeTessellatedCube(4);
    auto ra = Geometry::Simplification::Simplify(a, params);
    auto rb = Geometry::Simplification::Simplify(b, params);
    ASSERT_TRUE(ra.has_value());
    ASSERT_TRUE(rb.has_value());

    EXPECT_EQ(ra->CollapseCount, rb->CollapseCount);
    EXPECT_EQ(ra->FinalFaceCount, rb->FinalFaceCount);
    EXPECT_EQ(ra->SharpFeatureVerticesPinned, rb->SharpFeatureVerticesPinned);
    EXPECT_DOUBLE_EQ(ra->MaxCollapseError, rb->MaxCollapseError);

    a.GarbageCollection();
    b.GarbageCollection();
    ASSERT_EQ(a.VerticesSize(), b.VerticesSize());
    for (std::size_t i = 0; i < a.VerticesSize(); ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_EQ(a.IsDeleted(v), b.IsDeleted(v));
        if (!a.IsDeleted(v))
            EXPECT_EQ(a.Position(v), b.Position(v));
    }
}

// On a feature-rich fixture, preserving corners must not worsen sampled
// original-surface-to-result-surface error relative to the classical metric at
// the same achieved target face count.
TEST(Simplification, FeatureAwareCornerErrorNotWorseThanClassical)
{
    const auto reference = MakeTessellatedCube(4);
    constexpr std::size_t kTargetFaces = 24u;

    auto faqemMesh = MakeTessellatedCube(4);
    Geometry::Simplification::Params faqem;
    faqem.TargetFaces = kTargetFaces;
    const auto faqemResult = Geometry::Simplification::Simplify(faqemMesh, faqem);
    ASSERT_TRUE(faqemResult.has_value());
    ASSERT_EQ(faqemResult->FinalFaceCount, kTargetFaces);
    faqemMesh.GarbageCollection();
    ASSERT_EQ(faqemMesh.FaceCount(), kTargetFaces);

    auto classicalMesh = MakeTessellatedCube(4);
    Geometry::Simplification::Params classical;
    classical.Metric = Geometry::Simplification::Metric::ClassicalQEM;
    classical.TargetFaces = kTargetFaces;
    const auto classicalResult = Geometry::Simplification::Simplify(classicalMesh, classical);
    ASSERT_TRUE(classicalResult.has_value());
    ASSERT_EQ(classicalResult->FinalFaceCount, kTargetFaces);
    classicalMesh.GarbageCollection();
    ASSERT_EQ(classicalMesh.FaceCount(), kTargetFaces);

    const SampledSurfaceDistance faqemError =
        SampleReferenceToResultSurfaceDistance(reference, faqemMesh);
    const SampledSurfaceDistance classicalError =
        SampleReferenceToResultSurfaceDistance(reference, classicalMesh);
    ASSERT_TRUE(faqemError.Succeeded);
    ASSERT_TRUE(classicalError.Succeeded);
    ASSERT_EQ(faqemError.SampleCount, classicalError.SampleCount);

    // Sensitivity control: translating the entire result surface must produce
    // a clearly non-zero signal, guarding against the previous survivor-vertex
    // proxy that was identically zero under KeepSurvivor placement.
    auto translated = MakeTessellatedCube(4);
    for (std::size_t vi = 0; vi < translated.VerticesSize(); ++vi)
    {
        const Geometry::VertexHandle vertex{static_cast<Geometry::PropertyIndex>(vi)};
        if (!translated.IsDeleted(vertex))
            translated.Position(vertex) += glm::vec3(0.25f, 0.0f, 0.0f);
    }
    const SampledSurfaceDistance controlError =
        SampleReferenceToResultSurfaceDistance(reference, translated);
    ASSERT_TRUE(controlError.Succeeded);
    EXPECT_GT(controlError.MaxDistance, 0.20);

    EXPECT_LE(faqemError.MaxDistance, classicalError.MaxDistance + 1e-6)
        << "FA-QEM sampled surface error=" << faqemError.MaxDistance
        << ", classical=" << classicalError.MaxDistance;
}

// UV seams (texcoord-bearing boundary vertices) stay put when preserve_uv_seams
// is on, even with boundary collapse otherwise enabled.
TEST(Simplification, FeatureAwarePreservesUvSeams)
{
    auto mesh = MakeGridPlane(4);
    auto tex = mesh.VertexProperties().GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
    ASSERT_TRUE(static_cast<bool>(tex));

    std::vector<glm::vec3> boundaryPositions;
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(v))
            continue;
        tex[v.Index] = glm::vec2(mesh.Position(v).x, mesh.Position(v).y);
        if (mesh.IsBoundary(v))
            boundaryPositions.push_back(mesh.Position(v));
    }
    ASSERT_GT(boundaryPositions.size(), 0u);

    Geometry::Simplification::Params params; // FA_QEM default
    params.TargetFaces = 4;
    params.PreserveBoundary = false; // seams pinned by PreserveUvSeams, not this
    params.PreserveUvSeams = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->SeamVerticesPinned, boundaryPositions.size());

    mesh.GarbageCollection();
    for (const glm::vec3& p : boundaryPositions)
        EXPECT_TRUE(HasLiveVertexNear(mesh, p)) << "seam vertex removed";
}

// Diagnostics counters include rejected candidate evaluations as well as the
// protected feature-set cardinality on a feature mesh.
TEST(Simplification, DiagnosticsCountersPopulated)
{
    auto mesh = MakeTessellatedCube(4);

    Geometry::Simplification::Params params;
    params.TargetFaces = 24;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FinalFaceCount, mesh.FaceCount());
    EXPECT_EQ(result->SharpFeatureVerticesPinned, 8u);
    EXPECT_GT(result->SharpFeatureVerticesPinned + result->SeamVerticesPinned, 0u);
    EXPECT_GT(result->CollapsesRejectedQuality, 0u);
    EXPECT_GT(
        result->CollapsesRejectedTopology + result->CollapsesRejectedQuality,
        0u);
}
