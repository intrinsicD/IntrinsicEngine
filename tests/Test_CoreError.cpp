// =============================================================================
// Test_CoreError — Contract tests for Core.Error.
//
// Covers: ErrorCode enum, ErrorCodeToString, Expected<T> type alias,
//         Ok/Err helpers, Unit/Result void-success type, and monadic chaining.
//
// Target: IntrinsicCoreTests (pure algorithmic, no GPU, no ECS).
// =============================================================================

#include <gtest/gtest.h>
#include <string>

import Core.Error;

using namespace Core;

// ---------------------------------------------------------------------------
// ErrorCode: all defined codes have non-empty string representations.
// ---------------------------------------------------------------------------
TEST(CoreError, ErrorCodeToStringCoversAllCodes)
{
    // Spot-check representative codes from each category.
    EXPECT_EQ(ErrorCodeToString(ErrorCode::Success), "Success");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::OutOfMemory), "OutOfMemory");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::FileNotFound), "FileNotFound");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::DeviceLost), "DeviceLost");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::AssetNotLoaded), "AssetNotLoaded");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::ThreadViolation), "ThreadViolation");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::Unknown), "Unknown");
}

// ---------------------------------------------------------------------------
// ErrorCodeToString: unknown code maps to "Unknown".
// ---------------------------------------------------------------------------
TEST(CoreError, UnknownCodeMapsToUnknown)
{
    const auto code = static_cast<ErrorCode>(12345);
    EXPECT_EQ(ErrorCodeToString(code), "Unknown");
}

// ---------------------------------------------------------------------------
// Ok<T>: creates a successful expected with the given value.
// ---------------------------------------------------------------------------
TEST(CoreError, OkCreatesSuccessValue)
{
    Expected<int> result = Ok(42);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

// ---------------------------------------------------------------------------
// Err<T>: creates an error expected with the given code.
// ---------------------------------------------------------------------------
TEST(CoreError, ErrCreatesErrorValue)
{
    Expected<int> result = Err<int>(ErrorCode::InvalidArgument);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------------------
// Result (void success): Ok() creates a successful unit result.
// ---------------------------------------------------------------------------
TEST(CoreError, ResultOkIsSuccess)
{
    Result result = Ok();
    EXPECT_TRUE(result.has_value());
}

// ---------------------------------------------------------------------------
// Result (void error): Err() creates an error result.
// ---------------------------------------------------------------------------
TEST(CoreError, ResultErrIsError)
{
    Result result = Err(ErrorCode::FileReadError);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::FileReadError);
}

// ---------------------------------------------------------------------------
// Monadic chaining: and_then on Expected<T>.
// ---------------------------------------------------------------------------
TEST(CoreError, MonadicAndThenChaining)
{
    auto step1 = [](int x) -> Expected<int> {
        return Ok(x * 2);
    };

    auto step2 = [](int x) -> Expected<int> {
        if (x > 100)
            return Err<int>(ErrorCode::OutOfRange);
        return Ok(x + 1);
    };

    // Success path.
    Expected<int> result = Ok(10).and_then(step1).and_then(step2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 21); // (10 * 2) + 1

    // Error propagation: 100 * 2 = 200 > 100 → OutOfRange.
    Expected<int> errResult = Ok(100).and_then(step1).and_then(step2);
    ASSERT_FALSE(errResult.has_value());
    EXPECT_EQ(errResult.error(), ErrorCode::OutOfRange);
}

// ---------------------------------------------------------------------------
// Monadic chaining: transform on Expected<T>.
// ---------------------------------------------------------------------------
TEST(CoreError, MonadicTransform)
{
    Expected<int> result = Ok(5).transform([](int x) { return x * 3; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 15);

    // Transform is not called on error.
    Expected<int> errResult = Err<int>(ErrorCode::InvalidState)
        .transform([](int x) { return x * 3; });
    ASSERT_FALSE(errResult.has_value());
    EXPECT_EQ(errResult.error(), ErrorCode::InvalidState);
}

// ---------------------------------------------------------------------------
// Monadic chaining: or_else on Expected<T>.
// ---------------------------------------------------------------------------
TEST(CoreError, MonadicOrElse)
{
    auto fallback = [](ErrorCode) -> Expected<int> {
        return Ok(-1); // Default fallback value.
    };

    // Success: or_else not called.
    Expected<int> okResult = Ok(42).or_else(fallback);
    ASSERT_TRUE(okResult.has_value());
    EXPECT_EQ(*okResult, 42);

    // Error: or_else provides fallback.
    Expected<int> errResult = Err<int>(ErrorCode::ResourceNotFound).or_else(fallback);
    ASSERT_TRUE(errResult.has_value());
    EXPECT_EQ(*errResult, -1);
}

// ---------------------------------------------------------------------------
// ErrorCode categories: range checks for error code organization.
// ---------------------------------------------------------------------------
TEST(CoreError, ErrorCodeCategories)
{
    // Resource errors: 100-199
    EXPECT_GE(static_cast<uint32_t>(ErrorCode::OutOfMemory), 100u);
    EXPECT_LT(static_cast<uint32_t>(ErrorCode::ResourceCorrupted), 200u);

    // I/O errors: 200-299
    EXPECT_GE(static_cast<uint32_t>(ErrorCode::FileNotFound), 200u);
    EXPECT_LT(static_cast<uint32_t>(ErrorCode::PermissionDenied), 300u);

    // Validation errors: 300-399
    EXPECT_GE(static_cast<uint32_t>(ErrorCode::InvalidArgument), 300u);
    EXPECT_LT(static_cast<uint32_t>(ErrorCode::TypeMismatch), 400u);

    // Graphics/RHI errors: 400-499
    EXPECT_GE(static_cast<uint32_t>(ErrorCode::DeviceLost), 400u);
    EXPECT_LT(static_cast<uint32_t>(ErrorCode::SwapchainOutOfDate), 500u);

    // Asset errors: 500-599
    EXPECT_GE(static_cast<uint32_t>(ErrorCode::AssetNotLoaded), 500u);
    EXPECT_LT(static_cast<uint32_t>(ErrorCode::AssetTypeMismatch), 600u);

    // Threading errors: 600-699
    EXPECT_GE(static_cast<uint32_t>(ErrorCode::ThreadViolation), 600u);
    EXPECT_LT(static_cast<uint32_t>(ErrorCode::DeadlockDetected), 700u);
}
