#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <vector>
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "CurvatureBoundaryGraph.hpp"
import Geometry.HalfedgeMesh;
import Geometry.Properties;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut;

namespace
{
namespace B = Geometry::CurvatureSegmentation::BoundaryDetail;
namespace C = Geometry::CurvatureSegmentation;
using Labels = std::vector<std::uint32_t>;
double Cost(const std::vector<B::Edge> &edges, const Labels &labels)
{
    double value = 0;
    for (auto e : edges)
    {
        if (e.Hard && labels[e.A] == labels[e.B])
            return std::numeric_limits<double>::infinity();
        if (labels[e.A] != labels[e.B])
            value += e.Cost;
    }
    return value;
}
// Independent exhaustive assignments, including redundant label permutations.
double BruteCost(std::uint32_t n, const std::vector<B::Edge> &edges)
{
    Labels labels(n);
    double best = std::numeric_limits<double>::infinity();
    std::uint64_t combinations = 1;
    for (std::uint32_t i = 0; i < n; ++i)
        combinations *= n;
    for (std::uint64_t code = 0; code < combinations; ++code)
    {
        auto remaining = code;
        for (auto &label : labels)
        {
            label = remaining % n;
            remaining /= n;
        }
        best = std::min(best, Cost(edges, labels));
    }
    return best;
}
struct Grid
{
    Geometry::HalfedgeMesh::Mesh Mesh;
    std::vector<std::uint8_t> Hard;
    std::vector<double> Soft;
};
Grid MakeGrid(bool alternate = false, bool gap = false)
{
    Grid g;
    std::vector<Geometry::VertexHandle> vertices;
    for (int y = 0; y <= 8; ++y)
        for (int x = 0; x <= 8; ++x)
            vertices.push_back(g.Mesh.AddVertex({float(x), float(y), 0}));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
        {
            auto a = vertices[y * 9 + x], b = vertices[y * 9 + x + 1],
                 c = vertices[(y + 1) * 9 + x + 1], d = vertices[(y + 1) * 9 + x];
            if (alternate)
            {
                EXPECT_TRUE(g.Mesh.AddTriangle(a, b, d));
                EXPECT_TRUE(g.Mesh.AddTriangle(b, c, d));
            }
            else
            {
                EXPECT_TRUE(g.Mesh.AddTriangle(a, b, c));
                EXPECT_TRUE(g.Mesh.AddTriangle(a, c, d));
            }
        }
    g.Hard.resize(g.Mesh.EdgesSize());
    g.Soft.resize(g.Mesh.EdgesSize());
    for (auto e : g.Mesh.LiveEdges())
    {
        auto h = g.Mesh.Halfedge(e, 0);
        auto a = g.Mesh.Position(g.Mesh.FromVertex(h)),
             b = g.Mesh.Position(g.Mesh.ToVertex(h));
        if (a.x == 4 && b.x == 4 && (!gap || std::min(a.y, b.y) != 4))
            g.Soft[e.Index] = 1;
    }
    return g;
}
} // namespace

TEST(CurvatureBoundaryGraph, ExactOracleMatchesIndependentAssignments)
{
    std::mt19937 random(319);
    for (int sample = 0; sample < 40; ++sample)
    {
        std::vector<B::Edge> edges;
        for (std::uint32_t a = 0; a < 5; ++a)
            for (std::uint32_t b = a + 1; b < 5; ++b)
                if (random() % 3)
                    edges.push_back(
                        {a, b, double(int(random() % 11) - 5), random() % 11 == 0});
        auto r = B::Solve(5, edges);
        ASSERT_EQ(r.State, B::Status::Success);
        EXPECT_TRUE(r.Exact);
        EXPECT_DOUBLE_EQ(r.Energy, BruteCost(5, edges));
        EXPECT_DOUBLE_EQ(r.Energy, Cost(edges, r.Labels));
        EXPECT_LE(r.LowerBound, r.Energy);
    }
}

