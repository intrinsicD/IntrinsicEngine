#include <gtest/gtest.h>

#include <array>
#include <cstdint>
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
