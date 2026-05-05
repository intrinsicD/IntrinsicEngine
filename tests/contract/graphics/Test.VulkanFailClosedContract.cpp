#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Null;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
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

TEST(VulkanFailClosedContract, InitializeWithNullWindowSkipsBootstrapWithoutVulkanHandles)
{
    Extrinsic::Core::Config::WindowConfig windowConfig{};
    Extrinsic::Platform::Backends::Null::NullWindow window{windowConfig};

    Extrinsic::Core::Config::RenderConfig renderConfig{};
    renderConfig.EnablePromotedVulkanDevice = true;
    renderConfig.EnableValidation = false;

    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);

    device->Initialize(window, renderConfig);
    EXPECT_FALSE(device->IsOperational());

    const Extrinsic::Backends::Vulkan::VulkanBootstrapDiagnosticsSnapshot diagnostics =
        Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();
    EXPECT_EQ(diagnostics.Status,
              Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::SkippedNoNativeWindow);
    EXPECT_FALSE(diagnostics.NativeWindowAvailable);
    EXPECT_FALSE(diagnostics.InstanceCreated);
    EXPECT_FALSE(diagnostics.SurfaceCreated);
    EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
    EXPECT_FALSE(diagnostics.LogicalDeviceCreated);
    EXPECT_FALSE(diagnostics.GraphicsQueueAcquired);
    EXPECT_FALSE(diagnostics.PresentQueueAcquired);
    EXPECT_FALSE(diagnostics.TransferQueueAcquired);
    EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
    EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
    EXPECT_EQ(diagnostics.FrameCommandPoolCount, 0u);
    EXPECT_EQ(diagnostics.FrameCommandBufferCount, 0u);
    EXPECT_EQ(diagnostics.FrameFenceCount, 0u);
    EXPECT_EQ(diagnostics.FrameImageAcquiredSemaphoreCount, 0u);
    EXPECT_EQ(diagnostics.FrameRenderDoneSemaphoreCount, 0u);
    EXPECT_FALSE(diagnostics.SwapchainSurfaceSupported);
    EXPECT_FALSE(diagnostics.SwapchainCreated);
    EXPECT_FALSE(diagnostics.SwapchainImagesEnumerated);
    EXPECT_FALSE(diagnostics.SwapchainImageViewsCreated);
    EXPECT_FALSE(diagnostics.SwapchainImagesRegistered);
    EXPECT_FALSE(diagnostics.DescriptorIndexingSupported);
    EXPECT_FALSE(diagnostics.TimelineSemaphoreSupported);
    EXPECT_FALSE(diagnostics.DynamicRenderingSupported);
    EXPECT_FALSE(diagnostics.BufferDeviceAddressSupported);
    EXPECT_FALSE(diagnostics.RequiredDeviceFeaturesSupported);
    EXPECT_FALSE(diagnostics.DescriptorIndexingEnabled);
    EXPECT_FALSE(diagnostics.TimelineSemaphoreEnabled);
    EXPECT_FALSE(diagnostics.DynamicRenderingEnabled);
    EXPECT_FALSE(diagnostics.BufferDeviceAddressEnabled);
    EXPECT_EQ(diagnostics.SwapchainImageCount, 0u);
    EXPECT_EQ(diagnostics.SwapchainImageViewCount, 0u);
    EXPECT_EQ(diagnostics.SwapchainImageHandleCount, 0u);
    EXPECT_EQ(diagnostics.SwapchainWidth, 0u);
    EXPECT_EQ(diagnostics.SwapchainHeight, 0u);

    device->Shutdown();
    EXPECT_FALSE(device->IsOperational());
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

TEST(VulkanFailClosedContract, BeginFrameOnNonOperationalDeviceIncrementsAttemptCounter)
{
    // GRAPHICS-018 fail-closed contract: VulkanDevice::BeginFrame must return
    // false on a non-operational device and increment a process-monotonic
    // attempt counter, so CPU CI surfaces accidental frame loops driving a
    // fail-closed Vulkan device. Mirrors the bindless/transfer/pipeline
    // counter pattern.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t before =
        Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount();

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(device->BeginFrame(frame));
    EXPECT_FALSE(device->BeginFrame(frame));

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount(),
              before + 2u);
}