TEST(CurvatureBoundaryGraph, HardCutCannotBeBypassedThroughPositivePath)
{
    std::vector<B::Edge> edges{{0, 1, 100, false}, {1, 2, 100, false}, {0, 2, 1, true}};
    for (auto exact : {0u, 8u})
    {
        auto r = B::Solve(3, edges, {.ExactNodeLimit = exact});
        ASSERT_EQ(r.State, B::Status::Success);
        EXPECT_NE(r.Labels[0], r.Labels[2]);
        EXPECT_DOUBLE_EQ(r.Energy, 101);
    }
}

TEST(CurvatureBoundaryGraph, WiderMovesRecoverAcrossPrematureContraction)
{
    std::vector<B::Edge> edges;
    for (std::uint32_t a = 0; a < 6; ++a)
        for (std::uint32_t b = a + 1; b < 6; ++b)
            edges.push_back(
                {a, b, a / 3 == b / 3 ? 5.0 : (a == 2 && b == 3 ? 9.0 : -2.0), false});
    auto r = B::Solve(6, edges, {.ExactNodeLimit = 0});
    ASSERT_EQ(r.State, B::Status::Success);
    EXPECT_DOUBLE_EQ(r.Energy, BruteCost(6, edges));
}

TEST(CurvatureBoundaryGraph, HeuristicIsFeasibleDeterministicAndBoundedByOracle)
{
    std::mt19937 random(912);
    for (int sample = 0; sample < 80; ++sample)
    {
        std::vector<B::Edge> edges;
        for (std::uint32_t a = 0; a < 7; ++a)
            for (std::uint32_t b = a + 1; b < 7; ++b)
                if (random() % 2)
                    edges.push_back(
                        {a, b, double(int(random() % 15) - 7), random() % 13 == 0});
        auto exact = B::Solve(7, edges), r = B::Solve(7, edges, {.ExactNodeLimit = 0});
        ASSERT_EQ(r.State, B::Status::Success) << sample;
        EXPECT_FALSE(r.Exact);
        EXPECT_GE(r.Energy, exact.Energy);
        EXPECT_DOUBLE_EQ(r.Energy, Cost(edges, r.Labels));
        EXPECT_LE(r.Energy, r.InitialEnergy);
        std::reverse(edges.begin(), edges.end());
        for (auto &e : edges)
            std::swap(e.A, e.B);
        auto repeated = B::Solve(7, edges, {.ExactNodeLimit = 0});
        EXPECT_EQ(r.Labels, repeated.Labels);
        EXPECT_DOUBLE_EQ(r.Energy, repeated.Energy);
    }
}

TEST(CurvatureBoundaryGraph, InvalidAndLimitedInputsReturnNoPartition)
{
    for (auto edges :
         {std::vector<B::Edge>{{0, 3, 1, false}}, std::vector<B::Edge>{{0, 0, 1, false}},
          std::vector<B::Edge>{{0, 1, std::numeric_limits<double>::infinity(), false}}})
    {
        auto r = B::Solve(3, edges);
        EXPECT_EQ(r.State, B::Status::InvalidInput);
        EXPECT_TRUE(r.Labels.empty());
    }
    std::vector<B::Edge> edges{{0, 1, 1, false}, {1, 2, -1, false}};
    auto limited = B::Solve(3, edges, {.MaximumMoves = 1});
    EXPECT_EQ(limited.State, B::Status::WorkLimit);
    EXPECT_TRUE(limited.Labels.empty());
}

TEST(CurvatureBoundaryGraph, DisconnectedNodesPublishSeparateConnectedRegions)
{
    auto r = B::Solve(4, {});
    ASSERT_EQ(r.State, B::Status::Success);
    EXPECT_EQ(r.Labels, (Labels{0, 1, 2, 3}));
}

