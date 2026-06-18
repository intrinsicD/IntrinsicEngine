#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Extrinsic.Core.Memory;
import Extrinsic.Core.Error;
import Extrinsic.Core.Telemetry;

using namespace Extrinsic::Core::Memory;

namespace
{
    struct alignas(16) Aligned16
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float W = 0.0f;
    };

    struct LifecycleTracker
    {
        static inline int DestructorCount = 0;

        int Value = 0;

        LifecycleTracker() = default;
        explicit LifecycleTracker(const int value) : Value(value) {}

        ~LifecycleTracker()
        {
            ++DestructorCount;
        }
    };

    struct OrderTracker
    {
        static inline std::vector<int> DestructionOrder{};

        int Id = 0;

        OrderTracker() = default;
        explicit OrderTracker(const int id) : Id(id) {}

        ~OrderTracker()
        {
            DestructionOrder.push_back(Id);
        }
    };
}

TEST(CoreMemoryScopeStack, BasicPodAllocation)
{
    ScopeStack stack(1024);

    auto intResult = stack.New<int>(42);
    ASSERT_TRUE(intResult.has_value());
    EXPECT_EQ(**intResult, 42);

    auto floatResult = stack.New<float>(3.14f);
    ASSERT_TRUE(floatResult.has_value());
    EXPECT_FLOAT_EQ(**floatResult, 3.14f);

    EXPECT_GT(stack.Used(), 0u);
    EXPECT_EQ(stack.DestructorCount(), 0u);
}

TEST(CoreMemoryScopeStack, NonTrivialDestructorCalledOnReset)
{
    LifecycleTracker::DestructorCount = 0;

    ScopeStack stack(1024);
    auto result = stack.New<LifecycleTracker>(100);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)->Value, 100);
    EXPECT_EQ(stack.DestructorCount(), 1u);
    EXPECT_EQ(LifecycleTracker::DestructorCount, 0);

    stack.Reset();
    EXPECT_EQ(stack.DestructorCount(), 0u);
    EXPECT_EQ(stack.Used(), 0u);
    EXPECT_EQ(LifecycleTracker::DestructorCount, 1);
}

TEST(CoreMemoryScopeStack, DestructionOrderLifo)
{
    OrderTracker::DestructionOrder.clear();

    {
        ScopeStack stack(1024);
        auto first = stack.New<OrderTracker>(1);
        auto second = stack.New<OrderTracker>(2);
        auto third = stack.New<OrderTracker>(3);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
    }

    ASSERT_EQ(OrderTracker::DestructionOrder.size(), 3u);
    EXPECT_EQ(OrderTracker::DestructionOrder[0], 3);
    EXPECT_EQ(OrderTracker::DestructionOrder[1], 2);
    EXPECT_EQ(OrderTracker::DestructionOrder[2], 1);
}

TEST(CoreMemoryScopeStack, ArrayDestructionOrderIsReversed)
{
    OrderTracker::DestructionOrder.clear();

    {
        ScopeStack stack(4096);
        auto arrayResult = stack.NewArray<OrderTracker>(4);
        ASSERT_TRUE(arrayResult.has_value());

        auto values = *arrayResult;
        values[0].Id = 100;
        values[1].Id = 101;
        values[2].Id = 102;
        values[3].Id = 103;
        EXPECT_EQ(stack.DestructorCount(), 1u);
    }

    ASSERT_EQ(OrderTracker::DestructionOrder.size(), 4u);
    EXPECT_EQ(OrderTracker::DestructionOrder[0], 103);
    EXPECT_EQ(OrderTracker::DestructionOrder[1], 102);
    EXPECT_EQ(OrderTracker::DestructionOrder[2], 101);
    EXPECT_EQ(OrderTracker::DestructionOrder[3], 100);
}

