#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

import Extrinsic.Backends.Vulkan;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;

TEST(VulkanFailClosedContract, DeviceConstructorIsFailClosedWithoutGpuBringup)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);

    EXPECT_FALSE(device->IsOperational());

    EXPECT_FALSE(device->CreateBuffer(Extrinsic::RHI::BufferDesc{.SizeBytes = 64u}).IsValid());
    EXPECT_FALSE(device->CreateTexture(Extrinsic::RHI::TextureDesc{
        .Width = 1u,
        .Height = 1u,
        .DepthOrArrayLayers = 1u,
        .MipLevels = 1u,
        .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
        .Dimension = Extrinsic::RHI::TextureDimension::Tex2D,
        .Usage = Extrinsic::RHI::TextureUsage::Sampled,
    }).IsValid());
    EXPECT_FALSE(device->CreateSampler(Extrinsic::RHI::SamplerDesc{}).IsValid());
    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());

    const std::uint64_t beforeFallbackAllocations =
        Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount();
    EXPECT_EQ(device->GetBindlessHeap().AllocateTextureSlot({}, {}), Extrinsic::RHI::kInvalidBindlessIndex);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount(),
              beforeFallbackAllocations + 1u);

    const Extrinsic::RHI::TransferToken token = device->GetTransferQueue().UploadBuffer({}, nullptr, 0u, 0u);
    EXPECT_FALSE(token.IsValid());
    EXPECT_TRUE(device->GetTransferQueue().IsComplete(token));
}
