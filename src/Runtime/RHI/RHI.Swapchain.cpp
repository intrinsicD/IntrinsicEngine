module;
#include "RHI.Vulkan.hpp"
#include "RHI.DestructionUtils.hpp"
#include <cassert>
#include <vector>
#include <algorithm>
#include <limits>
#include <memory>
#include <string_view>

#include "GLFW/glfw3.h"

module RHI.Swapchain;
import RHI.Device;
import Core.Logging;
import Core.Window;

namespace RHI {

    VkPresentModeKHR SelectPresentMode(PresentPolicy policy,
                                        const std::vector<VkPresentModeKHR>& availableModes)
    {
        auto has = [&](VkPresentModeKHR mode) {
            return std::find(availableModes.begin(), availableModes.end(), mode)
                   != availableModes.end();
        };

        switch (policy)
        {
            case PresentPolicy::VSync:
                return VK_PRESENT_MODE_FIFO_KHR;  // Always available per Vulkan spec.

            case PresentPolicy::LowLatency:
                if (has(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
                return VK_PRESENT_MODE_FIFO_KHR;

            case PresentPolicy::Uncapped:
                if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
                if (has(VK_PRESENT_MODE_MAILBOX_KHR))   return VK_PRESENT_MODE_MAILBOX_KHR;
                return VK_PRESENT_MODE_FIFO_KHR;

            case PresentPolicy::EditorThrottled:
                if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                return VK_PRESENT_MODE_FIFO_KHR;
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VulkanSwapchain::VulkanSwapchain(std::shared_ptr<VulkanDevice> device, Core::Windowing::Window& window,
                                     PresentPolicy policy)
        : m_Device(device), m_Window(window), m_PresentPolicy(policy)
    {
        CreateSwapchain();
        CreateImageViews();
    }

    VulkanSwapchain::~VulkanSwapchain() {
        Cleanup();
    }

    void VulkanSwapchain::Recreate() {
        int width = m_Window.GetFramebufferWidth();
        int height = m_Window.GetFramebufferHeight();
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            width = m_Window.GetFramebufferWidth();
            height = m_Window.GetFramebufferHeight();
        }

        // Use timeline-semaphore-based drain instead of vkDeviceWaitIdle.
        // The caller (SimpleRenderer::OnResize) may have already drained the graphics queue,
        // in which case this completes immediately. When called from other paths (e.g.
        // present-policy changes), this ensures graphics-queue work is complete before
        // tearing down swapchain resources.
        if (!m_Device->WaitForGraphicsIdle())
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        // 1. Cleanup OLD views via SafeDestroy for consistency with other RHI resources.
        DestructionUtils::SafeDestroyBatch(*m_Device, m_ImageViews, vkDestroyImageView);

        // 2. Create NEW swapchain using the OLD one as reference
        VkSwapchainKHR oldSwapchain = m_Swapchain;
        CreateSwapchain(); // This overwrites m_Swapchain with the new handle

        // 3. Destroy the OLD swapchain via SafeDestroy
        DestructionUtils::SafeDestroyVk(*m_Device, oldSwapchain, vkDestroySwapchainKHR);

        // 4. Create Views for the new swapchain
        CreateImageViews();
    }

    void VulkanSwapchain::Cleanup() {
        DestructionUtils::SafeDestroyBatch(*m_Device, m_ImageViews, vkDestroyImageView);
        m_Images.clear();
        DestructionUtils::SafeDestroyVk(*m_Device, m_Swapchain, vkDestroySwapchainKHR);
    }

    void VulkanSwapchain::SetPresentPolicy(PresentPolicy policy)
    {
        m_PresentPolicy = policy;
    }

    void VulkanSwapchain::CreateSwapchain() {
        auto support = m_Device->QuerySwapchainSupport();

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(support.Formats);
        VkPresentModeKHR presentMode = SelectPresentMode(m_PresentPolicy, support.PresentModes);
        m_ActivePresentMode = presentMode;
        VkExtent2D extent = ChooseSwapExtent(support.Capabilities);

        uint32_t imageCount = support.Capabilities.minImageCount + 1;
        if (support.Capabilities.maxImageCount > 0 && imageCount > support.Capabilities.maxImageCount) {
            imageCount = support.Capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_Device->GetSurface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        QueueFamilyIndices indices = m_Device->GetQueueIndices();
        assert(indices.GraphicsFamily.has_value() && "GraphicsFamily must be set for swapchain creation");
        assert(indices.PresentFamily.has_value() && "PresentFamily must be set for swapchain creation");
        uint32_t queueFamilyIndices[] = {indices.GraphicsFamily.value(), indices.PresentFamily.value()};

        if (indices.GraphicsFamily != indices.PresentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = support.Capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        // IMPORTANT: Pass the current (soon to be old) swapchain
        createInfo.oldSwapchain = m_Swapchain;

        // Use a temporary handle so we don't overwrite m_Swapchain if creation fails
        VkSwapchainKHR newSwapchain;
        if (vkCreateSwapchainKHR(m_Device->GetLogicalDevice(), &createInfo, nullptr, &newSwapchain) != VK_SUCCESS) {
            Core::Log::Error("Failed to create swapchain!");
            return;
        }
        m_Swapchain = newSwapchain;

        vkGetSwapchainImagesKHR(m_Device->GetLogicalDevice(), m_Swapchain, &imageCount, nullptr);
        m_Images.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device->GetLogicalDevice(), m_Swapchain, &imageCount, m_Images.data());

        m_ImageFormat = surfaceFormat.format;
        m_Extent = extent;

        Core::Log::Info("Swapchain Created/Resized: {}x{}, PresentMode={} (policy={})",
                        extent.width, extent.height,
                        static_cast<int>(m_ActivePresentMode),
                        ToString(m_PresentPolicy));
    }

    void VulkanSwapchain::CreateImageViews() {
        m_ImageViews.resize(m_Images.size());
        for (size_t i = 0; i < m_Images.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_Images[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_ImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_Device->GetLogicalDevice(), &createInfo, nullptr, &m_ImageViews[i]) != VK_SUCCESS) {
                Core::Log::Error("Failed to create image views!");
            }
        }
    }

    VkSurfaceFormatKHR VulkanSwapchain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& availableFormat : formats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return formats[0];
    }

    VkExtent2D VulkanSwapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            int width = m_Window.GetFramebufferWidth();
            int height = m_Window.GetFramebufferHeight();
            VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            return actualExtent;
        }
    }
}