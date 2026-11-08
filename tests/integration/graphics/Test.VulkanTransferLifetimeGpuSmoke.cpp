#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "OperationalCounterStability.hpp"

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
        bool Initialize(bool validation = false, bool forceGraphicsCompute = false)
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
            config.MethodComputeOnGraphicsQueue = forceGraphicsCompute;
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

        RHI::BufferHandle MakeStorageBuffer(std::uint64_t bytes, const char* name)
        {
            return Device->CreateBuffer({
                .SizeBytes = bytes,
                .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
                .DebugName = name,
            });
        }

        // Records copy(src -> dst) and returns dst for readback: the method-compute shape on the transfer service.
        static RHI::BufferHandle RecordCopy(
            RHI::ICommandContext& commands, RHI::BufferHandle src, RHI::BufferHandle dst, std::uint64_t bytes)
        {
            commands.BufferBarrier(src, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::TransferRead);
            commands.BufferBarrier(dst, RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
            commands.CopyBuffer(src, dst, 0u, 0u, bytes);
            commands.BufferBarrier(dst, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::TransferRead);
            return dst;
        }

        static std::vector<std::byte> Pattern(std::size_t bytes)
        {
            std::vector<std::byte> pattern(bytes);
            for (std::size_t i = 0; i < bytes; ++i)
                pattern[i] = static_cast<std::byte>((i * 2654435761u) >> 13u);
            return pattern;
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

// ARCH-023: method compute runs on the async compute lane. The upload it reads is NOT drained on the host first,
// so only the compute lane's GPU wait on the graphics queue's tail orders it; the copy and host-read barrier run on
// the compute lane and the token names that lane. A later frame copies a compute-written buffer, acquiring the
// published compute value; the graphics-lane download proves the frame saw the compute writes. Removing the tail
// wait or the frame acquisition drops the matching counter (and the synchronization layer reports the hazard).
TEST_F(VulkanTransferLifetimeGpuSmoke, ComputeLaneOrdersUploadComputeReadbackAndFrameVisibility)
{
    const char* previousLayerEnables = std::getenv("VK_LAYER_ENABLES");
    const std::string restoreLayerEnables = previousLayerEnables ? previousLayerEnables : "";
    Extrinsic::Tests::Support::OperationalCounterStability::EnableSynchronizationValidation();
    const bool initialized = Initialize(true);
    if (previousLayerEnables)
        (void)::setenv("VK_LAYER_ENABLES", restoreLayerEnables.c_str(), 1);
    else
        (void)::unsetenv("VK_LAYER_ENABLES");
    if (!initialized)
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const RHI::TransferQueueDiagnostics before = queue.GetDiagnostics();
    EXPECT_EQ(before.ComputeLane.Requested, RHI::QueueAffinity::AsyncCompute);
    if (before.ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "No async compute queue: the method-compute lane was demoted to graphics.";
    EXPECT_FALSE(before.ComputeLane.Demoted);
    const auto bootstrap = Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
    ASSERT_TRUE(bootstrap.MethodComputeQueueAcquired);
    const auto expectedQueue = Vulkan::SelectMethodComputeQueue(bootstrap.GraphicsQueueFamily,
        bootstrap.GraphicsComputeQueueCount,
        bootstrap.AsyncComputeQueueFamily,
        bootstrap.AsyncComputeQueueIndex,
        bootstrap.FrameComputeQueueCount);
    EXPECT_EQ(bootstrap.MethodComputeQueueFamily, expectedQueue.Family);
    EXPECT_EQ(bootstrap.MethodComputeQueueIndex, expectedQueue.Index);
    if (bootstrap.FrameComputeQueueCount > bootstrap.AsyncComputeQueueIndex + 1u ||
        (bootstrap.GraphicsComputeQueueCount > 1u &&
            bootstrap.GraphicsQueueFamily != bootstrap.AsyncComputeQueueFamily))
        EXPECT_FALSE(before.ComputeSharesFrameComputeQueue);

    EXPECT_EQ(before.ComputeSharesFrameComputeQueue,
        bootstrap.MethodComputeQueueFamily == bootstrap.AsyncComputeQueueFamily &&
            bootstrap.MethodComputeQueueIndex == bootstrap.AsyncComputeQueueIndex);
    const auto validationErrors = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;

    constexpr std::uint64_t kBytes = 64u * 1024u;
    const auto expected = Pattern(kBytes);
    const auto source = MakeStorageBuffer(kBytes, "ComputeLaneSmoke.Source");
    const auto output = MakeStorageBuffer(kBytes, "ComputeLaneSmoke.Output");
    ASSERT_TRUE(source.IsValid() && output.IsValid());
    ASSERT_TRUE(queue.UploadBuffer(source, std::span<const std::byte>{expected}, 0u).IsValid());

    std::vector<std::byte> actual(kBytes);
    unsigned callbacks = 0;
    std::vector<std::byte> callbackBytes(kBytes);
    RHI::ReadbackToken callbackRead;
    const auto token = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            return RecordCopy(commands, source, output, kBytes);
        },
        kBytes,
        {.Destination = std::span<std::byte>{actual},
            .Callback = [&](std::span<const std::byte>)
            {
                ++callbacks;
                const auto visibility = queue.GetDiagnostics().ComputeVisibilityWaits;
                callbackRead =
                    queue.DownloadBuffer(output, kBytes, 0u, RHI::ReadbackSink::CopyTo(std::span{callbackBytes}));
                EXPECT_EQ(queue.GetDiagnostics().ComputeVisibilityWaits, visibility + 1u)
                    << "The result is visible before its callback can submit a consumer.";
            }});
    ASSERT_TRUE(token.IsValid());
    EXPECT_EQ(Vulkan::LaneOfTicket(token.Value), Vulkan::TransferLane::Compute);
    const RHI::ReadbackToken fence = queue.ComputeFence();
    EXPECT_EQ(fence, token) << "the fence covers the latest compute submission";
    ASSERT_TRUE(Drain(queue, token));
    EXPECT_TRUE(queue.IsComplete(fence));
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(actual, expected) << "upload -> compute -> readback known answer";
    ASSERT_TRUE(callbackRead.IsValid());
    ASSERT_TRUE(Drain(queue, callbackRead));
    EXPECT_EQ(callbackBytes, expected);
    const RHI::TransferQueueDiagnostics afterCompute = queue.GetDiagnostics();
    EXPECT_EQ(afterCompute.ComputeSubmissions - before.ComputeSubmissions, 1u);
    EXPECT_EQ(afterCompute.ComputeGraphicsWaits - before.ComputeGraphicsWaits, 1u);
    EXPECT_EQ(afterCompute.ComputeHostBarriers - before.ComputeHostBarriers, 1u);

    // Synchronous scene uploads acquire published results without a host wait for every method submission.
    const auto visibilityBeforeUpload = queue.GetDiagnostics().ComputeVisibilityWaits;
    Device->WriteBuffer(source, expected.data(), kBytes);
    EXPECT_EQ(queue.GetDiagnostics().ComputeVisibilityWaits, visibilityBeforeUpload + 1u);
    std::vector<std::byte> uploaded(kBytes);
    const auto uploadReadback =
        queue.DownloadBuffer(source, kBytes, 0u, RHI::ReadbackSink::CopyTo(std::span{uploaded}));
    ASSERT_TRUE(Drain(queue, uploadReadback));
    EXPECT_EQ(uploaded, expected);

    // A refused recording submits nothing, and a zero-byte request is completion-only on the compute lane.
    EXPECT_FALSE(Device
            ->SubmitComputeReadback(
                [](RHI::ICommandContext&)
                {
                    return RHI::BufferHandle{};
                },
                4u,
                RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}))
            .IsValid());
    EXPECT_EQ(queue.ComputeFence(), fence);
    constexpr std::uint32_t kFill = 0xa5a5a5a5u;
    unsigned completions = 0;
    const auto fill = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            commands.BufferBarrier(output, RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
            commands.FillBuffer(output, 0u, kBytes, kFill);
            return output;
        },
        0u,
        RHI::ReadbackSink::Invoke(
            [&](std::span<const std::byte> bytes)
            {
                completions += bytes.empty() ? 1u : 100u;
            }));
    ASSERT_TRUE(fill.IsValid());
    EXPECT_EQ(Vulkan::LaneOfTicket(fill.Value), Vulkan::TransferLane::Compute);
    ASSERT_TRUE(Drain(queue, fill));
    EXPECT_EQ(completions, 1u);
    EXPECT_EQ(queue.GetDiagnostics().ComputeSubmissions - before.ComputeSubmissions, 2u);

    // The frame reads the published compute result (render-side consumer) into `source`.
    const auto visibilityBeforeFrame = queue.GetDiagnostics().ComputeVisibilityWaits;
    RHI::FrameHandle frame;
    ASSERT_TRUE(Device->BeginFrame(frame));
    auto& context = Device->GetGraphicsContext(frame.FrameIndex);
    context.Begin();
    (void)RecordCopy(context, output, source, kBytes);
    context.TextureBarrier(
        Device->GetBackbufferHandle(frame), RHI::TextureLayout::Undefined, RHI::TextureLayout::Present);
    context.End();
    ASSERT_TRUE(SubmitPresent(frame));
    EXPECT_GE(queue.GetDiagnostics().ComputeVisibilityWaits - visibilityBeforeFrame, 1u)
        << "the frame acquires the completed compute result";
    std::vector<std::byte> rendered(kBytes);
    const auto download = queue.DownloadBuffer(source, kBytes, 0u, RHI::ReadbackSink::CopyTo(std::span{rendered}));
    ASSERT_TRUE(download.IsValid());
    EXPECT_EQ(Vulkan::LaneOfTicket(download.Value), Vulkan::TransferLane::Graphics);
    ASSERT_TRUE(Drain(queue, download));
    for (std::size_t i = 0; i < kBytes; i += 4u)
        ASSERT_EQ(std::to_integer<unsigned>(rendered[i]), 0xa5u) << "byte " << i;
    Device->WaitIdle();
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, validationErrors)
        << "validation (core and synchronization) across the cross-queue handoffs";
    Device->DestroyBuffer(source);
    Device->DestroyBuffer(output);
}

