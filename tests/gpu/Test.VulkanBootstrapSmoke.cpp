#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.RHI.Types;

namespace
{
    [[nodiscard]] bool CommandSucceeded(const std::string& command)
    {
        return std::system(command.c_str()) == 0;
    }
}

TEST(VulkanBootstrapSmoke, InitializeCreatesPerFrameResourcesOrFailsCleanly)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; Vulkan bootstrap smoke is opt-in for window-capable hosts.";
    }

    Extrinsic::Core::Config::WindowConfig windowConfig{};
    windowConfig.Title = "Intrinsic Vulkan Bootstrap Smoke";
    windowConfig.Width = 64;
    windowConfig.Height = 64;
    windowConfig.Resizable = false;

    std::unique_ptr<Extrinsic::Platform::IWindow> window =
        Extrinsic::Platform::Backends::Glfw::CreateWindow(windowConfig);
    ASSERT_NE(window, nullptr);
    if (window->GetNativeHandle() == nullptr)
    {
        GTEST_SKIP() << "GLFW window creation failed; Vulkan bootstrap smoke requires a native surface.";
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
    EXPECT_FALSE(device->IsOperational())
        << "phase-1 bootstrap must not mark Vulkan operational before swapchain/resource reconciliation";

    const Extrinsic::Backends::Vulkan::VulkanBootstrapDiagnosticsSnapshot diagnostics =
        Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();

    EXPECT_TRUE(diagnostics.NativeWindowAvailable);
    EXPECT_TRUE(diagnostics.VolkInitialized)
        << "with a GLFW native window, bootstrap should reach the Vulkan loader before any clean failure";

    const std::uint32_t expectedFramesInFlight = device->GetFramesInFlight();
    const auto expectRequiredQueuesFound = [&diagnostics]()
    {
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        if (!diagnostics.AsyncComputeQueueFound)
        {
            EXPECT_FALSE(diagnostics.AsyncComputeQueueDedicated);
        }
    };
    const auto expectRequiredQueuesAcquired = [&diagnostics]()
    {
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
        if (diagnostics.AsyncComputeQueueFound)
        {
            EXPECT_TRUE(diagnostics.AsyncComputeQueueAcquired);
        }
        else
        {
            EXPECT_FALSE(diagnostics.AsyncComputeQueueAcquired);
        }
    };
    const auto expectNoQueuesAcquired = [&diagnostics]()
    {
        EXPECT_FALSE(diagnostics.GraphicsQueueAcquired);
        EXPECT_FALSE(diagnostics.AsyncComputeQueueAcquired);
        EXPECT_FALSE(diagnostics.PresentQueueAcquired);
        EXPECT_FALSE(diagnostics.TransferQueueAcquired);
    };

    if (diagnostics.PhysicalDeviceSelected)
    {
        EXPECT_TRUE(diagnostics.DescriptorIndexingSupported);
        EXPECT_TRUE(diagnostics.TimelineSemaphoreSupported);
        EXPECT_TRUE(diagnostics.DynamicRenderingSupported);
        EXPECT_TRUE(diagnostics.BufferDeviceAddressSupported);
        EXPECT_TRUE(diagnostics.RequiredDeviceFeaturesSupported);
    }
    else
    {
        EXPECT_FALSE(diagnostics.DescriptorIndexingEnabled);
        EXPECT_FALSE(diagnostics.TimelineSemaphoreEnabled);
        EXPECT_FALSE(diagnostics.DynamicRenderingEnabled);
        EXPECT_FALSE(diagnostics.BufferDeviceAddressEnabled);
    }

    if (diagnostics.LogicalDeviceCreated)
    {
        EXPECT_TRUE(diagnostics.DescriptorIndexingEnabled);
        EXPECT_TRUE(diagnostics.TimelineSemaphoreEnabled);
        EXPECT_TRUE(diagnostics.DynamicRenderingEnabled);
        EXPECT_TRUE(diagnostics.BufferDeviceAddressEnabled);
    }

    switch (diagnostics.Status)
    {
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_TRUE(diagnostics.MemoryAllocatorCreated);
        EXPECT_TRUE(diagnostics.PerFrameResourcesCreated);
        EXPECT_EQ(diagnostics.FrameCommandPoolCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameCommandBufferCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameFenceCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameImageAcquiredSemaphoreCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameRenderDoneSemaphoreCount, expectedFramesInFlight);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_TRUE(diagnostics.SwapchainCreated);
        EXPECT_TRUE(diagnostics.SwapchainImagesEnumerated);
        EXPECT_TRUE(diagnostics.SwapchainImageViewsCreated);
        EXPECT_TRUE(diagnostics.SwapchainImagesRegistered);
        EXPECT_GT(diagnostics.SwapchainImageCount, 0u);
        EXPECT_EQ(diagnostics.SwapchainImageViewCount, diagnostics.SwapchainImageCount);
        EXPECT_EQ(diagnostics.SwapchainImageHandleCount, diagnostics.SwapchainImageCount);
        EXPECT_GT(diagnostics.SwapchainWidth, 0u);
        EXPECT_GT(diagnostics.SwapchainHeight, 0u);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedPerFrameResources:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_TRUE(diagnostics.MemoryAllocatorCreated);
        EXPECT_TRUE(diagnostics.PerFrameResourcesCreated);
        EXPECT_EQ(diagnostics.FrameCommandPoolCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameCommandBufferCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameFenceCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameImageAcquiredSemaphoreCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameRenderDoneSemaphoreCount, expectedFramesInFlight);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_FALSE(diagnostics.SwapchainCreated);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedMemoryAllocator:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_TRUE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedLogicalDevice:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::ProbedPhysicalDevice:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_FALSE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectNoQueuesAcquired();
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedLogicalDeviceCreation:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        expectRequiredQueuesFound();
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        if (diagnostics.LogicalDeviceCreated)
        {
            EXPECT_FALSE(diagnostics.GraphicsQueueAcquired &&
                         (!diagnostics.AsyncComputeQueueFound ||
                          diagnostics.AsyncComputeQueueAcquired) &&
                         diagnostics.PresentQueueAcquired &&
                         diagnostics.TransferQueueAcquired);
        }
        EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedMemoryAllocatorCreation:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        EXPECT_EQ(diagnostics.FrameCommandPoolCount, 0u);
        EXPECT_EQ(diagnostics.FrameCommandBufferCount, 0u);
        EXPECT_EQ(diagnostics.FrameFenceCount, 0u);
        EXPECT_EQ(diagnostics.FrameImageAcquiredSemaphoreCount, 0u);
        EXPECT_EQ(diagnostics.FrameRenderDoneSemaphoreCount, 0u);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedPerFrameResourceCreation:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_TRUE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        EXPECT_LE(diagnostics.FrameCommandPoolCount, expectedFramesInFlight);
        EXPECT_LE(diagnostics.FrameCommandBufferCount, expectedFramesInFlight);
        EXPECT_LE(diagnostics.FrameFenceCount, expectedFramesInFlight);
        EXPECT_LE(diagnostics.FrameImageAcquiredSemaphoreCount, expectedFramesInFlight);
        EXPECT_LE(diagnostics.FrameRenderDoneSemaphoreCount, expectedFramesInFlight);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::CreatedSwapchain:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainCreation:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainImageEnumeration:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainImageViewCreation:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.LogicalDeviceCreated);
        expectRequiredQueuesFound();
        expectRequiredQueuesAcquired();
        EXPECT_TRUE(diagnostics.MemoryAllocatorCreated);
        EXPECT_TRUE(diagnostics.PerFrameResourcesCreated);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_EQ(diagnostics.FrameCommandPoolCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameCommandBufferCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameFenceCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameImageAcquiredSemaphoreCount, expectedFramesInFlight);
        EXPECT_EQ(diagnostics.FrameRenderDoneSemaphoreCount, expectedFramesInFlight);
        if (diagnostics.Status == Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSwapchainCreation)
        {
            EXPECT_FALSE(diagnostics.SwapchainImagesEnumerated);
            EXPECT_FALSE(diagnostics.SwapchainImageViewsCreated);
            EXPECT_FALSE(diagnostics.SwapchainImagesRegistered);
        }
        else
        {
            EXPECT_TRUE(diagnostics.SwapchainCreated);
            EXPECT_GT(diagnostics.SwapchainWidth, 0u);
            EXPECT_GT(diagnostics.SwapchainHeight, 0u);
            EXPECT_LE(diagnostics.SwapchainImageViewCount, diagnostics.SwapchainImageCount);
            EXPECT_LE(diagnostics.SwapchainImageHandleCount, diagnostics.SwapchainImageCount);
        }
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedNoSuitablePhysicalDevice:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
        EXPECT_FALSE(diagnostics.LogicalDeviceCreated);
        EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSurfaceCreation:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedPhysicalDeviceEnumeration:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
        EXPECT_FALSE(diagnostics.LogicalDeviceCreated);
        EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedInstanceCreation:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedRequiredInstanceExtensions:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedVolkInitialize:
        EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
        EXPECT_FALSE(diagnostics.LogicalDeviceCreated);
        EXPECT_FALSE(diagnostics.MemoryAllocatorCreated);
        EXPECT_FALSE(diagnostics.PerFrameResourcesCreated);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::NotStarted:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::SkippedNoNativeWindow:
        FAIL() << "GLFW smoke supplied a native window, so bootstrap should not remain unstarted or skip for missing native window";
    }

    const Extrinsic::Backends::Vulkan::VulkanServiceDiagnosticsSnapshot serviceDiagnostics =
        Extrinsic::Backends::Vulkan::GetVulkanServiceDiagnosticsSnapshot();
    if (diagnostics.Status == Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages)
    {
        EXPECT_EQ(serviceDiagnostics.Status,
                  Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready);
        EXPECT_TRUE(serviceDiagnostics.BindlessHeapCreated);
        EXPECT_TRUE(serviceDiagnostics.GlobalPipelineLayoutCreated);
        EXPECT_TRUE(serviceDiagnostics.TransferQueueCreated);
        EXPECT_TRUE(serviceDiagnostics.CommandContextsRebound);
        EXPECT_EQ(serviceDiagnostics.CommandContextRebindCount, expectedFramesInFlight);
        EXPECT_GT(serviceDiagnostics.BindlessCapacity, 0u);
        EXPECT_TRUE(serviceDiagnostics.LiveOperationalPrerequisitesReady);
        EXPECT_TRUE(serviceDiagnostics.OperationalSafetyPrerequisitesReady);
        EXPECT_FALSE(serviceDiagnostics.PublicServicesExposed);
        EXPECT_FALSE(serviceDiagnostics.PublicServicesRemainFailClosed);
        EXPECT_FALSE(serviceDiagnostics.PublicBindlessHeapExposed);
        EXPECT_TRUE(serviceDiagnostics.PublicTransferQueueExposed);

        const Extrinsic::RHI::BufferHandle sceneTableBuffer = device->CreateBuffer({
            .SizeBytes = sizeof(Extrinsic::RHI::GpuSceneTable),
            .Usage = Extrinsic::RHI::BufferUsage::Storage | Extrinsic::RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = "VulkanBootstrapSmoke.SceneTable",
        });
        ASSERT_TRUE(sceneTableBuffer.IsValid())
            << "service-ready guarded bootstrap should cover canonical scene-table storage buffers";
        EXPECT_NE(device->GetBufferDeviceAddress(sceneTableBuffer), 0u)
            << "scene-table buffers need BDA for canonical push-constant binding";

        Extrinsic::RHI::GpuSceneTable emptySceneTable{};
        device->WriteBuffer(sceneTableBuffer, &emptySceneTable, sizeof(emptySceneTable), 0u);

        // GRAPHICS-082: stage a pattern into a host-visible TransferSrc buffer
        // via WriteBuffer; the GPU consumes it later via CopyBuffer inside the
        // frame loop below, and ReadBuffer pulls the device-visible bytes back
        // through a separate host-visible TransferDst destination. This routes
        // the host write through the actual VMA flush gate before any device
        // read so the assertion would catch a missing or broken
        // vmaFlushAllocation on non-HOST_COHERENT memory (on HOST_COHERENT
        // hosts the flush is a documented no-op and the GPU still sees the
        // pattern).
        constexpr std::uint32_t kHostVisibleFlushPattern = 0xA5C3'5AB6u;
        const Extrinsic::RHI::BufferHandle flushSrcBuffer = device->CreateBuffer({
            .SizeBytes = sizeof(kHostVisibleFlushPattern),
            .Usage = Extrinsic::RHI::BufferUsage::TransferSrc,
            .HostVisible = true,
            .DebugName = "VulkanBootstrapSmoke.HostVisibleWriteFlush.Src",
        });
        ASSERT_TRUE(flushSrcBuffer.IsValid())
            << "service-ready guarded bootstrap should create host-visible buffers for GRAPHICS-082 flush coverage";
        const Extrinsic::RHI::BufferHandle flushDstBuffer = device->CreateBuffer({
            .SizeBytes = sizeof(kHostVisibleFlushPattern),
            .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
            .HostVisible = true,
            .DebugName = "VulkanBootstrapSmoke.HostVisibleWriteFlush.Dst",
        });
        ASSERT_TRUE(flushDstBuffer.IsValid())
            << "service-ready guarded bootstrap should create the host-visible readback destination for GRAPHICS-082";

        device->WriteBuffer(flushSrcBuffer, &kHostVisibleFlushPattern,
                            sizeof(kHostVisibleFlushPattern), 0u);

        const Extrinsic::RHI::BufferHandle indirectArgsBuffer = device->CreateBuffer({
            .SizeBytes = sizeof(Extrinsic::RHI::GpuDrawIndexedCommand),
            .Usage = Extrinsic::RHI::BufferUsage::Storage |
                     Extrinsic::RHI::BufferUsage::Indirect |
                     Extrinsic::RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = "VulkanBootstrapSmoke.IndirectArgs",
        });
        ASSERT_TRUE(indirectArgsBuffer.IsValid())
            << "canonical culling draw-bucket indirect buffers should be creatable through the promoted backend";
        EXPECT_NE(device->GetBufferDeviceAddress(indirectArgsBuffer), 0u);

        const Extrinsic::RHI::TextureHandle depthTarget = device->CreateTexture({
            .Width = diagnostics.SwapchainWidth,
            .Height = diagnostics.SwapchainHeight,
            .Fmt = Extrinsic::RHI::Format::D32_FLOAT,
            .Usage = Extrinsic::RHI::TextureUsage::DepthTarget | Extrinsic::RHI::TextureUsage::Sampled,
            .DebugName = "VulkanBootstrapSmoke.SceneDepth",
        });
        ASSERT_TRUE(depthTarget.IsValid())
            << "canonical depth-prepass attachment textures should be creatable after service-ready bootstrap";

        const Extrinsic::RHI::TextureHandle colorTarget = device->CreateTexture({
            .Width = diagnostics.SwapchainWidth,
            .Height = diagnostics.SwapchainHeight,
            .Fmt = Extrinsic::RHI::Format::RGBA16_FLOAT,
            .Usage = Extrinsic::RHI::TextureUsage::ColorTarget | Extrinsic::RHI::TextureUsage::Sampled,
            .DebugName = "VulkanBootstrapSmoke.SceneColorHDR",
        });
        ASSERT_TRUE(colorTarget.IsValid())
            << "canonical surface/deferred color attachments should be creatable after service-ready bootstrap";

        const Extrinsic::RHI::TextureHandle sampledTexture = device->CreateTexture({
            .Width = 1u,
            .Height = 1u,
            .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
            .Usage = Extrinsic::RHI::TextureUsage::Sampled | Extrinsic::RHI::TextureUsage::TransferDst,
            .DebugName = "VulkanBootstrapSmoke.SampledTexture",
        });
        ASSERT_TRUE(sampledTexture.IsValid())
            << "canonical material texture descriptors should have a creatable sampled texture path";

        const Extrinsic::RHI::SamplerHandle sampler = device->CreateSampler({
            .DebugName = "VulkanBootstrapSmoke.MaterialSampler",
        });
        ASSERT_TRUE(sampler.IsValid())
            << "canonical material texture descriptors should have a creatable sampler path";

        const std::array<std::uint32_t, 1u> texel{0xff'ff'ff'ffu};
        device->WriteTexture(sampledTexture, texel.data(), sizeof(texel), 0u, 0u);

        Extrinsic::RHI::ITransferQueue& transferQueue = device->GetTransferQueue();
        const auto uploadFullChain = [&](const Extrinsic::RHI::TextureDesc& desc,
                                         const char* label) -> bool
        {
            const Extrinsic::RHI::TextureHandle texture = device->CreateTexture(desc);
            if (!texture.IsValid())
            {
                ADD_FAILURE() << label << ": service-ready guarded bootstrap should create the sampled texture";
                return false;
            }

            auto layoutOr = Extrinsic::RHI::ComputeFullChainUploadLayout(desc);
            if (!layoutOr.has_value())
            {
                ADD_FAILURE() << label << ": CPU upload layout computation failed";
                device->DestroyTexture(texture);
                return false;
            }

            std::vector<std::byte> fullChainBytes(static_cast<std::size_t>(layoutOr->TotalBytes), std::byte{0});
            for (const Extrinsic::RHI::TextureUploadSubresource& sub : layoutOr->Subresources)
            {
                const std::byte pattern{static_cast<unsigned char>(0x20u + sub.MipLevel + sub.ArrayLayer * 8u)};
                std::fill_n(fullChainBytes.data() + static_cast<std::size_t>(sub.OffsetBytes),
                            static_cast<std::size_t>(sub.SizeBytes),
                            pattern);
            }

            const std::uint64_t beforeFallbackTransfer =
                Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount();
            const Extrinsic::RHI::TransferToken token = transferQueue.UploadTextureFullChain(
                texture,
                std::span<const std::byte>{fullChainBytes});
            if (!token.IsValid())
            {
                ADD_FAILURE() << label << ": service-ready public transfer queue should return a live token";
                device->DestroyTexture(texture);
                return false;
            }
            if (Extrinsic::Backends::Vulkan::GetFallbackTransferUploadAttemptCount() != beforeFallbackTransfer)
            {
                ADD_FAILURE() << label << ": upload smoke routed through the fallback transfer queue";
                device->DestroyTexture(texture);
                return false;
            }

            device->WaitIdle();
            if (!transferQueue.IsComplete(token))
            {
                ADD_FAILURE() << label << ": transfer token did not complete after WaitIdle()";
                device->DestroyTexture(texture);
                return false;
            }
            transferQueue.CollectCompleted();
            device->DestroyTexture(texture);
            return true;
        };

        const Extrinsic::RHI::TextureDesc fullChainTextureDesc{
            .Width = 4u,
            .Height = 4u,
            .MipLevels = 3u,
            .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
            .Dimension = Extrinsic::RHI::TextureDimension::Tex2D,
            .Usage = Extrinsic::RHI::TextureUsage::Sampled | Extrinsic::RHI::TextureUsage::TransferDst,
            .DebugName = "VulkanBootstrapSmoke.FullChainTexture",
        };
        ASSERT_TRUE(uploadFullChain(fullChainTextureDesc, "2D full-chain texture"));

        const Extrinsic::RHI::TextureDesc arrayFullChainTextureDesc{
            .Width = 4u,
            .Height = 4u,
            .DepthOrArrayLayers = 2u,
            .MipLevels = 3u,
            .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
            .Dimension = Extrinsic::RHI::TextureDimension::Tex2D,
            .Usage = Extrinsic::RHI::TextureUsage::Sampled | Extrinsic::RHI::TextureUsage::TransferDst,
            .DebugName = "VulkanBootstrapSmoke.ArrayFullChainTexture",
        };
        ASSERT_TRUE(uploadFullChain(arrayFullChainTextureDesc, "2D array full-chain texture"));

        const Extrinsic::RHI::TextureDesc cubeFullChainTextureDesc{
            .Width = 4u,
            .Height = 4u,
            .DepthOrArrayLayers = 6u,
            .MipLevels = 3u,
            .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
            .Dimension = Extrinsic::RHI::TextureDimension::TexCube,
            .Usage = Extrinsic::RHI::TextureUsage::Sampled | Extrinsic::RHI::TextureUsage::TransferDst,
            .DebugName = "VulkanBootstrapSmoke.CubeFullChainTexture",
        };
        ASSERT_TRUE(uploadFullChain(cubeFullChainTextureDesc, "cubemap full-chain texture"));

        const std::uint64_t beforeCommandRecordingFallback =
            Extrinsic::Backends::Vulkan::GetFallbackCommandRecordingAttemptCount();
        Extrinsic::RHI::FrameHandle frame{};
        const bool frameBegun = device->BeginFrame(frame);
        const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot beginDiagnostics =
            Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
        EXPECT_FALSE(beginDiagnostics.DeviceOperational)
            << "guarded empty-frame smoke must not mark the promoted Vulkan device operational";
        EXPECT_TRUE(beginDiagnostics.SwapchainAvailable);
        EXPECT_TRUE(beginDiagnostics.SwapchainImagesAvailable);

        if (frameBegun)
        {
            EXPECT_TRUE(beginDiagnostics.BeginStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::Acquired ||
                        beginDiagnostics.BeginStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::Suboptimal);
            EXPECT_EQ(beginDiagnostics.LastFrameIndex, frame.FrameIndex);
            EXPECT_EQ(beginDiagnostics.LastSwapchainImageIndex, frame.SwapchainImageIndex);

            Extrinsic::RHI::ICommandContext& graphicsContext = device->GetGraphicsContext(frame.FrameIndex);
            graphicsContext.Begin();
            // GRAPHICS-082: GPU consumes the host-visible source so the
            // device actually reads the bytes that WriteBuffer's flush gate
            // is responsible for making visible.
            graphicsContext.CopyBuffer(flushSrcBuffer, flushDstBuffer,
                                       0u, 0u, sizeof(kHostVisibleFlushPattern));
            const Extrinsic::RHI::TextureHandle backbuffer = device->GetBackbufferHandle(frame);
            ASSERT_TRUE(backbuffer.IsValid()) << "acquired Vulkan swapchain image should map to a registered RHI handle";
            graphicsContext.TextureBarrier(backbuffer,
                                           Extrinsic::RHI::TextureLayout::Undefined,
                                           Extrinsic::RHI::TextureLayout::Present);
            graphicsContext.End();

            device->EndFrame(frame);
            const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot endDiagnostics =
                Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
            EXPECT_EQ(endDiagnostics.EndStatus,
                      Extrinsic::Backends::Vulkan::VulkanFrameEndStatus::Submitted);
            EXPECT_EQ(endDiagnostics.LastVkResult, 0);
            EXPECT_FALSE(endDiagnostics.DeviceOperational);

            device->Present(frame);
            const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot presentDiagnostics =
                Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
            EXPECT_TRUE(presentDiagnostics.PresentStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFramePresentStatus::Presented ||
                        presentDiagnostics.PresentStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFramePresentStatus::Suboptimal ||
                        presentDiagnostics.PresentStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFramePresentStatus::OutOfDate);
            EXPECT_FALSE(presentDiagnostics.DeviceOperational);
            EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackCommandRecordingAttemptCount(),
                      beforeCommandRecordingFallback)
                << "guarded empty-frame command recording should use a live Vulkan command buffer";

            // GRAPHICS-082: ReadBuffer drains the queue via vkDeviceWaitIdle
            // and invalidates the destination mapping before memcpy, so the
            // assertion reflects what the device actually wrote through
            // vkCmdCopyBuffer from the host-visible source.
            std::uint32_t flushReadback = 0u;
            device->ReadBuffer(flushDstBuffer, &flushReadback,
                               sizeof(flushReadback), 0u);
            EXPECT_EQ(flushReadback, kHostVisibleFlushPattern)
                << "GPU CopyBuffer must observe the host-visible WriteBuffer pattern; "
                   "a missing vmaFlushAllocation on non-HOST_COHERENT memory would surface here";
        }
        else
        {
            EXPECT_TRUE(beginDiagnostics.BeginStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::OutOfDate ||
                        beginDiagnostics.BeginStatus ==
                            Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::FailedAcquire)
                << "service-ready bootstrap may skip only for a structured acquire/out-of-date failure";
        }

        device->Resize(0u, diagnostics.SwapchainHeight);
        const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot zeroResizeDiagnostics =
            Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
        EXPECT_EQ(zeroResizeDiagnostics.ResizeStatus,
                  Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::RecordedPendingRecreate);
        EXPECT_EQ(zeroResizeDiagnostics.LastRequestedWidth, 0u);
        EXPECT_EQ(zeroResizeDiagnostics.LastRequestedHeight, diagnostics.SwapchainHeight);
        EXPECT_TRUE(zeroResizeDiagnostics.ResizePending);
        EXPECT_FALSE(zeroResizeDiagnostics.DeviceLost);
        EXPECT_FALSE(zeroResizeDiagnostics.DeviceOperational);

        device->Resize(diagnostics.SwapchainWidth, diagnostics.SwapchainHeight);
        const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot resizeDiagnostics =
            Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
        EXPECT_TRUE(resizeDiagnostics.ResizeStatus ==
                        Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::Recreated ||
                    resizeDiagnostics.ResizeStatus ==
                        Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::FailedRecreate)
            << "service-ready guarded resize must either recreate the swapchain or report structured failure";
        EXPECT_EQ(resizeDiagnostics.LastRequestedWidth, diagnostics.SwapchainWidth);
        EXPECT_EQ(resizeDiagnostics.LastRequestedHeight, diagnostics.SwapchainHeight);
        EXPECT_FALSE(resizeDiagnostics.DeviceOperational);
        if (resizeDiagnostics.ResizeStatus == Extrinsic::Backends::Vulkan::VulkanFrameResizeStatus::Recreated)
        {
            EXPECT_EQ(resizeDiagnostics.LastVkResult, 0);
            EXPECT_TRUE(resizeDiagnostics.SwapchainAvailable);
            EXPECT_TRUE(resizeDiagnostics.SwapchainImagesAvailable);
            EXPECT_FALSE(resizeDiagnostics.ResizePending);
            const Extrinsic::Platform::Extent2D recreatedExtent = device->GetBackbufferExtent();
            EXPECT_GT(recreatedExtent.Width, 0);
            EXPECT_GT(recreatedExtent.Height, 0);
        }

        const std::uint64_t beforeFallbackBindless =
            Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount();
        EXPECT_EQ(device->GetBindlessHeap().AllocateTextureSlot({}, {}),
                  Extrinsic::RHI::kInvalidBindlessIndex);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount(),
                  beforeFallbackBindless + 1u)
            << "live bindless service exists internally, but public access must remain fail-closed until operational";

        if (!CommandSucceeded("glslc --version >/dev/null 2>&1"))
        {
            SUCCEED() << "glslc is not available on PATH; skipping opt-in Vulkan pipeline creation sub-check.";
        }
        else
        {
            const std::filesystem::path sourceRoot = std::filesystem::path{__FILE__}
                .parent_path()
                .parent_path()
                .parent_path();
            const std::filesystem::path shaderDir = std::filesystem::temp_directory_path() /
                "intrinsic-vulkan-bootstrap-smoke";
            std::filesystem::create_directories(shaderDir);
            const std::filesystem::path computeSource = shaderDir / "pipeline_smoke.comp";
            const std::filesystem::path computeSpv = shaderDir / "pipeline_smoke.comp.spv";
            const std::filesystem::path depthSpv = shaderDir / "depth_prepass.vert.spv";
            {
                std::ofstream shader{computeSource};
                ASSERT_TRUE(shader) << "failed to create temporary compute shader source";
                shader << "#version 450\n"
                          "layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;\n"
                          "void main() {}\n";
            }

            const std::string compileCommand = "glslc \"" + computeSource.string() + "\" -o \"" +
                computeSpv.string() + "\" >/dev/null 2>&1";
            ASSERT_TRUE(CommandSucceeded(compileCommand)) << "failed to compile temporary compute shader with glslc";

            const Extrinsic::Backends::Vulkan::VulkanPipelineDiagnosticsSnapshot beforePipelineDiagnostics =
                Extrinsic::Backends::Vulkan::GetVulkanPipelineDiagnosticsSnapshot();

            Extrinsic::RHI::PipelineDesc pipelineDesc{};
            pipelineDesc.ComputeShaderPath = computeSpv.string();
            pipelineDesc.DebugName = "VulkanBootstrapSmoke.ComputePipeline";
            const Extrinsic::RHI::PipelineHandle pipeline = device->CreatePipeline(pipelineDesc);
            EXPECT_TRUE(pipeline.IsValid())
                << "guarded service-ready bootstrap should support direct Vulkan compute pipeline creation "
                   "even though renderer/manager paths remain gated by IsOperational()";

            const Extrinsic::Backends::Vulkan::VulkanPipelineDiagnosticsSnapshot pipelineDiagnostics =
                Extrinsic::Backends::Vulkan::GetVulkanPipelineDiagnosticsSnapshot();
            EXPECT_EQ(pipelineDiagnostics.Status,
                      Extrinsic::Backends::Vulkan::VulkanPipelineCreationStatus::CreatedCompute);
            EXPECT_EQ(pipelineDiagnostics.LastVkResult, 0);
            EXPECT_TRUE(pipelineDiagnostics.DeviceAvailable);
            EXPECT_FALSE(pipelineDiagnostics.DeviceOperational);
            EXPECT_TRUE(pipelineDiagnostics.GlobalPipelineLayoutAvailable);
            EXPECT_TRUE(pipelineDiagnostics.ComputePipeline);
            EXPECT_GT(pipelineDiagnostics.ShaderBytesRead, 0u);
            EXPECT_EQ(pipelineDiagnostics.SuccessfulPipelineCreations,
                      beforePipelineDiagnostics.SuccessfulPipelineCreations + 1u);

            device->DestroyPipeline(pipeline);

            const std::filesystem::path shaderSourceDir = sourceRoot / "assets" / "shaders";
            const std::string depthCompileCommand = "glslc \"" +
                (shaderSourceDir / "depth_prepass.vert").string() + "\" -I \"" +
                shaderSourceDir.string() + "\" -o \"" + depthSpv.string() +
                "\" --target-env=vulkan1.3 >/dev/null 2>&1";
            ASSERT_TRUE(CommandSucceeded(depthCompileCommand))
                << "failed to compile canonical depth-prepass shader with glslc";

            Extrinsic::RHI::PipelineDesc depthPipelineDesc{};
            depthPipelineDesc.VertexShaderPath = depthSpv.string();
            depthPipelineDesc.ColorTargetCount = 0u;
            depthPipelineDesc.DepthTargetFormat = Extrinsic::RHI::Format::D32_FLOAT;
            depthPipelineDesc.PushConstantSize = sizeof(Extrinsic::RHI::GpuScenePushConstants);
            depthPipelineDesc.DebugName = "VulkanBootstrapSmoke.DepthPrepassPipeline";
            const Extrinsic::RHI::PipelineHandle depthPipeline = device->CreatePipeline(depthPipelineDesc);
            EXPECT_TRUE(depthPipeline.IsValid())
                << "guarded service-ready bootstrap should support canonical depth-only graphics pipeline creation";

            const Extrinsic::Backends::Vulkan::VulkanPipelineDiagnosticsSnapshot depthPipelineDiagnostics =
                Extrinsic::Backends::Vulkan::GetVulkanPipelineDiagnosticsSnapshot();
            EXPECT_EQ(depthPipelineDiagnostics.Status,
                      Extrinsic::Backends::Vulkan::VulkanPipelineCreationStatus::CreatedGraphics);
            EXPECT_EQ(depthPipelineDiagnostics.LastVkResult, 0);
            EXPECT_FALSE(depthPipelineDiagnostics.DeviceOperational);
            EXPECT_FALSE(depthPipelineDiagnostics.ComputePipeline);
            EXPECT_EQ(depthPipelineDiagnostics.ColorTargetCount, 0u);
            EXPECT_EQ(depthPipelineDiagnostics.PushConstantSize,
                      sizeof(Extrinsic::RHI::GpuScenePushConstants));
            EXPECT_GT(depthPipelineDiagnostics.ShaderBytesRead, 0u);

            device->DestroyPipeline(depthPipeline);
        }

        device->DestroySampler(sampler);
        device->DestroyTexture(sampledTexture);
        device->DestroyTexture(colorTarget);
        device->DestroyTexture(depthTarget);
        device->DestroyBuffer(indirectArgsBuffer);
        device->DestroyBuffer(sceneTableBuffer);
        device->DestroyBuffer(flushDstBuffer);
        device->DestroyBuffer(flushSrcBuffer);
    }
    else
    {
        Extrinsic::RHI::FrameHandle frame{};
        EXPECT_FALSE(device->BeginFrame(frame))
            << "bootstrap without registered swapchain images must keep frame acquisition fail-closed";
        const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot lifecycleDiagnostics =
            Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
        EXPECT_TRUE(lifecycleDiagnostics.BeginStatus ==
                        Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::SkippedNotOperational ||
                    lifecycleDiagnostics.BeginStatus ==
                        Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::SkippedNoSwapchain ||
                    lifecycleDiagnostics.BeginStatus ==
                        Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::SkippedNoSwapchainImages);
        EXPECT_EQ(lifecycleDiagnostics.BeginFrameAttempts,
                  Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount());
        EXPECT_FALSE(lifecycleDiagnostics.DeviceOperational);

        EXPECT_NE(serviceDiagnostics.Status,
                  Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready);
    }

    device->Shutdown();
    EXPECT_FALSE(device->IsOperational());
}
