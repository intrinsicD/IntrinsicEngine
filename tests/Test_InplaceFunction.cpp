#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

import Core.InplaceFunction;

using Core::InplaceFunction;

// =========================================================================
// Compile-time contract tests
// =========================================================================

TEST(InplaceFunction, IsNotCopyConstructible)
{
    EXPECT_FALSE((std::is_copy_constructible_v<InplaceFunction<void()>>));
}

TEST(InplaceFunction, IsNotCopyAssignable)
{
    EXPECT_FALSE((std::is_copy_assignable_v<InplaceFunction<void()>>));
}

TEST(InplaceFunction, IsMoveConstructible)
{
    EXPECT_TRUE((std::is_move_constructible_v<InplaceFunction<void()>>));
}

TEST(InplaceFunction, IsMoveAssignable)
{
    EXPECT_TRUE((std::is_move_assignable_v<InplaceFunction<void()>>));
}

TEST(InplaceFunction, IsNothrowMoveConstructible)
{
    EXPECT_TRUE((std::is_nothrow_move_constructible_v<InplaceFunction<void()>>));
}

TEST(InplaceFunction, IsNothrowMoveAssignable)
{
    EXPECT_TRUE((std::is_nothrow_move_assignable_v<InplaceFunction<void()>>));
}

// =========================================================================
// Default construction
// =========================================================================

