module;
#include <vector>
#include <string>
#include <set>
#include <mutex>
#include <functional>
#include <string_view>

#include "RHI.Vulkan.hpp"


module Runtime.RHI.Device;
import Core.Logging;

namespace Runtime::RHI
{
    const std::vector<const char*> DEVICE_EXTENSIONS = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VulkanDevice::VulkanDevice(VulkanContext& context, VkSurfaceKHR surface)
        : m_Surface(surface)
    {
        PickPhysicalDevice(context.GetInstance());

        // FIX: Abort initialization if no physical device was selected
        if (m_PhysicalDevice == VK_NULL_HANDLE) {
            m_IsValid = false;
            return;
        }

        CreateLogicalDevice(context);

        // FIX: Check if logical device creation succeeded
        if (m_Device == VK_NULL_HANDLE) {
            m_IsValid = false;
            return;
        }

        CreateCommandPool();
    }

    VulkanDevice::~VulkanDevice()
    {
        // ... (Destructor remains the same) ...
        // 1. Wait for GPU to stop
        if (m_Device) vkDeviceWaitIdle(m_Device);

        // 2. Flush ALL Deletion Queues
        {
            std::lock_guard lock(m_DeletionMutex);
            for (auto& queue : m_DeletionQueue)
            {
                for (auto& fn : queue) fn();
                queue.clear();
            }
        }

        // 3. Destroy Thread Pools
        {
            std::lock_guard lock(m_ThreadPoolsMutex);
            for (auto pool : m_ThreadCommandPools)
            {
                vkDestroyCommandPool(m_Device, pool, nullptr);
            }
            m_ThreadCommandPools.clear();
        }

        // 4. Destroy Main Resources
        if (m_CommandPool) vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        if (m_Allocator) vmaDestroyAllocator(m_Allocator);

        // 5. Destroy Device
        if (m_Device) vkDestroyDevice(m_Device, nullptr);
    }

