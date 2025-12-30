#include <gtest/gtest.h>
#include <atomic>
import Core.Tasks;
import Core;

using namespace Core::Tasks;

TEST(CoreTasks, BasicDispatch) {
    Scheduler::Initialize(2);
    
    std::atomic<int> counter = 0;
    
    // Dispatch 100 tasks
    for(int i = 0; i < 100; ++i) {
        Scheduler::Dispatch([&counter]() {
            counter++;
        });
    }
    
    Scheduler::WaitForAll();
    
    EXPECT_EQ(counter, 100);
    
    Scheduler::Shutdown();
}