TEST(CoreMemoryScopeStack, MixedPodAndNonTrivialTracksOnlyDestructors)
{
    OrderTracker::DestructionOrder.clear();

    {
        ScopeStack stack(2048);
        auto pod = stack.New<int>(1);
        auto trackedA = stack.New<OrderTracker>(1);
        auto floating = stack.New<double>(2.0);
        auto trackedB = stack.New<OrderTracker>(2);
        auto aligned = stack.New<Aligned16>();
        auto trackedC = stack.New<OrderTracker>(3);

        ASSERT_TRUE(pod.has_value());
        ASSERT_TRUE(floating.has_value());
        ASSERT_TRUE(aligned.has_value());
        ASSERT_TRUE(trackedA.has_value());
        ASSERT_TRUE(trackedB.has_value());
        ASSERT_TRUE(trackedC.has_value());
        EXPECT_EQ(stack.DestructorCount(), 3u);
    }

    ASSERT_EQ(OrderTracker::DestructionOrder.size(), 3u);
    EXPECT_EQ(OrderTracker::DestructionOrder[0], 3);
    EXPECT_EQ(OrderTracker::DestructionOrder[1], 2);
    EXPECT_EQ(OrderTracker::DestructionOrder[2], 1);
}

TEST(CoreMemoryScopeStack, SharedPtrSupport)
{
    int sharedDestructed = 0;
    auto makeObserved = [&]()
    {
        return std::shared_ptr<int>(
            new int(42),
            [&](int* value)
            {
                delete value;
                ++sharedDestructed;
            });
    };

    auto external = makeObserved();
    {
        ScopeStack stack(1024);

        auto sharedResult = stack.New<std::shared_ptr<int>>(external);
        ASSERT_TRUE(sharedResult.has_value());
        EXPECT_EQ(*(*sharedResult)->get(), 42);
        EXPECT_EQ(stack.DestructorCount(), 1u);

        external.reset();
        EXPECT_EQ(sharedDestructed, 0);
    }

    EXPECT_EQ(sharedDestructed, 1);
}

TEST(CoreMemoryScopeStack, StringSupport)
{
    ScopeStack stack(1024);

    auto result = stack.New<std::string>("Debug Pass Name: ForwardLighting");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(**result, "Debug Pass Name: ForwardLighting");
    EXPECT_EQ(stack.DestructorCount(), 1u);
}

TEST(CoreMemoryScopeStack, DirectArenaAccessForPodBypassesDestructorTracking)
{
    ScopeStack stack(1024);

    auto& arena = stack.Arena();
    auto result = arena.New<int>(999);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(**result, 999);
    EXPECT_EQ(stack.DestructorCount(), 0u);
}

TEST(CoreMemoryScopeStack, MoveTransfersDestructorListOnce)
{
    OrderTracker::DestructionOrder.clear();

    ScopeStack original(1024);
    auto result = original.New<OrderTracker>(7);
    ASSERT_TRUE(result.has_value());

    ScopeStack moved(std::move(original));
    EXPECT_EQ(moved.DestructorCount(), 1u);
    EXPECT_EQ(original.Used(), 0u);
    EXPECT_EQ(original.DestructorCount(), 0u);

    moved.Reset();
    ASSERT_EQ(OrderTracker::DestructionOrder.size(), 1u);
    EXPECT_EQ(OrderTracker::DestructionOrder[0], 7);
}

TEST(CoreMemoryLinearArena, Initialization)
{
    constexpr size_t requested = 1024;
    LinearArena arena(requested);

    EXPECT_EQ(arena.Used(), 0u);
    EXPECT_GE(arena.Capacity(), requested);
    EXPECT_NE(arena.Epoch(), 0u);
}

TEST(CoreMemoryLinearArena, BasicPrimitiveAllocation)
{
    LinearArena arena(1024);

    auto first = arena.New<int>(42);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(**first, 42);
    EXPECT_GE(arena.Used(), sizeof(int));

    auto second = arena.New<double>(3.14);
    ASSERT_TRUE(second.has_value());
    EXPECT_DOUBLE_EQ(**second, 3.14);
    EXPECT_GT(reinterpret_cast<uintptr_t>(*second), reinterpret_cast<uintptr_t>(*first));
}