    // ... (SubmitToGraphicsQueue, Present, RegisterThreadLocalPool, FlushDeletionQueue, SafeDestroy remain the same) ...
    VkResult VulkanDevice::SubmitToGraphicsQueue(const VkSubmitInfo& submitInfo, VkFence fence)
    {
        std::scoped_lock lock(m_QueueMutex);
        return vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, fence);
    }

    VkResult VulkanDevice::Present(const VkPresentInfoKHR& presentInfo)
    {
        std::scoped_lock lock(m_QueueMutex);
        return vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    }

    void VulkanDevice::RegisterThreadLocalPool(VkCommandPool pool)
    {
        std::lock_guard lock(m_ThreadPoolsMutex);
        m_ThreadCommandPools.push_back(pool);
    }

    void VulkanDevice::FlushDeletionQueue(uint32_t frameIndex)
    {
        m_CurrentFrameIndex = frameIndex;
        std::lock_guard lock(m_DeletionMutex);
        auto& queue = m_DeletionQueue[frameIndex];
        for (auto& fn : queue) fn();
        queue.clear();
    }

    void VulkanDevice::SafeDestroy(std::function<void()>&& deleteFn)
    {
        std::lock_guard lock(m_DeletionMutex);
        m_DeletionQueue[m_CurrentFrameIndex].push_back(std::move(deleteFn));
    }

    void VulkanDevice::PickPhysicalDevice(VkInstance instance)
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0)
        {
            Core::Log::Error("Failed to find GPUs with Vulkan support!");
            m_IsValid = false;
            return;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        for (const auto& device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_PhysicalDevice = device;
                break;
            }
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            Core::Log::Error("Failed to find a suitable GPU! Checked {} devices.", deviceCount);
            m_IsValid = false;
        }
        else
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
            Core::Log::Info("Selected GPU: {}", props.deviceName);
        }
    }

    void VulkanDevice::CreateLogicalDevice(VulkanContext& context)
    {
        // ... (Keep existing implementation logic) ...
        m_Indices = FindQueueFamilies(m_PhysicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies;
        if (m_Indices.GraphicsFamily.has_value())
            uniqueQueueFamilies.insert(m_Indices.GraphicsFamily.value());
        if (m_Indices.PresentFamily.has_value())
            uniqueQueueFamilies.insert(m_Indices.PresentFamily.value());

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.pNext = &dynamicState;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;

        vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);

        // Optional: Warn if features are missing (keep existing warnings)
        if (!features13.dynamicRendering) Core::Log::Error("Vulkan 1.3 Dynamic Rendering not supported!");
        if (!features13.synchronization2) Core::Log::Error("Vulkan 1.3 Sync2 not supported!");

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &features2;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();

        std::vector<const char*> enabledExtensions = DEVICE_EXTENSIONS;
        if (m_Surface == VK_NULL_HANDLE)
        {
            std::erase_if(enabledExtensions, [](const char* ext) { return std::string_view(ext) == VK_KHR_SWAPCHAIN_EXTENSION_NAME; });
        }

        createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create logical device!");
            m_IsValid = false;
            return;
        }

        volkLoadDevice(m_Device);

        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = context.GetInstance();
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;

        if (vmaCreateAllocator(&allocatorInfo, &m_Allocator) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create VMA allocator!");
            m_IsValid = false;
            return;
        }

        vkGetDeviceQueue(m_Device, m_Indices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
        if (m_Indices.PresentFamily.has_value())
        {
            vkGetDeviceQueue(m_Device, m_Indices.PresentFamily.value(), 0, &m_PresentQueue);
        }
    }

    void VulkanDevice::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_Indices.GraphicsFamily.value();

        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create command pool!");
            m_IsValid = false;
        }
    }

    // MODIFIED: Diagnostic logging added
    bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        QueueFamilyIndices indices = FindQueueFamilies(device);
        bool extensionsSupported = CheckDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported)
        {
            if (m_Surface != VK_NULL_HANDLE)
            {
                uint32_t formatCount;
                vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);

                uint32_t presentModeCount;
                vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);

                swapChainAdequate = (formatCount != 0) && (presentModeCount != 0);
            }
            else
            {
                swapChainAdequate = true;
            }
        }

        // Diagnostics
        if (!indices.GraphicsFamily.has_value())
            Core::Log::Warn("GPU '{}' rejected: No Graphics Queue.", props.deviceName);

        if (m_Surface != VK_NULL_HANDLE && !indices.PresentFamily.has_value())
            Core::Log::Warn("GPU '{}' rejected: No Presentation Queue support.", props.deviceName);

        if (!extensionsSupported)
            Core::Log::Warn("GPU '{}' rejected: Missing required extensions.", props.deviceName);

        if (!swapChainAdequate)
            Core::Log::Warn("GPU '{}' rejected: Swapchain incompatible (formats/modes).", props.deviceName);

        bool indicesComplete = indices.GraphicsFamily.has_value() &&
                               (m_Surface == VK_NULL_HANDLE || indices.PresentFamily.has_value());

        return indicesComplete && extensionsSupported && swapChainAdequate;
    }

    // ... (Rest of file: CheckDeviceExtensionSupport, FindQueueFamilies, QuerySwapchainSupport remain the same) ...
    bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());

        if (m_Surface == VK_NULL_HANDLE)
        {
            requiredExtensions.erase(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        for (const auto& extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices;
        uint32_t queueFamilyCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.GraphicsFamily = i;
            }

            if (m_Surface != VK_NULL_HANDLE)
            {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
                if (presentSupport)
                {
                    indices.PresentFamily = i;
                }
            }

            if (indices.GraphicsFamily.has_value() && (m_Surface == VK_NULL_HANDLE || indices.PresentFamily.has_value())) break;
            i++;
        }
        return indices;
    }

    SwapchainSupportDetails VulkanDevice::QuerySwapchainSupport() const
    {
        SwapchainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &details.Capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            details.Formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, details.Formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            details.PresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &presentModeCount,
                                                      details.PresentModes.data());
        }
        return details;
    }
}