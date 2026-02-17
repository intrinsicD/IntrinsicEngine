#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

import Geometry;

TEST(RuntimeGraph, AddEdge_FindEdge)
{
    Geometry::Graph::Graph g;

    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({0.0f, 1.0f, 0.0f});

    ASSERT_TRUE(v0.IsValid());
    ASSERT_TRUE(v1.IsValid());
    ASSERT_TRUE(v2.IsValid());

    auto e01 = g.AddEdge(v0, v1);
    ASSERT_TRUE(e01.has_value());

    // Duplicate should be rejected (both orientations).
    EXPECT_FALSE(g.AddEdge(v0, v1).has_value());
    EXPECT_FALSE(g.AddEdge(v1, v0).has_value());

    auto he = g.FindHalfedge(v0, v1);
    ASSERT_TRUE(he.has_value());
    EXPECT_EQ(g.ToVertex(*he), v1);

    auto e = g.FindEdge(v0, v1);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(*e, *e01);

    EXPECT_FALSE(g.FindEdge(v1, v2).has_value());
}

TEST(RuntimeGraph, DeleteVertex_ThenGarbageCollect)
{
    Geometry::Graph::Graph g;

    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({0.0f, 1.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v1, v2).has_value());

    EXPECT_EQ(g.VertexCount(), 3u);
    EXPECT_EQ(g.EdgeCount(), 2u);

    g.DeleteVertex(v1);
    EXPECT_TRUE(g.HasGarbage());

    g.GarbageCollection();
    EXPECT_FALSE(g.HasGarbage());

    // v1 is removed, and both incident edges are deleted.
    EXPECT_EQ(g.VertexCount(), 2u);
    EXPECT_EQ(g.EdgeCount(), 0u);
}


TEST(RuntimeGraph, ForceDirectedLayoutRejectsDegenerateInputs)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());

    std::vector<glm::vec2> positions(1, glm::vec2(0.0f));
    EXPECT_FALSE(Geometry::Graph::ComputeForceDirectedLayout(g, positions).has_value());

    Geometry::Graph::ForceDirectedLayoutParams params{};
    params.MaxIterations = 0;
    positions.resize(g.VerticesSize(), glm::vec2(0.0f));
    EXPECT_FALSE(Geometry::Graph::ComputeForceDirectedLayout(g, positions, params).has_value());
}

TEST(RuntimeGraph, ForceDirectedLayoutProducesFiniteSeparatedEmbedding)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({2.0f, 0.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v1, v2).has_value());

    std::vector<glm::vec2> positions(g.VerticesSize(), glm::vec2(0.0f));

    Geometry::Graph::ForceDirectedLayoutParams params{};
    params.MaxIterations = 96;
    params.CoolingFactor = 0.92f;

    const auto result = Geometry::Graph::ComputeForceDirectedLayout(g, positions, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ActiveVertexCount, 3u);
    EXPECT_EQ(result->ActiveEdgeCount, 2u);
    EXPECT_GT(result->IterationsPerformed, 0u);

    for (const glm::vec2& p : positions)
    {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
    }

    const float d01 = glm::length(positions[v0.Index] - positions[v1.Index]);
    const float d12 = glm::length(positions[v1.Index] - positions[v2.Index]);
    EXPECT_GT(d01, 1.0e-3f);
    EXPECT_GT(d12, 1.0e-3f);
}


TEST(RuntimeGraph, SpectralLayoutRejectsDegenerateInputs)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());

    std::vector<glm::vec2> positions(1, glm::vec2(0.0f));
    EXPECT_FALSE(Geometry::Graph::ComputeSpectralLayout(g, positions).has_value());

    Geometry::Graph::SpectralLayoutParams params{};
    params.MaxIterations = 0;
    positions.resize(g.VerticesSize(), glm::vec2(0.0f));
    EXPECT_FALSE(Geometry::Graph::ComputeSpectralLayout(g, positions, params).has_value());
}

