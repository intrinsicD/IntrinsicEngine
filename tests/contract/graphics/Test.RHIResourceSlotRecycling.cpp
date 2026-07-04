#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

import Extrinsic.Backends.Null;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;

namespace
{
    void AdvancePastRetirementWindow(Extrinsic::RHI::IDevice& device)
    {
        Extrinsic::RHI::FrameHandle frame{};
        for (std::uint32_t i = 0; i < device.GetFramesInFlight() + 1u; ++i)
        {
            ASSERT_TRUE(device.BeginFrame(frame));
            device.EndFrame(frame);
        }
    }
}

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

    AdvancePastRetirementWindow(*device);

    const Extrinsic::RHI::BufferHandle reused = device->CreateBuffer(desc);
    ASSERT_TRUE(reused.IsValid());
    EXPECT_EQ(reused.Index, first.Index);
    EXPECT_GT(reused.Generation, first.Generation);

    device->DestroyBuffer(reused);
}

TEST(RHIPlacedMemoryContract, NullDeviceReportsRequirementsAndCreatesMemoryBlock)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device =
        Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    const Extrinsic::RHI::BufferDesc deviceLocalDesc{
        .SizeBytes = 64u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = false,
        .DebugName = "PlacedMemory.DeviceLocalBuffer",
    };
    const Extrinsic::RHI::BufferDesc hostVisibleDesc{
        .SizeBytes = 64u,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "PlacedMemory.HostVisibleBuffer",
    };

    const Extrinsic::RHI::ResourceMemoryRequirements deviceLocalReq =
        device->GetBufferMemoryRequirements(deviceLocalDesc);
    const Extrinsic::RHI::ResourceMemoryRequirements hostVisibleReq =
        device->GetBufferMemoryRequirements(hostVisibleDesc);

    ASSERT_TRUE(deviceLocalReq.IsValid());
    ASSERT_TRUE(hostVisibleReq.IsValid());
    EXPECT_GE(deviceLocalReq.SizeBytes, deviceLocalDesc.SizeBytes);
    EXPECT_GE(hostVisibleReq.SizeBytes, hostVisibleDesc.SizeBytes);
    EXPECT_NE(deviceLocalReq.MemoryTypeBits, hostVisibleReq.MemoryTypeBits);

    const Extrinsic::RHI::MemoryBlockHandle block = device->CreateMemoryBlock({
        .SizeBytes = deviceLocalReq.SizeBytes,
        .AlignmentBytes = deviceLocalReq.AlignmentBytes,
        .MemoryTypeBits = deviceLocalReq.MemoryTypeBits,
        .DebugName = "PlacedMemory.DeviceLocalBlock",
    });
    ASSERT_TRUE(block.IsValid());

    const Extrinsic::RHI::MemoryBlockInfo info = device->GetMemoryBlockInfo(block);
    ASSERT_TRUE(info.IsValid);
    EXPECT_EQ(info.SizeBytes, deviceLocalReq.SizeBytes);
    EXPECT_EQ(info.AlignmentBytes, deviceLocalReq.AlignmentBytes);
    EXPECT_EQ(info.MemoryTypeBits, deviceLocalReq.MemoryTypeBits);
    EXPECT_NE(info.SelectedMemoryTypeBit & deviceLocalReq.MemoryTypeBits, 0u);

    device->DestroyMemoryBlock(block);
}

