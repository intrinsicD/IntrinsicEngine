#include <gtest/gtest.h>
#include <vector>
#include <cstdint>

import Core.Memory;

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

    LifecycleTracker(int val) : data(val)
    {
    }

    ~LifecycleTracker() { s_DestructorCount++; }
};

int LifecycleTracker::s_DestructorCount = 0;

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
        auto res = arena.New<LifecycleTracker>(10);
        ASSERT_TRUE(res.has_value());
    } // res goes out of scope, but it's just a pointer. No delete called.

    arena.Reset();

    // CRITICAL: LinearArena does NOT call destructors.
    // It just moves the offset pointer back to 0.
    // This test confirms that behavior so we don't rely on RAII inside the arena.
    EXPECT_EQ(LifecycleTracker::s_DestructorCount, 0);

    // Correct usage for non-POD types manually (if ever needed):
    // std::destroy_at(ptr);
    // but normally we only put PODs in here.
}