TEST(RuntimeGraph, SpectralLayoutProducesFiniteCenteredEmbedding)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({2.0f, 0.0f, 0.0f});
    auto v3 = g.AddVertex({3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v1, v2).has_value());
    ASSERT_TRUE(g.AddEdge(v2, v3).has_value());

    std::vector<glm::vec2> positions(g.VerticesSize(), glm::vec2(0.0f));

    Geometry::Graph::SpectralLayoutParams params{};
    params.MaxIterations = 120;
    params.ConvergenceTolerance = 1.0e-6f;

    const auto result = Geometry::Graph::ComputeSpectralLayout(g, positions, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ActiveVertexCount, 4u);
    EXPECT_EQ(result->ActiveEdgeCount, 3u);
    EXPECT_GT(result->IterationsPerformed, 0u);

    glm::vec2 centroid(0.0f);
    for (const glm::vec2& p : positions)
    {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
        centroid += p;
    }
    centroid /= static_cast<float>(positions.size());

    EXPECT_NEAR(centroid.x, 0.0f, 1.0e-3f);
    EXPECT_NEAR(centroid.y, 0.0f, 1.0e-3f);

    const float d01 = glm::length(positions[v0.Index] - positions[v1.Index]);
    const float d12 = glm::length(positions[v1.Index] - positions[v2.Index]);
    const float d23 = glm::length(positions[v2.Index] - positions[v3.Index]);
    EXPECT_GT(d01 + d12 + d23, 1.0e-2f);
}


TEST(RuntimeGraph, SpectralLayoutNormalizedVariantProducesFiniteEmbedding)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({2.0f, 0.0f, 0.0f});
    auto v3 = g.AddVertex({3.0f, 0.0f, 0.0f});
    auto v4 = g.AddVertex({4.0f, 0.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v0, v2).has_value());
    ASSERT_TRUE(g.AddEdge(v0, v3).has_value());
    ASSERT_TRUE(g.AddEdge(v0, v4).has_value());

    std::vector<glm::vec2> positions(g.VerticesSize(), glm::vec2(0.0f));

    Geometry::Graph::SpectralLayoutParams params{};
    params.MaxIterations = 140;
    params.StepScale = 0.7f;
    params.ConvergenceTolerance = 1.0e-6f;
    params.Variant = Geometry::Graph::SpectralLayoutParams::LaplacianVariant::NormalizedSymmetric;

    const auto result = Geometry::Graph::ComputeSpectralLayout(g, positions, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ActiveVertexCount, 5u);
    EXPECT_EQ(result->ActiveEdgeCount, 4u);

    for (const glm::vec2& p : positions)
    {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
    }

    const float centerToLeaf0 = glm::length(positions[v0.Index] - positions[v1.Index]);
    const float centerToLeaf1 = glm::length(positions[v0.Index] - positions[v2.Index]);
    EXPECT_GT(centerToLeaf0 + centerToLeaf1, 1.0e-2f);
}

TEST(RuntimeGraph, HierarchicalLayoutRejectsDegenerateInputs)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());

    std::vector<glm::vec2> positions(1, glm::vec2(0.0f));
    EXPECT_FALSE(Geometry::Graph::ComputeHierarchicalLayout(g, positions).has_value());

    positions.resize(g.VerticesSize(), glm::vec2(0.0f));
    Geometry::Graph::HierarchicalLayoutParams params{};
    params.LayerSpacing = 0.0f;
    EXPECT_FALSE(Geometry::Graph::ComputeHierarchicalLayout(g, positions, params).has_value());
}

