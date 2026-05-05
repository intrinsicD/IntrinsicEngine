#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;

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

    device->Initialize(*window, renderConfig);
    EXPECT_FALSE(device->IsOperational())
        << "phase-1 bootstrap must not mark Vulkan operational before swapchain/resource reconciliation";

    const Extrinsic::Backends::Vulkan::VulkanBootstrapDiagnosticsSnapshot diagnostics =
        Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();

    EXPECT_TRUE(diagnostics.NativeWindowAvailable);
    EXPECT_TRUE(diagnostics.VolkInitialized)
        << "with a GLFW native window, bootstrap should reach the Vulkan loader before any clean failure";

    const std::uint32_t expectedFramesInFlight = device->GetFramesInFlight();

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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_FALSE(diagnostics.GraphicsQueueAcquired);
        EXPECT_FALSE(diagnostics.PresentQueueAcquired);
        EXPECT_FALSE(diagnostics.TransferQueueAcquired);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedLogicalDeviceCreation:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        if (diagnostics.LogicalDeviceCreated)
        {
            EXPECT_FALSE(diagnostics.GraphicsQueueAcquired &&
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.GraphicsQueueAcquired);
        EXPECT_TRUE(diagnostics.PresentQueueAcquired);
        EXPECT_TRUE(diagnostics.TransferQueueAcquired);
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

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(device->BeginFrame(frame))
        << "guarded bootstrap must keep frame acquisition fail-closed until Vulkan is marked operational";

    const Extrinsic::Backends::Vulkan::VulkanFrameLifecycleDiagnosticsSnapshot lifecycleDiagnostics =
        Extrinsic::Backends::Vulkan::GetVulkanFrameLifecycleDiagnosticsSnapshot();
    EXPECT_EQ(lifecycleDiagnostics.BeginStatus,
              Extrinsic::Backends::Vulkan::VulkanFrameBeginStatus::SkippedNotOperational);
    EXPECT_EQ(lifecycleDiagnostics.BeginFrameAttempts,
              Extrinsic::Backends::Vulkan::GetFallbackBeginFrameAttemptCount());
    EXPECT_FALSE(lifecycleDiagnostics.DeviceOperational);
    if (diagnostics.Status == Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::RegisteredSwapchainImages)
    {
        EXPECT_TRUE(lifecycleDiagnostics.SwapchainAvailable);
        EXPECT_TRUE(lifecycleDiagnostics.SwapchainImagesAvailable);
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
        EXPECT_TRUE(serviceDiagnostics.PublicServicesRemainFailClosed);

        const std::uint64_t beforeFallbackBindless =
            Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount();
        EXPECT_EQ(device->GetBindlessHeap().AllocateTextureSlot({}, {}),
                  Extrinsic::RHI::kInvalidBindlessIndex);
        EXPECT_EQ(Extrinsic::Backends::Vulkan::GetFallbackBindlessAllocationAttemptCount(),
                  beforeFallbackBindless + 1u)
            << "live bindless service exists internally, but public access must remain fail-closed until operational";
    }
    else
    {
        EXPECT_NE(serviceDiagnostics.Status,
                  Extrinsic::Backends::Vulkan::VulkanServiceBootstrapStatus::Ready);
    }

    device->Shutdown();
    EXPECT_FALSE(device->IsOperational());
}


