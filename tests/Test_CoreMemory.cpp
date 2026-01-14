#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <span>
#include <memory>

import Core;

using namespace Core::Memory;

// Helper struct to test alignment
struct alignas(16) AlignedStruct16
{
    float x, y, z, w;
};

// Helper to track object lifetime
struct LifecycleTracker
{
    static int s_DestructorCount;
    int data = 0;

    LifecycleTracker() = default;
    explicit LifecycleTracker(int val) : data(val)
    {
    }

    ~LifecycleTracker() { s_DestructorCount++; }
};

int LifecycleTracker::s_DestructorCount = 0;

// Helper to track destruction order (for ScopeStack LIFO verification)
struct OrderTracker
{
    static std::vector<int> s_DestructionOrder;
    int id = 0;

    OrderTracker() = default;
    explicit OrderTracker(int id) : id(id) {}
    ~OrderTracker() { s_DestructionOrder.push_back(id); }
};

std::vector<int> OrderTracker::s_DestructionOrder;

// -----------------------------------------------------------------------------
// ScopeStack Tests
// -----------------------------------------------------------------------------

TEST(ScopeStack, BasicPODAllocation)
{
    ScopeStack stack(1024);

    // Allocate POD types (should work like LinearArena)
    auto intResult = stack.New<int>(42);
    ASSERT_TRUE(intResult.has_value());
    EXPECT_EQ(**intResult, 42);

    auto floatResult = stack.New<float>(3.14f);
    ASSERT_TRUE(floatResult.has_value());
    EXPECT_FLOAT_EQ(**floatResult, 3.14f);

    EXPECT_GT(stack.GetUsed(), 0u);
    EXPECT_EQ(stack.GetDestructorCount(), 0u); // POD types don't track destructors
}

TEST(ScopeStack, NonTrivialDestructorCalled)
{
    LifecycleTracker::s_DestructorCount = 0;

    {
        ScopeStack stack(1024);

        auto result = stack.New<LifecycleTracker>(100);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ((*result)->data, 100);
        EXPECT_EQ(stack.GetDestructorCount(), 1u);

        // Destructor not called yet
        EXPECT_EQ(LifecycleTracker::s_DestructorCount, 0);
    }
    // Stack destructor calls Reset() which invokes destructors
    EXPECT_EQ(LifecycleTracker::s_DestructorCount, 1);
}

TEST(ScopeStack, DestructionOrderLIFO)
{
    OrderTracker::s_DestructionOrder.clear();

    {
        ScopeStack stack(1024);

        // Allocate objects in order: 1, 2, 3
        auto r1 = stack.New<OrderTracker>(1);
        auto r2 = stack.New<OrderTracker>(2);
        auto r3 = stack.New<OrderTracker>(3);

        ASSERT_TRUE(r1.has_value() && r2.has_value() && r3.has_value());
    }

    // Should be destroyed in LIFO order: 3, 2, 1
    ASSERT_EQ(OrderTracker::s_DestructionOrder.size(), 3u);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[0], 3);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[1], 2);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[2], 1);
}

TEST(ScopeStack, ExplicitReset)
{
    OrderTracker::s_DestructionOrder.clear();

    ScopeStack stack(1024);

    auto r1 = stack.New<OrderTracker>(10);
    auto r2 = stack.New<OrderTracker>(20);
    ASSERT_TRUE(r1.has_value() && r2.has_value());

    EXPECT_EQ(stack.GetDestructorCount(), 2u);
    EXPECT_GT(stack.GetUsed(), 0u);

    stack.Reset();

    // After reset: destructors called, memory reused
    EXPECT_EQ(stack.GetUsed(), 0u);
    EXPECT_EQ(stack.GetDestructorCount(), 0u);

    // LIFO order: 20, 10
    ASSERT_EQ(OrderTracker::s_DestructionOrder.size(), 2u);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[0], 20);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[1], 10);
}

