module;
#include <vector>
#include <optional>
#include <mutex>
#include "RHI/RHI.Vulkan.hpp"

export module Runtime.RHI.Device;

import Runtime.RHI.Context;
import Core.Logging;

namespace Runtime::RHI
{
    export struct QueueFamilyIndices
    {
        std::optional<uint32_t> GraphicsFamily;
        std::optional<uint32_t> PresentFamily;

        bool IsComplete() const
        {
            return GraphicsFamily.has_value() && PresentFamily.has_value();
        }
    };

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR Capabilities;
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
    };

    export class VulkanDevice
    {
    public:
        VulkanDevice(VulkanContext& context, VkSurfaceKHR surface);
        ~VulkanDevice();

        // No copy
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        [[nodiscard]] VkDevice GetLogicalDevice() const { return m_Device; }
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        [[nodiscard]] VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        [[nodiscard]] VkQueue GetPresentQueue() const { return m_PresentQueue; }
        [[nodiscard]] QueueFamilyIndices GetQueueIndices() const { return m_Indices; }
        [[nodiscard]] VkSurfaceKHR GetSurface() const { return m_Surface; }
        [[nodiscard]] VkCommandPool GetCommandPool() const { return m_CommandPool; }
        [[nodiscard]] VmaAllocator GetAllocator() const { return m_Allocator; }

        // Helper to query swapchain support (used later by Swapchain module)
        [[nodiscard]] SwapchainSupportDetails QuerySwapchainSupport() const;

        [[nodiscard]] std::mutex& GetQueueMutex() { return m_QueueMutex; }

        void RegisterThreadLocalPool(VkCommandPool pool);

    private:
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;

        VkSurfaceKHR m_Surface = VK_NULL_HANDLE; // Weak Ref (Owned by Context/Window logic usually, but we hold handle)

        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        QueueFamilyIndices m_Indices;

        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;

        mutable std::mutex m_QueueMutex;
        std::mutex m_ThreadPoolsMutex;
        std::vector<VkCommandPool> m_ThreadCommandPools;

        void PickPhysicalDevice(VkInstance instance);
        void CreateLogicalDevice(VulkanContext & context);
        void CreateCommandPool();

        bool IsDeviceSuitable(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    };
}