TEST(CurvatureBoundaryPartition,
     StrongContourAndShortCompletionSeparateEqualCurvatureSurface)
{
    for (bool alternate : {false, true})
        for (bool gap : {false, true})
        {
            auto g = MakeGrid(alternate, gap);
            auto r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft});
            ASSERT_TRUE(r.Succeeded()) << C::ToString(r.Diagnostics.Status);
            EXPECT_EQ(r.Diagnostics.RegionCount, 2);
            EXPECT_EQ(r.Diagnostics.ClosureBoundaryCount, gap ? 1 : 0);
            for (auto e : g.Mesh.LiveEdges())
            {
                auto h = g.Mesh.Halfedge(e, 0);
                auto a = g.Mesh.Position(g.Mesh.FromVertex(h)),
                     b = g.Mesh.Position(g.Mesh.ToVertex(h));
                EXPECT_EQ(r.EdgeBoundaries[e.Index], a.x == 4 && b.x == 4);
            }
        }
}

TEST(CurvatureBoundaryPartition, HomogeneousSurfaceStaysWholeAndPreservesTopology)
{
    auto g = MakeGrid();
    std::fill(g.Soft.begin(), g.Soft.end(), 0.0);
    auto vertices = g.Mesh.VertexCount(), faces = g.Mesh.FaceCount(),
         edges = g.Mesh.EdgeCount();
    auto r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft});
    ASSERT_TRUE(r.Succeeded());
    EXPECT_EQ(r.Diagnostics.RegionCount, 1);
    EXPECT_EQ(r.Diagnostics.BoundaryCount, 0);
    EXPECT_EQ(g.Mesh.VertexCount(), vertices);
    EXPECT_EQ(g.Mesh.FaceCount(), faces);
    EXPECT_EQ(g.Mesh.EdgeCount(), edges);
}

TEST(CurvatureBoundaryPartition, UniformScalePreservesPartitionAndObjective)
{
    auto g = MakeGrid(false, true);
    auto r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft});
    ASSERT_TRUE(r.Succeeded());
    for (auto v : g.Mesh.LiveVertices())
        g.Mesh.Position(v) *= 1000.0f;
    auto scaled = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft});
    ASSERT_TRUE(scaled.Succeeded());
    EXPECT_EQ(r.FaceRegions, scaled.FaceRegions);
    EXPECT_NEAR(r.Diagnostics.FinalEnergy, scaled.Diagnostics.FinalEnergy, 1e-12);
}

TEST(CurvatureBoundaryPartition, InvalidEvidenceAndBudgetFailClosed)
{
    auto g = MakeGrid();
    g.Soft[0] = std::numeric_limits<double>::quiet_NaN();
    auto r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft});
    EXPECT_EQ(r.Diagnostics.Status, C::BoundaryPartitionStatus::InvalidEvidence);
    EXPECT_TRUE(r.FaceRegions.empty());
    g.Soft[0] = 0;
    C::BoundaryPartitionParams p;
    p.MaximumMoves = 1;
    r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft}, p);
    EXPECT_EQ(r.Diagnostics.Status, C::BoundaryPartitionStatus::WorkLimit);
    EXPECT_TRUE(r.FaceRegions.empty());
}

