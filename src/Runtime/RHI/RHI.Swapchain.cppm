module;

#include <vector>
#include <cstdint>
#include <memory>
#include <string_view>

#include "RHI.Vulkan.hpp"

export module RHI.Swapchain;

import RHI.Device;
import Core.Window; // For Core::Windowing::Window

namespace RHI
{
    // -------------------------------------------------------------------------
    // Present Policy — engine-level frame-pacing intent
    // -------------------------------------------------------------------------
    // Maps to Vulkan present modes with fallback logic.
    // The swapchain selects the best available VkPresentModeKHR for the
    // requested policy; if the preferred mode is unavailable, it falls back
    // to VK_PRESENT_MODE_FIFO_KHR (always supported per Vulkan spec).
    export enum class PresentPolicy : uint8_t
    {
        VSync           = 0,  // VK_PRESENT_MODE_FIFO_KHR — tear-free, guaranteed available
        LowLatency      = 1,  // VK_PRESENT_MODE_MAILBOX_KHR — lowest latency without tearing
        Uncapped        = 2,  // VK_PRESENT_MODE_IMMEDIATE_KHR — no sync, may tear
        EditorThrottled = 3,  // VK_PRESENT_MODE_FIFO_RELAXED_KHR → FIFO fallback
    };

    export [[nodiscard]] constexpr std::string_view ToString(PresentPolicy policy)
    {
        switch (policy)
        {
            case PresentPolicy::VSync:           return "VSync";
            case PresentPolicy::LowLatency:      return "LowLatency";
            case PresentPolicy::Uncapped:         return "Uncapped";
            case PresentPolicy::EditorThrottled: return "EditorThrottled";
        }
        return "Unknown";
    }

    // Select the best available VkPresentModeKHR for a given policy.
    // Pure function — does not touch Vulkan state.
    export [[nodiscard]] VkPresentModeKHR SelectPresentMode(
        PresentPolicy policy,
        const std::vector<VkPresentModeKHR>& availableModes);

    export class VulkanSwapchain
    {
    public:
        VulkanSwapchain(std::shared_ptr<VulkanDevice> device, Core::Windowing::Window& window,
                        PresentPolicy policy = PresentPolicy::VSync);
        ~VulkanSwapchain();

        void Recreate(); // Call this when window resizes

        // Change the present policy. Takes effect on the next Recreate() or at next frame
        // if the swapchain needs to be rebuilt.
        void SetPresentPolicy(PresentPolicy policy);

        [[nodiscard]] PresentPolicy GetPresentPolicy() const { return m_PresentPolicy; }
        [[nodiscard]] VkPresentModeKHR GetActivePresentMode() const { return m_ActivePresentMode; }

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

        PresentPolicy m_PresentPolicy = PresentPolicy::VSync;
        VkPresentModeKHR m_ActivePresentMode = VK_PRESENT_MODE_FIFO_KHR;

        void CreateSwapchain();
        void CreateImageViews();
        void Cleanup();

        // Helpers
        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    };
}
