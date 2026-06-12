#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

import Extrinsic.Backends.Null;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;

TEST(RHIResourceSlotRecycling, NullDeviceReusesDestroyedBufferSlotWithNewGeneration)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device =
        Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    const Extrinsic::RHI::BufferDesc desc{
        .SizeBytes = 64u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = true,
        .DebugName = "SlotRecycling.Buffer",
    };

    const Extrinsic::RHI::BufferHandle first = device->CreateBuffer(desc);
    ASSERT_TRUE(first.IsValid());
    device->DestroyBuffer(first);

    Extrinsic::RHI::FrameHandle frame{};
    for (std::uint32_t i = 0; i < device->GetFramesInFlight() + 1u; ++i)
    {
        ASSERT_TRUE(device->BeginFrame(frame));
        device->EndFrame(frame);
    }

    const Extrinsic::RHI::BufferHandle reused = device->CreateBuffer(desc);
    ASSERT_TRUE(reused.IsValid());
    EXPECT_EQ(reused.Index, first.Index);
    EXPECT_GT(reused.Generation, first.Generation);

    device->DestroyBuffer(reused);
}
