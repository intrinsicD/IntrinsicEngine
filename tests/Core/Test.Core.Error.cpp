#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

import Extrinsic.Core.Error;

using Extrinsic::Core::ErrorCode;
using Extrinsic::Core::Expected;
using Extrinsic::Core::Ok;
using Extrinsic::Core::Err;
using Extrinsic::Core::Result;
using Extrinsic::Core::Unit;
using Extrinsic::Core::unit;
using Extrinsic::Core::Error::ToUnderlying;
using Extrinsic::Core::Error::ToString;

// -----------------------------------------------------------------------------
// ErrorCode numeric contract
// -----------------------------------------------------------------------------

TEST(CoreError, ErrorCodeNumericValuesAreStable)
{
    EXPECT_EQ(ToUnderlying(ErrorCode::Success), 0u);

    // Resource errors (100-199)
    EXPECT_EQ(ToUnderlying(ErrorCode::OutOfMemory), 100u);
    EXPECT_EQ(ToUnderlying(ErrorCode::ResourceNotFound), 101u);
    EXPECT_EQ(ToUnderlying(ErrorCode::ResourceBusy), 102u);
    EXPECT_EQ(ToUnderlying(ErrorCode::ResourceCorrupted), 103u);

    // I/O errors (200-299)
    EXPECT_EQ(ToUnderlying(ErrorCode::FileNotFound), 200u);
    EXPECT_EQ(ToUnderlying(ErrorCode::FileReadError), 201u);
    EXPECT_EQ(ToUnderlying(ErrorCode::FileWriteError), 202u);
    EXPECT_EQ(ToUnderlying(ErrorCode::InvalidPath), 203u);
    EXPECT_EQ(ToUnderlying(ErrorCode::PermissionDenied), 204u);
    EXPECT_EQ(ToUnderlying(ErrorCode::UnsupportedFormat), 205u);

    // Validation errors (300-399)
    EXPECT_EQ(ToUnderlying(ErrorCode::InvalidArgument), 300u);
    EXPECT_EQ(ToUnderlying(ErrorCode::InvalidState), 301u);
    EXPECT_EQ(ToUnderlying(ErrorCode::InvalidFormat), 302u);
    EXPECT_EQ(ToUnderlying(ErrorCode::OutOfRange), 303u);
    EXPECT_EQ(ToUnderlying(ErrorCode::TypeMismatch), 304u);

    // Graphics/RHI errors (400-499)
    EXPECT_EQ(ToUnderlying(ErrorCode::DeviceLost), 400u);
    EXPECT_EQ(ToUnderlying(ErrorCode::OutOfDeviceMemory), 401u);
    EXPECT_EQ(ToUnderlying(ErrorCode::ShaderCompilationFailed), 402u);
    EXPECT_EQ(ToUnderlying(ErrorCode::PipelineCreationFailed), 403u);
    EXPECT_EQ(ToUnderlying(ErrorCode::SwapchainOutOfDate), 404u);

    // Asset errors (500-599)
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetNotLoaded), 500u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetLoadFailed), 501u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetLoaderMissing), 502u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetTypeMismatch), 503u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetDecodeFailed), 504u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetUploadFailed), 505u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetUnsupportedFormat), 506u);
    EXPECT_EQ(ToUnderlying(ErrorCode::AssetInvalidData), 507u);

    // Threading errors (600-699)
    EXPECT_EQ(ToUnderlying(ErrorCode::ThreadViolation), 600u);
    EXPECT_EQ(ToUnderlying(ErrorCode::DeadlockDetected), 601u);

    EXPECT_EQ(ToUnderlying(ErrorCode::Unknown), 999u);
}

TEST(CoreError, ToUnderlyingIsConstexpr)
{
    constexpr auto v = ToUnderlying(ErrorCode::InvalidArgument);
    static_assert(v == 300u);
    EXPECT_EQ(v, 300u);
}

// -----------------------------------------------------------------------------
// ToString
// -----------------------------------------------------------------------------