// ARCH-023: retiring frames no longer proves compute-lane work finished. A buffer destroyed after a compute
// submission is released only once that submission's completion was collected, however many frames pass.
TEST_F(VulkanTransferLifetimeGpuSmoke, DeferredDestroyWaitsForCollectedComputeCompletion)
{
    if (!Initialize())
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "No async compute queue: the method-compute lane was demoted to graphics.";
    const auto buffer = MakeStorageBuffer(16u, "ComputeLaneSmoke.Destroyed");
    ASSERT_TRUE(buffer.IsValid());
    const auto token = Device->SubmitComputeReadback(
        [buffer](RHI::ICommandContext& commands)
        {
            commands.FillBuffer(buffer, 0u, 16u, 1u);
            return buffer;
        },
        0u,
        RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}));
    ASSERT_TRUE(token.IsValid());
    const auto before = Deletes();
    Device->DestroyBuffer(buffer);
    for (unsigned i = 0; i < 4u; ++i) // every frame slot retires at least once; nothing collects the compute lane
    {
        RHI::FrameHandle frame;
        ASSERT_TRUE(Device->BeginFrame(frame));
        EXPECT_EQ(Deletes(), before) << "frame " << i << ": frames alone must not release compute-lane resources";
        ASSERT_TRUE(RecordFrame(frame));
        ASSERT_TRUE(SubmitPresent(frame));
    }
    ASSERT_TRUE(Drain(queue, token));
    RHI::FrameHandle frame;
    ASSERT_TRUE(Device->BeginFrame(frame));
    EXPECT_EQ(Deletes() - before, 1u) << "released once the compute completion was collected";
    ASSERT_TRUE(RecordFrame(frame));
    ASSERT_TRUE(SubmitPresent(frame));
    Device->Shutdown();
    EXPECT_EQ(Deletes() - before, 1u);
}