TEST(CurvatureBoundaryGraph, RegionalExactOracleMatchesIndependentModelFits)
{
    std::mt19937 random(146);
    for (int trial = 0; trial < 16; ++trial)
    {
        std::vector<B::Edge> edges;
        std::vector<B::Sample> samples;
        for (std::uint32_t v = 0; v < 5; ++v)
        {
            samples.push_back({0.1 + double(random() % 5) / 10,
                               {double(int(random() % 11) - 5) / 3,
                                double(int(random() % 9) - 4) / 3}});
            for (std::uint32_t w = v + 1; w < 5; ++w)
                edges.push_back(
                    {v, w, double(int(random() % 9) - 4) / 10, random() % 17 == 0});
        }
        const auto objective = [&](const Labels &labels)
        {
            double value = Cost(edges, labels);
            if (!std::isfinite(value))
                return value;
            for (std::uint32_t label = 0; label < 5; ++label)
            {
                double weight = 0;
                std::array<double, 2> mean{};
                for (std::size_t v = 0; v < 5; ++v)
                    if (labels[v] == label)
                    {
                        weight += samples[v].Weight;
                        for (int axis = 0; axis < 2; ++axis)
                            mean[axis] += samples[v].Weight * samples[v].Value[axis];
                    }
                if (weight == 0)
                    continue;
                value += 0.12;
                for (auto &x : mean)
                    x /= weight;
                for (std::size_t v = 0; v < 5; ++v)
                    if (labels[v] == label)
                        for (int axis = 0; axis < 2; ++axis)
                        {
                            double delta = samples[v].Value[axis] - mean[axis];
                            value += samples[v].Weight * delta * delta;
                        }
            }
            return value;
        };
        double minimum = std::numeric_limits<double>::infinity();
        Labels labels(5);
        for (std::uint32_t code = 0; code < 3125; ++code)
        {
            auto remaining = code;
            for (auto &label : labels)
            {
                label = remaining % 5;
                remaining /= 5;
            }
            minimum = std::min(minimum, objective(labels));
        }
        auto exact = B::Solve(5, edges, {.RegionCost = 0.12}, samples);
        ASSERT_EQ(exact.State, B::Status::Success);
        EXPECT_NEAR(exact.Energy, minimum, 1e-11);
        auto heuristic =
            B::Solve(5, edges, {.RegionCost = 0.12, .ExactNodeLimit = 0}, samples);
        ASSERT_EQ(heuristic.State, B::Status::Success) << trial;
        EXPECT_NEAR(heuristic.Energy, objective(heuristic.Labels), 1e-11);
        EXPECT_GE(heuristic.Energy, minimum - 1e-11);
    }
}

TEST(CurvatureBoundaryGraph, RegionCostCountsConnectedPieces)
{
    std::vector<B::Sample> samples(4, {1, {0, 0}});
    for (auto exact : {0u, 8u})
    {
        auto r = B::Solve(4, {}, {.RegionCost = 0.2, .ExactNodeLimit = exact}, samples);
        ASSERT_EQ(r.State, B::Status::Success);
        EXPECT_DOUBLE_EQ(r.Energy, 0.8);
        EXPECT_EQ(r.Labels, (Labels{0, 1, 2, 3}));
    }
}

TEST(CurvatureBoundaryPartition, RegionalFieldScalesAndFailsClosed)
{
    auto g = MakeGrid(false, true);
    std::vector<double> maximum(g.Mesh.VerticesSize()), minimum(g.Mesh.VerticesSize());
    for (auto v : g.Mesh.LiveVertices())
        maximum[v.Index] = g.Mesh.Position(v).x < 4 ? 1.0 : 3.0;
    C::BoundaryPartitionParams p;
    p.ModelWeight = 1;
    p.BoundaryScale = 0.02;
    p.RegionCost = 0.0012566370614359173;
    auto r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft}, p, maximum, minimum);
    ASSERT_TRUE(r.Succeeded());
    for (auto v : g.Mesh.LiveVertices())
    {
        g.Mesh.Position(v) *= 1000.0f;
        maximum[v.Index] /= 1000;
    }
    auto scaled =
        C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft}, p, maximum, minimum);
    ASSERT_TRUE(scaled.Succeeded());
    EXPECT_EQ(r.FaceRegions, scaled.FaceRegions);
    EXPECT_NEAR(r.Diagnostics.FinalEnergy, scaled.Diagnostics.FinalEnergy, 1e-11);
    maximum[0] = -1;
    auto bad =
        C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft}, p, maximum, minimum);
    EXPECT_EQ(bad.Diagnostics.Status, C::BoundaryPartitionStatus::InvalidCurvature);
    EXPECT_TRUE(bad.FaceRegions.empty());
}

TEST(CurvatureBoundaryPartition, FlowLimitReturnsNoPartialPartition)
{
    auto g = MakeGrid(false, true);
    C::BoundaryPartitionParams p;
    p.MaximumFlowEdgeVisits = 1;
    auto r = C::PartitionFeatureBoundaries(g.Mesh, {g.Hard, g.Soft}, p);
    EXPECT_EQ(r.Diagnostics.Status, C::BoundaryPartitionStatus::WorkLimit);
    EXPECT_TRUE(r.FaceRegions.empty());
}

