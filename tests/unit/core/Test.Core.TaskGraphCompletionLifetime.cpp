#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Tasks;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Dag;

namespace
{
    class SchedulerFixture
    {
    public:
        explicit SchedulerFixture(const unsigned workerCount)
        {
            Tasks::Scheduler::Initialize(workerCount);
        }

        ~SchedulerFixture()
        {
            Tasks::Scheduler::WaitForAll();
            Tasks::Scheduler::Shutdown();
        }

        SchedulerFixture(const SchedulerFixture&) = delete;
        SchedulerFixture& operator=(const SchedulerFixture&) = delete;
    };
}

TEST(CoreTaskGraphCompletionLifetime, ExecuteKeepsCompletionStateAliveUntilWorkerClosuresRetire)
{
    SchedulerFixture scheduler{4};

    constexpr std::uint32_t kEpochs = 300u;
    constexpr std::uint32_t kPasses = 24u;
    std::atomic<std::uint32_t> executed{0u};

    for (std::uint32_t epoch = 0u; epoch < kEpochs; ++epoch)
    {
        TaskGraph graph;
        for (std::uint32_t pass = 0u; pass < kPasses; ++pass)
        {
            graph.AddPass(std::string("RacePass") + std::to_string(epoch) + "_" + std::to_string(pass),
                          [](TaskGraphBuilder&) {},
                          [&executed]()
                          {
                              executed.fetch_add(1u, std::memory_order_acq_rel);
                              std::this_thread::yield();
                          });
        }

        ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed at epoch " << epoch;
        ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed at epoch " << epoch;
    }

    EXPECT_EQ(executed.load(std::memory_order_acquire), kEpochs * kPasses);
}
