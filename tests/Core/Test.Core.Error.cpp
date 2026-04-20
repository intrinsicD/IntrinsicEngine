#include <gtest/gtest.h>

import Extrinsic.Core.Error;

using namespace Extrinsic::Core;

TEST(CoreError, ToUnderlyingAndString)
{
    EXPECT_EQ(Error::ToUnderlying(ErrorCode::InvalidArgument), 300u);
    EXPECT_EQ(Error::ToString(ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(Error::ToString(static_cast<ErrorCode>(123456u)), "Unknown");
}

TEST(CoreError, OkErrHelpersForExpected)
{
    auto ok = Ok(42);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(*ok, 42);

    auto err = Err<int>(ErrorCode::OutOfRange);
    ASSERT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), ErrorCode::OutOfRange);
}

TEST(CoreError, OkErrHelpersForResult)
{
    auto ok = Ok();
    EXPECT_TRUE(ok.has_value());

    auto err = Err(ErrorCode::InvalidState);
    ASSERT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), ErrorCode::InvalidState);
}
