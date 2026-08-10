#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.CurvatureSegmentation;
import Geometry.Properties;

namespace
{
    namespace Segment = Geometry::CurvatureSegmentation;

    struct CurvatureFixture
    {
        Geometry::HalfedgeMesh::Mesh Mesh{};
        std::vector<double> K1{};
        std::vector<double> K2{};
    };

    [[nodiscard]] CurvatureFixture MakeSeparatedCurvatureTriangles()
    {
        CurvatureFixture fixture{};
        constexpr std::uint32_t countPerGroup = 8u;
        for (std::uint32_t group = 0u; group < 2u; ++group)
        {
            for (std::uint32_t i = 0u; i < countPerGroup; ++i)
            {
                const float x = static_cast<float>(
                    3u * (group * countPerGroup + i));
                const auto v0 = fixture.Mesh.AddVertex({x, 0.0f, 0.0f});
                const auto v1 = fixture.Mesh.AddVertex({x + 1.0f, 0.0f, 0.0f});
                const auto v2 = fixture.Mesh.AddVertex({x, 1.0f, 0.0f});
                EXPECT_TRUE(fixture.Mesh.AddTriangle(v0, v1, v2));

                const double noise =
                    0.02 * static_cast<double>(static_cast<int>(i % 5u) - 2);
                const double k1 = group == 0u ? -2.0 + noise : 3.0 + noise;
                const double k2 = group == 0u ? -4.0 - noise : 1.0 - noise;
                for (std::uint32_t vertex = 0u; vertex < 3u; ++vertex)
                {
                    fixture.K1.push_back(k1);
                    fixture.K2.push_back(k2);
                }
            }
        }
        return fixture;
    }

    struct FoldFixture
    {
        CurvatureFixture Curvature{};
        Geometry::EdgeHandle FoldEdge{};
    };

    [[nodiscard]] FoldFixture MakeFoldFixture()
    {
        FoldFixture fixture{};
        auto& mesh = fixture.Curvature.Mesh;
        constexpr std::uint32_t segments = 4u;
        std::vector<Geometry::VertexHandle> left;
        std::vector<Geometry::VertexHandle> foldVertices;
        std::vector<Geometry::VertexHandle> right;
        for (std::uint32_t j = 0u; j <= segments; ++j)
        {
            const float y = static_cast<float>(j);
            left.push_back(mesh.AddVertex({-1.0f, y, 0.0f}));
            foldVertices.push_back(mesh.AddVertex({0.0f, y, 0.0f}));
            right.push_back(mesh.AddVertex({1.0f, y, 1.0f}));
            const double level = static_cast<double>(j);
            fixture.Curvature.K1.insert(
                fixture.Curvature.K1.end(),
                {-1.0 + 0.03 * level,
                 0.02 * level * level,
                 1.0 - 0.01 * level});
            fixture.Curvature.K2.insert(
                fixture.Curvature.K2.end(),
                {-2.0 + 0.015 * level * level,
                 -0.025 * level,
                 2.0 + 0.02 * level});
        }
        for (std::uint32_t j = 0u; j < segments; ++j)
        {
            EXPECT_TRUE(mesh.AddTriangle(
                left[j], foldVertices[j], left[j + 1u]));
            EXPECT_TRUE(mesh.AddTriangle(
                foldVertices[j], foldVertices[j + 1u], left[j + 1u]));
            EXPECT_TRUE(mesh.AddTriangle(
                foldVertices[j], right[j], foldVertices[j + 1u]));
            EXPECT_TRUE(mesh.AddTriangle(
                right[j], right[j + 1u], foldVertices[j + 1u]));
        }
        const auto fold = mesh.FindEdge(foldVertices[1u], foldVertices[2u]);
        EXPECT_TRUE(fold.has_value());
        fixture.FoldEdge = *fold;
        return fixture;
    }

