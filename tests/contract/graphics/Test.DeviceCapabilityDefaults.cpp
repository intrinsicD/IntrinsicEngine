// GRAPHICS-149: devices without compute probes answer conservatively.
#include <memory>
#include <gtest/gtest.h>
import Extrinsic.RHI.Device;
import Extrinsic.Backends.Null;

TEST(DeviceCapabilityDefaults, NullDeviceReportsConservativeComputeCapabilities)
{
    const auto device = Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_TRUE(device);
    EXPECT_FALSE(device->SupportsShaderFloat64());
    EXPECT_FALSE(device->SupportsShaderInt64Atomics());
    EXPECT_FALSE(device->SupportsSubgroupArithmetic());
    EXPECT_EQ(device->SubgroupSize(), 0u) << "no width may be assumed";
    EXPECT_EQ(device->MaxComputeSharedMemoryBytes(), 16384u) << "the Vulkan-guaranteed minimum";
}
