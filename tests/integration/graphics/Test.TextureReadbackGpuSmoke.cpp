#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace
{
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] bool DrainUntilComplete(RHI::ITransferQueue& queue,
                                          RHI::TransferToken token)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            if (queue.IsComplete(token))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        return queue.IsComplete(token);
    }

    [[nodiscard]] bool DrainUntilComplete(RHI::ITransferQueue& queue,
                                          RHI::ReadbackToken token)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            if (queue.IsComplete(token))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        return queue.IsComplete(token);
    }

    [[nodiscard]] bool TransitionTexture(RHI::IDevice& device,
                                         RHI::TextureHandle texture,
                                         RHI::TextureLayout before,
                                         RHI::TextureLayout after)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return false;

        const std::array<RHI::QueueSubmitBatchDesc, 1> batches{
            RHI::QueueSubmitBatchDesc{.Queue = RHI::QueueAffinity::Transfer},
        };
        const RHI::FrameQueueSubmitPlanDesc plan{
            .Batches = std::span<const RHI::QueueSubmitBatchDesc>{batches},
        };
        if (!device.BeginFrameQueueSubmitPlan(frame, plan))
            return false;

        RHI::ICommandContext& context =
            device.GetQueueSubmitContext(RHI::QueueAffinity::Transfer, frame.FrameIndex, 0u);
        context.Begin();
        context.TextureBarrier(texture, before, after);
        context.End();
        device.EndFrame(frame);
        device.Present(frame);
        return true;
    }
}

TEST(TextureReadbackGpuSmoke, TransferQueueDownloadsTextureSubresourceWithoutWaitIdle)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; gpu;vulkan texture readback smoke is opt-in.";
    }

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Title = "Intrinsic Texture Readback gpu;vulkan smoke";
    windowConfig.Width = 64;
    windowConfig.Height = 64;
    windowConfig.Resizable = false;

    std::unique_ptr<Extrinsic::Platform::IWindow> window =
        Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
    ASSERT_NE(window, nullptr);
    if (window->GetNativeHandle() == nullptr)
    {
        GTEST_SKIP() << "GLFW window creation failed; gpu;vulkan texture readback smoke requires a native surface.";
    }

    Extrinsic::Core::Config::RenderConfig renderConfig{};
    renderConfig.EnablePromotedVulkanDevice = true;
    renderConfig.EnableValidation = false;

    std::unique_ptr<RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);

    device->Initialize(RHI::MakeDeviceCreateDesc(
        renderConfig,
        window->GetFramebufferExtent(),
        window->GetNativeHandle()));

    const auto bootstrap = Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
    const auto services = Extrinsic::Backends::Vulkan::GetVulkanServiceDiagnosticsSnapshot();
    if (bootstrap.Status != Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages ||
        services.Status != Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready ||
        !services.PublicTransferQueueExposed)
    {
        device->Shutdown();
        GTEST_SKIP() << "Promoted Vulkan transfer queue was not service-ready on this host.";
    }

    const RHI::TextureDesc desc{
        .Width = 2u,
        .Height = 2u,
        .DepthOrArrayLayers = 2u,
        .MipLevels = 2u,
        .Fmt = RHI::Format::RGBA8_UNORM,
        .Dimension = RHI::TextureDimension::Tex2D,
        .Usage = RHI::TextureUsage::Sampled |
                 RHI::TextureUsage::TransferDst |
                 RHI::TextureUsage::TransferSrc,
        .InitialLayout = RHI::TextureLayout::Undefined,
        .DebugName = "TextureReadbackGpuSmoke.Texture",
    };
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const RHI::TextureUploadLayout& layout = *layoutOr;

    std::vector<std::byte> upload(static_cast<std::size_t>(layout.TotalBytes));
    for (std::size_t i = 0; i < upload.size(); ++i)
        upload[i] = static_cast<std::byte>(0x10u + static_cast<unsigned>(i));

    const RHI::TextureUploadSubresource* targetSubresource = nullptr;
    for (const RHI::TextureUploadSubresource& subresource : layout.Subresources)
    {
        if (subresource.MipLevel == 1u && subresource.ArrayLayer == 1u)
        {
            targetSubresource = &subresource;
            break;
        }
    }
    ASSERT_NE(targetSubresource, nullptr);
    ASSERT_EQ(targetSubresource->SizeBytes, 4u);

    std::array<std::byte, 4> expected{};
    std::copy_n(upload.begin() + static_cast<std::ptrdiff_t>(targetSubresource->OffsetBytes),
                expected.size(),
                expected.begin());

    const RHI::TextureHandle texture = device->CreateTexture(desc);
    ASSERT_TRUE(texture.IsValid());

    RHI::ITransferQueue& queue = device->GetTransferQueue();
    const std::uint64_t fallbackUploadsBefore =
        Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount();

    const RHI::TransferToken uploadToken =
        queue.UploadTextureFullChain(texture, std::span<const std::byte>{upload});
    ASSERT_TRUE(uploadToken.IsValid());
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
              fallbackUploadsBefore)
        << "service-ready texture readback smoke routed upload through the fallback transfer queue";
    ASSERT_TRUE(DrainUntilComplete(queue, uploadToken))
        << "timed out waiting for transfer-queue texture upload completion";

    ASSERT_TRUE(TransitionTexture(*device,
                                  texture,
                                  RHI::TextureLayout::ShaderReadOnly,
                                  RHI::TextureLayout::TransferSrc))
        << "failed to submit ShaderReadOnly->TransferSrc texture barrier";

    std::array<std::byte, expected.size()> actual{};
    std::uint32_t callbackCount = 0u;
    RHI::ReadbackSink sink{
        .Destination = std::span<std::byte>{actual},
        .Callback = [&](std::span<const std::byte> bytes)
        {
            ++callbackCount;
            EXPECT_EQ(bytes.size_bytes(), expected.size());
        },
    };

    const RHI::ReadbackToken readback =
        queue.DownloadTexture(texture,
                              RHI::TextureLayout::TransferSrc,
                              targetSubresource->MipLevel,
                              targetSubresource->ArrayLayer,
                              std::move(sink));
    ASSERT_TRUE(readback.IsValid());
    EXPECT_FALSE(queue.IsComplete(readback));

    ASSERT_TRUE(DrainUntilComplete(queue, readback))
        << "timed out waiting for transfer-queue texture readback completion";
    EXPECT_EQ(callbackCount, 1u);
    EXPECT_EQ(actual, expected);

    const RHI::TransferQueueDiagnostics diagnostics = queue.GetDiagnostics();
    EXPECT_GE(diagnostics.DownloadsQueued, 1u);
    EXPECT_GE(diagnostics.DownloadsCompleted, 1u);
    EXPECT_GE(diagnostics.ReadbackBytesStaged, expected.size());
    EXPECT_GE(diagnostics.ReadbackRingHighWaterBytes, expected.size());

    ASSERT_TRUE(TransitionTexture(*device,
                                  texture,
                                  RHI::TextureLayout::TransferSrc,
                                  RHI::TextureLayout::ShaderReadOnly))
        << "failed to submit TransferSrc->ShaderReadOnly texture barrier";

    device->DestroyTexture(texture);
    device->Shutdown();
}