TEST(InplaceFunction, DefaultConstructedIsEmpty)
{
    InplaceFunction<void()> fn;
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(InplaceFunction, NullptrConstructedIsEmpty)
{
    InplaceFunction<void()> fn(nullptr);
    EXPECT_FALSE(static_cast<bool>(fn));
}

// =========================================================================
// Construction from callable
// =========================================================================

TEST(InplaceFunction, ConstructFromLambda)
{
    bool called = false;
    InplaceFunction<void()> fn([&called]() { called = true; });
    EXPECT_TRUE(static_cast<bool>(fn));
    fn();
    EXPECT_TRUE(called);
}

TEST(InplaceFunction, ConstructFromFunctionPointer)
{
    static int s_Value = 0;
    auto freeFunc = +[](int x) { s_Value = x; };

    InplaceFunction<void(int)> fn(freeFunc);
    EXPECT_TRUE(static_cast<bool>(fn));
    fn(42);
    EXPECT_EQ(s_Value, 42);
}

TEST(InplaceFunction, ConstructFromStatelessLambda)
{
    InplaceFunction<int(int, int)> fn([](int a, int b) { return a + b; });
    EXPECT_EQ(fn(3, 4), 7);
}

// =========================================================================
// Return values
// =========================================================================

TEST(InplaceFunction, ReturnsInt)
{
    InplaceFunction<int()> fn([]() { return 42; });
    EXPECT_EQ(fn(), 42);
}

TEST(InplaceFunction, ReturnsFloat)
{
    InplaceFunction<float(float, float)> fn([](float a, float b) { return a * b; });
    EXPECT_FLOAT_EQ(fn(3.0f, 4.0f), 12.0f);
}

TEST(InplaceFunction, ReturnsVoid)
{
    int counter = 0;
    InplaceFunction<void()> fn([&counter]() { ++counter; });
    fn();
    fn();
    EXPECT_EQ(counter, 2);
}

TEST(InplaceFunction, ReturnsBool)
{
    InplaceFunction<bool(int)> fn([](int x) { return x > 0; });
    EXPECT_TRUE(fn(1));
    EXPECT_FALSE(fn(-1));
}

// =========================================================================
// Stateful callables (captures)
// =========================================================================

TEST(InplaceFunction, CapturesByValue)
{
    int x = 10;
    int y = 20;
    InplaceFunction<int()> fn([x, y]() { return x + y; });
    EXPECT_EQ(fn(), 30);
}

TEST(InplaceFunction, CapturesByReference)
{
    int counter = 0;
    InplaceFunction<void()> fn([&counter]() { ++counter; });
    fn();
    fn();
    fn();
    EXPECT_EQ(counter, 3);
}

TEST(InplaceFunction, MutableLambda)
{
    int callCount = 0;
    InplaceFunction<int()> fn([callCount]() mutable { return ++callCount; });
    EXPECT_EQ(fn(), 1);
    EXPECT_EQ(fn(), 2);
    EXPECT_EQ(fn(), 3);
}

// =========================================================================
// Captures with non-trivial types (unique_ptr)
// =========================================================================

TEST(InplaceFunction, CapturesUniquePtr)
{
    auto ptr = std::make_unique<int>(99);
    InplaceFunction<int(), 128> fn([p = std::move(ptr)]() { return *p; });
    EXPECT_EQ(fn(), 99);
}

// =========================================================================
// Move construction
// =========================================================================

TEST(InplaceFunction, MoveConstructFromPopulated)
{
    int value = 0;
    InplaceFunction<void()> a([&value]() { value = 42; });
    InplaceFunction<void()> b(std::move(a));

    EXPECT_FALSE(static_cast<bool>(a)); // source is empty after move
    EXPECT_TRUE(static_cast<bool>(b));
    b();
    EXPECT_EQ(value, 42);
}

TEST(InplaceFunction, MoveConstructFromEmpty)
{
    InplaceFunction<void()> a;
    InplaceFunction<void()> b(std::move(a));
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_FALSE(static_cast<bool>(b));
}

// =========================================================================
// Move assignment
// =========================================================================

TEST(InplaceFunction, MoveAssignFromPopulatedToEmpty)
{
    int value = 0;
    InplaceFunction<void()> a([&value]() { value = 42; });
    InplaceFunction<void()> b;

    b = std::move(a);
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_TRUE(static_cast<bool>(b));
    b();
    EXPECT_EQ(value, 42);
}

TEST(InplaceFunction, MoveAssignFromPopulatedToPopulated)
{
    int valueA = 0;
    int valueB = 0;
    InplaceFunction<void()> a([&valueA]() { valueA = 1; });
    InplaceFunction<void()> b([&valueB]() { valueB = 2; });

    b = std::move(a);
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_TRUE(static_cast<bool>(b));
    b();
    EXPECT_EQ(valueA, 1);
    EXPECT_EQ(valueB, 0); // old callable was destroyed, never invoked
}

TEST(InplaceFunction, MoveAssignFromEmptyToPopulated)
{
    int value = 0;
    InplaceFunction<void()> a;
    InplaceFunction<void()> b([&value]() { value = 42; });

    b = std::move(a);
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_FALSE(static_cast<bool>(b));
    EXPECT_EQ(value, 0); // never called
}

TEST(InplaceFunction, SelfMoveAssignIsNoOp)
{
    int value = 0;
    InplaceFunction<void()> fn([&value]() { value = 42; });

    // Self-move should not crash or change state
    auto* ptr = &fn;
    *ptr = std::move(fn);
    EXPECT_TRUE(static_cast<bool>(*ptr));
    (*ptr)();
    EXPECT_EQ(value, 42);
}

// =========================================================================
// nullptr assignment (reset)
// =========================================================================

TEST(InplaceFunction, AssignNullptrResetsToEmpty)
{
    InplaceFunction<void()> fn([]() {});
    EXPECT_TRUE(static_cast<bool>(fn));

    fn = nullptr;
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(InplaceFunction, AssignNullptrToEmptyIsNoOp)
{
    InplaceFunction<void()> fn;
    fn = nullptr;
    EXPECT_FALSE(static_cast<bool>(fn));
}

// =========================================================================
// Swap
// =========================================================================

TEST(InplaceFunction, SwapBothPopulated)
{
    int a = 0, b = 0;
    InplaceFunction<void()> fnA([&a]() { a = 1; });
    InplaceFunction<void()> fnB([&b]() { b = 2; });

    swap(fnA, fnB);

    fnA();
    EXPECT_EQ(b, 2);
    fnB();
    EXPECT_EQ(a, 1);
}

TEST(InplaceFunction, SwapOneEmpty)
{
    int value = 0;
    InplaceFunction<void()> fnA([&value]() { value = 42; });
    InplaceFunction<void()> fnB;

    swap(fnA, fnB);

    EXPECT_FALSE(static_cast<bool>(fnA));
    EXPECT_TRUE(static_cast<bool>(fnB));
    fnB();
    EXPECT_EQ(value, 42);
}

// =========================================================================
// Multiple arguments
// =========================================================================

TEST(InplaceFunction, ThreeArguments)
{
    InplaceFunction<int(int, int, int)> fn([](int a, int b, int c) { return a + b + c; });
    EXPECT_EQ(fn(1, 2, 3), 6);
}

TEST(InplaceFunction, MixedArgumentTypes)
{
    InplaceFunction<float(int, float, bool)> fn([](int i, float f, bool b) {
        return b ? static_cast<float>(i) + f : 0.0f;
    });
    EXPECT_FLOAT_EQ(fn(10, 0.5f, true), 10.5f);
    EXPECT_FLOAT_EQ(fn(10, 0.5f, false), 0.0f);
}

// =========================================================================
// Pointer/reference argument passing
// =========================================================================

TEST(InplaceFunction, PassByPointer)
{
    InplaceFunction<void(int*)> fn([](int* p) { *p = 99; });
    int value = 0;
    fn(&value);
    EXPECT_EQ(value, 99);
}

TEST(InplaceFunction, PassByReference)
{
    InplaceFunction<void(int&)> fn([](int& x) { x = 77; });
    int value = 0;
    fn(value);
    EXPECT_EQ(value, 77);
}

TEST(InplaceFunction, PassByConstReference)
{
    InplaceFunction<int(const std::string&)> fn([](const std::string& s) {
        return static_cast<int>(s.size());
    });
    EXPECT_EQ(fn("hello"), 5);
}

// =========================================================================
// Destructor correctness
// =========================================================================

namespace
{
    struct DestructorTracker
    {
        int* Counter;
        explicit DestructorTracker(int& c) : Counter(&c) {}
        DestructorTracker(DestructorTracker&& o) noexcept : Counter(o.Counter) { o.Counter = nullptr; }
        DestructorTracker& operator=(DestructorTracker&&) = delete;
        ~DestructorTracker() { if (Counter) ++(*Counter); }

        void operator()() const {}
    };
}

TEST(InplaceFunction, DestructorCalledOnDestruction)
{
    int dtorCount = 0;
    {
        InplaceFunction<void()> fn{DestructorTracker{dtorCount}};
        EXPECT_TRUE(static_cast<bool>(fn));
        // dtorCount may be 1 here from temporary destruction
    }
    // The stored callable's destructor must have been called
    EXPECT_GE(dtorCount, 1);
}

TEST(InplaceFunction, DestructorCalledOnNullptrAssign)
{
    int dtorCount = 0;
    InplaceFunction<void()> fn{DestructorTracker{dtorCount}};
    int countAfterConstruct = dtorCount;

    fn = nullptr;
    EXPECT_EQ(dtorCount, countAfterConstruct + 1);
}

TEST(InplaceFunction, DestructorCalledOnMoveAssign)
{
    int dtorCountA = 0;
    int dtorCountB = 0;

    InplaceFunction<void()> fnA{DestructorTracker{dtorCountA}};
    int countA = dtorCountA;

    InplaceFunction<void()> fnB{DestructorTracker{dtorCountB}};
    int countB = dtorCountB;

    fnB = std::move(fnA);
    // B's old callable should have been destroyed
    EXPECT_EQ(dtorCountB, countB + 1);
}

// =========================================================================
// Custom buffer sizes
// =========================================================================

TEST(InplaceFunction, SmallBuffer)
{
    // A lambda that captures just one int should fit in 16 bytes.
    int x = 42;
    InplaceFunction<int(), 16> fn([x]() { return x; });
    EXPECT_EQ(fn(), 42);
}

TEST(InplaceFunction, LargeBuffer)
{
    // Capture many values that exceed the default 64 bytes.
    int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8;
    int i = 9, j = 10, k = 11, l = 12, m = 13, n = 14, o = 15, p = 16;
    InplaceFunction<int(), 256> fn([=]() {
        return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
    });
    EXPECT_EQ(fn(), 136);
}

// =========================================================================
// Functor objects
// =========================================================================

namespace
{
    struct Adder
    {
        int Base;
        int operator()(int x) const { return Base + x; }
    };

    struct Multiplier
    {
        float Factor;
        float operator()(float x) const { return Factor * x; }
    };
}

TEST(InplaceFunction, ConstructFromFunctor)
{
    InplaceFunction<int(int)> fn(Adder{100});
    EXPECT_EQ(fn(5), 105);
}

TEST(InplaceFunction, ConstructFromAnotherFunctor)
{
    InplaceFunction<float(float)> fn(Multiplier{2.5f});
    EXPECT_FLOAT_EQ(fn(4.0f), 10.0f);
}

// =========================================================================
// Usage in containers (vector of InplaceFunction)
// =========================================================================

TEST(InplaceFunction, StoredInVector)
{
    std::vector<InplaceFunction<int(int)>> fns;
    fns.push_back(InplaceFunction<int(int)>([](int x) { return x * 2; }));
    fns.push_back(InplaceFunction<int(int)>([](int x) { return x * 3; }));
    fns.push_back(InplaceFunction<int(int)>([](int x) { return x + 10; }));

    EXPECT_EQ(fns[0](5), 10);
    EXPECT_EQ(fns[1](5), 15);
    EXPECT_EQ(fns[2](5), 15);
}

// =========================================================================
// Chained operations
// =========================================================================

TEST(InplaceFunction, ReassignDifferentCallable)
{
    InplaceFunction<int()> fn([]() { return 1; });
    EXPECT_EQ(fn(), 1);

    fn = InplaceFunction<int()>([]() { return 2; });
    EXPECT_EQ(fn(), 2);

    fn = InplaceFunction<int()>([]() { return 3; });
    EXPECT_EQ(fn(), 3);
}

// =========================================================================
// Const invocation (mutable storage allows calling through const ref)
// =========================================================================

TEST(InplaceFunction, InvokeThroughConstRef)
{
    InplaceFunction<int()> fn([]() { return 42; });
    const auto& cref = fn;
    EXPECT_EQ(cref(), 42);
}

TEST(InplaceFunction, MutableLambdaThroughConstRef)
{
    int counter = 0;
    InplaceFunction<void()> fn([counter]() mutable { ++counter; });
    const auto& cref = fn;
    cref(); // Must work even though lambda is mutable
}

// =========================================================================
// Default buffer size constant
// =========================================================================

TEST(InplaceFunction, DefaultBufferSizeIs64)
{
    EXPECT_EQ(Core::kDefaultInplaceFunctionSize, 64u);
}
