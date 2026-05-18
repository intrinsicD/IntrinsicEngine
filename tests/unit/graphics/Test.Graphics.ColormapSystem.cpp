#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Core.Config.Render;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    constexpr std::size_t kExpectedColormapCount = static_cast<std::size_t>(Graphics::Colormap::kColormapCount);
    constexpr std::uint64_t kExpectedLutBytes = 256u * 4u;
}

TEST(GraphicsColormapSystem, InitializeSubmitsLutUploadsThroughTransferQueue)
{
    MockDevice device;
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::ColormapSystem colormaps;

    colormaps.Initialize(device, textures, samplers);

    EXPECT_TRUE(colormaps.IsInitialized());
    EXPECT_TRUE(colormaps.IsReady());
    EXPECT_TRUE(device.TextureWrites.empty());
    ASSERT_EQ(device.TransferQueue.TextureUploads.size(), kExpectedColormapCount);
    ASSERT_EQ(device.TransferQueue.Issued.size(), kExpectedColormapCount);

    for (const auto& upload : device.TransferQueue.TextureUploads)
    {
        EXPECT_TRUE(upload.Texture.IsValid());
        EXPECT_EQ(upload.SizeBytes, kExpectedLutBytes);
        EXPECT_EQ(upload.MipLevel, 0u);
        EXPECT_EQ(upload.ArrayLayer, 0u);
        ASSERT_EQ(upload.Data.size(), static_cast<std::size_t>(kExpectedLutBytes));
        EXPECT_EQ(std::to_integer<std::uint8_t>(upload.Data[3]), 255u);
        EXPECT_EQ(std::to_integer<std::uint8_t>(upload.Data[upload.Data.size() - 1u]), 255u);
    }

    EXPECT_NE(colormaps.GetBindlessIndex(Graphics::Colormap::Type::Viridis), RHI::kInvalidBindlessIndex);
    EXPECT_EQ(colormaps.GetBindlessIndex(Graphics::Colormap::Type::Count), RHI::kInvalidBindlessIndex);

    colormaps.Shutdown();
}

TEST(GraphicsColormapSystem, ReadinessTracksPendingTransferTokens)
{
    MockDevice device;
    device.TransferQueue.AlwaysComplete = false;
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::ColormapSystem colormaps;

    colormaps.Initialize(device, textures, samplers);

    EXPECT_TRUE(colormaps.IsInitialized());
    EXPECT_FALSE(colormaps.IsReady());
    EXPECT_EQ(colormaps.GetBindlessIndex(Graphics::Colormap::Type::Viridis), RHI::kInvalidBindlessIndex);

    device.TransferQueue.AlwaysComplete = true;
    EXPECT_TRUE(colormaps.IsReady());
    EXPECT_NE(colormaps.GetBindlessIndex(Graphics::Colormap::Type::Viridis), RHI::kInvalidBindlessIndex);

    colormaps.Shutdown();
}

TEST(GraphicsColormapSystem, InvalidTransferTokensKeepColormapsNotReady)
{
    MockDevice device;
    device.TransferQueue.FailTextureUploads = true;
    RHI::TextureManager textures{device, device.Bindless};
    RHI::SamplerManager samplers{device};
    Graphics::ColormapSystem colormaps;

    colormaps.Initialize(device, textures, samplers);

    EXPECT_TRUE(colormaps.IsInitialized());
    EXPECT_FALSE(colormaps.IsReady());
    EXPECT_EQ(colormaps.GetBindlessIndex(Graphics::Colormap::Type::Viridis), RHI::kInvalidBindlessIndex);
    EXPECT_TRUE(device.TextureWrites.empty());
    EXPECT_EQ(device.TransferQueue.TextureUploads.size(), kExpectedColormapCount);
    EXPECT_TRUE(device.TransferQueue.Issued.empty());

    colormaps.Shutdown();
}