TEST(VulkanFailClosedContract, EndFrameOnNonOperationalDeviceIncrementsAttemptCounter)
{
    // GRAPHICS-018 fail-closed contract: VulkanDevice::EndFrame must take the
    // fail-closed early-return path on a non-operational device and increment
    // a process-monotonic attempt counter, mirroring the BeginFrame counter.
    // This catches an unbalanced renderer/runtime frame loop that drives
    // EndFrame against a fail-closed Vulkan device, and pairs with
    // GetFallbackBeginFrameAttemptCount() so CPU diagnostics can observe both
    // halves of the frame-loop pair.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t before =
        Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount();

    const Extrinsic::RHI::FrameHandle frame{};
    device->EndFrame(frame);
    device->EndFrame(frame);

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount(),
              before + 2u);
}

TEST(VulkanFailClosedContract, BeginEndFrameCountersTrackPairwiseOnNonOperationalDevice)
{
    // GRAPHICS-018 fail-closed contract: when a renderer/runtime frame loop
    // calls BeginFrame followed by EndFrame against a fail-closed Vulkan
    // device, both fail-closed counters must advance in lockstep. Asserting
    // matching deltas locks in pair semantics so a future refactor cannot
    // silently demote one half (e.g. drop the EndFrame counter or skip the
    // BeginFrame breadcrumb) without surfacing in CPU CI.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t beforeBegin =
        Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount();
    const std::uint64_t beforeEnd =
        Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount();

    constexpr std::uint64_t kPairs = 5u;
    for (std::uint64_t i = 0; i < kPairs; ++i)
    {
        Extrinsic::RHI::FrameHandle frame{};
        EXPECT_FALSE(device->BeginFrame(frame));
        device->EndFrame(frame);
    }

    const std::uint64_t afterBegin =
        Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount();
    const std::uint64_t afterEnd =
        Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount();

    EXPECT_EQ(afterBegin - beforeBegin, kPairs);
    EXPECT_EQ(afterEnd - beforeEnd, kPairs);
    EXPECT_EQ(afterBegin - beforeBegin, afterEnd - beforeEnd);
}

TEST(VulkanFailClosedContract, PresentOnNonOperationalDeviceIncrementsAttemptCounter)
{
    // GRAPHICS-018 fail-closed contract: VulkanDevice::Present must take a
    // fail-closed early-return path on a non-operational device and increment
    // a process-monotonic attempt counter. This completes CPU-visible coverage
    // for the renderer/runtime BeginFrame -> EndFrame -> Present lifecycle.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t before =
        Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount();

    const Extrinsic::RHI::FrameHandle frame{};
    device->Present(frame);
    device->Present(frame);

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount(),
              before + 2u);
}

TEST(VulkanFailClosedContract, BeginEndPresentCountersTrackLifecycleOnNonOperationalDevice)
{
    // Paired renderer/runtime lifecycle calls against the fail-closed Vulkan
    // device must advance all three frame-loop diagnostics. Present is called
    // by runtime after renderer EndFrame, so tracking it separately catches
    // presentation loops that Begin/End counters alone cannot identify.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t beforeBegin =
        Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount();
    const std::uint64_t beforeEnd =
        Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount();
    const std::uint64_t beforePresent =
        Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount();

    constexpr std::uint64_t kFrames = 4u;
    for (std::uint64_t i = 0; i < kFrames; ++i)
    {
        Extrinsic::RHI::FrameHandle frame{};
        EXPECT_FALSE(device->BeginFrame(frame));
        device->EndFrame(frame);
        device->Present(frame);
    }

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount() - beforeBegin,
              kFrames);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount() - beforeEnd,
              kFrames);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount() - beforePresent,
              kFrames);
}