TEST(RuntimeGraph, HierarchicalLayoutProducesLayeredEmbedding)
{
    Geometry::Graph::Graph g;
    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({2.0f, 0.0f, 0.0f});
    auto v3 = g.AddVertex({3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v0, v2).has_value());
    ASSERT_TRUE(g.AddEdge(v1, v3).has_value());

    std::vector<glm::vec2> positions(g.VerticesSize(), glm::vec2(0.0f));
    Geometry::Graph::HierarchicalLayoutParams params{};
    params.RootVertexIndex = v0.Index;
    params.LayerSpacing = 2.0f;
    params.NodeSpacing = 1.5f;

    const auto result = Geometry::Graph::ComputeHierarchicalLayout(g, positions, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ActiveVertexCount, 4u);
    EXPECT_EQ(result->ActiveEdgeCount, 3u);
    EXPECT_EQ(result->ComponentCount, 1u);
    EXPECT_EQ(result->LayerCount, 3u);
    EXPECT_EQ(result->MaxLayerWidth, 2u);

    for (const auto& p : positions)
    {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
    }

    EXPECT_NEAR(positions[v0.Index].y, 0.0f, 1.0e-4f);
    EXPECT_NEAR(positions[v1.Index].y, -2.0f, 1.0e-4f);
    EXPECT_NEAR(positions[v2.Index].y, -2.0f, 1.0e-4f);
    EXPECT_NEAR(positions[v3.Index].y, -4.0f, 1.0e-4f);

    EXPECT_GT(std::abs(positions[v1.Index].x - positions[v2.Index].x), 1.0e-4f);
}


TEST(RuntimeGraph, HierarchicalLayoutCrossingMinimizationReducesCrossings)
{
    Geometry::Graph::Graph g;
    auto r = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto a = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto b = g.AddVertex({2.0f, 0.0f, 0.0f});
    auto c = g.AddVertex({3.0f, 0.0f, 0.0f});
    auto d = g.AddVertex({4.0f, 0.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(r, a).has_value());
    ASSERT_TRUE(g.AddEdge(r, b).has_value());
    ASSERT_TRUE(g.AddEdge(a, d).has_value());
    ASSERT_TRUE(g.AddEdge(b, c).has_value());

    std::vector<glm::vec2> baseline(g.VerticesSize(), glm::vec2(0.0f));
    std::vector<glm::vec2> improved(g.VerticesSize(), glm::vec2(0.0f));

    Geometry::Graph::HierarchicalLayoutParams noSweep{};
    noSweep.RootVertexIndex = r.Index;
    noSweep.CrossingMinimizationSweeps = 0;

    Geometry::Graph::HierarchicalLayoutParams sweep = noSweep;
    sweep.CrossingMinimizationSweeps = 8;

    const auto noSweepResult = Geometry::Graph::ComputeHierarchicalLayout(g, baseline, noSweep);
    const auto sweepResult = Geometry::Graph::ComputeHierarchicalLayout(g, improved, sweep);

    ASSERT_TRUE(noSweepResult.has_value());
    ASSERT_TRUE(sweepResult.has_value());

    EXPECT_GE(noSweepResult->CrossingCount, 1u);
    EXPECT_LE(sweepResult->CrossingCount, noSweepResult->CrossingCount);
}

TEST(RuntimeGraph, HierarchicalLayoutAutoRootCentersLongPath)
{
    Geometry::Graph::Graph g;
    std::array<Geometry::VertexHandle, 7> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i] = g.AddVertex({static_cast<float>(i), 0.0f, 0.0f});
    }

    for (std::size_t i = 0; i + 1 < vertices.size(); ++i)
    {
        ASSERT_TRUE(g.AddEdge(vertices[i], vertices[i + 1]).has_value());
    }

    std::vector<glm::vec2> positions(g.VerticesSize(), glm::vec2(0.0f));
    Geometry::Graph::HierarchicalLayoutParams params{};
    params.RootVertexIndex = Geometry::kInvalidIndex;
    params.LayerSpacing = 1.0f;
    params.NodeSpacing = 1.0f;

    const auto result = Geometry::Graph::ComputeHierarchicalLayout(g, positions, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->LayerCount, 4u);

    const std::uint32_t centerIndex = vertices[vertices.size() / 2].Index;
    EXPECT_NEAR(positions[centerIndex].y, 0.0f, 1.0e-4f);
    EXPECT_NEAR(positions[vertices.front().Index].y, -3.0f, 1.0e-4f);
    EXPECT_NEAR(positions[vertices.back().Index].y, -3.0f, 1.0e-4f);
}
