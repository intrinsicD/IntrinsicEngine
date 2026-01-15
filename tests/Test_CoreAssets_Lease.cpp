#include <gtest/gtest.h>
#include <thread>
#include <chrono>

import Core;

using namespace Core;
using namespace Core::Assets;

TEST(AssetSystem, Pin_LeaseBasic)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("value", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(7);
    });

    Core::Tasks::Scheduler::WaitForAll();

    auto leaseResult = manager.Pin<int>(handle);
    ASSERT_TRUE(leaseResult.has_value());

    auto lease = *leaseResult;
    ASSERT_TRUE((bool)lease);
    ASSERT_NE(lease.get(), nullptr);
    EXPECT_EQ(*lease, 7);
    EXPECT_EQ(*lease.get(), 7);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_RespectsProcessingGate)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("test", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(1);
    });

    Core::Tasks::Scheduler::WaitForAll();

    manager.MoveToProcessing(handle);
    EXPECT_EQ(manager.GetState(handle), LoadState::Processing);

    auto lease = manager.Pin<int>(handle);
    EXPECT_FALSE(lease.has_value());
    EXPECT_EQ(lease.error(), ErrorCode::AssetNotLoaded);

    Core::Tasks::Scheduler::Shutdown();
}

TEST(AssetSystem, Pin_TypeMismatch)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto handle = manager.Load<int>("number", [](const std::string&, AssetHandle)
    {
        return std::make_shared<int>(123);
    });

    Core::Tasks::Scheduler::WaitForAll();

    auto mismatch = manager.Pin<float>(handle);
    EXPECT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error(), ErrorCode::AssetTypeMismatch);

    Core::Tasks::Scheduler::Shutdown();
}

