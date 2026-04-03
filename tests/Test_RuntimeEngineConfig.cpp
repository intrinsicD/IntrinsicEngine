#include <gtest/gtest.h>

import Runtime.Engine;

TEST(RuntimeEngineConfig, ValidateEngineConfig_ReportsErrorsForHardInvalidFields)
{
    Runtime::EngineConfig config{};
    config.Width = 0;
    config.Height = -10;
    config.FrameContextCount = 0;
    config.BenchmarkMode = true;
    config.BenchmarkFrames = 0;
    config.BenchmarkOutputPath.clear();

    const Runtime::EngineConfigValidationResult validation = Runtime::ValidateEngineConfig(config);

    EXPECT_TRUE(validation.HasErrors());
    EXPECT_GT(validation.Issues.size(), 0u);
}

TEST(RuntimeEngineConfig, ValidateEngineConfig_SanitizesInvalidFrameLoopAndPacingValues)
{
    Runtime::EngineConfig config{};
    config.FixedStepHz = -5.0;
    config.MaxFrameDeltaSeconds = -1.0;
    config.MaxSubstepsPerFrame = 0;
    config.FramePacing.ActiveFps = -1.0;
    config.FramePacing.IdleFps = -1.0;
    config.FramePacing.IdleTimeoutSeconds = -1.0;
    config.FrameArenaSize = 1024;

    const Runtime::EngineConfigValidationResult validation = Runtime::ValidateEngineConfig(config);

    EXPECT_FALSE(validation.HasErrors());
    EXPECT_DOUBLE_EQ(validation.Sanitized.FixedStepHz, Runtime::DefaultFixedStepHz);
    EXPECT_DOUBLE_EQ(validation.Sanitized.MaxFrameDeltaSeconds, Runtime::DefaultMaxFrameDeltaSeconds);
    EXPECT_EQ(validation.Sanitized.MaxSubstepsPerFrame, Runtime::DefaultMaxSubstepsPerFrame);
    EXPECT_DOUBLE_EQ(validation.Sanitized.FramePacing.ActiveFps, 60.0);
    EXPECT_DOUBLE_EQ(validation.Sanitized.FramePacing.IdleFps, 15.0);
    EXPECT_DOUBLE_EQ(validation.Sanitized.FramePacing.IdleTimeoutSeconds, 2.0);
    EXPECT_GE(validation.Sanitized.FrameArenaSize, 64u * 1024u);
}