    [[nodiscard]] CurvatureFixture MakeAnalyticRegimeFixture()
    {
        CurvatureFixture fixture{};
        constexpr std::array<glm::dvec2, 4u> regimes{{
            {0.0, 0.0},     // plane
            {10.0, 10.0},   // sphere
            {20.0, 0.0},    // cylinder
            {10.0, -10.0},  // saddle
        }};
        constexpr std::uint32_t facesPerRegime = 8u;
        for (std::uint32_t regime = 0u;
             regime < regimes.size();
             ++regime)
        {
            for (std::uint32_t i = 0u; i < facesPerRegime; ++i)
            {
                const float x = static_cast<float>(
                    3u * (regime * facesPerRegime + i));
                const auto v0 =
                    fixture.Mesh.AddVertex({x, 0.0f, 0.0f});
                const auto v1 =
                    fixture.Mesh.AddVertex({x + 1.0f, 0.0f, 0.0f});
                const auto v2 =
                    fixture.Mesh.AddVertex({x, 1.0f, 0.0f});
                EXPECT_TRUE(fixture.Mesh.AddTriangle(v0, v1, v2));

                const double k1 = regimes[regime].x;
                const double k2 = regimes[regime].y;
                fixture.K1.insert(fixture.K1.end(), 3u, k1);
                fixture.K2.insert(fixture.K2.end(), 3u, k2);
            }
        }
        return fixture;
    }

    [[nodiscard]] CurvatureFixture MakeSingleOutlierStrip()
    {
        CurvatureFixture fixture{};
        constexpr std::uint32_t faceCount = 15u;
        for (std::uint32_t i = 0u; i < faceCount + 2u; ++i)
        {
            static_cast<void>(fixture.Mesh.AddVertex(glm::vec3{
                static_cast<float>(i / 2u),
                static_cast<float>(i % 2u),
                0.0f,
            }));
        }
        for (std::uint32_t face = 0u; face < faceCount; ++face)
        {
            const Geometry::VertexHandle v0{face};
            const Geometry::VertexHandle v1{face + 1u};
            const Geometry::VertexHandle v2{face + 2u};
            EXPECT_TRUE(
                face % 2u == 0u
                    ? fixture.Mesh.AddTriangle(v0, v1, v2)
                    : fixture.Mesh.AddTriangle(v1, v0, v2));
        }

        std::vector<double> faceK1(faceCount, -1.0);
        faceK1[faceCount / 2u] = 2.0;
        fixture.K1.assign(faceCount + 2u, -1.0);
        for (std::uint32_t face = 1u; face < faceCount; ++face)
        {
            fixture.K1[face + 2u] =
                3.0 * faceK1[face] -
                fixture.K1[face] - fixture.K1[face + 1u];
        }
        fixture.K2.resize(fixture.K1.size());
        std::transform(
            fixture.K1.begin(),
            fixture.K1.end(),
            fixture.K2.begin(),
            [](const double k1) { return k1 - 1.0; });
        return fixture;
    }

    [[nodiscard]] Segment::CurvatureSegmentationParams FixedParams(
        const std::uint32_t count)
    {
        Segment::CurvatureSegmentationParams params{};
        params.SelectionMode = Segment::ComponentSelectionMode::FixedCount;
        params.FixedComponentCount = count;
        params.MinimumRegionFaces = 1u;
        params.MaxSpatialIterations = 20u;
        params.Seed = 17u;
        return params;
    }

    [[nodiscard]] bool SamePartition(
        const std::vector<std::uint32_t>& lhs,
        const std::vector<std::uint32_t>& rhs)
    {
        if (lhs.size() != rhs.size())
            return false;
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            for (std::size_t j = 0u; j < lhs.size(); ++j)
            {
                if ((lhs[i] == lhs[j]) != (rhs[i] == rhs[j]))
                    return false;
            }
        }
        return true;
    }
}