TEST(VulkanFailClosedContract, ResizeOnNonOperationalDeviceIncrementsAttemptCounterAndRecordsExtent)
{
    // GRAPHICS-018 fail-closed contract: Resize against the promoted Vulkan
    // device before swapchain bring-up cannot recreate swapchain resources, but
    // it must leave deterministic CPU-visible breadcrumbs and preserve the
    // requested extent for runtime/configuration diagnostics.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    const std::uint64_t before =
        Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount();

    device->Resize(640u, 360u);
    device->Resize(1280u, 720u);

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount(),
              before + 2u);

    const auto extent = device->GetBackbufferExtent();
    EXPECT_EQ(extent.Width, 1280);
    EXPECT_EQ(extent.Height, 720);
}

TEST(VulkanFailClosedContract, FrameLifecycleDiagnosticsSnapshotReportsFailClosedStatuses)
{
    // The lifecycle diagnostics snapshot gives CPU/default CI a structured view
    // of the last Begin/End/Present/Resize outcome before real Vulkan acquire,
    // submit, present, and resize paths become operational. It must agree with
    // the existing fail-closed counters while keeping status taxonomy explicit.
    const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot before =
        Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();

    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(device->BeginFrame(frame));
    frame.FrameIndex = 7u;
    frame.SwapchainImageIndex = 3u;
    device->EndFrame(frame);
    device->Present(frame);
    device->Resize(1920u, 1080u);

    const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot after =
        Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();

    EXPECT_EQ(after.BeginStatus,
              Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::SkippedNotOperational);
    EXPECT_EQ(after.EndStatus,
              Extrinsic::Backends::Vulkan::VulkanFrameEndStatus::SkippedNotOperational);
    EXPECT_EQ(after.PresentStatus,
              Extrinsic::Backends::Vulkan::VulkanFramePresentStatus::SkippedNotOperational);
    EXPECT_EQ(after.ResizeStatus,
              Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::RecordedPendingNotOperational);
    EXPECT_EQ(after.LastVkResult, 0);
    EXPECT_EQ(after.LastFrameIndex, 7u);
    EXPECT_EQ(after.LastSwapchainImageIndex, 3u);
    EXPECT_EQ(after.LastRequestedWidth, 1920u);
    EXPECT_EQ(after.LastRequestedHeight, 1080u);
    EXPECT_FALSE(after.DeviceOperational);
    EXPECT_FALSE(after.SwapchainAvailable);
    EXPECT_FALSE(after.SwapchainImagesAvailable);

    EXPECT_EQ(after.BeginFrameAttempts - before.BeginFrameAttempts, 1u);
    EXPECT_EQ(after.EndFrameAttempts - before.EndFrameAttempts, 1u);
    EXPECT_EQ(after.PresentAttempts - before.PresentAttempts, 1u);
    EXPECT_EQ(after.ResizeAttempts - before.ResizeAttempts, 1u);
    EXPECT_EQ(after.BeginFrameAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount());
    EXPECT_EQ(after.EndFrameAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount());
    EXPECT_EQ(after.PresentAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount());
    EXPECT_EQ(after.ResizeAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount());
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

TEST(VulkanFailClosedContract, FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles)
{
    // GRAPHICS-018Q nonblocking clarification: fail-closed fallback counters
    // must remain process-monotonic across full Initialize/Shutdown cycles
    // (and across destruction/re-creation of VulkanDevice instances) so that
    // diagnostics spanning full-engine restarts of the Vulkan backend stay
    // accurate. The pipeline reason must also persist across Shutdown so the
    // last observed reason is queryable after a backend tear-down.
    const std::uint64_t beforeBindless =
        Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount();
    const std::uint64_t beforeTransfer =
        Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount();
    const std::uint64_t beforePipeline =
        Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount();
    const std::uint64_t beforeBeginFrame =
        Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount();
    const std::uint64_t beforeEndFrame =
        Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount();
    const std::uint64_t beforePresent =
        Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount();
    const std::uint64_t beforeResize =
        Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount();

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    Extrinsic::Core::Config::RenderConfig renderConfig{};

    auto fireFallbackPaths = [](Extrinsic::RHI::IDevice& device)
    {
        EXPECT_EQ(device.GetBindlessHeap().AllocateTextureSlot({}, {}),
                  Extrinsic::RHI::kInvalidBindlessIndex);
        const Extrinsic::RHI::TransferToken token =
            device.GetTransferQueue().UploadBuffer({}, nullptr, 0u, 0u);
        EXPECT_FALSE(token.IsValid());
        EXPECT_FALSE(device.CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());
        Extrinsic::RHI::FrameHandle frame{};
        EXPECT_FALSE(device.BeginFrame(frame));
        device.EndFrame(frame);
        device.Present(frame);
        device.Resize(800u, 600u);
    };

    {
        Extrinsic::Platform::Backends::Null::NullWindow window{windowConfig};
        std::unique_ptr<Extrinsic::RHI::IDevice> device =
            Extrinsic::Backends::Vulkan::CreateVulkanDevice();
        ASSERT_NE(device, nullptr);

        device->Initialize(window, renderConfig);
        ASSERT_FALSE(device->IsOperational());
        fireFallbackPaths(*device);
        device->Shutdown();
        ASSERT_FALSE(device->IsOperational());

        // Counters and the last pipeline reason must survive Shutdown without
        // being reset to None or zero by the lifecycle transition.
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount(),
                  beforeBindless + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
                  beforeTransfer + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount(),
                  beforePipeline + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount(),
                  beforeBeginFrame + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount(),
                  beforeEndFrame + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount(),
                  beforePresent + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount(),
                  beforeResize + 1u);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetLastFallbackPipelineReason(),
                  Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp);
    }

    // First device fully destroyed; counters must persist across instances.
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount(),
              beforeBindless + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
              beforeTransfer + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount(),
              beforePipeline + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount(),
              beforeBeginFrame + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount(),
              beforeEndFrame + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount(),
              beforePresent + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount(),
              beforeResize + 1u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetLastFallbackPipelineReason(),
              Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp);

    {
        Extrinsic::Platform::Backends::Null::NullWindow window{windowConfig};
        std::unique_ptr<Extrinsic::RHI::IDevice> device =
            Extrinsic::Backends::Vulkan::CreateVulkanDevice();
        ASSERT_NE(device, nullptr);

        device->Initialize(window, renderConfig);
        ASSERT_FALSE(device->IsOperational());
        fireFallbackPaths(*device);
        device->Shutdown();
    }

    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount(),
              beforeBindless + 2u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount(),
              beforeTransfer + 2u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount(),
              beforePipeline + 2u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount(),
              beforeBeginFrame + 2u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount(),
              beforeEndFrame + 2u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount(),
              beforePresent + 2u);
    EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount(),
              beforeResize + 2u);
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
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::NotStarted) == 0u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::SkippedNoNativeWindow) == 1u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::ProbedPhysicalDevice) == 8u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedLogicalDevice) == 9u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedLogicalDeviceCreation) == 10u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedMemoryAllocator) == 11u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedMemoryAllocatorCreation) == 12u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedPerFrameResources) == 13u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedPerFrameResourceCreation) == 14u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedSwapchain) == 15u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainCreation) == 16u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainImageEnumeration) == 17u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainImageViewCreation) == 18u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages) == 19u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::SkippedNotOperational) == 1u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::FailedAcquire) == 5u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFrameEndStatus::SkippedNotOperational) == 1u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFrameEndStatus::FailedSubmit) == 3u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFramePresentStatus::SkippedNotOperational) == 1u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFramePresentStatus::FailedPresent) == 6u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::RecordedPendingNotOperational) == 1u);
static_assert(static_cast<std::uint8_t>(
                  Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::FailedRecreate) == 5u);