TEST(CurvatureBoundaryGraph, RegionalMergeRefreshesEverySurvivingNeighbor)
{
    std::vector<B::Edge> edges;
    std::vector<B::Sample> samples(100, B::Sample{1, {1, 0}});
    samples[0].Value = {0, 0};
    for (std::uint32_t v = 1; v < samples.size(); ++v)
        edges.push_back({0, v, 10, false});
    const auto result = B::Solve(samples.size(), edges, {}, samples);
    ASSERT_EQ(result.State, B::Status::Success);
    EXPECT_EQ(result.Contractions, 99);
    EXPECT_EQ(*std::max_element(result.Labels.begin(), result.Labels.end()), 0);
    EXPECT_NEAR(result.Energy, 0.99, 1e-12);
}

TEST(CurvatureBoundaryGraph, RegionalWeightOverflowFailsAndTinyWeightsRetainVariance)
{
    const std::vector<B::Edge> edges{{0, 1, 0.001, false}};
    const std::vector<B::Sample> overflow{{1e308, {0, 0}}, {1e308, {1e-155, 0}}};
    const auto invalid = B::Solve(2, edges, {}, overflow);
    EXPECT_EQ(invalid.State, B::Status::InvalidInput);
    EXPECT_TRUE(invalid.Labels.empty());
    const std::vector<B::Sample> disparate{{1e-300, {1e154, 0}}, {1e300, {0, 0}}};
    const auto valid = B::Solve(2, edges, {}, disparate);
    ASSERT_EQ(valid.State, B::Status::Success);
    EXPECT_NE(valid.Labels[0], valid.Labels[1]);
    EXPECT_DOUBLE_EQ(valid.Energy, 0.001);
}

TEST(CurvatureBoundaryGraph, AreaCleanupReportsItsEnergyIncrease)
{
    const std::vector<B::Edge> edges{{0, 1, -1, false}, {1, 2, -1, false}};
    const std::vector<double> areas{0.4, 0.001, 0.001};
    B::Options options;
    options.MinimumRegionArea = 0.01;
    const auto result = B::Solve(3, edges, options, {}, areas);
    ASSERT_EQ(result.State, B::Status::Success);
    EXPECT_EQ(result.Labels, (Labels{0, 0, 0}));
    EXPECT_DOUBLE_EQ(result.OptimizedEnergy, -2);
    EXPECT_DOUBLE_EQ(result.Energy, 0);
    EXPECT_EQ(result.AreaMerges, 2);
    EXPECT_EQ(result.UnmergeableSmallRegions, 0);
    EXPECT_FALSE(result.Exact);
}

TEST(CurvatureBoundaryGraph, AreaCleanupNeverBypassesHardCutsAndRetainsIsolatedRegions)
{
    const std::vector<B::Edge> edges{
        {0, 1, -3, false}, {1, 2, -2, true}, {0, 2, -1, false}};
    const std::vector<double> areas{0.001, 0.4, 0.4, 0.001};
    B::Options options;
    options.MinimumRegionArea = 0.01;
    const auto result = B::Solve(4, edges, options, {}, areas);
    ASSERT_EQ(result.State, B::Status::Success);
    EXPECT_EQ(result.Labels[0], result.Labels[2]);
    EXPECT_NE(result.Labels[1], result.Labels[2]);
    EXPECT_NE(result.Labels[3], result.Labels[2]);
    EXPECT_EQ(result.AreaMerges, 1);
    EXPECT_EQ(result.UnmergeableSmallRegions, 1);
    const std::vector<double> allSmallAreas(4, 0.001);
    const auto blocked = B::Solve(4, edges, options, {}, allSmallAreas);
    ASSERT_EQ(blocked.State, B::Status::Success);
    EXPECT_EQ(blocked.Labels[0], blocked.Labels[2]);
    EXPECT_NE(blocked.Labels[1], blocked.Labels[2]);
    EXPECT_EQ(blocked.AreaMerges, 1);
    EXPECT_EQ(blocked.UnmergeableSmallRegions, 3);
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity()})
    {
        auto badAreas = areas;
        badAreas[0] = invalid;
        const auto failure = B::Solve(4, edges, options, {}, badAreas);
        EXPECT_EQ(failure.State, B::Status::InvalidInput);
        EXPECT_TRUE(failure.Labels.empty());
    }
}

