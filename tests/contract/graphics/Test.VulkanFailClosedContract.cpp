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

    const Extrinsic::Backends::Vulkan::FallbackDiagnosticsSnapshot after =
        Extrinsic::Backends::Vulkan::GetFallbackDiagnosticsSnapshot();

    EXPECT_EQ(after.BindlessAllocationAttempts - before.BindlessAllocationAttempts, 2u);
    EXPECT_EQ(after.TransferUploadAttempts - before.TransferUploadAttempts, 1u);
    EXPECT_EQ(after.PipelineCreationAttempts - before.PipelineCreationAttempts, 3u);
    EXPECT_EQ(after.LastPipelineReason,
              Extrinsic::Backends::Vulkan::FallbackPipelineReason::PreBringUp);
}