TEST(CoreError, ToStringKnownCodes)
{
    EXPECT_EQ(ToString(ErrorCode::Success), "Success");
    EXPECT_EQ(ToString(ErrorCode::OutOfMemory), "OutOfMemory");
    EXPECT_EQ(ToString(ErrorCode::ResourceNotFound), "ResourceNotFound");
    EXPECT_EQ(ToString(ErrorCode::ResourceBusy), "ResourceBusy");
    EXPECT_EQ(ToString(ErrorCode::ResourceCorrupted), "ResourceCorrupted");
    EXPECT_EQ(ToString(ErrorCode::FileNotFound), "FileNotFound");
    EXPECT_EQ(ToString(ErrorCode::FileReadError), "FileReadError");
    EXPECT_EQ(ToString(ErrorCode::FileWriteError), "FileWriteError");
    EXPECT_EQ(ToString(ErrorCode::InvalidPath), "InvalidPath");
    EXPECT_EQ(ToString(ErrorCode::PermissionDenied), "PermissionDenied");
    EXPECT_EQ(ToString(ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(ToString(ErrorCode::InvalidState), "InvalidState");
    EXPECT_EQ(ToString(ErrorCode::InvalidFormat), "InvalidFormat");
    EXPECT_EQ(ToString(ErrorCode::OutOfRange), "OutOfRange");
    EXPECT_EQ(ToString(ErrorCode::TypeMismatch), "TypeMismatch");
    EXPECT_EQ(ToString(ErrorCode::DeviceLost), "DeviceLost");
    EXPECT_EQ(ToString(ErrorCode::OutOfDeviceMemory), "OutOfDeviceMemory");
    EXPECT_EQ(ToString(ErrorCode::ShaderCompilationFailed), "ShaderCompilationFailed");
    EXPECT_EQ(ToString(ErrorCode::PipelineCreationFailed), "PipelineCreationFailed");
    EXPECT_EQ(ToString(ErrorCode::SwapchainOutOfDate), "SwapchainOutOfDate");
    EXPECT_EQ(ToString(ErrorCode::AssetNotLoaded), "AssetNotLoaded");
    EXPECT_EQ(ToString(ErrorCode::AssetLoadFailed), "AssetLoadFailed");
    EXPECT_EQ(ToString(ErrorCode::AssetTypeMismatch), "AssetTypeMismatch");
    EXPECT_EQ(ToString(ErrorCode::AssetDecodeFailed), "AssetDecodeFailed");
    EXPECT_EQ(ToString(ErrorCode::AssetUploadFailed), "AssetUploadFailed");
    EXPECT_EQ(ToString(ErrorCode::AssetUnsupportedFormat), "AssetUnsupportedFormat");
    EXPECT_EQ(ToString(ErrorCode::AssetInvalidData), "AssetInvalidData");
    EXPECT_EQ(ToString(ErrorCode::ThreadViolation), "ThreadViolation");
    EXPECT_EQ(ToString(ErrorCode::DeadlockDetected), "DeadlockDetected");
}

TEST(CoreError, ToStringUnknownCodeFallsThroughDefault)
{
    // The Unknown case, codes that are defined but not in the switch (e.g. UnsupportedFormat
    // and AssetLoaderMissing), and truly unmapped values all hit the default branch.
    EXPECT_EQ(ToString(ErrorCode::Unknown), "Unknown");
    EXPECT_EQ(ToString(ErrorCode::UnsupportedFormat), "Unknown");
    EXPECT_EQ(ToString(ErrorCode::AssetLoaderMissing), "Unknown");

    const auto synthetic = static_cast<ErrorCode>(987654u);
    EXPECT_EQ(ToString(synthetic), "Unknown");
}

TEST(CoreError, ToStringIsConstexpr)
{
    constexpr std::string_view s = ToString(ErrorCode::DeviceLost);
    static_assert(s == "DeviceLost");
    EXPECT_EQ(s, "DeviceLost");
}

// -----------------------------------------------------------------------------
// Expected<T> / Ok / Err
// -----------------------------------------------------------------------------

TEST(CoreError, OkWrapsValue)
{
    Expected<int> r = Ok(42);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(CoreError, OkWithMovableValue)
{
    Expected<std::string> r = Ok(std::string("hello"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "hello");
}

TEST(CoreError, ErrWrapsErrorCode)
{
    Expected<int> r = Err<int>(ErrorCode::FileNotFound);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::FileNotFound);
}

TEST(CoreError, ExpectedMonadicAndThen)
{
    Expected<int> r = Ok(5);
    auto chained = r
        .and_then([](int x) -> Expected<int> { return Ok(x * 2); })
        .and_then([](int x) -> Expected<int> { return Ok(x + 1); });
    ASSERT_TRUE(chained.has_value());
    EXPECT_EQ(*chained, 11);
}

TEST(CoreError, ExpectedMonadicAndThenPropagatesError)
{
    Expected<int> r = Err<int>(ErrorCode::OutOfMemory);
    auto chained = r.and_then([](int x) -> Expected<int> { return Ok(x * 2); });
    ASSERT_FALSE(chained.has_value());
    EXPECT_EQ(chained.error(), ErrorCode::OutOfMemory);
}

// -----------------------------------------------------------------------------
// Unit + Result void
// -----------------------------------------------------------------------------

TEST(CoreError, UnitIsEmptyType)
{
    static_assert(std::is_empty_v<Unit>);
    (void)unit;
    SUCCEED();
}

TEST(CoreError, ResultOkProducesValue)
{
    Result r = Ok();
    ASSERT_TRUE(r.has_value());
}

TEST(CoreError, ResultErrProducesErrorCode)
{
    Result r = Err(ErrorCode::AssetLoadFailed);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::AssetLoadFailed);
}

TEST(CoreError, ResultOkIsConstexpr)
{
    constexpr Result r = Ok();
    static_assert(r.has_value());
    EXPECT_TRUE(r.has_value());
}
