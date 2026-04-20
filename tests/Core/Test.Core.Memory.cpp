#include <gtest/gtest.h>

#include <memory>
#include <memory_resource>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Memory;

using namespace Extrinsic::Core::Memory;

namespace
{
    struct alignas(16) Aligned16
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
        uint32_t w = 0;
    };

    struct LifecycleTracker
    {
        static int DestructCount;
        int Value = 0;
        explicit LifecycleTracker(int v = 0) : Value(v) {}
        ~LifecycleTracker() { ++DestructCount; }
    };

    int LifecycleTracker::DestructCount = 0;
}

TEST(CoreMemoryLinearArena, BasicAndAlignment)
{
    LinearArena arena(1024);

    auto oneByte = arena.AllocBytes(1, 1);
    ASSERT_TRUE(oneByte.has_value());

    auto aligned = arena.New<Aligned16>();
    ASSERT_TRUE(aligned.has_value());

    const auto addr = reinterpret_cast<uintptr_t>(*aligned);
    EXPECT_EQ(addr % 16u, 0u);
    EXPECT_GE(arena.Used(), sizeof(Aligned16));
}

TEST(CoreMemoryLinearArena, MarkerRewind)
{
    LinearArena arena(1024);

    auto a = arena.New<int>(1);
    ASSERT_TRUE(a.has_value());

    auto marker = arena.Mark();

    auto b = arena.New<int>(2);
    ASSERT_TRUE(b.has_value());
    EXPECT_GT(arena.Used(), marker.Offset);

    auto rewind = arena.Rewind(marker);
    ASSERT_TRUE(rewind.has_value());
    EXPECT_EQ(arena.Used(), marker.Offset);

    auto c = arena.New<int>(3);
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(**c, 3);
}

TEST(CoreMemoryLinearArena, CrossThreadViolation)
{
    LinearArena arena(512);
    auto ok = arena.New<int>(42);
    ASSERT_TRUE(ok.has_value());

    AllocError err = AllocError::OutOfMemory;
    std::thread worker([&]()
    {
        auto result = arena.AllocBytes(8, alignof(uint64_t));
        if (!result)
            err = result.error();
    });
    worker.join();

    EXPECT_EQ(err, AllocError::ThreadViolation);
}

TEST(CoreMemoryScopeStack, NonTrivialDestructorsOnReset)
{
    LifecycleTracker::DestructCount = 0;

    {
        ScopeStack scope(4096);
        auto p1 = scope.New<LifecycleTracker>(10);
        auto p2 = scope.New<LifecycleTracker>(20);
        ASSERT_TRUE(p1.has_value());
        ASSERT_TRUE(p2.has_value());
        EXPECT_EQ(scope.DestructorCount(), 2u);

        scope.Reset();
        EXPECT_EQ(scope.DestructorCount(), 0u);
        EXPECT_EQ(LifecycleTracker::DestructCount, 2);
    }

    EXPECT_EQ(LifecycleTracker::DestructCount, 2);
}

TEST(CoreMemoryArenaAllocator, WorksWithStdVector)
{
    LinearArena arena(16 * 1024);
    ArenaAllocator<int> alloc(arena);
    std::vector<int, ArenaAllocator<int>> data(alloc);

    for (int i = 0; i < 256; ++i)
        data.push_back(i);

    ASSERT_EQ(data.size(), 256u);
    EXPECT_EQ(data.front(), 0);
    EXPECT_EQ(data.back(), 255);
}

TEST(CoreMemoryPmr, WorksWithPmrString)
{
    LinearArena arena(4096);
    ArenaMemoryResource mr(arena);
    std::pmr::string s(&mr);
    s = "extrinsic-memory";

    EXPECT_EQ(s, "extrinsic-memory");
    EXPECT_GT(arena.Used(), 0u);
}


TEST(CoreMemoryTelemetry, TracksAllocations)
{
    Telemetry::Reset();

    LinearArena arena(1024);
    auto a = arena.AllocBytes(64, 16);
    auto b = arena.AllocBytes(32, 16);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());

    const auto snap = Telemetry::Snapshot();
    EXPECT_GE(snap.AllocCount, 2u);
    EXPECT_GE(snap.AllocBytes, 96u);
}