TEST(RHIPlacedMemoryContract, NullDeviceCreatesPlacedBufferAndTextureWithRecordedPlacement)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device =
        Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    const Extrinsic::RHI::BufferDesc bufferDesc{
        .SizeBytes = 1024u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = false,
        .DebugName = "PlacedMemory.Buffer",
    };
    const Extrinsic::RHI::TextureDesc textureDesc{
        .Width = 16u,
        .Height = 8u,
        .DepthOrArrayLayers = 1u,
        .MipLevels = 1u,
        .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
        .Dimension = Extrinsic::RHI::TextureDimension::Tex2D,
        .Usage = Extrinsic::RHI::TextureUsage::ColorTarget | Extrinsic::RHI::TextureUsage::Sampled,
        .InitialLayout = Extrinsic::RHI::TextureLayout::Undefined,
        .SampleCount = 1u,
        .DebugName = "PlacedMemory.Texture",
    };

    const Extrinsic::RHI::ResourceMemoryRequirements bufferReq =
        device->GetBufferMemoryRequirements(bufferDesc);
    const Extrinsic::RHI::ResourceMemoryRequirements textureReq =
        device->GetTextureMemoryRequirements(textureDesc);
    ASSERT_TRUE(bufferReq.IsValid());
    ASSERT_TRUE(textureReq.IsValid());

    const Extrinsic::RHI::MemoryBlockHandle bufferBlock = device->CreateMemoryBlock({
        .SizeBytes = bufferReq.SizeBytes,
        .AlignmentBytes = bufferReq.AlignmentBytes,
        .MemoryTypeBits = bufferReq.MemoryTypeBits,
        .DebugName = "PlacedMemory.BufferBlock",
    });
    const Extrinsic::RHI::MemoryBlockHandle textureBlock = device->CreateMemoryBlock({
        .SizeBytes = textureReq.SizeBytes + textureReq.AlignmentBytes,
        .AlignmentBytes = textureReq.AlignmentBytes,
        .MemoryTypeBits = textureReq.MemoryTypeBits,
        .DebugName = "PlacedMemory.TextureBlock",
    });
    ASSERT_TRUE(bufferBlock.IsValid());
    ASSERT_TRUE(textureBlock.IsValid());

    const Extrinsic::RHI::BufferHandle buffer = device->CreatePlacedBuffer({
        .Desc = bufferDesc,
        .Placement = {.Block = bufferBlock, .OffsetBytes = 0u},
    });
    const Extrinsic::RHI::TextureHandle texture = device->CreatePlacedTexture({
        .Desc = textureDesc,
        .Placement = {.Block = textureBlock, .OffsetBytes = textureReq.AlignmentBytes},
    });
    ASSERT_TRUE(buffer.IsValid());
    ASSERT_TRUE(texture.IsValid());

    const Extrinsic::RHI::PlacedResourceInfo bufferPlacement =
        device->GetBufferMemoryPlacement(buffer);
    const Extrinsic::RHI::PlacedResourceInfo texturePlacement =
        device->GetTextureMemoryPlacement(texture);

    ASSERT_TRUE(bufferPlacement.IsPlaced);
    ASSERT_TRUE(texturePlacement.IsPlaced);
    EXPECT_EQ(bufferPlacement.Block, bufferBlock);
    EXPECT_EQ(bufferPlacement.OffsetBytes, 0u);
    EXPECT_EQ(bufferPlacement.SizeBytes, bufferReq.SizeBytes);
    EXPECT_NE(bufferPlacement.MemoryTypeBit & bufferReq.MemoryTypeBits, 0u);
    EXPECT_EQ(texturePlacement.Block, textureBlock);
    EXPECT_EQ(texturePlacement.OffsetBytes, textureReq.AlignmentBytes);
    EXPECT_EQ(texturePlacement.SizeBytes, textureReq.SizeBytes);
    EXPECT_NE(texturePlacement.MemoryTypeBit & textureReq.MemoryTypeBits, 0u);

    device->DestroyBuffer(buffer);
    device->DestroyTexture(texture);
    device->DestroyMemoryBlock(bufferBlock);
    device->DestroyMemoryBlock(textureBlock);
}