// ARCH-023: render.method_compute_graphics_queue forces the graphics lane; the result and the zero-byte/refusal
// contract are unchanged and the diagnostics report the configured lane, with no cross-queue handoffs.
TEST_F(VulkanTransferLifetimeGpuSmoke, ForcedGraphicsComputeLaneKeepsKnownAnswer)
{
    if (!Initialize(false, true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const RHI::TransferQueueDiagnostics before = queue.GetDiagnostics();
    EXPECT_EQ(before.ComputeLane.Requested, RHI::QueueAffinity::Graphics);
    EXPECT_EQ(before.ComputeLane.Resolved, RHI::QueueAffinity::Graphics);
    EXPECT_FALSE(before.ComputeLane.Demoted);
    constexpr std::uint64_t kBytes = 4096u;
    const auto expected = Pattern(kBytes);
    const auto source = MakeStorageBuffer(kBytes, "ComputeLaneSmoke.ForcedSource");
    const auto output = MakeStorageBuffer(kBytes, "ComputeLaneSmoke.ForcedOutput");
    ASSERT_TRUE(source.IsValid() && output.IsValid());
    ASSERT_TRUE(queue.UploadBuffer(source, std::span<const std::byte>{expected}, 0u).IsValid());
    std::vector<std::byte> actual(kBytes);
    const auto token = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            return RecordCopy(commands, source, output, kBytes);
        },
        kBytes,
        RHI::ReadbackSink::CopyTo(std::span{actual}));
    ASSERT_TRUE(token.IsValid());
    EXPECT_EQ(Vulkan::LaneOfTicket(token.Value), Vulkan::TransferLane::Graphics);
    ASSERT_TRUE(Drain(queue, token));
    EXPECT_EQ(actual, expected);
    const RHI::TransferQueueDiagnostics after = queue.GetDiagnostics();
    EXPECT_EQ(after.ComputeSubmissions, before.ComputeSubmissions);
    EXPECT_EQ(after.ComputeGraphicsWaits, before.ComputeGraphicsWaits);
    EXPECT_EQ(after.ComputeVisibilityWaits, before.ComputeVisibilityWaits);
    EXPECT_FALSE(queue.ComputeFence().IsValid());
    Device->DestroyBuffer(source);
    Device->DestroyBuffer(output);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, AnUploadFinishesWhileUnrelatedMethodComputeIsHeldPending)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "A separate method queue is required for this independence check.";
    const auto errors = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
    const auto method = MakeStorageBuffer(4u, "PendingCompute.Method");
    const auto upload = MakeStorageBuffer(4u, "PendingCompute.UnrelatedUpload");
    ASSERT_TRUE(method.IsValid() && upload.IsValid());
    const std::uint32_t zero = 0u, expected = 0x12345678u;
    Device->WriteBuffer(upload, &zero, sizeof(zero));
    RHI::ReadbackToken pending;
    bool called = false;
    ASSERT_TRUE(Vulkan::WithVulkanQueueGate(Device.get(),
        Vulkan::VulkanGatedQueue::MethodCompute,
        [&]
        {
            called = true;
            pending = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    commands.FillBuffer(method, 0u, 4u, 42u);
                    return method;
                },
                0u,
                RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}));
            ASSERT_TRUE(pending.IsValid());
            EXPECT_FALSE(queue.IsComplete(pending));
            Device->WriteBuffer(upload, &expected, sizeof(expected));
            std::array<std::uint32_t, 1> actual{};
            const auto read = queue.DownloadBuffer(
                upload, sizeof(expected), 0u, RHI::ReadbackSink::CopyTo(std::as_writable_bytes(std::span{actual})));
            ASSERT_TRUE(read.IsValid());
            ASSERT_TRUE(Drain(queue, read));
            EXPECT_EQ(actual[0], expected);
            EXPECT_FALSE(queue.IsComplete(pending)) << "The upload finished before the method gate opened.";
        }));
    EXPECT_TRUE(called);
    ASSERT_TRUE(Drain(queue, pending));
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, errors);
    Device->DestroyBuffer(method);
    Device->DestroyBuffer(upload);
}

TEST_F(VulkanTransferLifetimeGpuSmoke, SynchronousHostVisibleReadAcquiresUndeliveredComputeWrites)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    const auto buffer = Device->CreateBuffer({.SizeBytes = 4u,
        .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "HostRead.UndeliveredCompute"});
    ASSERT_TRUE(buffer.IsValid());
    constexpr std::uint32_t expected = 0x2468ace0u;
    const auto token = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            commands.FillBuffer(buffer, 0u, 4u, expected);
            return buffer;
        },
        0u,
        RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {}));
    ASSERT_TRUE(token.IsValid());
    const auto barriers = Vulkan::GetReadBufferHostBarrierCount();
    std::uint32_t actual = 0u;
    Device->ReadBuffer(buffer, &actual, sizeof(actual));
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(Vulkan::GetReadBufferHostBarrierCount(), barriers + 1u);
    ASSERT_TRUE(Drain(Device->GetTransferQueue(), token));
    Device->DestroyBuffer(buffer);
}

// ARCH-023 render-independent progress (S1-S3). Each test names the mechanism it fails without.
namespace
{
    constexpr std::uint64_t kHeldWaitNanoseconds = 300'000'000u; // what "still pending" means: 0.3 s
    constexpr std::uint64_t kDoneWaitNanoseconds = 5'000'000'000u;

    RHI::ReadbackSink IgnoreBytes()
    {
        return RHI::ReadbackSink::Invoke([](std::span<const std::byte>) {});
    }

    // A completion-only method submission over `buffer`; `recorded` (when given) notes that its recorder ran.
    RHI::ReadbackToken SubmitCompletionOnly(
        RHI::IDevice& device, const RHI::BufferHandle buffer, bool* recorded = nullptr)
    {
        return device.SubmitComputeReadback(
            [buffer, recorded](RHI::ICommandContext&)
            {
                if (recorded)
                    *recorded = true;
                return buffer;
            },
            0u,
            IgnoreBytes());
    }
}