TEST(CurvatureSegmentation, FixedCountFitsSignedPrincipalCurvatureGmm)
{
    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    const auto result = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, FixedParams(2u));
    ASSERT_TRUE(result.Succeeded()) << Segment::ToString(result.Diagnostics.Status);
    EXPECT_EQ(result.Diagnostics.RequestedComponentCount, 2u);
    EXPECT_EQ(result.Diagnostics.SelectedComponentCount, 2u);
    EXPECT_EQ(result.Diagnostics.ActiveComponentCount, 2u);
    EXPECT_EQ(result.Diagnostics.Candidates.size(), 1u);
    EXPECT_TRUE(result.Diagnostics.Candidates.front().Selected);
    EXPECT_TRUE(result.Diagnostics.Candidates.front().FitSucceeded);
    EXPECT_EQ(result.FaceComponents.size(), fixture.Mesh.FacesSize());
    EXPECT_EQ(result.FaceRegions.size(), fixture.Mesh.FacesSize());
    EXPECT_EQ(result.FaceRegionColors.size(), fixture.Mesh.FacesSize());
    EXPECT_TRUE(std::all_of(
        result.FaceComponents.begin(),
        result.FaceComponents.end(),
        [](const std::uint32_t label)
        { return label != Segment::kInvalidLabel; }));
}

TEST(CurvatureSegmentation, AutomaticModeSelectsSeparatedPopulations)
{
    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    Segment::CurvatureSegmentationParams params{};
    params.SelectionMode = Segment::ComponentSelectionMode::Automatic;
    params.AutomaticMinComponents = 1u;
    params.AutomaticMaxComponents = 4u;
    params.AutomaticFitTolerance = 0.12;
    params.AutomaticComplexityWeight = 1.0;
    params.MinimumRegionFaces = 1u;
    params.Seed = 17u;

    const auto result = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, params);
    ASSERT_TRUE(result.Succeeded()) << Segment::ToString(result.Diagnostics.Status);
    EXPECT_EQ(result.Diagnostics.SelectedComponentCount, 2u);
    EXPECT_TRUE(result.Diagnostics.AutomaticFitToleranceSatisfied);
    ASSERT_EQ(result.Diagnostics.Candidates.size(), 4u);
    EXPECT_EQ(std::count_if(
                  result.Diagnostics.Candidates.begin(),
                  result.Diagnostics.Candidates.end(),
                  [](const auto& candidate) { return candidate.Selected; }),
              1);
}

TEST(CurvatureSegmentation,
     SignedFeaturesSeparatePlaneSphereCylinderAndSaddleRegimes)
{
    CurvatureFixture fixture = MakeAnalyticRegimeFixture();
    const auto result = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, FixedParams(4u));
    ASSERT_TRUE(result.Succeeded())
        << Segment::ToString(result.Diagnostics.Status);
    ASSERT_EQ(result.Diagnostics.SelectedComponentCount, 4u);

    constexpr std::uint32_t facesPerRegime = 8u;
    std::array<std::uint32_t, 4u> regimeLabels{};
    for (std::uint32_t regime = 0u; regime < regimeLabels.size(); ++regime)
    {
        regimeLabels[regime] =
            result.FaceComponents[regime * facesPerRegime];
        for (std::uint32_t i = 1u; i < facesPerRegime; ++i)
        {
            EXPECT_EQ(
                result.FaceComponents[regime * facesPerRegime + i],
                regimeLabels[regime]);
        }
    }
    std::sort(regimeLabels.begin(), regimeLabels.end());
    EXPECT_EQ(std::unique(regimeLabels.begin(), regimeLabels.end()),
              regimeLabels.end());
}

