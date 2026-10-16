#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>

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
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace
{
    namespace RHI = Extrinsic::RHI;
    namespace Vulkan = Extrinsic::Backends::Vulkan;

    template <typename Token>
    bool Drain(RHI::ITransferQueue& queue, Token token)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do
        {
            queue.CollectCompleted();
            if (queue.IsComplete(token)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        } while (std::chrono::steady_clock::now() < deadline);
        return queue.IsComplete(token);
    }

    class VulkanTransferLifetimeGpuSmoke : public testing::Test
    {
    protected:
        bool Initialize(bool validation = false)
        {
            if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
                return Unavailable("GLFW cannot initialize.");
            Extrinsic::Core::Config::WindowConfig windowConfig;
            windowConfig.Title = "Intrinsic Vulkan transfer/lifetime smoke";
            windowConfig.Width = 64;
            windowConfig.Height = 64;
            windowConfig.Resizable = false;
            Window = Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
            if (!Window || !Window->GetNativeHandle())
                return Unavailable("Native GLFW surface is unavailable.");
            Extrinsic::Core::Config::RenderConfig config;
            config.EnablePromotedVulkanDevice = true;
            config.EnableValidation = validation;
            Device = Vulkan::CreateVulkanDevice();
            if (!Device) return Unavailable("Vulkan device factory is unavailable.");
            Device->Initialize(RHI::MakeDeviceCreateDesc(
                config, Window->GetFramebufferExtent(), Window->GetNativeHandle()));
            const auto bootstrap = Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
            const auto services = Vulkan::GetVulkanServiceDiagnosticsSnapshot();
            if (bootstrap.Status != Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages ||
                services.Status != Vulkan::VulkanServiceBootstrapStatus::Ready ||
                !services.PublicTransferQueueExposed)
                return Unavailable("Vulkan surface/transfer services are unavailable.");
            if (validation && (!bootstrap.ValidationEnabled || !bootstrap.DebugUtilsEnabled))
                return Unavailable("Vulkan validation layer/debug-utils are unavailable.");
            // Backend-only test gate: no renderer recipe was executed. The
            // direct-device transport precedent publishes this prerequisite.
            Device->NoteRecipeGraphValidation(true);
            if (!Device->IsOperational())
                return Unavailable("Vulkan backend operational prerequisites are unavailable.");
            return true;
        }

        void TearDown() override
        {
            if (Device) Device->Shutdown();
        }

        bool Unavailable(std::string reason)
        {
            SkipReason = std::move(reason);
            return false;
        }

        RHI::BufferHandle MakeBuffer()
        {
            return Device->CreateBuffer({
                .SizeBytes = 4u,
                .Usage = RHI::BufferUsage::TransferDst,
                .DebugName = "TransferLifetimeSmoke.Buffer",
            });
        }

        RHI::TextureHandle MakeTexture(RHI::Format format = RHI::Format::RGBA8_UNORM,
                                       std::uint32_t extent = 8u)
        {
            return Device->CreateTexture({
                .Width = extent, .Height = extent, .MipLevels = 1u, .Fmt = format,
                .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst |
                         RHI::TextureUsage::TransferSrc,
                .DebugName = "TransferLifetimeSmoke.Texture",
            });
        }

        bool RecordFrame(const RHI::FrameHandle& frame, RHI::BufferHandle buffer = {},
                         RHI::TextureHandle readbackTexture = {})
        {
            auto& context = Device->GetGraphicsContext(frame.FrameIndex);
            context.Begin();
            if (buffer.IsValid()) context.FillBuffer(buffer, 0u, 4u, 0x12345678u);
            if (readbackTexture.IsValid())
                context.TextureBarrier(readbackTexture, RHI::TextureLayout::ShaderReadOnly,
                                       RHI::TextureLayout::TransferSrc);
            const auto backbuffer = Device->GetBackbufferHandle(frame);
            if (!backbuffer.IsValid()) return false;
            context.TextureBarrier(backbuffer, RHI::TextureLayout::Undefined, RHI::TextureLayout::Present);
            context.End();
            return true;
        }

        bool SubmitPresent(const RHI::FrameHandle& frame)
        {
            Device->EndFrame(frame);
            if (Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot().EndStatus !=
                Vulkan::VulkanFrameEndStatus::Submitted) return false;
            Device->Present(frame);
            const auto status = Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot().PresentStatus;
            return status == Vulkan::VulkanFramePresentStatus::Presented ||
                   status == Vulkan::VulkanFramePresentStatus::Suboptimal;
        }

        static std::uint64_t Deletes()
        {
            return Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot().DeferredDeletesExecuted;
        }

        void ExpectRetirementAtSlot(std::uint32_t submittingSlot, std::uint64_t before)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
            bool reused = false;
            for (unsigned count = 0; count < 6u && std::chrono::steady_clock::now() < deadline; ++count)
            {
                RHI::FrameHandle frame;
                ASSERT_TRUE(Device->BeginFrame(frame));
                if (frame.FrameIndex == submittingSlot) reused = true;
                EXPECT_EQ(Deletes() - before, reused ? 1u : 0u);
                ASSERT_TRUE(RecordFrame(frame));
                ASSERT_TRUE(SubmitPresent(frame));
                if (reused) break;
            }
            ASSERT_TRUE(reused) << "submitting frame slot was not reused within the bounded frame loop";
            RHI::FrameHandle extra;
            ASSERT_TRUE(Device->BeginFrame(extra));
            EXPECT_EQ(Deletes() - before, 1u);
            ASSERT_TRUE(RecordFrame(extra));
            ASSERT_TRUE(SubmitPresent(extra));
            Device->Shutdown();
            EXPECT_EQ(Deletes() - before, 1u);
        }

        std::unique_ptr<Extrinsic::Platform::IWindow> Window;
        std::unique_ptr<RHI::IDevice> Device;
        std::string SkipReason;
    };
}

