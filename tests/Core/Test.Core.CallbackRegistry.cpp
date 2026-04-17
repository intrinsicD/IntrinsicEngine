#include <gtest/gtest.h>
 
#include <atomic>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
 
import Extrinsic.Core.CallbackRegistry;
import Extrinsic.Core.StrongHandle;
 
using Extrinsic::Core::CallbackRegistry;
using Extrinsic::Core::StrongHandle;
 
namespace
{
    struct LoaderTag
    {
    };
 
    struct EventTag
    {
    };
 
    enum class FakeError : std::uint32_t
    {
        Ok = 0,
        Missing = 1,
        Corrupt = 2,
    };
 
    using ExpectedLoader = CallbackRegistry<std::expected<int, FakeError>(std::string_view), LoaderTag>;
    using VoidListener = CallbackRegistry<void(int), EventTag>;
}
 
// -----------------------------------------------------------------------------
// Basic register / invoke
// -----------------------------------------------------------------------------
 
TEST(CoreCallbackRegistry, RegisteredCallbackIsInvokable)
{
    ExpectedLoader reg;
    auto tk = reg.Register([](std::string_view s) -> std::expected<int, FakeError>
    {
        return static_cast<int>(s.size());
    });
    EXPECT_TRUE(tk.IsValid());
    EXPECT_EQ(reg.Size(), 1u);
 
    auto result = reg.InvokeOr(tk, FakeError::Missing, std::string_view{"hello"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 5);
}
 
TEST(CoreCallbackRegistry, InvokeReturnsOptionalForNonExpected)
{
    CallbackRegistry<int(int), LoaderTag> reg;
    auto tk = reg.Register([](int x) { return x * 2; });
 
    auto opt = reg.Invoke(tk, 21);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, 42);
}
 
TEST(CoreCallbackRegistry, InvokeVoidReturnsBool)
{
    VoidListener reg;
    std::atomic<int> calls{0};
    auto tk = reg.Register([&](int x) { calls += x; });
 
    EXPECT_TRUE(reg.Invoke(tk, 5));
    EXPECT_TRUE(reg.Invoke(tk, 7));
    EXPECT_EQ(calls.load(), 12);
}
 
// -----------------------------------------------------------------------------
// Staleness: generational safety
// -----------------------------------------------------------------------------
 
TEST(CoreCallbackRegistry, UnregisterInvalidatesToken)
{
    ExpectedLoader reg;
    auto tk = reg.Register([](std::string_view) -> std::expected<int, FakeError>
    {
        return 1;
    });
    EXPECT_TRUE(reg.Unregister(tk));
    EXPECT_FALSE(reg.Contains(tk));
    EXPECT_EQ(reg.Size(), 0u);
 
    auto r = reg.InvokeOr(tk, FakeError::Missing, std::string_view{""});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), FakeError::Missing);
 
    auto opt = reg.Invoke(tk, std::string_view{""});
    EXPECT_FALSE(opt.has_value());
}
 
TEST(CoreCallbackRegistry, DoubleUnregisterReturnsFalse)
{
    ExpectedLoader reg;
    auto tk = reg.Register([](std::string_view) -> std::expected<int, FakeError>
    {
        return 1;
    });
    EXPECT_TRUE(reg.Unregister(tk));
    EXPECT_FALSE(reg.Unregister(tk));
}
 
TEST(CoreCallbackRegistry, DefaultTokenIsInvalidAndStale)
{
    ExpectedLoader reg;
    ExpectedLoader::Token defaulted{};
    EXPECT_FALSE(defaulted.IsValid());
    EXPECT_FALSE(reg.Contains(defaulted));
    EXPECT_FALSE(reg.Invoke(defaulted, std::string_view{""}).has_value());
}
 
TEST(CoreCallbackRegistry, StaleTokenDoesNotReachRecycledSlot)
{
    ExpectedLoader reg;
    auto first = reg.Register([](std::string_view) -> std::expected<int, FakeError>
    {
        return 100;
    });
    EXPECT_TRUE(reg.Unregister(first));
 
    // Next Register must re-use the freed slot (same Index), but the
    // Generation must have advanced so the stale token cannot invoke it.
    auto second = reg.Register([](std::string_view) -> std::expected<int, FakeError>
    {
        return 200;
    });
    EXPECT_EQ(second.Index, first.Index);
    EXPECT_NE(second.Generation, first.Generation);
 
    auto stale = reg.InvokeOr(first, FakeError::Missing, std::string_view{""});
    ASSERT_FALSE(stale.has_value());
    EXPECT_EQ(stale.error(), FakeError::Missing);
 
    auto fresh = reg.InvokeOr(second, FakeError::Missing, std::string_view{""});
    ASSERT_TRUE(fresh.has_value());
    EXPECT_EQ(*fresh, 200);
}
 
