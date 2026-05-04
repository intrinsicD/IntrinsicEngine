#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Runtime.Engine;

TEST(RuntimeDeviceSelection, DefaultsToNullFallbackForVulkanBackend)
{
    Extrinsic::Core::Config::RenderConfig config{};

    const Extrinsic::Runtime::RuntimeDeviceSelection selection =
        Extrinsic::Runtime::SelectRuntimeDeviceBackend(config, true);

    EXPECT_FALSE(config.EnablePromotedVulkanDevice);
    EXPECT_FALSE(selection.UsePromotedVulkanDevice);
    EXPECT_TRUE(selection.FallsBackToNullDevice);
}

TEST(RuntimeDeviceSelection, RequiresCompiledPromotedVulkanBackend)
{
    Extrinsic::Core::Config::RenderConfig config{};
    config.EnablePromotedVulkanDevice = true;

    const Extrinsic::Runtime::RuntimeDeviceSelection selection =
        Extrinsic::Runtime::SelectRuntimeDeviceBackend(config, false);

    EXPECT_FALSE(selection.UsePromotedVulkanDevice);
    EXPECT_TRUE(selection.FallsBackToNullDevice);
}

TEST(RuntimeDeviceSelection, SelectsPromotedVulkanOnlyWhenConfigAndBuildOptIn)
{
    Extrinsic::Core::Config::RenderConfig config{};
    config.EnablePromotedVulkanDevice = true;

    const Extrinsic::Runtime::RuntimeDeviceSelection selection =
        Extrinsic::Runtime::SelectRuntimeDeviceBackend(config, true);

    EXPECT_TRUE(selection.UsePromotedVulkanDevice);
    EXPECT_FALSE(selection.FallsBackToNullDevice);
}

