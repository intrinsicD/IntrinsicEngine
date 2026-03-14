module;
#include "RHI.Vulkan.hpp"
#include <vector>
#include <algorithm>
#include <limits>
#include <memory>

#include "GLFW/glfw3.h"

module RHI:Swapchain.Impl;
import :Swapchain;
import :Device;
import Core.Logging;
import Core.Window;

namespace RHI {

    VulkanSwapchain::VulkanSwapchain(std::shared_ptr<VulkanDevice> device, Core::Windowing::Window& window)
        : m_Device(device), m_Window(window)
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

        vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        // 1. Cleanup OLD views via SafeDestroy for consistency with other RHI resources.
        // vkDeviceWaitIdle above guarantees GPU is idle, but SafeDestroy keeps the pattern uniform.
        VkDevice logicalDevice = m_Device->GetLogicalDevice();
        if (!m_ImageViews.empty())
        {
            std::vector<VkImageView> oldViews = std::move(m_ImageViews);
            m_Device->SafeDestroy([logicalDevice, oldViews = std::move(oldViews)]() {
                for (auto view : oldViews)
                    vkDestroyImageView(logicalDevice, view, nullptr);
            });
        }
        m_ImageViews.clear();

        // 2. Create NEW swapchain using the OLD one as reference
        VkSwapchainKHR oldSwapchain = m_Swapchain;
        CreateSwapchain(); // This overwrites m_Swapchain with the new handle

        // 3. Destroy the OLD swapchain via SafeDestroy
        if (oldSwapchain != VK_NULL_HANDLE) {
            m_Device->SafeDestroy([logicalDevice, oldSwapchain]() {
                vkDestroySwapchainKHR(logicalDevice, oldSwapchain, nullptr);
            });
        }

        // 4. Create Views for the new swapchain
        CreateImageViews();
    }

    void VulkanSwapchain::Cleanup() {
        // Defer image view destruction through SafeDestroy to ensure any
        // in-flight frames referencing these views have completed.
        VkDevice logicalDevice = m_Device->GetLogicalDevice();
        if (!m_ImageViews.empty())
        {
            std::vector<VkImageView> views = std::move(m_ImageViews);
            m_Device->SafeDestroy([logicalDevice, views = std::move(views)]() {
                for (auto view : views)
                    vkDestroyImageView(logicalDevice, view, nullptr);
            });
        }
        m_ImageViews.clear();
        m_Images.clear();

        if (m_Swapchain != VK_NULL_HANDLE) {
            VkSwapchainKHR swapchain = m_Swapchain;
            m_Device->SafeDestroy([logicalDevice, swapchain]() {
                vkDestroySwapchainKHR(logicalDevice, swapchain, nullptr);
            });
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanSwapchain::CreateSwapchain() {
        auto support = m_Device->QuerySwapchainSupport();

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(support.Formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(support.PresentModes);
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

        Core::Log::Info("Swapchain Created/Resized: {}x{}", extent.width, extent.height);
    }

    // ... (CreateImageViews, ChooseSwap* functions remain the same as previous step) ...
    // Paste them here or keep them if you are editing the file.

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

    VkPresentModeKHR VulkanSwapchain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
        for (const auto& availablePresentMode : presentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
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