TEST(ScopeStack, NewArrayWithDestructors)
{
    LifecycleTracker::s_DestructorCount = 0;

    {
        ScopeStack stack(4096);

        // Allocate array of non-trivially destructible objects
        auto arrResult = stack.NewArray<LifecycleTracker>(5);
        ASSERT_TRUE(arrResult.has_value());

        auto arr = *arrResult;
        EXPECT_EQ(arr.size(), 5u);

        // Array counts as a single destructor entry
        EXPECT_EQ(stack.GetDestructorCount(), 1u);
        EXPECT_EQ(LifecycleTracker::s_DestructorCount, 0);
    }

    // All 5 elements should be destroyed
    EXPECT_EQ(LifecycleTracker::s_DestructorCount, 5);
}

TEST(ScopeStack, NewArrayDestructionOrderReversed)
{
    OrderTracker::s_DestructionOrder.clear();

    {
        ScopeStack stack(4096);

        // Allocate array of 4 elements
        auto arrResult = stack.NewArray<OrderTracker>(4);
        ASSERT_TRUE(arrResult.has_value());

        auto arr = *arrResult;
        // Manually set IDs after default construction
        arr[0].id = 100;
        arr[1].id = 101;
        arr[2].id = 102;
        arr[3].id = 103;
    }

    // Array elements should be destroyed in reverse order: 103, 102, 101, 100
    ASSERT_EQ(OrderTracker::s_DestructionOrder.size(), 4u);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[0], 103);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[1], 102);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[2], 101);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[3], 100);
}

TEST(ScopeStack, MixedPODAndNonTrivial)
{
    OrderTracker::s_DestructionOrder.clear();

    {
        ScopeStack stack(2048);

        // Mix POD and non-trivial allocations
        auto pod1 = stack.New<int>(1);
        auto tracked1 = stack.New<OrderTracker>(1);
        auto pod2 = stack.New<double>(2.0);
        auto tracked2 = stack.New<OrderTracker>(2);
        auto pod3 = stack.New<AlignedStruct16>();
        auto tracked3 = stack.New<OrderTracker>(3);

        ASSERT_TRUE(pod1.has_value() && pod2.has_value() && pod3.has_value());
        ASSERT_TRUE(tracked1.has_value() && tracked2.has_value() && tracked3.has_value());

        // Only non-trivial types tracked
        EXPECT_EQ(stack.GetDestructorCount(), 3u);
    }

    // Non-trivial destructors in LIFO order: 3, 2, 1
    ASSERT_EQ(OrderTracker::s_DestructionOrder.size(), 3u);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[0], 3);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[1], 2);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[2], 1);
}

TEST(ScopeStack, SharedPtrSupport)
{
    // Use case: Capturing std::shared_ptr in render passes
    static int s_SharedDestructed = 0;
    s_SharedDestructed = 0;

    // Use a custom deleter so we can observe *exactly* when the last shared_ptr releases ownership.
    auto makeObserved = []()
    {
        return std::shared_ptr<int>(
            new int(42),
            [](int* p)
            {
                delete p;
                s_SharedDestructed++;
            });
    };

    auto external = makeObserved();

    {
        ScopeStack stack(1024);

        auto sharedResult = stack.New<std::shared_ptr<int>>(external);
        ASSERT_TRUE(sharedResult.has_value());
        EXPECT_EQ(*(*sharedResult)->get(), 42);

        // Destructor for shared_ptr is tracked.
        EXPECT_EQ(stack.GetDestructorCount(), 1u);

        // Still alive because 'external' holds a ref.
        EXPECT_EQ(s_SharedDestructed, 0);

        // Drop external ref; object must stay alive until stack destroys its entry.
        external.reset();
        EXPECT_EQ(s_SharedDestructed, 0);
    }

    // Stack destructor called Reset(), releasing the last ref.
    EXPECT_EQ(s_SharedDestructed, 1);
}

TEST(ScopeStack, StringSupport)
{
    // Use case: Capturing std::string for debugging
    {
        ScopeStack stack(1024);

        auto strResult = stack.New<std::string>("Debug Pass Name: ForwardLighting");
        ASSERT_TRUE(strResult.has_value());
        EXPECT_EQ(**strResult, "Debug Pass Name: ForwardLighting");

        EXPECT_EQ(stack.GetDestructorCount(), 1u);
    }
    // No crash = destructor was properly called
}

