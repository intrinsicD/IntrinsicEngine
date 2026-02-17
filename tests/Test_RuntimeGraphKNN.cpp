#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

import Geometry;

TEST(RuntimeGraphKNN, ReturnsNulloptForDegenerateInputs)
{
    Geometry::Graph::Graph graph;

    std::array<glm::vec3, 0> empty{};
    EXPECT_FALSE(Geometry::Graph::BuildKNNGraph(graph, empty).has_value());

    std::array<glm::vec3, 3> points{
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{0.0F, 1.0F, 0.0F},
    };

    Geometry::Graph::KNNBuildParams params{};
    params.K = 0;
    EXPECT_FALSE(Geometry::Graph::BuildKNNGraph(graph, points, params).has_value());
}

TEST(RuntimeGraphKNN, UnionConnectivityBuildsSymmetricNeighborhoodGraph)
{
    Geometry::Graph::Graph graph;
    std::array<glm::vec3, 4> points{
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{2.0F, 0.0F, 0.0F},
        glm::vec3{3.0F, 0.0F, 0.0F},
    };

    Geometry::Graph::KNNBuildParams params{};
    params.K = 1;
    params.Connectivity = Geometry::Graph::KNNConnectivity::Union;

    const auto result = Geometry::Graph::BuildKNNGraph(graph, points, params);
    ASSERT_TRUE(result.has_value());

    // Chain topology: (0-1), (1-2), (2-3)
    EXPECT_EQ(result->VertexCount, 4u);
    EXPECT_EQ(result->InsertedEdgeCount, 3u);
    EXPECT_EQ(graph.VertexCount(), 4u);
    EXPECT_EQ(graph.EdgeCount(), 3u);
}

TEST(RuntimeGraphKNN, MutualConnectivityFiltersNonReciprocalNeighbors)
{
    Geometry::Graph::Graph graph;
    std::array<glm::vec3, 4> points{
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{0.1F, 0.0F, 0.0F},
        glm::vec3{5.0F, 0.0F, 0.0F},
        glm::vec3{10.0F, 0.0F, 0.0F},
    };

    Geometry::Graph::KNNBuildParams params{};
    params.K = 1;
    params.Connectivity = Geometry::Graph::KNNConnectivity::Mutual;

    const auto result = Geometry::Graph::BuildKNNGraph(graph, points, params);
    ASSERT_TRUE(result.has_value());

    // Only the closest pair (0,1) is reciprocal for k=1.
    EXPECT_EQ(result->InsertedEdgeCount, 1u);
    EXPECT_EQ(graph.EdgeCount(), 1u);
}

TEST(RuntimeGraphKNN, CoincidentPointsAreRejectedByDistanceEpsilon)
{
    Geometry::Graph::Graph graph;
    std::array<glm::vec3, 3> points{
        glm::vec3{1.0F, 2.0F, 3.0F},
        glm::vec3{1.0F, 2.0F, 3.0F},
        glm::vec3{2.0F, 2.0F, 3.0F},
    };

    Geometry::Graph::KNNBuildParams params{};
    params.K = 2;
    params.MinDistanceEpsilon = 1.0e-6F;

    const auto result = Geometry::Graph::BuildKNNGraph(graph, points, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->DegeneratePairCount, 0u);
    EXPECT_EQ(graph.VertexCount(), 3u);
}


TEST(RuntimeGraphKNN, BuildFromIndicesCreatesExpectedGraph)
{
    Geometry::Graph::Graph graph;

    std::array<glm::vec3, 4> points{
        glm::vec3{0.0F, 0.0F, 0.0F},
        glm::vec3{1.0F, 0.0F, 0.0F},
        glm::vec3{2.0F, 0.0F, 0.0F},
        glm::vec3{3.0F, 0.0F, 0.0F},
    };

    std::array<std::vector<std::uint32_t>, 4> indices{
        std::vector<std::uint32_t>{1},
        std::vector<std::uint32_t>{0, 2},
        std::vector<std::uint32_t>{1, 3},
        std::vector<std::uint32_t>{2},
    };

    const auto result = Geometry::Graph::BuildKNNGraphFromIndices(graph, points, indices);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->CandidateEdgeCount, 6u);
    EXPECT_EQ(result->InsertedEdgeCount, 3u);
    EXPECT_EQ(graph.EdgeCount(), 3u);
}

TEST(RuntimeGraphKNN, BuildFromIndicesRejectsInvalidAndCoincidentPairs)
{
    Geometry::Graph::Graph graph;

    std::array<glm::vec3, 3> points{
        glm::vec3{1.0F, 2.0F, 3.0F},
        glm::vec3{1.0F, 2.0F, 3.0F},
        glm::vec3{4.0F, 5.0F, 6.0F},
    };

    std::array<std::vector<std::uint32_t>, 3> indices{
        std::vector<std::uint32_t>{1, 2, 99},
        std::vector<std::uint32_t>{0},
        std::vector<std::uint32_t>{2},
    };

    Geometry::Graph::KNNFromIndicesParams params{};
    params.MinDistanceEpsilon = 1.0e-5F;

    const auto result = Geometry::Graph::BuildKNNGraphFromIndices(graph, points, indices, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->InsertedEdgeCount, 1u);
    EXPECT_GE(result->DegeneratePairCount, 3u);
    EXPECT_EQ(graph.EdgeCount(), 1u);
}


TEST(RuntimeGraphKNN, OctreeBuilderMatchesBruteForceNeighborSets)
{
    std::vector<glm::vec3> points{
        {-2.0F, 0.0F, 1.0F},
        {-1.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F},
        {1.0F, 1.0F, 0.0F},
        {2.0F, 0.0F, 1.0F},
        {0.0F, 2.0F, 2.0F},
    };

    Geometry::Graph::KNNBuildParams params{};
    params.K = 3;
    params.Connectivity = Geometry::Graph::KNNConnectivity::Mutual;

    std::vector<std::vector<std::uint32_t>> bruteForce(points.size());
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(points.size()); ++i)
    {
        std::vector<std::pair<float, std::uint32_t>> distances;
        for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(points.size()); ++j)
        {
            if (i == j) continue;
            const glm::vec3 d = points[j] - points[i];
            distances.emplace_back(glm::dot(d, d), j);
        }

        std::sort(distances.begin(), distances.end(), [](const auto& a, const auto& b)
        {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        bruteForce[i].reserve(params.K);
        for (std::size_t k = 0; k < static_cast<std::size_t>(params.K) && k < distances.size(); ++k)
        {
            bruteForce[i].push_back(distances[k].second);
        }
    }

    Geometry::Graph::Graph octreeGraph;
    const auto octreeResult = Geometry::Graph::BuildKNNGraph(octreeGraph, points, params);
    ASSERT_TRUE(octreeResult.has_value());

    Geometry::Graph::Graph referenceGraph;
    Geometry::Graph::KNNFromIndicesParams fromIndicesParams{};
    fromIndicesParams.Connectivity = params.Connectivity;
    const auto referenceResult = Geometry::Graph::BuildKNNGraphFromIndices(referenceGraph, points, bruteForce,
        fromIndicesParams);
    ASSERT_TRUE(referenceResult.has_value());

    EXPECT_EQ(octreeResult->CandidateEdgeCount, referenceResult->CandidateEdgeCount);
    EXPECT_EQ(octreeResult->InsertedEdgeCount, referenceResult->InsertedEdgeCount);
    EXPECT_EQ(octreeGraph.EdgeCount(), referenceGraph.EdgeCount());
}