// S1: compute waits only for the graphics lane's last issued ticket, so three chained compute pages (each reading the
// last one's output) complete, through WaitCompute and TakeCompletedReadback with no CollectCompleted, while the
// graphics queue is held. Mutation: the former fresh graphics-tail signal per page queues behind the gate, nothing
// completes before the release.
TEST_F(VulkanTransferLifetimeGpuSmoke, ThreeChainedComputePagesFinishWhileGraphicsIsHeld)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "A separate method queue is required: on the graphics lane compute shares the held queue.";
    const auto errors = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
    const auto a = MakeStorageBuffer(4u, "HeldGraphics.A");
    const auto b = MakeStorageBuffer(4u, "HeldGraphics.B");
    const auto c = MakeStorageBuffer(4u, "HeldGraphics.C");
    ASSERT_TRUE(a.IsValid() && b.IsValid() && c.IsValid());
    constexpr std::uint32_t kExpected = 0x5eed1234u;
    // An earlier upload completes before the gate: its ticket is what compute waits for.
    const std::uint32_t zero = 0u;
    const auto upload = queue.UploadBuffer(c, &zero, sizeof(zero), 0u);
    ASSERT_TRUE(upload.IsValid());
    ASSERT_TRUE(Drain(queue, upload));
    std::array<RHI::ReadbackToken, 3> pages{};
    std::array<RHI::ComputeWaitStatus, 3> waited{};
    std::uint32_t taken = 0u;
    bool took = false, tookTwice = true;
    ASSERT_TRUE(Vulkan::WithVulkanQueueGate(Device.get(),
        Vulkan::VulkanGatedQueue::Graphics,
        [&]
        {
            pages[0] = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    commands.FillBuffer(a, 0u, 4u, kExpected);
                    commands.BufferBarrier(a, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::TransferRead);
                    return a;
                },
                0u,
                IgnoreBytes());
            pages[1] = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    return RecordCopy(commands, a, b, 4u);
                },
                0u,
                IgnoreBytes());
            pages[2] = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    return RecordCopy(commands, b, c, 4u);
                },
                4u,
                RHI::ReadbackSink::Pull());
            for (std::size_t i = 0; i < pages.size(); ++i)
                waited[i] = pages[i].IsValid() ? queue.WaitCompute(pages[i], kDoneWaitNanoseconds)
                                               : RHI::ComputeWaitStatus::Failed;
            took = queue.TakeCompletedReadback(pages[2], std::as_writable_bytes(std::span{&taken, 1u}));
            std::uint32_t again = 0u;
            tookTwice = queue.TakeCompletedReadback(pages[2], std::as_writable_bytes(std::span{&again, 1u}));
        }));
    for (std::size_t i = 0; i < pages.size(); ++i)
    {
        ASSERT_TRUE(pages[i].IsValid()) << i;
        EXPECT_EQ(waited[i], RHI::ComputeWaitStatus::Complete) << "page " << i << " finished with graphics held";
    }
    EXPECT_TRUE(took);
    EXPECT_EQ(taken, kExpected) << "the chain's known answer, taken on the waiting thread";
    EXPECT_FALSE(tookTwice) << "bytes are taken once";
    EXPECT_FALSE(queue.IsComplete(pages[2])) << "IsComplete follows the next collection";
    const auto completed = queue.GetDiagnostics().DownloadsCompleted;
    ASSERT_TRUE(Drain(queue, pages[2]));
    EXPECT_EQ(queue.GetDiagnostics().DownloadsCompleted, completed) << "the taken bytes counted once";
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, errors);
    for (const auto buffer : {a, b, c})
        Device->DestroyBuffer(buffer);
}

// S1 keeps producer visibility: an upload issued while graphics is held orders the compute page that reads it. The
// page stays pending while the upload is held and reads the uploaded value after the release. Mutation: no graphics
// wait (a "no-wait" flag) completes the page early with the old bytes.
TEST_F(VulkanTransferLifetimeGpuSmoke, AnUploadIssuedWhileGraphicsIsHeldOrdersTheComputePageThatReadsIt)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "A separate method queue is required: on the graphics lane queue order alone orders them.";
    const auto source = MakeStorageBuffer(4u, "HeldUpload.Source");
    const auto output = MakeStorageBuffer(4u, "HeldUpload.Output");
    ASSERT_TRUE(source.IsValid() && output.IsValid());
    const std::uint32_t before = 7u, uploaded = 0xabcdef01u;
    const auto first = queue.UploadBuffer(source, &before, sizeof(before), 0u);
    ASSERT_TRUE(Drain(queue, first));
    const auto waitsBefore = queue.GetDiagnostics().ComputeGraphicsWaits;
    RHI::ReadbackToken page;
    RHI::ComputeWaitStatus whileHeld = RHI::ComputeWaitStatus::Complete;
    ASSERT_TRUE(Vulkan::WithVulkanQueueGate(Device.get(),
        Vulkan::VulkanGatedQueue::Graphics,
        [&]
        {
            ASSERT_TRUE(queue.UploadBuffer(source, &uploaded, sizeof(uploaded), 0u).IsValid());
            page = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    return RecordCopy(commands, source, output, 4u);
                },
                4u,
                RHI::ReadbackSink::Pull());
            ASSERT_TRUE(page.IsValid());
            whileHeld = queue.WaitCompute(page, kHeldWaitNanoseconds);
        }));
    EXPECT_EQ(whileHeld, RHI::ComputeWaitStatus::Pending) << "the page waited for the held upload";
    EXPECT_EQ(queue.GetDiagnostics().ComputeGraphicsWaits, waitsBefore + 1u);
    ASSERT_EQ(queue.WaitCompute(page, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    std::uint32_t read = 0u;
    ASSERT_TRUE(queue.TakeCompletedReadback(page, std::as_writable_bytes(std::span{&read, 1u})));
    EXPECT_EQ(read, uploaded);
    ASSERT_TRUE(Drain(queue, page));
    Device->DestroyBuffer(source);
    Device->DestroyBuffer(output);
}

// S2: a distinct method queue has its own host lock: with the frame queues' lock held by another thread (a blocked
// present), a compute page still submits and completes within a bound. Shared hardware is skipped truthfully, after
// the diagnostics say the lock is shared. Mutation: one device-wide queue mutex blocks the submission until the
// holder lets go (after 5 s).
TEST_F(VulkanTransferLifetimeGpuSmoke, MethodComputeSubmitsWhileTheFrameQueueLockIsHeld)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const auto diagnostics = queue.GetDiagnostics();
    if (diagnostics.ComputeSharesFrameQueueLock)
    {
        bool called = false;
        EXPECT_FALSE(Vulkan::WithVulkanFrameQueueLockHeld(Device.get(),
            [&]
            {
                called = true;
            }))
            << "the seam refuses a shared lock (it would deadlock the submitting thread)";
        EXPECT_FALSE(called);
        GTEST_SKIP() << "Method compute shares a frame queue (and its host lock) on this device.";
    }
    const auto buffer = MakeStorageBuffer(4u, "LockSeparation.Buffer");
    ASSERT_TRUE(buffer.IsValid());
    std::atomic<bool> held{false}, release{false};
    bool ran = false;
    std::thread holder(
        [&]
        {
            ran = Vulkan::WithVulkanFrameQueueLockHeld(Device.get(),
                [&]
                {
                    held = true;
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
                    while (!release && std::chrono::steady_clock::now() < deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                });
            held = true; // also when it could not hold the lock
        });
    while (!held)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    const auto start = std::chrono::steady_clock::now();
    const auto page = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            commands.FillBuffer(buffer, 0u, 4u, 0x0badf00du);
            commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::TransferRead);
            return buffer;
        },
        4u,
        RHI::ReadbackSink::Pull());
    const auto status = page.IsValid() ? queue.WaitCompute(page, kDoneWaitNanoseconds) : RHI::ComputeWaitStatus::Failed;
    const auto elapsed = std::chrono::steady_clock::now() - start;
    release = true;
    holder.join();
    ASSERT_TRUE(ran) << "the frame lock was held by the other thread";
    EXPECT_EQ(status, RHI::ComputeWaitStatus::Complete);
    EXPECT_LT(elapsed, std::chrono::seconds{2}) << "the submission did not wait for the frame queues' lock";
    std::uint32_t read = 0u;
    EXPECT_TRUE(queue.TakeCompletedReadback(page, std::as_writable_bytes(std::span{&read, 1u})));
    EXPECT_EQ(read, 0x0badf00du);
    ASSERT_TRUE(Drain(queue, page));
    Device->DestroyBuffer(buffer);
}