TEST(VulkanFailClosedContract, FallbackDiagnosticsSnapshotMatchesIndividualGetters)
{
    // The aggregate snapshot is the preferred CPU diagnostics surface for
    // consumers that want all fail-closed counters and the last pipeline
    // reason in a single call. Each field of the snapshot must agree with the
    // corresponding individual accessor at the moment of capture, so callers
    // can switch to the snapshot without observing drift relative to existing
    // free-function getters.
    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    EXPECT_EQ(device->GetBindlessHeap().AllocateTextureSlot({}, {}),
              Extrinsic::RHI::kInvalidBindlessIndex);
    EXPECT_FALSE(device->GetTransferQueue().UploadBuffer({}, nullptr, 0u, 0u).IsValid());
    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());
    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(device->BeginFrame(frame));
    device->EndFrame(frame);
    device->Present(frame);
    device->Resize(1024u, 768u);

    const Extrinsic::Backends::Vulkan::FallbackDiagnosticsSnapshot snapshot =
        Extrinsic::Backends::Vulkan::GetFallbackDiagnosticsSnapshot();

    EXPECT_EQ(snapshot.BindlessAllocationAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount());
    EXPECT_EQ(snapshot.TransferUploadAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount());
    EXPECT_EQ(snapshot.PipelineCreationAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackPipelineCreationAttemptCount());
    EXPECT_EQ(snapshot.LastPipelineReason,
              Extrinsic::Backends::Vulkan::GetLastFallbackPipelineReason());
    EXPECT_EQ(snapshot.LastPipelineReason,
              Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp);
    EXPECT_EQ(snapshot.BeginFrameAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount());
    EXPECT_EQ(snapshot.EndFrameAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackEndFrameAttemptCount());
    EXPECT_EQ(snapshot.PresentAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackPresentAttemptCount());
    EXPECT_EQ(snapshot.ResizeAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackResizeAttemptCount());
}