TEST(CurvatureSegmentation,
     AutomaticModeReportsToleranceFallbackWhenNoCandidateQualifies)
{
    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    Segment::CurvatureSegmentationParams params{};
    params.SelectionMode = Segment::ComponentSelectionMode::Automatic;
    params.AutomaticMinComponents = 1u;
    params.AutomaticMaxComponents = 1u;
    params.AutomaticFitTolerance = 1.0e-12;
    params.MinimumRegionFaces = 1u;
    params.Seed = 17u;

    const auto result = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, params);
    ASSERT_TRUE(result.Succeeded())
        << Segment::ToString(result.Diagnostics.Status);
    EXPECT_FALSE(result.Diagnostics.AutomaticFitToleranceSatisfied);
    ASSERT_EQ(result.Diagnostics.Candidates.size(), 1u);
    EXPECT_FALSE(result.Diagnostics.Candidates.front()
                     .FitToleranceSatisfied);
    EXPECT_TRUE(result.Diagnostics.Candidates.front().Selected);
    EXPECT_GT(result.Diagnostics.NormalizedRmsFit,
              params.AutomaticFitTolerance);
}

TEST(CurvatureSegmentation,
     IncreasingSpatialWeightReducesSmoothEdgeDisagreement)
{
    CurvatureFixture fixture = MakeSingleOutlierStrip();
    auto unregularizedParams = FixedParams(2u);
    unregularizedParams.SpatialWeight = 0.0;
    unregularizedParams.FeatureSensitivity = 0.0;
    const auto unregularized = Segment::Segment(
        fixture.Mesh,
        fixture.K1,
        fixture.K2,
        unregularizedParams);
    ASSERT_TRUE(unregularized.Succeeded());
    ASSERT_GT(unregularized.Diagnostics.BoundaryEdgeCount, 0u);

    auto regularizedParams = unregularizedParams;
    regularizedParams.SpatialWeight = 1.0e6;
    const auto regularized = Segment::Segment(
        fixture.Mesh,
        fixture.K1,
        fixture.K2,
        regularizedParams);
    ASSERT_TRUE(regularized.Succeeded());
    EXPECT_LT(regularized.Diagnostics.BoundaryEdgeCount,
              unregularized.Diagnostics.BoundaryEdgeCount);
    EXPECT_GT(regularized.Diagnostics.SpatialLabelMoves, 0u);
    EXPECT_LE(regularized.Diagnostics.FinalEnergy,
              regularized.Diagnostics.InitialEnergy + 1.0e-10);
}

TEST(CurvatureSegmentation, FeatureWeightPreservesFoldBoundary)
{
    FoldFixture fixture = MakeFoldFixture();
    auto params = FixedParams(2u);
    params.SpatialWeight = 40.0;
    params.FeatureSensitivity = 20.0;

    const auto result = Segment::Segment(
        fixture.Curvature.Mesh,
        fixture.Curvature.K1,
        fixture.Curvature.K2,
        params);
    ASSERT_TRUE(result.Succeeded()) << Segment::ToString(result.Diagnostics.Status);
    ASSERT_LT(fixture.FoldEdge.Index, result.EdgeBoundaries.size());
    EXPECT_EQ(result.EdgeBoundaries[fixture.FoldEdge.Index], 1u);
    EXPECT_EQ(result.EdgeBoundaryColors[fixture.FoldEdge.Index].a, 1.0f);
    EXPECT_GE(result.Diagnostics.BoundaryEdgeCount, 1u);
    EXPECT_LE(result.Diagnostics.FinalEnergy, result.Diagnostics.InitialEnergy + 1.0e-10);
}

TEST(CurvatureSegmentation, ConnectedRegionsAreContiguousAndDeterministic)
{
    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    const auto params = FixedParams(2u);
    const auto first = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, params);
    const auto second = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, params);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(first.FaceComponents, second.FaceComponents);
    EXPECT_EQ(first.FaceRegions, second.FaceRegions);
    EXPECT_EQ(first.EdgeBoundaries, second.EdgeBoundaries);
    EXPECT_EQ(first.FaceRegionColors, second.FaceRegionColors);

    std::vector<std::uint32_t> regions = first.FaceRegions;
    std::sort(regions.begin(), regions.end());
    regions.erase(std::unique(regions.begin(), regions.end()), regions.end());
    ASSERT_EQ(regions.size(), first.Diagnostics.ConnectedRegionCount);
    for (std::uint32_t i = 0u; i < regions.size(); ++i)
        EXPECT_EQ(regions[i], i);
}