// Backend lifecycle coverage for a renderer abort before command recording starts.
// The executable guard must be present before this test is run against missing cleanup.
TEST_F(VulkanTransferLifetimeGpuSmoke, NeverRecordedFramesPresentAndKeepSlotsReusable)
{
    if (!Initialize(true)) GTEST_SKIP() << SkipReason;
    const auto validationErrors = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
    // Four recorded controls prove all three slots and one reuse. Alternating
    // omitted/recorded frames then exercise omitted recording on every slot.
    for (unsigned step = 0; step < 10u; ++step)
    {
        SCOPED_TRACE(step);
        RHI::FrameHandle frame;
        ASSERT_TRUE(Device->BeginFrame(frame));
        const bool omitRecording = step >= 4u && step % 2u == 0u;
        if (!omitRecording) ASSERT_TRUE(RecordFrame(frame));
        Device->EndFrame(frame);
        ASSERT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, validationErrors);
        ASSERT_EQ(Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot().EndStatus,
                  Vulkan::VulkanFrameEndStatus::Submitted);
        // Stop before Present or another acquire if the guarded partial fix refused submission.
        Device->Present(frame);
        const auto presented = Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot().PresentStatus;
        ASSERT_TRUE(presented == Vulkan::VulkanFramePresentStatus::Presented ||
                    presented == Vulkan::VulkanFramePresentStatus::Suboptimal);
        ASSERT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, validationErrors);
    }
    Device->WaitIdle();
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, validationErrors);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, SingleSubresourceRejectsWrongByteCounts)
{
    if (!Initialize()) GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const std::array<std::byte, 257> bytes{};
    for (std::uint64_t count : {64u, 257u})
    {
        const auto texture = MakeTexture();
        ASSERT_TRUE(texture.IsValid());
        const auto token = queue.UploadTexture(texture, bytes.data(), count, 0u, 0u);
        EXPECT_FALSE(token.IsValid()) << "8x8 RGBA8 requires exactly 256 bytes, got " << count;
        // Baseline accepts bad requests; finish that real work before cleanup.
        if (token.IsValid()) ASSERT_TRUE(Drain(queue, token));
        Device->DestroyTexture(texture);
    }
}

