#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

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

TEST(VulkanFailClosedContract, FallbackTransferQueueIncrementsUploadCounter)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t before =
        Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount();

    Extrinsic::RHI::ITransferQueue& queue = device->GetTransferQueue();

    const Extrinsic::RHI::TransferToken bufferTokenPtr = queue.UploadBuffer({}, nullptr, 0u, 0u);
    EXPECT_FALSE(bufferTokenPtr.IsValid());
    EXPECT_TRUE(queue.IsComplete(bufferTokenPtr));

    const std::span<const std::byte> emptyBytes{};
    const Extrinsic::RHI::TransferToken bufferTokenSpan = queue.UploadBuffer({}, emptyBytes, 0u);
    EXPECT_FALSE(bufferTokenSpan.IsValid());
    EXPECT_TRUE(queue.IsComplete(bufferTokenSpan));

    const Extrinsic::RHI::TransferToken textureToken = queue.UploadTexture({}, nullptr, 0u, 0u, 0u);
    EXPECT_FALSE(textureToken.IsValid());
    EXPECT_TRUE(queue.IsComplete(textureToken));

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
              before + 3u);
}

TEST(VulkanFailClosedContract, CreatePipelineIncrementsCreationCounter)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t before =
        Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount();

    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());
    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount(),
              before + 2u);
}

TEST(VulkanFailClosedContract, CreatePipelineReportsPreBringUpReason)
{
    // A freshly constructed VulkanDevice has not been Initialize()d, so the
    // global pipeline layout is missing and m_Operational is false. The
    // fail-closed CreatePipeline guard must report PreBringUp rather than the
    // ShaderMissing reason that only fires on operational devices.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetLastFallbackPipelineReason(),
              Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp);
}

// ShaderMissing requires an operational VulkanDevice, which the CPU-only
// contract surface cannot reach until real swapchain/device bring-up lands.
// The enum value is asserted here so accidental renames keep the contract
// stable for the future operational-path test in
// `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md`.
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::FallbackPipelineReason::None) == 0u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp) == 1u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::FallbackPipelineReason::ShaderMissing) == 2u);