// -----------------------------------------------------------------------------
// Error flattening via InvokeOr
// -----------------------------------------------------------------------------
 
TEST(CoreCallbackRegistry, InvokeOrForwardsCallableError)
{
    ExpectedLoader reg;
    auto tk = reg.Register([](std::string_view) -> std::expected<int, FakeError>
    {
        return std::unexpected(FakeError::Corrupt);
    });
    auto r = reg.InvokeOr(tk, FakeError::Missing, std::string_view{""});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), FakeError::Corrupt);
}
 
// -----------------------------------------------------------------------------
// Independence: multiple slots do not collide
// -----------------------------------------------------------------------------
 
TEST(CoreCallbackRegistry, IndependentSlotsDoNotInterfere)
{
    ExpectedLoader reg;
    auto a = reg.Register([](std::string_view) -> std::expected<int, FakeError> { return 1; });
    auto b = reg.Register([](std::string_view) -> std::expected<int, FakeError> { return 2; });
    auto c = reg.Register([](std::string_view) -> std::expected<int, FakeError> { return 3; });
 
    EXPECT_EQ(reg.Size(), 3u);
    EXPECT_NE(a.Index, b.Index);
    EXPECT_NE(b.Index, c.Index);
 
    EXPECT_EQ(*reg.InvokeOr(a, FakeError::Missing, std::string_view{""}), 1);
    EXPECT_EQ(*reg.InvokeOr(b, FakeError::Missing, std::string_view{""}), 2);
    EXPECT_EQ(*reg.InvokeOr(c, FakeError::Missing, std::string_view{""}), 3);
 
    EXPECT_TRUE(reg.Unregister(b));
    EXPECT_EQ(reg.Size(), 2u);
    EXPECT_EQ(*reg.InvokeOr(a, FakeError::Missing, std::string_view{""}), 1);
    EXPECT_FALSE(reg.InvokeOr(b, FakeError::Missing, std::string_view{""}).has_value());
    EXPECT_EQ(*reg.InvokeOr(c, FakeError::Missing, std::string_view{""}), 3);
}
 
// -----------------------------------------------------------------------------
// Concurrency: concurrent Invoke + Unregister must not crash or race.
// -----------------------------------------------------------------------------
 
TEST(CoreCallbackRegistry, ConcurrentInvokeAndReregisterIsSafe)
{
    ExpectedLoader reg;
    std::atomic<bool> stop{false};
 
    auto tk = reg.Register([](std::string_view s) -> std::expected<int, FakeError>
    {
        return static_cast<int>(s.size());
    });
 
    // Reader thread: spin invoking. Stale tokens return the sentinel - that's
    // fine. What must not happen is a crash, a data race, or an invocation
    // of the wrong callable after unregister/re-register.
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed))
        {
            auto r = reg.InvokeOr(tk, FakeError::Missing, std::string_view{"abcd"});
            if (r.has_value())
            {
                EXPECT_EQ(*r, 4);
            }
        }
    });
 
    for (int i = 0; i < 500; ++i)
    {
        (void)reg.Unregister(tk);
        tk = reg.Register([](std::string_view s) -> std::expected<int, FakeError>
        {
            return static_cast<int>(s.size());
        });
    }
 
    stop.store(true, std::memory_order_relaxed);
    reader.join();
}
 
// -----------------------------------------------------------------------------
// Type safety: tokens with different Tags are not interchangeable (compile
// time). We verify the surface area at runtime instead via template
// instantiation separation.
// -----------------------------------------------------------------------------
 
TEST(CoreCallbackRegistry, DifferentTagRegistriesHaveDistinctTokenTypes)
{
    static_assert(!std::is_same_v<ExpectedLoader::Token, VoidListener::Token>,
                  "Tokens from different tags must be distinct types.");
    SUCCEED();
}