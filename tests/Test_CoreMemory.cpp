#include <gtest/gtest.h>
import Core.Memory;

using namespace Core::Memory;

TEST(CoreMemory, LinearArena_BasicAllocation) {
    LinearArena arena(1024);
    
    auto res1 = arena.New<int>(42);
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(**res1, 42);
    
    auto res2 = arena.New<double>(3.14159);
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(**res2, 3.14159);
}

TEST(CoreMemory, LinearArena_Alignment) {
    LinearArena arena(1024);
    
    auto b = arena.Alloc(1, 1);
    (void)b; // FIX: Suppress unused variable warning
    
    auto aligned = arena.Alloc(64, 128);
    ASSERT_TRUE(aligned.has_value());
    
    uintptr_t addr = reinterpret_cast<uintptr_t>(*aligned);
    EXPECT_EQ(addr % 128, 0);
}

TEST(CoreMemory, LinearArena_Reset) {
    LinearArena arena(1024);
    
    size_t initialUsed = arena.GetUsed();
    auto res = arena.NewArray<int>(100);
    (void)res; // FIX: Suppress unused variable warning
    
    EXPECT_GT(arena.GetUsed(), initialUsed);
    
    arena.Reset();
    EXPECT_EQ(arena.GetUsed(), 0);
}

TEST(CoreMemory, LinearArena_OOM) {
    LinearArena arena(128); 
    
    auto res = arena.Alloc(200); 
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), AllocatorError::OutOfMemory);
}