TEST(ScopeStack, DirectArenaAccessForPOD)
{
    ScopeStack stack(1024);

    // Use GetArena() for POD allocations to bypass destructor tracking overhead
    auto& arena = stack.GetArena();
    auto podResult = arena.New<int>(999);
    ASSERT_TRUE(podResult.has_value());
    EXPECT_EQ(**podResult, 999);

    // No destructor tracked since we went directly to LinearArena
    EXPECT_EQ(stack.GetDestructorCount(), 0u);
}

TEST(ScopeStack, MoveSemantics)
{
    OrderTracker::s_DestructionOrder.clear();

    ScopeStack stack1(1024);
    auto r1 = stack1.New<OrderTracker>(1);
    ASSERT_TRUE(r1.has_value());

    // Move construct
    ScopeStack stack2(std::move(stack1));
    EXPECT_EQ(stack2.GetDestructorCount(), 1u);

    // Original should be empty
    EXPECT_EQ(stack1.GetUsed(), 0u);
    EXPECT_EQ(stack1.GetDestructorCount(), 0u);

    // Destruction should only happen once
    stack2.Reset();
    ASSERT_EQ(OrderTracker::s_DestructionOrder.size(), 1u);
    EXPECT_EQ(OrderTracker::s_DestructionOrder[0], 1);
}

// -----------------------------------------------------------------------------
// Basic Functionality
// -----------------------------------------------------------------------------

TEST(LinearArena, Initialization)
{
    size_t size = 1024;
    LinearArena arena(size);

    EXPECT_EQ(arena.GetUsed(), 0);
    EXPECT_GE(arena.GetTotal(), size); // Might be slightly larger due to alignment padding
}

TEST(LinearArena, BasicPrimitiveAllocation)
{
    LinearArena arena(1024);

    // 1. Allocate an int
    auto result = arena.New<int>(42);
    ASSERT_TRUE(result.has_value());
    int* ptr = *result;
    EXPECT_EQ(*ptr, 42);
    EXPECT_GE(arena.GetUsed(), sizeof(int));

    // 2. Allocate a double immediately after
    auto result2 = arena.New<double>(3.14);
    ASSERT_TRUE(result2.has_value());
    double* ptr2 = *result2;
    EXPECT_EQ(*ptr2, 3.14);

    // Pointers should be close (offset by alignment)
    EXPECT_GT((uintptr_t)ptr2, (uintptr_t)ptr);
}

// -----------------------------------------------------------------------------
// Alignment Strategy (Critical for GPU Data)
// -----------------------------------------------------------------------------

TEST(LinearArena, AlignmentEnforcement)
{
    LinearArena arena(1024);

    // 1. Allocate 1 byte to throw off alignment
    auto byteRes = arena.Alloc(1, 1);
    ASSERT_TRUE(byteRes.has_value());
    void* bytePtr = *byteRes;

    // 2. Allocate 16-byte aligned struct
    // The allocator must insert padding bytes here
    auto structRes = arena.New<AlignedStruct16>();
    ASSERT_TRUE(structRes.has_value());
    AlignedStruct16* structPtr = *structRes;

    uintptr_t addr = reinterpret_cast<uintptr_t>(structPtr);

    // Check alignment
    EXPECT_EQ(addr % 16, 0);

    // Check relative positioning
    uintptr_t byteAddr = reinterpret_cast<uintptr_t>(bytePtr);
    EXPECT_GT(addr, byteAddr);

    // We expect padding: 1 byte used -> align to 16 -> needs 15 bytes padding
    size_t expectedOffset = 16 + sizeof(AlignedStruct16);
    // Note: Implementation details might vary slightly based on start address,
    // but arena start is usually aligned to cache line (64).
    EXPECT_EQ(arena.GetUsed(), expectedOffset);
}

// -----------------------------------------------------------------------------
// Array Allocation
// -----------------------------------------------------------------------------