TEST(CoreMemoryLinearArena, AlignmentEnforcement)
{
    LinearArena arena(1024);

    auto byteResult = arena.AllocBytes(1, 1);
    ASSERT_TRUE(byteResult.has_value());
    auto* bytePtr = byteResult->data();

    auto alignedResult = arena.New<Aligned16>();
    ASSERT_TRUE(alignedResult.has_value());
    auto* aligned = *alignedResult;

    const auto alignedAddress = reinterpret_cast<uintptr_t>(aligned);
    const auto byteAddress = reinterpret_cast<uintptr_t>(bytePtr);

    EXPECT_EQ(alignedAddress % alignof(Aligned16), 0u);
    EXPECT_GT(alignedAddress, byteAddress);
    EXPECT_EQ(arena.Used(), 16u + sizeof(Aligned16));
}

TEST(CoreMemoryLinearArena, ArrayAllocation)
{
    LinearArena arena(2048);

    auto spanResult = arena.NewArray<int>(10);
    ASSERT_TRUE(spanResult.has_value());

    std::span<int> values = *spanResult;
    ASSERT_EQ(values.size(), 10u);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<int>(i);

    EXPECT_EQ(values[9], 9);
    EXPECT_EQ(arena.Used(), values.size() * sizeof(int));
}

TEST(CoreMemoryLinearArena, FrameResetLoopReusesAddress)
{
    LinearArena arena(sizeof(int) * 2);

    auto first = arena.New<int>(100);
    ASSERT_TRUE(first.has_value());
    const auto firstAddress = reinterpret_cast<uintptr_t>(*first);
    EXPECT_EQ(**first, 100);

    arena.Reset();
    EXPECT_EQ(arena.Used(), 0u);

    auto second = arena.New<int>(200);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(reinterpret_cast<uintptr_t>(*second), firstAddress);
    EXPECT_EQ(**second, 200);
}

TEST(CoreMemoryLinearArena, OutOfMemoryAndReset)
{
    LinearArena arena(128);

    auto first = arena.AllocBytes(100);
    ASSERT_TRUE(first.has_value());

    auto second = arena.AllocBytes(100);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), Extrinsic::Core::ErrorCode::OutOfMemory);

    arena.Reset();
    auto third = arena.AllocBytes(100);
    ASSERT_TRUE(third.has_value());
}

TEST(CoreMemoryLinearArena, ResetDoesNotCallDestructors)
{
    LinearArena arena(1024);
    LifecycleTracker::DestructorCount = 0;

    auto bytes = arena.AllocBytes(sizeof(LifecycleTracker), alignof(LifecycleTracker));
    ASSERT_TRUE(bytes.has_value());
    auto* object = std::construct_at(reinterpret_cast<LifecycleTracker*>(bytes->data()), 10);

    arena.Reset();
    EXPECT_EQ(LifecycleTracker::DestructorCount, 0);

    std::destroy_at(object);
    EXPECT_EQ(LifecycleTracker::DestructorCount, 1);
}

TEST(CoreMemoryLinearArena, MarkerRewind)
{
    LinearArena arena(1024);

    auto first = arena.New<int>(1);
    ASSERT_TRUE(first.has_value());

    const auto marker = arena.Mark();
    auto second = arena.New<int>(2);
    ASSERT_TRUE(second.has_value());
    EXPECT_GT(arena.Used(), marker.Offset);

    auto rewind = arena.Rewind(marker);
    ASSERT_TRUE(rewind.has_value());
    EXPECT_EQ(arena.Used(), marker.Offset);

    auto third = arena.New<int>(3);
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(**third, 3);
}

TEST(CoreMemoryLinearArena, CrossThreadAllocationReturnsError)
{
    LinearArena arena(1024);
    auto ownerThreadAllocation = arena.New<int>(42);
    ASSERT_TRUE(ownerThreadAllocation.has_value());

    Extrinsic::Core::ErrorCode crossThreadError = Extrinsic::Core::ErrorCode::OutOfMemory;
    std::thread worker([&]()
    {
        auto result = arena.AllocBytes(16);
        if (!result.has_value())
            crossThreadError = result.error();
    });
    worker.join();

    EXPECT_EQ(crossThreadError, Extrinsic::Core::ErrorCode::ThreadViolation);
}

