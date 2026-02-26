#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

import Core;

using namespace Core;

namespace
{
    constexpr uint32_t kNodeCount = 2000;
    constexpr int kWarmupFrames = 6;
    constexpr int kMeasuredFrames = 40;

    uint64_t Percentile(std::vector<uint64_t> values, double p)
    {
        if (values.empty()) return 0;
        std::sort(values.begin(), values.end());
        const size_t idx = static_cast<size_t>(std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1));
        return values[idx];
    }
}

TEST(ArchitectureSLO, FrameGraphP95P99BudgetsAt2000Nodes)
{
    Memory::ScopeStack scope(1024 * 512);
    FrameGraph graph(scope);

    for (uint32_t i = 0; i < kNodeCount; ++i)
    {
        graph.AddPass("NoOp",
            [](FrameGraphBuilder& b)
            {
                b.Read<uint32_t>();
            },
            []() {});
    }

    Tasks::Scheduler::Initialize(8);

    std::vector<uint64_t> compileNs;
    std::vector<uint64_t> executeNs;
    std::vector<uint64_t> criticalNs;
    compileNs.reserve(kMeasuredFrames);
    executeNs.reserve(kMeasuredFrames);
    criticalNs.reserve(kMeasuredFrames);

    for (int frame = 0; frame < (kWarmupFrames + kMeasuredFrames); ++frame)
    {
        auto compile = graph.Compile();
        ASSERT_TRUE(compile.has_value());

        graph.Execute();

        if (frame >= kWarmupFrames)
        {
            compileNs.push_back(graph.GetLastCompileTimeNs());
            executeNs.push_back(graph.GetLastExecuteTimeNs());
            criticalNs.push_back(graph.GetLastCriticalPathTimeNs());
        }

        graph.Reset();
        scope.Reset();

        for (uint32_t i = 0; i < kNodeCount; ++i)
        {
            graph.AddPass("NoOp",
                [](FrameGraphBuilder& b)
                {
                    b.Read<uint32_t>();
                },
                []() {});
        }
    }

    Tasks::Scheduler::Shutdown();

    const uint64_t compileP99 = Percentile(compileNs, 0.99);
    const uint64_t executeP95 = Percentile(executeNs, 0.95);
    const uint64_t criticalP95 = Percentile(criticalNs, 0.95);

    constexpr uint64_t kCompileP99BudgetNs = 350'000;
    constexpr uint64_t kExecuteP95BudgetNs = 1'500'000;
    constexpr uint64_t kCriticalP95BudgetNs = 900'000;

    EXPECT_LT(compileP99, kCompileP99BudgetNs);
    EXPECT_LT(executeP95, kExecuteP95BudgetNs);
    EXPECT_LT(criticalP95, kCriticalP95BudgetNs);
}