TEST(LinearArena, ArrayAllocation)
{
    LinearArena arena(2048);

    size_t count = 10;
    auto spanRes = arena.NewArray<int>(count);
    ASSERT_TRUE(spanRes.has_value());

    std::span<int> view = *spanRes;
    EXPECT_EQ(view.size(), count);

    // Test write access
    for (size_t i = 0; i < count; i++)
    {
        view[i] = static_cast<int>(i);
    }

    EXPECT_EQ(view[9], 9);
    EXPECT_EQ(arena.GetUsed(), count * sizeof(int));
}

// -----------------------------------------------------------------------------
// Reset & Reuse (The "Frame Loop" Simulation)
// -----------------------------------------------------------------------------

TEST(LinearArena, FrameResetLoop)
{
    // Small arena just enough for one object
    LinearArena arena(sizeof(int) * 2); // buffer a bit for alignment

    uintptr_t firstFrameAddr = 0;

    // Frame 0
    {
        auto res = arena.New<int>(100);
        ASSERT_TRUE(res.has_value());
        firstFrameAddr = reinterpret_cast<uintptr_t>(*res);
        EXPECT_EQ(**res, 100);
    }

    // End of Frame -> Reset
    arena.Reset();
    EXPECT_EQ(arena.GetUsed(), 0);

    // Frame 1
    {
        auto res = arena.New<int>(200);
        ASSERT_TRUE(res.has_value());
        uintptr_t secondFrameAddr = reinterpret_cast<uintptr_t>(*res);

        // CRITICAL: Address must be identical to Frame 0 (deterministic reuse)
        EXPECT_EQ(firstFrameAddr, secondFrameAddr);
        EXPECT_EQ(**res, 200);
    }
}

// -----------------------------------------------------------------------------
// Boundary Checks
// -----------------------------------------------------------------------------

TEST(LinearArena, OutOfMemory)
{
    LinearArena arena(128); // Very small

    // 1. Allocate most of it
    auto res1 = arena.Alloc(100);
    ASSERT_TRUE(res1.has_value());

    // 2. Try to allocate more than remains
    auto res2 = arena.Alloc(100);
    ASSERT_FALSE(res2.has_value());
    EXPECT_EQ(res2.error(), AllocatorError::OutOfMemory);

    // 3. Reset and ensure we can allocate again
    arena.Reset();
    auto res3 = arena.Alloc(100);
    ASSERT_TRUE(res3.has_value());
}

// -----------------------------------------------------------------------------
// Destructor Semantics (Warning Test)
// -----------------------------------------------------------------------------

TEST(LinearArena, NoDestructorOnReset)
{
    LinearArena arena(1024);
    LifecycleTracker::s_DestructorCount = 0;

    {
        // Manually allocate to bypass static_assert in New<T>
        auto res = arena.Alloc(sizeof(LifecycleTracker), alignof(LifecycleTracker));
        ASSERT_TRUE(res.has_value());
        std::construct_at(static_cast<LifecycleTracker*>(*res), 10);
    }

    arena.Reset();

    // CRITICAL: LinearArena does NOT call destructors.
    // It just moves the offset pointer back to 0.
    // This test confirms that behavior so we don't rely on RAII inside the arena.
    EXPECT_EQ(LifecycleTracker::s_DestructorCount, 0);

    // Correct usage for non-POD types manually (if ever needed):
    // std::destroy_at(ptr);
    // but normally we only put PODs in here.
}

TEST(LinearArena, MoveRebindsThreadAffinity)
{
    // LinearArena is thread-affine (not thread-safe). It can be moved, but the moved-from
    // arena should become inert and must not be used.

    LinearArena arena(1024);
    auto a0 = arena.New<int>(123);
    ASSERT_TRUE(a0.has_value());
    EXPECT_EQ(**a0, 123);

    // Move construct on the same thread.
    LinearArena moved(std::move(arena));
    auto a1 = moved.New<int>(456);
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(**a1, 456);

    // Moved-from arena is expected to be empty/inert. Using it is a programming error in debug.
    // We validate it doesn't report used bytes.
    EXPECT_EQ(arena.GetUsed(), 0u);
}