TEST(CurvatureSegmentation,
     DisconnectedEqualCurvaturePatchesShareComponentButNotRegion)
{
    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    const auto result = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, FixedParams(2u));
    ASSERT_TRUE(result.Succeeded());
    ASSERT_GE(result.FaceComponents.size(), 2u);
    EXPECT_EQ(result.FaceComponents[0], result.FaceComponents[1]);
    EXPECT_NE(result.FaceRegions[0], result.FaceRegions[1]);
}

TEST(CurvatureSegmentation,
     ComputeAndSegmentUsesExistingSignedCurvatureEstimator)
{
    Geometry::HalfedgeMesh::Mesh sphere =
        Geometry::HalfedgeMesh::MakeMeshIcosahedron();
    const auto result = Segment::ComputeAndSegment(
        sphere, FixedParams(1u));
    ASSERT_TRUE(result.Succeeded())
        << Segment::ToString(result.Diagnostics.Status);
    EXPECT_EQ(result.FaceComponents.size(), sphere.FacesSize());
    EXPECT_EQ(result.EdgeBoundaries.size(), sphere.EdgesSize());
    EXPECT_EQ(result.Diagnostics.SelectedComponentCount, 1u);
    EXPECT_EQ(result.Diagnostics.ConnectedRegionCount, 1u);
    EXPECT_EQ(result.Diagnostics.BoundaryEdgeCount, 0u);
}

TEST(CurvatureSegmentation, GlobalOrientationReversalPreservesPartition)
{
    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    const auto original = Segment::Segment(
        fixture.Mesh, fixture.K1, fixture.K2, FixedParams(2u));
    ASSERT_TRUE(original.Succeeded());

    std::vector<double> reversedK1(fixture.K1.size());
    std::vector<double> reversedK2(fixture.K2.size());
    for (std::size_t i = 0u; i < fixture.K1.size(); ++i)
    {
        reversedK1[i] = -fixture.K2[i];
        reversedK2[i] = -fixture.K1[i];
    }
    const auto reversed = Segment::Segment(
        fixture.Mesh, reversedK1, reversedK2, FixedParams(2u));
    ASSERT_TRUE(reversed.Succeeded());
    EXPECT_TRUE(SamePartition(
        original.FaceComponents, reversed.FaceComponents));
    EXPECT_EQ(original.EdgeBoundaries, reversed.EdgeBoundaries);
}

TEST(CurvatureSegmentation, FailsClosedOnInvalidInputs)
{
    Geometry::HalfedgeMesh::Mesh empty{};
    EXPECT_EQ(
        Segment::Segment(empty, {}, {}).Diagnostics.Status,
        Segment::SegmentationStatus::EmptyMesh);

    CurvatureFixture fixture = MakeSeparatedCurvatureTriangles();
    EXPECT_EQ(
        Segment::Segment(fixture.Mesh, {}, fixture.K2).Diagnostics.Status,
        Segment::SegmentationStatus::CurvatureCountMismatch);

    auto badParams = FixedParams(2u);
    badParams.SpatialWeight = -1.0;
    EXPECT_EQ(
        Segment::Segment(
            fixture.Mesh, fixture.K1, fixture.K2, badParams).Diagnostics.Status,
        Segment::SegmentationStatus::InvalidParameters);

    fixture.K1.front() = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        Segment::Segment(
            fixture.Mesh,
            fixture.K1,
            fixture.K2,
            FixedParams(2u)).Diagnostics.Status,
        Segment::SegmentationStatus::NonFiniteCurvature);
}

