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
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace
{
    [[nodiscard]] bool DrainUntilComplete(Extrinsic::RHI::ITransferQueue& queue,
                                          const Extrinsic::RHI::ReadbackToken token)
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
}

TEST(BufferReadbackGpuSmoke, TransferQueueDownloadsDeviceLocalBufferWithoutWaitIdle)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; gpu;vulkan buffer readback smoke is opt-in.";
    }

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Title = "Intrinsic Buffer Readback gpu;vulkan smoke";
    windowConfig.Width = 64;
    windowConfig.Height = 64;
    windowConfig.Resizable = false;

    std::unique_ptr<Extrinsic::Platform::IWindow> window =
        Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
    ASSERT_NE(window, nullptr);
    if (window->GetNativeHandle() == nullptr)
    {
        GTEST_SKIP() << "GLFW window creation failed; gpu;vulkan buffer readback smoke requires a native surface.";
    }

    Extrinsic::Core::Config::RenderConfig renderConfig{};
    renderConfig.EnablePromotedVulkanDevice = true;
    renderConfig.EnableValidation = false;

    std::unique_ptr<Extrinsic::RHI::IDevice> device =
        Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);

    device->Initialize(Extrinsic::RHI::MakeDeviceCreateDesc(
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
        std::byte{0x00}, std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
        std::byte{0x44}, std::byte{0x55}, std::byte{0x66}, std::byte{0x77},
        std::byte{0x88}, std::byte{0x99}, std::byte{0xaa}, std::byte{0xbb},
        std::byte{0xcc}, std::byte{0xdd}, std::byte{0xee}, std::byte{0xff},
    };

    const Extrinsic::RHI::BufferHandle buffer = device->CreateBuffer({
        .SizeBytes = expected.size(),
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst | Extrinsic::RHI::BufferUsage::TransferSrc,
        .HostVisible = false,
        .DebugName = "BufferReadbackGpuSmoke.DeviceLocalBuffer",
    });
    ASSERT_TRUE(buffer.IsValid());

    Extrinsic::RHI::ITransferQueue& queue = device->GetTransferQueue();
    const std::uint64_t fallbackUploadsBefore =
        Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount();

    const Extrinsic::RHI::TransferToken upload =
        queue.UploadBuffer(buffer, std::span<const std::byte>{expected}, 0u);
    ASSERT_TRUE(upload.IsValid());
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
              fallbackUploadsBefore)
        << "service-ready readback smoke routed upload through the fallback transfer queue";

    std::array<std::byte, expected.size()> actual{};
    std::uint32_t callbackCount = 0u;
    Extrinsic::RHI::ReadbackSink sink{
        .Destination = std::span<std::byte>{actual},
        .Callback = [&](std::span<const std::byte> bytes)
        {
            ++callbackCount;
            EXPECT_EQ(bytes.size_bytes(), expected.size());
        },
    };

    const Extrinsic::RHI::ReadbackToken readback =
        queue.DownloadBuffer(buffer, actual.size(), 0u, std::move(sink));
    ASSERT_TRUE(readback.IsValid());
    EXPECT_FALSE(queue.IsComplete(readback));

    ASSERT_TRUE(DrainUntilComplete(queue, readback))
        << "timed out waiting for transfer-queue readback completion";
    EXPECT_EQ(callbackCount, 1u);
    EXPECT_EQ(actual, expected);

    const Extrinsic::RHI::TransferQueueDiagnostics diagnostics = queue.GetDiagnostics();
    EXPECT_GE(diagnostics.DownloadsQueued, 1u);
    EXPECT_GE(diagnostics.DownloadsCompleted, 1u);
    EXPECT_GE(diagnostics.ReadbackBytesStaged, expected.size());
    EXPECT_GE(diagnostics.ReadbackRingHighWaterBytes, expected.size());

    device->DestroyBuffer(buffer);
    device->Shutdown();
}
