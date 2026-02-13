module;

#include <vector>
#include <cstdint>
#include <memory>

#include "RHI.Vulkan.hpp"

export module RHI:Swapchain;

import :Device;
import Core.Window; // For Core::Windowing::Window

namespace RHI
{
    export class VulkanSwapchain
    {
    public:
        VulkanSwapchain(std::shared_ptr<VulkanDevice> device, Core::Windowing::Window& window);
        ~VulkanSwapchain();

        void Recreate(); // Call this when window resizes

        [[nodiscard]] VkSwapchainKHR GetHandle() const { return m_Swapchain; }
        [[nodiscard]] VkFormat GetImageFormat() const { return m_ImageFormat; }
        [[nodiscard]] VkExtent2D GetExtent() const { return m_Extent; }

        // These are the targets we will render into
        [[nodiscard]] const std::vector<VkImageView>& GetImageViews() const { return m_ImageViews; }
        [[nodiscard]] const std::vector<VkImage>& GetImages() const { return m_Images; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        Core::Windowing::Window& m_Window;

        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_Images;
        std::vector<VkImageView> m_ImageViews;

        VkFormat m_ImageFormat;
        VkExtent2D m_Extent;

        void CreateSwapchain();
        void CreateImageViews();
        void Cleanup();

        // Helpers
        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    };
}