TEST(CurvatureSegmentation,
     DeletedSlotsAndOpenMeshBoundaryEdgesRemainUnpublished)
{
    Geometry::HalfedgeMesh::Mesh mesh{};
    const auto a0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    const auto a1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    const auto a2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    const auto b0 = mesh.AddVertex({3.0f, 0.0f, 0.0f});
    const auto b1 = mesh.AddVertex({4.0f, 0.0f, 0.0f});
    const auto b2 = mesh.AddVertex({3.0f, 1.0f, 0.0f});
    ASSERT_TRUE(mesh.AddTriangle(a0, a1, a2));
    ASSERT_TRUE(mesh.AddTriangle(b0, b1, b2));
    const std::size_t faceSlots = mesh.FacesSize();
    const std::size_t edgeSlots = mesh.EdgesSize();
    mesh.DeleteFace(Geometry::FaceHandle{0u});
    ASSERT_EQ(mesh.FacesSize(), faceSlots);
    ASSERT_EQ(mesh.EdgesSize(), edgeSlots);
    ASSERT_EQ(mesh.FaceCount(), 1u);

    const std::vector<double> k1(mesh.VerticesSize(), 2.0);
    const std::vector<double> k2(mesh.VerticesSize(), -1.0);
    const auto result = Segment::Segment(
        mesh, k1, k2, FixedParams(1u));
    ASSERT_TRUE(result.Succeeded())
        << Segment::ToString(result.Diagnostics.Status);
    ASSERT_EQ(result.FaceComponents.size(), faceSlots);
    ASSERT_EQ(result.EdgeBoundaries.size(), edgeSlots);
    EXPECT_EQ(result.FaceComponents[0u], Segment::kInvalidLabel);
    EXPECT_EQ(result.FaceRegions[0u], Segment::kInvalidLabel);
    EXPECT_EQ(result.FaceRegionColors[0u], glm::vec4(0.0f));
    EXPECT_NE(result.FaceComponents[1u], Segment::kInvalidLabel);
    EXPECT_EQ(result.FaceRegions[1u], 0u);
    EXPECT_TRUE(std::all_of(
        result.EdgeBoundaries.begin(),
        result.EdgeBoundaries.end(),
        [](const std::uint8_t boundary) { return boundary == 0u; }));
    EXPECT_TRUE(std::all_of(
        result.EdgeBoundaryColors.begin(),
        result.EdgeBoundaryColors.end(),
        [](const glm::vec4& color) { return color == glm::vec4(0.0f); }));
    EXPECT_EQ(result.Diagnostics.BoundaryEdgeCount, 0u);
}

TEST(CurvatureSegmentation, RejectsPolygonAndDegenerateFaces)
{
    Geometry::HalfedgeMesh::Mesh polygon{};
    const auto p0 = polygon.AddVertex({0.0f, 0.0f, 0.0f});
    const auto p1 = polygon.AddVertex({1.0f, 0.0f, 0.0f});
    const auto p2 = polygon.AddVertex({1.0f, 1.0f, 0.0f});
    const auto p3 = polygon.AddVertex({0.0f, 1.0f, 0.0f});
    EXPECT_TRUE(polygon.AddQuad(p0, p1, p2, p3));
    const std::vector<double> zeros(4u, 0.0);
    EXPECT_EQ(
        Segment::Segment(
            polygon, zeros, zeros, FixedParams(1u)).Diagnostics.Status,
        Segment::SegmentationStatus::NonTriangleFace);

    Geometry::HalfedgeMesh::Mesh degenerate{};
    const auto d0 = degenerate.AddVertex({0.0f, 0.0f, 0.0f});
    const auto d1 = degenerate.AddVertex({1.0f, 0.0f, 0.0f});
    const auto d2 = degenerate.AddVertex({2.0f, 0.0f, 0.0f});
    EXPECT_TRUE(degenerate.AddTriangle(d0, d1, d2));
    const std::vector<double> flat(3u, 0.0);
    EXPECT_EQ(
        Segment::Segment(
            degenerate, flat, flat, FixedParams(1u)).Diagnostics.Status,
        Segment::SegmentationStatus::DegenerateFace);
}
