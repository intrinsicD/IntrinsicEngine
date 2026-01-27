#include <gtest/gtest.h>

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