TEST(RHIPlacedMemoryContract, NullDeviceRejectsIncompatibleAndMisalignedPlacedResources)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device =
        Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    const Extrinsic::RHI::BufferDesc deviceLocalDesc{
        .SizeBytes = 512u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = false,
        .DebugName = "PlacedMemory.DeviceLocalReject",
    };
    const Extrinsic::RHI::BufferDesc hostVisibleDesc{
        .SizeBytes = 512u,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "PlacedMemory.HostVisibleReject",
    };

    const Extrinsic::RHI::ResourceMemoryRequirements deviceLocalReq =
        device->GetBufferMemoryRequirements(deviceLocalDesc);
    const Extrinsic::RHI::ResourceMemoryRequirements hostVisibleReq =
        device->GetBufferMemoryRequirements(hostVisibleDesc);
    ASSERT_TRUE(deviceLocalReq.IsValid());
    ASSERT_TRUE(hostVisibleReq.IsValid());

    const Extrinsic::RHI::MemoryBlockHandle hostBlock = device->CreateMemoryBlock({
        .SizeBytes = deviceLocalReq.SizeBytes,
        .AlignmentBytes = deviceLocalReq.AlignmentBytes,
        .MemoryTypeBits = hostVisibleReq.MemoryTypeBits,
        .DebugName = "PlacedMemory.HostOnlyBlock",
    });
    const Extrinsic::RHI::MemoryBlockHandle deviceBlock = device->CreateMemoryBlock({
        .SizeBytes = deviceLocalReq.SizeBytes + deviceLocalReq.AlignmentBytes,
        .AlignmentBytes = deviceLocalReq.AlignmentBytes,
        .MemoryTypeBits = deviceLocalReq.MemoryTypeBits,
        .DebugName = "PlacedMemory.DeviceBlock",
    });
    const Extrinsic::RHI::MemoryBlockHandle smallBlock = device->CreateMemoryBlock({
        .SizeBytes = deviceLocalReq.SizeBytes - 1u,
        .AlignmentBytes = deviceLocalReq.AlignmentBytes,
        .MemoryTypeBits = deviceLocalReq.MemoryTypeBits,
        .DebugName = "PlacedMemory.SmallBlock",
    });
    ASSERT_TRUE(hostBlock.IsValid());
    ASSERT_TRUE(deviceBlock.IsValid());
    ASSERT_TRUE(smallBlock.IsValid());

    EXPECT_FALSE(device->CreatePlacedBuffer({
        .Desc = deviceLocalDesc,
        .Placement = {.Block = hostBlock, .OffsetBytes = 0u},
    }).IsValid());
    EXPECT_FALSE(device->CreatePlacedBuffer({
        .Desc = deviceLocalDesc,
        .Placement = {.Block = deviceBlock, .OffsetBytes = 1u},
    }).IsValid());
    EXPECT_FALSE(device->CreatePlacedBuffer({
        .Desc = deviceLocalDesc,
        .Placement = {.Block = smallBlock, .OffsetBytes = 0u},
    }).IsValid());

    device->DestroyMemoryBlock(hostBlock);
    device->DestroyMemoryBlock(deviceBlock);
    device->DestroyMemoryBlock(smallBlock);
}

TEST(RHIResourceSlotRecycling, NullDeviceReusesDestroyedMemoryBlockSlotWithNewGeneration)
{
    std::unique_ptr<Extrinsic::RHI::IDevice> device =
        Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    const Extrinsic::RHI::BufferDesc desc{
        .SizeBytes = 64u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage,
        .HostVisible = false,
        .DebugName = "SlotRecycling.PlacedMemory.Buffer",
    };
    const Extrinsic::RHI::ResourceMemoryRequirements req =
        device->GetBufferMemoryRequirements(desc);
    ASSERT_TRUE(req.IsValid());

    const Extrinsic::RHI::MemoryBlockHandle first = device->CreateMemoryBlock({
        .SizeBytes = req.SizeBytes,
        .AlignmentBytes = req.AlignmentBytes,
        .MemoryTypeBits = req.MemoryTypeBits,
        .DebugName = "SlotRecycling.PlacedMemory.First",
    });
    ASSERT_TRUE(first.IsValid());
    device->DestroyMemoryBlock(first);

    AdvancePastRetirementWindow(*device);

    const Extrinsic::RHI::MemoryBlockHandle reused = device->CreateMemoryBlock({
        .SizeBytes = req.SizeBytes,
        .AlignmentBytes = req.AlignmentBytes,
        .MemoryTypeBits = req.MemoryTypeBits,
        .DebugName = "SlotRecycling.PlacedMemory.Reused",
    });
    ASSERT_TRUE(reused.IsValid());
    EXPECT_EQ(reused.Index, first.Index);
    EXPECT_GT(reused.Generation, first.Generation);

    device->DestroyMemoryBlock(reused);
}
