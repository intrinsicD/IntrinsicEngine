#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <glm/glm.hpp>
import Geometry;
import Geometry.PointLBVH;

namespace
{
    namespace SR = Geometry::SurfaceReconstruction;
    struct Rows
    {
        std::vector<std::uint32_t> Offsets{0}, Indices;
        Geometry::PointNeighborhoods View() const { return {Offsets, Indices}; }
    };
    Rows Query(std::span<const glm::vec3> points, std::span<const glm::vec3> queries, unsigned k,
               bool indexed)
    {
        Geometry::PointLBVH::Index index;
        if (indexed)
            EXPECT_TRUE(index.Build(points));
        Rows rows;
        for (auto q : queries)
        {
            const auto hits = indexed ? index.KNearest(q, k)
                                      : Geometry::PointLBVH::KNearestReference(points, q, k);
            for (auto hit : hits)
                rows.Indices.push_back(hit.Index);
            rows.Offsets.push_back(rows.Indices.size());
        }
        return rows;
    }
    std::vector<glm::vec3> Sphere()
    {
        std::vector<glm::vec3> points;
        for (unsigned i = 0; i < 53; ++i)
        {
            const float z = 1 - 2 * (float(i) + .5f) / 53, theta = float(i) * 2.39996323f;
            const float r = std::sqrt(1 - z * z);
            points.push_back({r * std::cos(theta), r * std::sin(theta), z});
        }
        return points;
    }
} // namespace
TEST(PointConstruction, PreparedFieldMatchesLegacyReconstructionAndCpuLbvh)
{
    const auto points = Sphere();
    SR::ReconstructionParams p;
    p.Resolution = 9;
    p.EstimateNormals = false;
    for (unsigned k : {1u, 4u})
    {
        p.KNeighbors = k;
        const auto prepared = SR::Prepare(points, points, p);
        ASSERT_TRUE(prepared);
        const auto queries = SR::GridQueries(*prepared, 0, prepared->Dimensions.VertexCount());
        const auto referenceRows = Query(points, queries, k == 1 ? 1 : k + 1, false);
        const auto indexedRows = Query(points, queries, k == 1 ? 1 : k + 1, true);
        EXPECT_EQ(referenceRows.Indices, indexedRows.Indices);
        const auto field = SR::EvaluateSignedDistances(prepared->Points, prepared->Normals, queries,
                                                       referenceRows.View(), p);
        const auto indexed = SR::EvaluateSignedDistances(prepared->Points, prepared->Normals,
                                                         queries, indexedRows.View(), p);
        ASSERT_TRUE(field);
        ASSERT_TRUE(indexed);
        EXPECT_EQ(*field, *indexed);
        const auto split = queries.size() / 2;
        auto first = SR::GridQueries(*prepared, 0, split),
             second = SR::GridQueries(*prepared, split, queries.size() - split);
        first.insert(first.end(), second.begin(), second.end());
        EXPECT_EQ(first, queries);
        auto extracted = SR::Extract(*prepared, *field);
        auto legacy = SR::Reconstruct(points, points, p);
        ASSERT_TRUE(extracted);
        ASSERT_TRUE(legacy);
        EXPECT_EQ(extracted->OutputFaceCount, legacy->OutputFaceCount);
        EXPECT_EQ(extracted->OutputVertexCount, legacy->OutputVertexCount);
        const auto actual = extracted->OutputMesh.Positions();
        const auto expected = legacy->OutputMesh.Positions();
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
            EXPECT_LE(glm::length(actual[i] - expected[i]), 1e-5f);
    }
}
TEST(PointConstruction, PlaneFieldRetainsSignedDistanceAndWeightedExtraCandidate)
{
    const std::vector<glm::vec3> points{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {2, 2, 0}};
    const std::vector<glm::vec3> normals(points.size(), {0, 0, 1});
    const std::vector<glm::vec3> queries{{.13f, .27f, 2}, {.4f, .3f, -.75f}};
    SR::ReconstructionParams p;
    p.KNeighbors = 2;
    auto rows = Query(points, queries, 3, false);
    auto field = SR::EvaluateSignedDistances(points, normals, queries, rows.View(), p);
    ASSERT_TRUE(field);
    EXPECT_NEAR((*field)[0], 2, 1e-6);
    EXPECT_NEAR((*field)[1], -.75, 1e-6);
    auto shortRows = Query(points, queries, 2, false);
    EXPECT_FALSE(SR::EvaluateSignedDistances(points, normals, queries, shortRows.View(), p));
}
TEST(PointConstruction, SuppliedFieldRejectsMalformedAndUnorderedRows)
{
    const std::vector<glm::vec3> points{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, normals(3, {0, 0, 1}),
        queries{{.1f, .2f, .3f}};
    SR::ReconstructionParams p;
    p.KNeighbors = 2;
    auto rows = Query(points, queries, 3, false);
    rows.Indices[1] = rows.Indices[0];
    EXPECT_FALSE(SR::EvaluateSignedDistances(points, normals, queries, rows.View(), p));
    rows = Query(points, queries, 3, false);
    std::swap(rows.Indices[0], rows.Indices[2]);
    EXPECT_FALSE(SR::EvaluateSignedDistances(points, normals, queries, rows.View(), p));
    rows = Query(points, queries, 3, false);
    rows.Offsets.back() = 99;
    EXPECT_FALSE(SR::EvaluateSignedDistances(points, normals, queries, rows.View(), p));
}
TEST(PointConstruction, NearestTieUsesSuppliedSourceIdentity)
{
    const std::vector<glm::vec3> points{{-1, 0, 0}, {1, 0, 0}, {0, 2, 0}},
        normals{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}}, queries{{0, 0, 0}};
    const auto rows = Query(points, queries, 1, false);
    EXPECT_EQ(rows.Indices.front(), 0u);
    const auto field = SR::EvaluateSignedDistances(points, normals, queries, rows.View());
    ASSERT_TRUE(field);
    EXPECT_FLOAT_EQ(field->front(), 1);
}
TEST(PointConstruction, PreparationPreservesFilteringAndRejectsUnsafeGrid)
{
    auto points = Sphere(), normals = points;
    points.push_back({NAN, 0, 0});
    normals.push_back({0, 0, 1});
    SR::ReconstructionParams p;
    p.EstimateNormals = false;
    auto prepared = SR::Prepare(points, normals, p);
    ASSERT_TRUE(prepared);
    EXPECT_EQ(prepared->Points.size(), 53u);
    p.MaxGridVertices = 8;
    EXPECT_FALSE(SR::Prepare(points, normals, p));
    p.MaxGridVertices = 1u << 24;
    p.BoundingBoxPadding = INFINITY;
    EXPECT_FALSE(SR::Prepare(points, normals, p));
    p.BoundingBoxPadding = .1f;
    p.Resolution = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(SR::Prepare(points, normals, p));
}
TEST(PointConstruction, GraphNeighborhoodsPreserveUnionMutualAndDegenerateFiltering)
{
    const std::vector<glm::vec3> points{
        {0, 0, 0}, {0, 0, 0}, {.11f, 0, 0}, {.7f, 0, 0}, {1.9f, 0, 0}};
    for (auto connectivity :
         {Geometry::Graph::KNNConnectivity::Union, Geometry::Graph::KNNConnectivity::Mutual})
    {
        Geometry::Graph::KNNBuildParams p;
        p.K = 2;
        p.Connectivity = connectivity;
        const auto rows = Query(points, points, 3, false), indexed = Query(points, points, 3, true);
        EXPECT_EQ(rows.Indices, indexed.Indices);
        Geometry::Graph::Graph expected, actual;
        auto a = Geometry::Graph::BuildKNNGraph(expected, points, p);
        auto b = Geometry::Graph::BuildKNNGraphFromNeighbors(actual, points, rows.View(), p);
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        EXPECT_EQ(a->InsertedEdgeCount, b->InsertedEdgeCount);
        EXPECT_EQ(a->CandidateEdgeCount, b->CandidateEdgeCount);
        EXPECT_EQ(a->DegeneratePairCount, b->DegeneratePairCount);
        for (unsigned i = 0; i < points.size(); ++i)
            for (unsigned j = i + 1; j < points.size(); ++j)
                EXPECT_EQ(expected.FindEdge(Geometry::VertexHandle{i}, Geometry::VertexHandle{j})
                              .has_value(),
                          actual.FindEdge(Geometry::VertexHandle{i}, Geometry::VertexHandle{j})
                              .has_value());
    }
}
TEST(PointConstruction, MalformedGraphRowsDoNotDestroyExistingGraph)
{
    const std::vector<glm::vec3> points{{0, 0, 0}, {1, 0, 0}, {3, 0, 0}};
    Geometry::Graph::Graph graph;
    static_cast<void>(graph.AddVertex({77, 0, 0}));
    Geometry::Graph::KNNBuildParams p;
    p.K = 1;
    auto rows = Query(points, points, 2, false);
    rows.Indices.back() = 99;
    EXPECT_FALSE(Geometry::Graph::BuildKNNGraphFromNeighbors(graph, points, rows.View(), p));
    EXPECT_EQ(graph.VertexCount(), 1u);
    EXPECT_EQ(graph.VertexPosition(Geometry::VertexHandle{0}).x, 77);
}
