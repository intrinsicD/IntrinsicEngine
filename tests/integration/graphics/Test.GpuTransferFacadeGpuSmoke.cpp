#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] bool DrainQueueUntilComplete(RHI::ITransferQueue& queue,
                                               const RHI::TransferToken token)
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

    [[nodiscard]] bool DrainQueueUntilDelivered(RHI::ITransferQueue& queue,
                                                const std::uint32_t& callbackCount)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (std::chrono::steady_clock::now() < deadline)
        {
            queue.CollectCompleted();
            if (callbackCount > 0u)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        queue.CollectCompleted();
        return callbackCount > 0u;
    }

    [[nodiscard]] bool SubmitFacadeDrain(RHI::IDevice& device,
                                         Graphics::GpuTransfer& transfer)
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
        transfer.DrainCompleted(context);
        context.End();
        device.EndFrame(frame);
        device.Present(frame);
        return true;
    }

    [[nodiscard]] Graphics::GpuTransferReadbackTicket SubmitFacadeReadback(
        RHI::IDevice& device,
        Graphics::GpuTransfer& transfer,
        Graphics::GpuTransferReadbackDesc desc)
    {
        RHI::FrameHandle frame{};
        if (!device.BeginFrame(frame))
            return {};

        const std::array<RHI::QueueSubmitBatchDesc, 1> batches{
            RHI::QueueSubmitBatchDesc{.Queue = RHI::QueueAffinity::Transfer},
        };
        const RHI::FrameQueueSubmitPlanDesc plan{
            .Batches = std::span<const RHI::QueueSubmitBatchDesc>{batches},
        };
        if (!device.BeginFrameQueueSubmitPlan(frame, plan))
            return {};

        RHI::ICommandContext& context =
            device.GetQueueSubmitContext(RHI::QueueAffinity::Transfer, frame.FrameIndex, 0u);
        context.Begin();
        const Graphics::GpuTransferReadbackTicket ticket =
            transfer.ScheduleReadback(context, std::move(desc));
        context.End();
        device.EndFrame(frame);
        device.Present(frame);
        return ticket;
    }
}

TEST(GpuTransferFacadeGpuSmoke, UploadThenReadbackRoundTripsThroughFacadeWithoutWaitIdle)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; gpu;vulkan GpuTransfer smoke is opt-in.";
    }

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Title = "Intrinsic GpuTransfer facade gpu;vulkan smoke";
    windowConfig.Width = 64;
    windowConfig.Height = 64;
    windowConfig.Resizable = false;

    std::unique_ptr<Extrinsic::Platform::IWindow> window =
        Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
    ASSERT_NE(window, nullptr);
    if (window->GetNativeHandle() == nullptr)
    {
        GTEST_SKIP() << "GLFW window creation failed; gpu;vulkan GpuTransfer smoke requires a native surface.";
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

    constexpr std::array<std::byte, 16> expected{
        std::byte{0x0f}, std::byte{0x1e}, std::byte{0x2d}, std::byte{0x3c},
        std::byte{0x4b}, std::byte{0x5a}, std::byte{0x69}, std::byte{0x78},
        std::byte{0x87}, std::byte{0x96}, std::byte{0xa5}, std::byte{0xb4},
        std::byte{0xc3}, std::byte{0xd2}, std::byte{0xe1}, std::byte{0xf0},
    };

    const RHI::BufferDesc bufferDesc{
        .SizeBytes = expected.size(),
        .Usage = RHI::BufferUsage::Storage |
                 RHI::BufferUsage::TransferDst |
                 RHI::BufferUsage::TransferSrc,
        .HostVisible = false,
        .DebugName = "GpuTransferFacadeGpuSmoke.DeviceLocalBuffer",
    };
    const RHI::BufferHandle buffer = device->CreateBuffer(bufferDesc);
    ASSERT_TRUE(buffer.IsValid());

    RHI::ITransferQueue& queue = device->GetTransferQueue();
    Graphics::GpuTransfer transfer{queue};
    const std::uint64_t fallbackUploadsBefore =
        Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount();

    const Graphics::GpuTransferUploadTicket upload =
        transfer.ScheduleUpload(Graphics::GpuTransferUploadDesc{
            .Destination = buffer,
            .DestinationDesc = bufferDesc,
            .Source = std::span<const std::byte>{expected},
            .DestinationOffsetBytes = 0u,
        });
    ASSERT_TRUE(upload.IsValid());
    EXPECT_FALSE(transfer.IsReady(upload));
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
              fallbackUploadsBefore)
        << "service-ready GpuTransfer smoke routed upload through the fallback transfer queue";

    ASSERT_TRUE(DrainQueueUntilComplete(queue, upload.Token))
        << "timed out waiting for facade upload token completion";
    EXPECT_FALSE(transfer.IsReady(upload));
    ASSERT_TRUE(SubmitFacadeDrain(*device, transfer))
        << "failed to submit facade upload-ready barrier";
    EXPECT_TRUE(transfer.IsReady(upload));

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

    const Graphics::GpuTransferReadbackTicket readback =
        SubmitFacadeReadback(*device,
                             transfer,
                             Graphics::GpuTransferReadbackDesc{
                                 .Source = buffer,
                                 .SourceDesc = bufferDesc,
                                 .SourceRange = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = actual.size()},
                                 .SourceAccess = RHI::MemoryAccess::ShaderRead,
                                 .Sink = std::move(sink),
                             });
    ASSERT_TRUE(readback.IsValid());
    EXPECT_FALSE(transfer.IsDelivered(readback));

    ASSERT_TRUE(DrainQueueUntilDelivered(queue, callbackCount))
        << "timed out waiting for facade readback delivery";
    EXPECT_EQ(callbackCount, 1u);
    EXPECT_EQ(actual, expected);
    EXPECT_FALSE(transfer.IsDelivered(readback));
    ASSERT_TRUE(SubmitFacadeDrain(*device, transfer))
        << "failed to submit facade readback drain";
    EXPECT_TRUE(transfer.IsDelivered(readback));

    const Graphics::GpuTransferDiagnostics diagnostics = transfer.GetDiagnostics();
    EXPECT_EQ(diagnostics.UploadsScheduled, 1u);
    EXPECT_EQ(diagnostics.UploadsReady, 1u);
    EXPECT_EQ(diagnostics.UploadBarriersEmitted, 1u);
    EXPECT_EQ(diagnostics.ReadbacksIssued, 1u);
    EXPECT_EQ(diagnostics.ReadbacksDelivered, 1u);
    EXPECT_EQ(diagnostics.ReadbackBarriersEmitted, 1u);

    device->DestroyBuffer(buffer);
    device->Shutdown();
}