TEST(VulkanFailClosedContract, FallbackDiagnosticsSnapshotIsProcessMonotonic)
{
    // Snapshots taken before/after a sequence of fail-closed fires must show
    // deltas equal to the number of fires of each kind, matching the
    // process-monotonic guarantee already asserted on the individual
    // counters. The aggregate accessor must not introduce any reset, scoping,
    // or off-by-one behavior relative to the individual getters.
    const Extrinsic::Backends::Vulkan::FallbackDiagnosticsSnapshot before =
        Extrinsic::Backends::Vulkan::GetFallbackDiagnosticsSnapshot();

    std::unique_ptr<Extrinsic::RHI::IDevice> device = Extrinsic::Backends::Vulkan::CreateVulkanDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_FALSE(device->IsOperational());

    EXPECT_EQ(device->GetBindlessHeap().AllocateTextureSlot({}, {}),
              Extrinsic::RHI::kInvalidBindlessIndex);
    EXPECT_EQ(device->GetBindlessHeap().AllocateTextureSlot({}, {}),
              Extrinsic::RHI::kInvalidBindlessIndex);
    EXPECT_FALSE(device->GetTransferQueue().UploadBuffer({}, nullptr, 0u, 0u).IsValid());
    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());
    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());
    EXPECT_FALSE(device->CreatePipeline(Extrinsic::RHI::PipelineDesc{}).IsValid());
    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(device->BeginFrame(frame));
    EXPECT_FALSE(device->BeginFrame(frame));
    EXPECT_FALSE(device->BeginFrame(frame));
    EXPECT_FALSE(device->BeginFrame(frame));
    device->EndFrame(frame);
    device->EndFrame(frame);
    device->Present(frame);
    device->Present(frame);
    device->Present(frame);
    device->Present(frame);
    device->Present(frame);
    device->Resize(320u, 200u);
    device->Resize(640u, 400u);
    device->Resize(960u, 600u);

    const Extrinsic::Backends::Vulkan::FallbackDiagnosticsSnapshot after =
        Extrinsic::Backends::Vulkan::GetFallbackDiagnosticsSnapshot();

    EXPECT_EQ(after.BindlessAllocationAttempts - before.BindlessAllocationAttempts, 2u);
    EXPECT_EQ(after.TransferUploadAttempts - before.TransferUploadAttempts, 1u);
    EXPECT_EQ(after.PipelineCreationAttempts - before.PipelineCreationAttempts, 3u);
    EXPECT_EQ(after.BeginFrameAttempts - before.BeginFrameAttempts, 4u);
    EXPECT_EQ(after.EndFrameAttempts - before.EndFrameAttempts, 2u);
    EXPECT_EQ(after.PresentAttempts - before.PresentAttempts, 5u);
    EXPECT_EQ(after.ResizeAttempts - before.ResizeAttempts, 3u);
    EXPECT_EQ(after.LastPipelineReason,
              Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp);
}