TEST_F(VulkanTransferLifetimeGpuSmoke, NonzeroStagingOffsetPreservesTextureBytes)
{
    if (!Initialize()) GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const auto prefix = MakeBuffer();
    const auto texture = MakeTexture();
    ASSERT_TRUE(prefix.IsValid());
    ASSERT_TRUE(texture.IsValid());
    const std::uint32_t prefixValue = 7u;
    std::array<std::byte, 256> expected;
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<std::byte>(i);
    const auto prefixToken = queue.UploadBuffer(prefix, &prefixValue, sizeof(prefixValue), 0u);
    ASSERT_TRUE(prefixToken.IsValid());
    const auto upload = queue.UploadTexture(texture, expected.data(), expected.size(), 0u, 0u);
    ASSERT_TRUE(upload.IsValid());
    ASSERT_TRUE(Drain(queue, upload));
    RHI::FrameHandle frame;
    ASSERT_TRUE(Device->BeginFrame(frame));
    ASSERT_TRUE(RecordFrame(frame, {}, texture));
    ASSERT_TRUE(SubmitPresent(frame));
    std::array<std::byte, 256> actual{};
    unsigned callbacks = 0;
    auto download = queue.DownloadTexture(texture, RHI::TextureLayout::TransferSrc, 0u, 0u,
        {.Destination = std::span<std::byte>{actual}, .Callback = [&](std::span<const std::byte> data) {
            ++callbacks;
            EXPECT_EQ(data.size(), actual.size());
        }});
    ASSERT_TRUE(download.IsValid());
    ASSERT_TRUE(Drain(queue, download));
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(actual, expected);
    Device->DestroyTexture(texture);
    Device->DestroyBuffer(prefix);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, TextureBlockAlignmentDoesNotEmitValidationError)
{
    if (!Initialize(true)) GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const auto prefix = MakeBuffer();
    const auto texture = MakeTexture(RHI::Format::RGBA32_FLOAT, 1u);
    ASSERT_TRUE(prefix.IsValid());
    ASSERT_TRUE(texture.IsValid());
    const std::uint32_t prefixValue = 7u;
    const std::array<float, 4> pixel{0.25f, 0.5f, 0.75f, 1.0f};
    const auto before = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
    const auto prefixToken = queue.UploadBuffer(prefix, &prefixValue, sizeof(prefixValue), 0u);
    ASSERT_TRUE(prefixToken.IsValid());
    const auto upload = queue.UploadTexture(texture, pixel.data(), sizeof(pixel), 0u, 0u);
    ASSERT_TRUE(upload.IsValid());
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, before);
    ASSERT_TRUE(Drain(queue, upload));
    Device->DestroyTexture(texture);
    Device->DestroyBuffer(prefix);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, DestroyAfterPresentWaitsForSubmittingSlot)
{
    if (!Initialize()) GTEST_SKIP() << SkipReason;
    const auto buffer = MakeBuffer();
    ASSERT_TRUE(buffer.IsValid());
    RHI::FrameHandle frame;
    ASSERT_TRUE(Device->BeginFrame(frame));
    ASSERT_TRUE(RecordFrame(frame, buffer));
    ASSERT_TRUE(SubmitPresent(frame));
    const auto before = Deletes();
    Device->DestroyBuffer(buffer);
    EXPECT_EQ(Deletes(), before);
    ExpectRetirementAtSlot(frame.FrameIndex, before);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, InFrameDestroyWaitsForSubmittingSlot)
{
    if (!Initialize()) GTEST_SKIP() << SkipReason;
    const auto buffer = MakeBuffer();
    ASSERT_TRUE(buffer.IsValid());
    RHI::FrameHandle frame;
    ASSERT_TRUE(Device->BeginFrame(frame));
    ASSERT_TRUE(RecordFrame(frame, buffer));
    const auto before = Deletes();
    Device->DestroyBuffer(buffer);
    EXPECT_EQ(Deletes(), before);
    ASSERT_TRUE(SubmitPresent(frame));
    ExpectRetirementAtSlot(frame.FrameIndex, before);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, NoFrameDestroyWaitsForShutdownAndExecutesOnce)
{
    if (!Initialize()) GTEST_SKIP() << SkipReason;
    const auto buffer = MakeBuffer();
    ASSERT_TRUE(buffer.IsValid());
    const auto before = Deletes();
    Device->DestroyBuffer(buffer);
    Device->WaitIdle();
    EXPECT_EQ(Deletes(), before);
    Device->Shutdown();
    EXPECT_EQ(Deletes() - before, 1u);
}
