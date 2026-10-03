#include <memory>
#include <string>
#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Runtime.DeviceBootstrap;
import Extrinsic.Runtime.DiagnosticsStream;
import Extrinsic.RHI.Device;

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

TEST(RuntimeDeviceSelection, DiagnosticsReportsNullFallbackFromFrozenStartupConfiguration)
{
    namespace R = Extrinsic::Runtime;
    Extrinsic::Core::Config::RenderConfig config{};
    const auto device = R::CreateRuntimeDevice(config);
    R::EditorDiagnosticsStream stream;
    stream.AttachDevice(config, *device);
    config.EnablePromotedVulkanDevice = true;
    const auto status = stream.ReadDeviceStatus();
    EXPECT_EQ(status.RequestedBackend, "vulkan");
    EXPECT_EQ(status.ActualBackend, "null");
    EXPECT_FALSE(status.IsOperational);
    EXPECT_FALSE(status.ValidationEnabled);
    EXPECT_EQ(status.ValidationErrorCount, 0u);
    EXPECT_NE(status.FallbackReason.find("disabled"), std::string::npos);
    stream.DetachDevice();
    EXPECT_EQ(stream.ReadDeviceStatus().ActualBackend, "unavailable");
}
