#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Core.ResourcePool;
import Extrinsic.Core.StrongHandle;

namespace
{
    struct ResourceTag;
    using Handle = Extrinsic::Core::StrongHandle<ResourceTag>;

    struct TrackedResource
    {
        int Value = 0;
        std::string Name;

        TrackedResource() = default;
        TrackedResource(int value, std::string name) : Value(value), Name(std::move(name)) {}
    };

    using ImmediatePool = Extrinsic::Core::ResourcePool<TrackedResource, Handle, 0>;
    using DefaultPool = Extrinsic::Core::ResourcePool<TrackedResource, Handle>;
}

TEST(CoreResourcePool, AddAndGetRoundTripsResource)
{
    ImmediatePool pool;

    const auto handle = pool.Add(TrackedResource{42, "answer"});
    ASSERT_TRUE(handle.IsValid());

    const auto result = pool.Get(handle);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)->Value, 42);
    EXPECT_EQ((*result)->Name, "answer");
}

TEST(CoreResourcePool, EmplaceForwardsConstructorArguments)
{
    ImmediatePool pool;

    const auto handle = pool.Emplace(7, "seven");
    ASSERT_TRUE(handle.IsValid());

    const auto result = pool.Get(handle);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)->Value, 7);
    EXPECT_EQ((*result)->Name, "seven");
}

TEST(CoreResourcePool, GetByIndexReturnsActiveResourceWithoutGenerationCheck)
{
    ImmediatePool pool;

    const auto handle = pool.Emplace(3, "bulk-upload");
    ASSERT_TRUE(handle.IsValid());

    const auto* raw = pool.GetByIndex(handle.Index);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->Value, 3);
    EXPECT_EQ(raw->Name, "bulk-upload");
}

TEST(CoreResourcePool, RemoveInvalidatesHandleImmediately)
{
    ImmediatePool pool;

    const auto handle = pool.Emplace(11, "removed");
    ASSERT_TRUE(pool.Get(handle).has_value());

    pool.Remove(handle, 7);

    const auto result = pool.Get(handle);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::ResourceNotFound);
    EXPECT_EQ(pool.GetIfValid(handle), nullptr);
    EXPECT_EQ(pool.GetByIndex(handle.Index), nullptr);
}

TEST(CoreResourcePool, ReusedSlotRejectsStaleHandleAndAdvancesGeneration)
{
    ImmediatePool pool;

    const auto oldHandle = pool.Emplace(1, "old");
    ASSERT_TRUE(oldHandle.IsValid());
    const auto reusedIndex = oldHandle.Index;

    pool.Remove(oldHandle, 10);
    pool.ProcessDeletions(11);

    const auto newHandle = pool.Emplace(2, "new");
    ASSERT_TRUE(newHandle.IsValid());
    EXPECT_EQ(newHandle.Index, reusedIndex);
    EXPECT_NE(newHandle.Generation, oldHandle.Generation);

    const auto stale = pool.Get(oldHandle);
    EXPECT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error(), Extrinsic::Core::ErrorCode::ResourceNotFound);
    EXPECT_EQ(pool.GetIfValid(oldHandle), nullptr);

    const auto* raw = pool.GetByIndex(reusedIndex);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->Value, 2);
    EXPECT_EQ(raw->Name, "new");
}

TEST(CoreResourcePool, DefaultRetirementFramesHonorsBoundary)
{
    DefaultPool pool;

    const auto oldHandle = pool.Emplace(5, "pending");
    const auto retiredIndex = oldHandle.Index;

    pool.Remove(oldHandle, 10);

    pool.ProcessDeletions(12);
    const auto blocked = pool.Emplace(99, "blocked");
    EXPECT_NE(blocked.Index, retiredIndex);

    pool.ProcessDeletions(13);
    const auto reused = pool.Emplace(100, "reused");
    EXPECT_EQ(reused.Index, retiredIndex);
}

TEST(CoreResourcePool, CapacityTracksAllocatedSlotsAndClearResetsIt)
{
    ImmediatePool pool;

    EXPECT_EQ(pool.Capacity(), 0u);
    const auto first = pool.Emplace(1, "one");
    const auto second = pool.Emplace(2, "two");
    EXPECT_EQ(pool.Capacity(), 2u);

    pool.Clear();
    EXPECT_EQ(pool.Capacity(), 0u);
    EXPECT_FALSE(pool.Get(first).has_value());
    EXPECT_FALSE(pool.Get(second).has_value());
}

TEST(CoreResourcePool, InvalidHandlesReturnResourceNotFound)
{
    ImmediatePool pool;

    const Handle invalid{};
    const auto invalidResult = pool.Get(invalid);
    EXPECT_FALSE(invalidResult.has_value());
    EXPECT_EQ(invalidResult.error(), Extrinsic::Core::ErrorCode::ResourceNotFound);
    EXPECT_EQ(pool.GetIfValid(invalid), nullptr);
    EXPECT_EQ(pool.GetByIndex(invalid.Index), nullptr);

    const Handle outOfRange{999u, 1u};
    const auto outOfRangeResult = pool.Get(outOfRange);
    EXPECT_FALSE(outOfRangeResult.has_value());
    EXPECT_EQ(outOfRangeResult.error(), Extrinsic::Core::ErrorCode::ResourceNotFound);
    EXPECT_EQ(pool.GetIfValid(outOfRange), nullptr);
}

TEST(CoreResourcePool, ConcurrentReadsAreSafe)
{
    ImmediatePool pool;
    std::vector<Handle> handles;
    handles.reserve(64);
    for (int i = 0; i < 64; ++i)
        handles.push_back(pool.Emplace(i, "bulk"));

    std::atomic<bool> allVisible{true};
    std::vector<std::thread> threads;
    threads.reserve(4);

    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&]()
        {
            for (int iter = 0; iter < 200; ++iter)
            {
                for (const auto& handle : handles)
                {
                    if (pool.GetIfValid(handle) == nullptr)
                    {
                        allVisible.store(false, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_TRUE(allVisible.load(std::memory_order_relaxed));
}


