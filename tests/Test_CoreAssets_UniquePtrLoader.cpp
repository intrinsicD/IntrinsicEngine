#include <gtest/gtest.h>

import Core;

using namespace Core;
using namespace Core::Assets;

struct NonCopyable
{
    NonCopyable() = default;
    explicit NonCopyable(int v) : Value(v) {}

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;

    int Value = 0;
};

TEST(AssetSystem, UniquePtrLoader_SupportsNonCopyable)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    auto loader = [](const std::string&, AssetHandle) -> std::unique_ptr<NonCopyable>
    {
        return std::make_unique<NonCopyable>(42);
    };

    auto handle = manager.Load<NonCopyable>("noncopy", loader);
    Core::Tasks::Scheduler::WaitForAll();

    auto raw = manager.GetRaw<NonCopyable>(handle);
    ASSERT_TRUE(raw.has_value());
    ASSERT_NE(*raw, nullptr);
    EXPECT_EQ((*raw)->Value, 42);

    auto lease = manager.Pin<NonCopyable>(handle);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ((*lease)->Value, 42);

    Core::Tasks::Scheduler::Shutdown();
}