TEST(CurvatureBoundaryPartition, SurfacePreflightFailsClosedWithoutChangingInput)
{
    auto grid = MakeGrid();
    auto check =
        [](const Geometry::HalfedgeMesh::Mesh &mesh, C::BoundaryPartitionStatus expected)
    {
        const std::vector<std::uint8_t> hard(mesh.EdgesSize());
        const std::vector<double> soft(mesh.EdgesSize());
        auto result = C::PartitionFeatureBoundaries(mesh, {hard, soft});
        EXPECT_EQ(result.Diagnostics.Status, expected);
        EXPECT_TRUE(result.FaceRegions.empty());
        EXPECT_TRUE(result.EdgeBoundaries.empty());
        EXPECT_TRUE(result.RegionAreas.empty());
    };
    Geometry::HalfedgeMesh::Mesh empty;
    check(empty, C::BoundaryPartitionStatus::EmptyMesh);
    auto nonfinite = grid.Mesh;
    nonfinite.Position(Geometry::VertexHandle{0}).x =
        std::numeric_limits<float>::infinity();
    check(nonfinite, C::BoundaryPartitionStatus::NonFinitePosition);
    auto isolated = grid.Mesh;
    isolated.AddVertex({100, 100, 100});
    check(isolated, C::BoundaryPartitionStatus::InvalidTopology);
    Geometry::HalfedgeMesh::Mesh degenerate;
    auto a = degenerate.AddVertex({0, 0, 0});
    auto b = degenerate.AddVertex({1, 0, 0});
    auto c = degenerate.AddVertex({2, 0, 0});
    ASSERT_TRUE(degenerate.AddTriangle(a, b, c));
    check(degenerate, C::BoundaryPartitionStatus::DegenerateFace);
    Geometry::HalfedgeMesh::Mesh quad;
    const std::array<Geometry::VertexHandle, 4> vertices{
        quad.AddVertex({0, 0, 0}), quad.AddVertex({1, 0, 0}), quad.AddVertex({1, 1, 0}),
        quad.AddVertex({0, 1, 0})};
    ASSERT_TRUE(quad.AddFace(vertices));
    check(quad, C::BoundaryPartitionStatus::NonTriangleFace);
    auto invalid = grid.Mesh;
    auto edge = *invalid.LiveEdges().begin();
    invalid.SetFace(invalid.Halfedge(edge, 0),
                    Geometry::FaceHandle{
                        static_cast<Geometry::PropertyIndex>(invalid.FacesSize() + 5)});
    check(invalid, C::BoundaryPartitionStatus::InvalidTopology);
}

TEST(CurvatureBoundaryPartition, ShortHardFactsDoNotSuppressAWholeSoftContour)
{
    auto grid = MakeGrid(false, false);
    for (auto edge : grid.Mesh.LiveEdges())
    {
        if (grid.Mesh.IsBoundary(edge))
            continue;
        const auto h = grid.Mesh.Halfedge(edge, 0);
        const auto a = grid.Mesh.Position(grid.Mesh.FromVertex(h));
        const auto b = grid.Mesh.Position(grid.Mesh.ToVertex(h));
        if (a.x == 4 && b.x == 4 && std::min(a.y, b.y) == 3)
            grid.Hard[edge.Index] = 1;
    }
    C::BoundaryPartitionParams params;
    params.HardFeatureExclusionRatio = 0.5;
    params.MinimumExclusionCurveLength = 0.5;
    auto result =
        C::PartitionFeatureBoundaries(grid.Mesh, {grid.Hard, grid.Soft}, params);
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Diagnostics.RegionCount, 2);
    for (auto edge : grid.Mesh.LiveEdges())
        if (grid.Hard[edge.Index])
            EXPECT_EQ(result.EdgeBoundaries[edge.Index], 1);
    params.HardFeatureExclusionRatio = 0;
    auto unattenuated =
        C::PartitionFeatureBoundaries(grid.Mesh, {grid.Hard, grid.Soft}, params);
    ASSERT_TRUE(unattenuated.Succeeded());
    EXPECT_EQ(result.FaceRegions, unattenuated.FaceRegions);
    EXPECT_DOUBLE_EQ(result.Diagnostics.FinalEnergy,
                     unattenuated.Diagnostics.FinalEnergy);
}

