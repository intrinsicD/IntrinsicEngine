// GRAPHICS-149: compute capability probes on an operational Vulkan device.
#include "RuntimeTestModule.hpp"
#include <bit>
#include <cstdio>
#include <memory>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;

namespace Runtime = Extrinsic::Runtime;
namespace
{
    // Exits after a few frames so the device has come up.
    class IdleApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Frame(double, double) override
        {
            if (++Frames >= 3) Kernel().RequestExit();
        }
        int Frames{0};
    };
}

TEST(GRAPHICS149ComputeCapabilities, ProbesReportTheVulkanDeviceConsistently)
{
    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.ReferenceScene.Enabled = false;
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::make_unique<IdleApp>());
    engine.Initialize();
    engine.Run();
    const auto& device = engine.GetDevice();
    if (!device.IsOperational())
    {
        engine.Shutdown();
        GTEST_SKIP() << "No operational Vulkan device";
    }
    std::printf("subgroup %u (arithmetic %d), shared memory %u bytes, int64 atomics %d, float64 %d\n",
                device.SubgroupSize(), int(device.SupportsSubgroupArithmetic()), device.MaxComputeSharedMemoryBytes(),
                int(device.SupportsShaderInt64Atomics()), int(device.SupportsShaderFloat64()));
    // Vulkan guarantees 16 KiB of shared memory and a power-of-two subgroup of at least one lane.
    EXPECT_GE(device.MaxComputeSharedMemoryBytes(), 16384u);
    EXPECT_GE(device.SubgroupSize(), 1u);
    EXPECT_LE(device.SubgroupSize(), 128u);
    EXPECT_TRUE(std::has_single_bit(device.SubgroupSize()));
    engine.Shutdown();
}
