#include <gtest/gtest.h>

import Extrinsic.Core.FrameClock;

TEST(CoreFrameClock, ClampedDeltaHonorsExplicitZeroLimit)
{
    Extrinsic::Core::FrameClock clock{};

    clock.BeginFrame();

    EXPECT_EQ(clock.FrameDeltaClamped(0.0), 0.0);
}

TEST(CoreFrameClock, EndFrameRecordsNonNegativeRawDelta)
{
    Extrinsic::Core::FrameClock clock{};

    clock.BeginFrame();
    clock.EndFrame();

    EXPECT_GE(clock.LastRawDelta(), 0.0);
}

TEST(CoreFrameClock, ResampleKeepsClockUsable)
{
    Extrinsic::Core::FrameClock clock{};

    clock.BeginFrame();
    clock.Resample();
    clock.EndFrame();

    EXPECT_GE(clock.LastRawDelta(), 0.0);
    EXPECT_EQ(clock.FrameDeltaClamped(0.0), 0.0);
}