// A held method queue does not hold up render frames: three frames begin, record, submit and present while a compute
// page stays pending (frames carry only the published compute wait). Mutation: a frame wait on unfinished method
// compute would block BeginFrame on that frame's fence (a hang the test timeout reports).
TEST_F(VulkanTransferLifetimeGpuSmoke, RenderFramesProceedWhileMethodComputeIsHeld)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "A separate method queue is required for this independence check.";
    const auto buffer = MakeStorageBuffer(4u, "HeldCompute.Buffer");
    ASSERT_TRUE(buffer.IsValid());
    RHI::ReadbackToken page;
    unsigned frames = 0u;
    std::chrono::steady_clock::duration slowest{};
    RHI::ComputeWaitStatus afterFrames = RHI::ComputeWaitStatus::Complete;
    ASSERT_TRUE(Vulkan::WithVulkanQueueGate(Device.get(),
        Vulkan::VulkanGatedQueue::MethodCompute,
        [&]
        {
            page = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    commands.FillBuffer(buffer, 0u, 4u, 1u);
                    return buffer;
                },
                0u,
                IgnoreBytes());
            ASSERT_TRUE(page.IsValid());
            for (unsigned i = 0; i < 3u; ++i)
            {
                const auto begin = std::chrono::steady_clock::now();
                RHI::FrameHandle frame;
                if (!Device->BeginFrame(frame) || !RecordFrame(frame) || !SubmitPresent(frame))
                    break;
                slowest = std::max(slowest, std::chrono::steady_clock::now() - begin);
                ++frames;
            }
            afterFrames = queue.WaitCompute(page, 0u);
        }));
    EXPECT_EQ(frames, 3u);
    EXPECT_LT(slowest, std::chrono::seconds{2});
    EXPECT_EQ(afterFrames, RHI::ComputeWaitStatus::Pending) << "the page was still held while the frames ran";
    EXPECT_EQ(queue.WaitCompute(page, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    ASSERT_TRUE(Drain(queue, page));
    Device->DestroyBuffer(buffer);
}

// ARCH-023 findings 1-2: a pulled readback belongs to its submitter. CollectCompleted running first neither delivers
// nor drops it (the worker still takes its exact bytes, once); taking first and collecting later is the same. An
// ordinary readback submitted after an untaken pulled one runs its callback once at collection but does not report
// complete before the pulled one is resolved (lane FIFO). A released (cancelled) one is freed once complete without a
// callback and cannot be taken. Many pulled pages without any collection reuse one staging slot. Mutation: the former
// claim-on-take (an Invoke sink taken after the wait) loses the bytes when collection wins; a delivered-ticket high
// watermark reports the later ordinary token complete while the pulled one is untaken.
TEST_F(VulkanTransferLifetimeGpuSmoke, PulledReadbacksBelongToTheirSubmitterWhateverCollectionDoes)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const auto errors = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
    const auto buffer = MakeStorageBuffer(4u, "Pulled.Buffer");
    ASSERT_TRUE(buffer.IsValid());
    const auto fill = [&](const std::uint32_t value, RHI::ReadbackSink sink)
    {
        return Device->SubmitComputeReadback(
            [&, value](RHI::ICommandContext& commands)
            {
                commands.FillBuffer(buffer, 0u, 4u, value);
                commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::TransferRead);
                return buffer;
            },
            4u,
            std::move(sink));
    };
    const auto take = [&](const RHI::ReadbackToken token, std::uint32_t& value)
    {
        return queue.TakeCompletedReadback(token, std::as_writable_bytes(std::span{&value, 1u}));
    };

    // Collection first, then the take.
    const auto first = fill(0x11111111u, RHI::ReadbackSink::Pull());
    ASSERT_TRUE(first.IsValid());
    ASSERT_EQ(queue.WaitCompute(first, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    const auto completedBefore = queue.GetDiagnostics().DownloadsCompleted;
    for (int i = 0; i < 3; ++i)
        queue.CollectCompleted();
    EXPECT_FALSE(queue.IsComplete(first)) << "collection does not consume a pulled readback";
    std::uint32_t value = 0u, again = 0u;
    ASSERT_TRUE(take(first, value));
    EXPECT_EQ(value, 0x11111111u);
    EXPECT_FALSE(take(first, again)) << "exactly once";
    EXPECT_EQ(queue.GetDiagnostics().DownloadsCompleted, completedBefore + 1u);
    queue.CollectCompleted();
    EXPECT_TRUE(queue.IsComplete(first));

    // An untaken pulled readback, then an ordinary one: its callback runs once, its completion waits for the take.
    const auto pulled = fill(0x22222222u, RHI::ReadbackSink::Pull());
    unsigned callbacks = 0u;
    std::uint32_t delivered = 0u;
    const auto ordinary = fill(0x33333333u,
        RHI::ReadbackSink::Invoke(
            [&](std::span<const std::byte> bytes)
            {
                ++callbacks;
                if (bytes.size() == 4u)
                    std::memcpy(&delivered, bytes.data(), 4u);
            }));
    ASSERT_TRUE(pulled.IsValid() && ordinary.IsValid());
    ASSERT_EQ(queue.WaitCompute(ordinary, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    queue.CollectCompleted();
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(delivered, 0x33333333u);
    EXPECT_FALSE(queue.IsComplete(ordinary)) << "a later token never reports complete over an untaken earlier one";
    ASSERT_TRUE(take(pulled, value));
    EXPECT_EQ(value, 0x22222222u);
    queue.CollectCompleted();
    EXPECT_TRUE(queue.IsComplete(pulled));
    EXPECT_TRUE(queue.IsComplete(ordinary));
    EXPECT_EQ(callbacks, 1u) << "the ordinary callback ran once";

    // Released while possibly pending: no callback, no take, freed at completion.
    const auto released = fill(0x44444444u, RHI::ReadbackSink::Pull());
    ASSERT_TRUE(released.IsValid());
    queue.ReleaseReadback(released);
    ASSERT_EQ(queue.WaitCompute(released, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    EXPECT_FALSE(take(released, value));
    ASSERT_TRUE(Drain(queue, released));

    // Bounded reuse with no collection at all: each page's slot returns at its take.
    const auto highWater = queue.GetDiagnostics().ReadbackRingHighWaterBytes;
    for (std::uint32_t i = 0; i < 256u; ++i)
    {
        const auto page = fill(i, RHI::ReadbackSink::Pull());
        ASSERT_TRUE(page.IsValid()) << i;
        ASSERT_EQ(queue.WaitCompute(page, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete) << i;
        ASSERT_TRUE(take(page, value)) << i;
        ASSERT_EQ(value, i);
    }
    EXPECT_EQ(queue.GetDiagnostics().ReadbackRingHighWaterBytes, highWater) << "no staging growth without frames";
    queue.CollectCompleted();
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, errors);
    Device->DestroyBuffer(buffer);
}

// ARCH-023 worker uploads (root worker-query review): CPU-produced hybrid rows reach compute through
// IDevice::WriteMappedBuffer into one host-visible staging buffer and an in-command copy on the method lane, never the
// graphics lane. With the graphics queue gated AND the frame queues' host lock held by another thread, three pages
// with changed inputs each read their own new value (the producer order: write, then the page that copies it); 256
// more pages keep the readback ring and graphics waits unchanged (one staging buffer, reused). Predictions, before any
// run: every page Complete within 5 s, read == written value, ComputeGraphicsWaits unchanged, high water unchanged.
// Mutations: UploadBuffer instead of the mapped write (the page waits on the held graphics lane: the 5 s wait times
// out and ComputeGraphicsWaits grows); a stale read (the copy dropped) fails read == written value.
TEST_F(VulkanTransferLifetimeGpuSmoke, MappedRowsFeedComputePagesWhileGraphicsAndItsHostLockAreHeld)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    const auto diagnostics = queue.GetDiagnostics();
    if (diagnostics.ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute || diagnostics.ComputeSharesFrameQueueLock)
        GTEST_SKIP() << "A separate method queue with its own host lock is required for this independence claim.";
    const auto errors = Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount;
    const auto staging = Device->CreateBuffer({.SizeBytes = 4u,
        .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "MappedRows.Staging"});
    const auto rows = MakeStorageBuffer(4u, "MappedRows.Rows");
    ASSERT_TRUE(staging.IsValid() && rows.IsValid());

    // Refusals write nothing: no data, no size, an invalid or destroyed handle, a device-local buffer, a range past
    // the end (offset and size each in range alone).
    const std::uint32_t probe = 1u;
    EXPECT_FALSE(Device->WriteMappedBuffer(staging, nullptr, 4u));
    EXPECT_FALSE(Device->WriteMappedBuffer(staging, &probe, 0u));
    EXPECT_FALSE(Device->WriteMappedBuffer(RHI::BufferHandle{}, &probe, 4u));
    EXPECT_FALSE(Device->WriteMappedBuffer(rows, &probe, 4u)) << "a device-local buffer has no mapping";
    EXPECT_FALSE(Device->WriteMappedBuffer(staging, &probe, 4u, 4u));
    EXPECT_FALSE(Device->WriteMappedBuffer(staging, &probe, 8u, 0u));

    const auto page = [&](RHI::ReadbackToken& token)
    {
        token = Device->SubmitComputeReadback(
            [&](RHI::ICommandContext& commands)
            {
                return RecordCopy(commands, staging, rows, 4u);
            },
            4u,
            RHI::ReadbackSink::Pull());
        if (!token.IsValid())
            return std::uint32_t{0u};
        std::uint32_t read = 0u;
        if (queue.WaitCompute(token, kDoneWaitNanoseconds) != RHI::ComputeWaitStatus::Complete ||
            !queue.TakeCompletedReadback(token, std::as_writable_bytes(std::span{&read, 1u})))
            return std::uint32_t{0u};
        return read;
    };
    const auto waits = queue.GetDiagnostics().ComputeGraphicsWaits;
    std::array<std::uint32_t, 3> read{};
    std::array<RHI::ReadbackToken, 3> tokens{};
    std::array<bool, 3> wrote{};
    std::uint64_t highWater = 0u, highWaterAfter = 1u, waitsAfter = 1u;
    std::atomic<bool> held{false}, release{false};
    bool lockHeld = false, gated = false;
    std::thread holder(
        [&]
        {
            lockHeld = Vulkan::WithVulkanFrameQueueLockHeld(Device.get(),
                [&]
                {
                    held = true;
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
                    while (!release && std::chrono::steady_clock::now() < deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                });
            held = true;
        });
    while (!held)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    gated = Vulkan::WithVulkanQueueGate(Device.get(),
        Vulkan::VulkanGatedQueue::Graphics,
        [&]
        {
            for (std::size_t i = 0; i < read.size(); ++i)
            {
                const std::uint32_t value = 0x51a90000u + std::uint32_t(i);
                wrote[i] = Device->WriteMappedBuffer(staging, &value, sizeof(value));
                read[i] = page(tokens[i]);
            }
            highWater = queue.GetDiagnostics().ReadbackRingHighWaterBytes;
            for (std::uint32_t i = 0; i < 256u; ++i)
            {
                RHI::ReadbackToken token;
                const std::uint32_t value = i;
                if (!Device->WriteMappedBuffer(staging, &value, sizeof(value)) || page(token) != value)
                {
                    highWaterAfter = ~std::uint64_t{0};
                    break;
                }
                highWaterAfter = queue.GetDiagnostics().ReadbackRingHighWaterBytes;
            }
            waitsAfter = queue.GetDiagnostics().ComputeGraphicsWaits;
        });
    release = true;
    holder.join();
    ASSERT_TRUE(lockHeld) << "the frame queues' host lock was held by the other thread";
    ASSERT_TRUE(gated);
    for (std::size_t i = 0; i < read.size(); ++i)
    {
        EXPECT_TRUE(wrote[i]) << i;
        EXPECT_EQ(read[i], 0x51a90000u + std::uint32_t(i)) << "page " << i << " read its own new rows";
    }
    EXPECT_EQ(highWaterAfter, highWater) << "the reused staging needs no readback growth and no frames";
    EXPECT_EQ(waitsAfter, waits) << "no page waited for the graphics lane";
    EXPECT_EQ(Vulkan::GetVulkanOperationalDiagnosticsSnapshot().VulkanValidationErrorCount, errors);
    Device->WaitIdle();
    Device->DestroyBuffer(staging);
    Device->DestroyBuffer(rows);
}

// ARCH-023 query-review B1: with method compute forced onto the graphics queue, a recorder runs under the graphics
// lane's record mutex. A transfer-queue upload or download, or a nested SubmitComputeReadback, from inside it is
// refused (invalid token, logged, the nested recorder never runs) instead of self-deadlocking, the submission itself
// still completes with its known answer, and the same thread's upload and compute submission after the call are
// accepted again. Predictions, before any run: all three nested tokens invalid and the nested recorder not invoked;
// read == written; the later upload and submission valid. Mutation: no recorder scope (the nested UploadBuffer takes
// the held mutex: the test hangs until the CTest timeout); no nested-submission guard (the nested
// SubmitComputeReadback takes the held mutex: hangs); a scope left set after the call (the later upload and
// submission are refused).
TEST_F(VulkanTransferLifetimeGpuSmoke, AGraphicsLaneRecorderRefusesNestedTransfersInsteadOfDeadlocking)
{
    if (!Initialize(true, true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    ASSERT_EQ(queue.GetDiagnostics().ComputeLane.Resolved, RHI::QueueAffinity::Graphics);
    const auto source = MakeStorageBuffer(4u, "NestedRecorder.Source");
    const auto output = MakeStorageBuffer(4u, "NestedRecorder.Output");
    ASSERT_TRUE(source.IsValid() && output.IsValid());
    const std::uint32_t written = 0x600dcafeu, nested = 1u;
    ASSERT_TRUE(Drain(queue, queue.UploadBuffer(source, &written, sizeof(written), 0u)));
    RHI::TransferToken nestedUpload{1u};
    RHI::ReadbackToken nestedDownload{1u}, nestedCompute{1u};
    bool nestedRecorded = false;
    const auto page = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            nestedUpload = queue.UploadBuffer(output, &nested, sizeof(nested), 0u);
            nestedDownload = queue.DownloadBuffer(source, 4u, 0u, IgnoreBytes());
            nestedCompute = SubmitCompletionOnly(*Device, source, &nestedRecorded);
            return RecordCopy(commands, source, output, 4u);
        },
        4u,
        RHI::ReadbackSink::Pull());
    EXPECT_FALSE(nestedUpload.IsValid()) << "refused inside the graphics-lane recorder";
    EXPECT_FALSE(nestedDownload.IsValid());
    EXPECT_FALSE(nestedCompute.IsValid()) << "a nested submission on the held lane is refused";
    EXPECT_FALSE(nestedRecorded);
    ASSERT_TRUE(page.IsValid());
    ASSERT_EQ(queue.WaitCompute(page, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    std::uint32_t read = 0u;
    ASSERT_TRUE(queue.TakeCompletedReadback(page, std::as_writable_bytes(std::span{&read, 1u})));
    EXPECT_EQ(read, written);
    const auto after = queue.UploadBuffer(source, &nested, sizeof(nested), 0u);
    EXPECT_TRUE(after.IsValid()) << "the refusal ends with the recorder";
    ASSERT_TRUE(Drain(queue, after));
    const auto later = SubmitCompletionOnly(*Device, source);
    ASSERT_TRUE(later.IsValid()) << "a submission after the recorder is accepted again";
    EXPECT_EQ(queue.WaitCompute(later, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    Device->DestroyBuffer(source);
    Device->DestroyBuffer(output);
}

// ARCH-023: on a separate method queue the recorder holds the compute lane's record mutex; a nested
// SubmitComputeReadback from inside it is refused (invalid, the nested recorder never runs) instead of
// self-deadlocking, the outer submission completes with its known answer, and a later submission is accepted.
// Predictions, before any run: nested token invalid, nested recorder not invoked; read == written; later valid and
// Complete. Mutation: no nested-submission guard (the nested call takes the held mutex: hangs until the CTest
// timeout); a scope left set after the call (the later submission is refused).
TEST_F(VulkanTransferLifetimeGpuSmoke, AComputeLaneRecorderRefusesANestedSubmissionInsteadOfDeadlocking)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "A separate method queue is required (the graphics-lane case has its own test).";
    const auto source = MakeStorageBuffer(4u, "NestedCompute.Source");
    const auto output = MakeStorageBuffer(4u, "NestedCompute.Output");
    ASSERT_TRUE(source.IsValid() && output.IsValid());
    const std::uint32_t written = 0x5eedf00du;
    ASSERT_TRUE(Drain(queue, queue.UploadBuffer(source, &written, sizeof(written), 0u)));
    RHI::ReadbackToken nestedCompute{1u};
    bool nestedRecorded = false;
    const auto page = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext& commands)
        {
            nestedCompute = SubmitCompletionOnly(*Device, source, &nestedRecorded);
            return RecordCopy(commands, source, output, 4u);
        },
        4u,
        RHI::ReadbackSink::Pull());
    EXPECT_FALSE(nestedCompute.IsValid()) << "a nested submission on the held lane is refused";
    EXPECT_FALSE(nestedRecorded);
    ASSERT_TRUE(page.IsValid());
    ASSERT_EQ(queue.WaitCompute(page, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    std::uint32_t read = 0u;
    ASSERT_TRUE(queue.TakeCompletedReadback(page, std::as_writable_bytes(std::span{&read, 1u})));
    EXPECT_EQ(read, written);
    const auto later = SubmitCompletionOnly(*Device, source);
    ASSERT_TRUE(later.IsValid()) << "a submission after the recorder is accepted again";
    EXPECT_EQ(queue.WaitCompute(later, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    Device->DestroyBuffer(source);
    Device->DestroyBuffer(output);
}

// ARCH-023 query-review B2: a LaneOrderOnly page (a prepared worker page) carries no graphics-ticket wait. With the
// graphics queue held and an upload into another buffer issued behind it, the page completes within the bound with
// its known answer and ComputeGraphicsWaits unchanged, while a default-order page submitted next stays pending until
// the gate opens (the positive control: the held upload is a real dependency of AfterIssuedTransfers). Predictions,
// before any run: lane-order page Complete and read == 7, waits unchanged; control Pending, then Complete with the
// uploaded value. Mutation: LaneOrderOnly ignored (the first page is also Pending and waits grow by 2).
TEST_F(VulkanTransferLifetimeGpuSmoke, ALaneOrderPageDoesNotWaitForAGraphicsUploadIssuedAfterItsInputs)
{
    if (!Initialize(true))
        GTEST_SKIP() << SkipReason;
    auto& queue = Device->GetTransferQueue();
    if (queue.GetDiagnostics().ComputeLane.Resolved != RHI::QueueAffinity::AsyncCompute)
        GTEST_SKIP() << "A separate method queue is required: on the graphics lane queue order alone orders them.";
    const auto input = MakeStorageBuffer(4u, "LaneOrder.Input");
    const auto unrelated = MakeStorageBuffer(4u, "LaneOrder.Unrelated");
    const auto output = MakeStorageBuffer(4u, "LaneOrder.Output");
    const auto control = MakeStorageBuffer(4u, "LaneOrder.Control");
    ASSERT_TRUE(input.IsValid() && unrelated.IsValid() && output.IsValid() && control.IsValid());
    const std::uint32_t prepared = 7u, later = 0xfeedu;
    ASSERT_TRUE(Drain(queue, queue.UploadBuffer(input, &prepared, sizeof(prepared), 0u)));
    // The run's prepared first submission: it waits for the input upload, the pages after it need not.
    const auto preparation = Device->SubmitComputeReadback(
        [&](RHI::ICommandContext&)
        {
            return input;
        },
        0u,
        IgnoreBytes());
    ASSERT_TRUE(preparation.IsValid());
    ASSERT_EQ(queue.WaitCompute(preparation, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    const auto waitsBefore = queue.GetDiagnostics().ComputeGraphicsWaits;
    RHI::ReadbackToken page, after;
    RHI::ComputeWaitStatus pageWhileHeld = RHI::ComputeWaitStatus::Failed,
                           controlWhileHeld = RHI::ComputeWaitStatus::Complete;
    std::uint64_t waitsWhileHeld = 0u;
    ASSERT_TRUE(Vulkan::WithVulkanQueueGate(Device.get(),
        Vulkan::VulkanGatedQueue::Graphics,
        [&]
        {
            ASSERT_TRUE(queue.UploadBuffer(unrelated, &later, sizeof(later), 0u).IsValid());
            page = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    return RecordCopy(commands, input, output, 4u);
                },
                4u,
                RHI::ReadbackSink::Pull(),
                RHI::ComputeInputOrder::LaneOrderOnly);
            ASSERT_TRUE(page.IsValid());
            pageWhileHeld = queue.WaitCompute(page, kDoneWaitNanoseconds);
            waitsWhileHeld = queue.GetDiagnostics().ComputeGraphicsWaits;
            after = Device->SubmitComputeReadback(
                [&](RHI::ICommandContext& commands)
                {
                    return RecordCopy(commands, unrelated, control, 4u);
                },
                4u,
                RHI::ReadbackSink::Pull());
            ASSERT_TRUE(after.IsValid());
            controlWhileHeld = queue.WaitCompute(after, kHeldWaitNanoseconds);
        }));
    EXPECT_EQ(pageWhileHeld, RHI::ComputeWaitStatus::Complete) << "the lane-order page ran with graphics held";
    EXPECT_EQ(waitsWhileHeld, waitsBefore) << "and carried no graphics-ticket wait";
    EXPECT_EQ(controlWhileHeld, RHI::ComputeWaitStatus::Pending) << "the default order waited for the held upload";
    std::uint32_t read = 0u, controlRead = 0u;
    ASSERT_TRUE(queue.TakeCompletedReadback(page, std::as_writable_bytes(std::span{&read, 1u})));
    EXPECT_EQ(read, prepared);
    ASSERT_EQ(queue.WaitCompute(after, kDoneWaitNanoseconds), RHI::ComputeWaitStatus::Complete);
    ASSERT_TRUE(queue.TakeCompletedReadback(after, std::as_writable_bytes(std::span{&controlRead, 1u})));
    EXPECT_EQ(controlRead, later);
    for (const auto buffer : {input, unrelated, output, control})
        Device->DestroyBuffer(buffer);
}
