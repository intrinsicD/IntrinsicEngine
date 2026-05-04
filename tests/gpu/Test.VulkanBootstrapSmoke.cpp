#include <gtest/gtest.h>

#include <memory>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;

TEST(VulkanBootstrapSmoke, InitializeProbesPhysicalDeviceOrFailsCleanly)
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
        << "phase-1 bootstrap must not mark Vulkan operational before logical device/swapchain/resource reconciliation";

    const Extrinsic::Backends::Vulkan::VulkanBootstrapDiagnosticsSnapshot diagnostics =
        Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot();

    EXPECT_TRUE(diagnostics.NativeWindowAvailable);
    EXPECT_TRUE(diagnostics.VolkInitialized)
        << "with a GLFW native window, bootstrap should reach the Vulkan loader before any clean failure";

    switch (diagnostics.Status)
    {
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::ProbedPhysicalDevice:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_TRUE(diagnostics.PhysicalDeviceSelected);
        EXPECT_TRUE(diagnostics.GraphicsQueueFound);
        EXPECT_TRUE(diagnostics.PresentQueueFound);
        EXPECT_TRUE(diagnostics.TransferQueueFound);
        EXPECT_TRUE(diagnostics.SwapchainExtensionSupported);
        EXPECT_TRUE(diagnostics.SwapchainSurfaceSupported);
        EXPECT_GT(diagnostics.PhysicalDeviceCount, 0u);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedNoSuitablePhysicalDevice:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_TRUE(diagnostics.SurfaceCreated);
        EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedSurfaceCreation:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedPhysicalDeviceEnumeration:
        EXPECT_TRUE(diagnostics.InstanceCreated);
        EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedInstanceCreation:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedRequiredInstanceExtensions:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::FailedVolkInitialize:
        EXPECT_FALSE(diagnostics.PhysicalDeviceSelected);
        break;
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::NotStarted:
    case Extrinsic::Backends::Vulkan::VulkanBootstrapStatus::SkippedNoNativeWindow:
        FAIL() << "GLFW smoke supplied a native window, so bootstrap should not remain unstarted or skip for missing native window";
    }

    device->Shutdown();
    EXPECT_FALSE(device->IsOperational());
}