TEST(CurvatureBoundaryPartition, OnlyQualifyingConnectedHardCurvesSeedAttenuation)
{
    auto grid = MakeGrid();
    for (auto edge : grid.Mesh.LiveEdges())
    {
        if (grid.Mesh.IsBoundary(edge))
            continue;
        auto h = grid.Mesh.Halfedge(edge, 0);
        auto a = grid.Mesh.Position(grid.Mesh.FromVertex(h));
        auto b = grid.Mesh.Position(grid.Mesh.ToVertex(h));
        if ((a.x == 3 && b.x == 3) || (a.x == 6 && b.x == 6 && std::min(a.y, b.y) == 3))
            grid.Hard[edge.Index] = 1;
    }
    C::BoundaryPartitionParams params;
    params.HardFeatureExclusionRatio = 0.5;
    params.MinimumExclusionCurveLength = 0.5;
    const auto result =
        C::PartitionFeatureBoundaries(grid.Mesh, {grid.Hard, grid.Soft}, params);
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Diagnostics.AttenuationCurveCount, 1);
    EXPECT_EQ(result.Diagnostics.AttenuationHardEdgeCount, 8);
    for (auto edge : grid.Mesh.LiveEdges())
        if (grid.Hard[edge.Index])
            EXPECT_EQ(result.EdgeBoundaries[edge.Index], 1);
    params.HardFeatureExclusionRatio = 0;
    const auto unattenuated =
        C::PartitionFeatureBoundaries(grid.Mesh, {grid.Hard, grid.Soft}, params);
    ASSERT_TRUE(unattenuated.Succeeded());
    // Both runs start from singleton faces, so their initial-energy difference
    // isolates changed soft costs without depending on the heuristic partition.
    EXPECT_GT(result.Diagnostics.InitialEnergy, unattenuated.Diagnostics.InitialEnergy);
    EXPECT_EQ(unattenuated.Diagnostics.AttenuationCurveCount, 0);
    EXPECT_EQ(unattenuated.Diagnostics.AttenuationHardEdgeCount, 0);
    params.HardFeatureExclusionRatio = 0.5;
    params.MinimumExclusionCurveLength = 0;
    const auto all =
        C::PartitionFeatureBoundaries(grid.Mesh, {grid.Hard, grid.Soft}, params);
    ASSERT_TRUE(all.Succeeded());
    EXPECT_EQ(all.Diagnostics.AttenuationCurveCount, 2);
    EXPECT_EQ(all.Diagnostics.AttenuationHardEdgeCount, 9);
}

TEST(CurvatureBoundaryPartition, CurveCoverageV1ProfileRemainsFrozen)
{
    const auto profile = Geometry::CurvatureSegmentation::BoundaryCurveCoverageProfileV1();
    EXPECT_DOUBLE_EQ(profile.FeatureWeight, 4.0);
    EXPECT_DOUBLE_EQ(profile.FeatureExponent, 3.0);
    EXPECT_DOUBLE_EQ(profile.BoundaryScale, 0.04);
    EXPECT_DOUBLE_EQ(profile.RegionCost, std::acos(-1.0) * 0.04 * 0.04);
    EXPECT_DOUBLE_EQ(profile.MinimumRegionArea, profile.RegionCost);
    EXPECT_DOUBLE_EQ(profile.HardFeatureExclusionRatio, 0.04);
    EXPECT_DOUBLE_EQ(profile.MinimumExclusionCurveLength, 0.04);
    EXPECT_DOUBLE_EQ(profile.ModelWeight, 0.0);
    EXPECT_EQ(profile.MaximumMoves, 2000000u);
    EXPECT_EQ(profile.MaximumFlowEdgeVisits, 20000000u);
}
