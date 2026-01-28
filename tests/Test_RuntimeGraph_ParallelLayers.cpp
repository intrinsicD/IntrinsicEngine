#include <gtest/gtest.h>

import Core;

#include <vector>

// We test Kahn layering behavior with a minimal DAG helper.
// This keeps the test Vulkan-free and verifies the expected execution layers contract.
namespace
{
    [[nodiscard]] std::vector<std::vector<uint32_t>> TopoLayers(uint32_t passCount,
                                                                const std::vector<std::pair<uint32_t, uint32_t>>& edges)
    {
        std::vector<std::vector<uint32_t>> out;
        std::vector<std::vector<uint32_t>> adj(passCount);
        std::vector<uint32_t> indeg(passCount, 0);

        for (auto [u, v] : edges)
        {
            adj[u].push_back(v);
            indeg[v]++;
        }

        std::vector<uint32_t> layer;
        for (uint32_t i = 0; i < passCount; ++i)
            if (indeg[i] == 0) layer.push_back(i);

        uint32_t processed = 0;
        while (!layer.empty())
        {
            out.push_back(layer);
            processed += (uint32_t)layer.size();

            std::vector<uint32_t> next;
            for (uint32_t u : layer)
            {
                for (uint32_t v : adj[u])
                {
                    if (--indeg[v] == 0)
                        next.push_back(v);
                }
            }

            layer = std::move(next);
        }

        EXPECT_EQ(processed, passCount);
        return out;
    }
}

TEST(RuntimeGraph, TopologicalLayersIndependentPasses)
{
    // Pass A (0) and B (1) are independent.
    // Pass C (2) depends on both.
    const auto layers = TopoLayers(/*passCount*/ 3, {{0, 2}, {1, 2}});

    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 2u);
    EXPECT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(layers[1][0], 2u);
}