TEST(CoreMemoryLinearArena, MoveSameThreadPreservesUsableStorage)
{
    LinearArena arena(1024);
    const auto first = arena.New<int>(123);
    ASSERT_TRUE(first.has_value());

    LinearArena moved(std::move(arena));
    auto second = moved.New<int>(456);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(**second, 456);
    EXPECT_EQ(arena.Used(), 0u);
    EXPECT_EQ(arena.Capacity(), 0u);
}

TEST(CoreMemoryLinearArena, MoveAcrossThreadsKeepsOriginalThreadAffinity)
{
    auto arena = std::make_unique<LinearArena>(1024);

    Extrinsic::Core::ErrorCode error = Extrinsic::Core::ErrorCode::OutOfMemory;
    std::thread worker([arena = std::move(arena), &error]() mutable
    {
        LinearArena moved(std::move(*arena));
        auto result = moved.New<int>(99);
        if (!result.has_value())
            error = result.error();
    });
    worker.join();

    EXPECT_EQ(error, Extrinsic::Core::ErrorCode::ThreadViolation);
}

TEST(CoreMemoryLinearArena, EpochChangesOnMove)
{
    LinearArena source(1024);
    const auto sourceEpoch = source.Epoch();

    LinearArena moved(std::move(source));
    EXPECT_NE(moved.Epoch(), 0u);
    EXPECT_NE(moved.Epoch(), sourceEpoch);
    EXPECT_EQ(source.Used(), 0u);
    EXPECT_EQ(source.Capacity(), 0u);
}

TEST(CoreMemoryArenaAllocator, BasicAllocationWorks)
{
    LinearArena arena(4096);
    ArenaAllocator<int> allocator(arena);

    int* value = allocator.allocate(1);
    ASSERT_NE(value, nullptr);
    *value = 42;
    EXPECT_EQ(*value, 42);

    allocator.deallocate(value, 1);
}

TEST(CoreMemoryArenaAllocator, WorksWithStdVector)
{
    LinearArena arena(16 * 1024);
    ArenaAllocator<int> allocator(arena);
    std::vector<int, ArenaAllocator<int>> values(allocator);

    for (int i = 0; i < 256; ++i)
        values.push_back(i);

    ASSERT_EQ(values.size(), 256u);
    EXPECT_EQ(values.front(), 0);
    EXPECT_EQ(values.back(), 255);
}

TEST(CoreMemoryArenaAllocator, RebindWorks)
{
    LinearArena arena(4096);
    ArenaAllocator<int> intAllocator(arena);

    ArenaAllocator<double> doubleAllocator(intAllocator);
    double* value = doubleAllocator.allocate(1);
    ASSERT_NE(value, nullptr);
    *value = 3.14;
    EXPECT_DOUBLE_EQ(*value, 3.14);
}

TEST(CoreMemoryArenaAllocator, EqualityComparison)
{
    LinearArena first(1024);
    LinearArena second(1024);
    ArenaAllocator<int> firstAllocator(first);
    ArenaAllocator<int> firstAllocatorAgain(first);
    ArenaAllocator<int> secondAllocator(second);

    EXPECT_EQ(firstAllocator, firstAllocatorAgain);
    EXPECT_NE(firstAllocator, secondAllocator);
}

TEST(CoreMemoryPmr, WorksWithPmrString)
{
    LinearArena arena(4096);
    ArenaMemoryResource memoryResource(arena);
    std::pmr::string value(&memoryResource);

    value = "extrinsic-memory";
    EXPECT_EQ(value, "extrinsic-memory");
    EXPECT_GT(arena.Used(), 0u);
}

TEST(CoreMemoryTelemetry, TracksAllocations)
{
    Extrinsic::Core::Telemetry::Alloc::Reset();

    LinearArena arena(1024);
    auto first = arena.AllocBytes(64, 16);
    auto second = arena.AllocBytes(32, 16);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    EXPECT_GE(Extrinsic::Core::Telemetry::Alloc::SnapshotCount(), 2u);
    EXPECT_GE(Extrinsic::Core::Telemetry::Alloc::SnapshotBytes(), 96u);
}
