#include <chrono>
#include <thread>

#include <gtest/gtest.h>

import Extrinsic.Core.FrameClock;

TEST(CoreFrameClock, FirstFrameDeltaIsZero)
{
    const Extrinsic::Core::FrameClock clock{};

    EXPECT_EQ(clock.FrameDeltaClamped(), 0.0);
}

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

TEST(CoreFrameClock, ClampedDeltaRemainsThePreviousCompletedFrameDuration)
{
    using namespace std::chrono_literals;

    Extrinsic::Core::FrameClock clock{};

    clock.BeginFrame();
    std::this_thread::sleep_for(2ms);
    clock.EndFrame();

    const double completedFrameDelta = clock.LastRawDelta();
    ASSERT_GT(completedFrameDelta, 0.0);
    const double nonClampingLimit = completedFrameDelta * 2.0;
    EXPECT_DOUBLE_EQ(
        clock.FrameDeltaClamped(nonClampingLimit), completedFrameDelta);

    std::this_thread::sleep_for(2ms);

    EXPECT_DOUBLE_EQ(
        clock.FrameDeltaClamped(nonClampingLimit), completedFrameDelta);
    EXPECT_DOUBLE_EQ(
        clock.FrameDeltaClamped(completedFrameDelta / 2.0),
        completedFrameDelta / 2.0);
}

TEST(CoreFrameClock, NegativeClampLimitProducesNonNegativeDelta)
{
    Extrinsic::Core::FrameClock clock{};

    clock.BeginFrame();
    clock.EndFrame();

    EXPECT_EQ(clock.FrameDeltaClamped(-1.0), 0.0);
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
