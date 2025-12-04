module;
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <set>
#include <mutex>

// --- 2. VMA Configuration ---
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_USE_NULLABILITY_ANNOTATIONS 0

#define VMA_SYSTEM_MALLOC(size) std::malloc(size)
#define VMA_SYSTEM_FREE(ptr) std::free(ptr)
#define VMA_SYSTEM_ALIGNED_MALLOC(size, align) std::aligned_alloc(align, size)
#define VMA_SYSTEM_ALIGNED_FREE(ptr) std::free(ptr)

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
        CreateLogicalDevice(context);
        CreateCommandPool();
    }

    VulkanDevice::~VulkanDevice()
    {
        vkDeviceWaitIdle(m_Device);

        {
            std::lock_guard lock(m_ThreadPoolsMutex);
            for (auto pool : m_ThreadCommandPools)
            {
                vkDestroyCommandPool(m_Device, pool, nullptr);
            }
            m_ThreadCommandPools.clear();
        }

        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        vmaDestroyAllocator(m_Allocator);
        vkDestroyDevice(m_Device, nullptr);
    }

    void VulkanDevice::RegisterThreadLocalPool(VkCommandPool pool) {
        std::lock_guard lock(m_ThreadPoolsMutex);
        m_ThreadCommandPools.push_back(pool);
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

        // Naive selection: First one that works.
        // TODO: Score them (Discrete > Integrated)
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
            Core::Log::Error("Failed to find a suitable GPU!");
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
        m_Indices = FindQueueFamilies(m_PhysicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            m_Indices.GraphicsFamily.value(),
            m_Indices.PresentFamily.value()
        };

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

        // --- Features ---
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE; // Usually desired

        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicStateFeatures{};
        dynamicStateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        dynamicStateFeatures.extendedDynamicState = VK_TRUE;

        // Enable Dynamic Rendering (Vulkan 1.3 Core)
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features13.pNext = &dynamicStateFeatures;

        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.features = deviceFeatures;
        deviceFeatures2.pNext = &features13;

        // --- Creation ---
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &deviceFeatures2; // Chain modern features

        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();

        createInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
        createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();

        // Deprecated in newer Vulkan but good for compatibility
        createInfo.enabledLayerCount = 0;

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create logical device!");
            m_IsValid = false;
            return;
        }

        // LOAD DEVICE POINTERS (Important for Volk)
        volkLoadDevice(m_Device);

        // Initialize VMA
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = context.GetInstance();
        // You might need to pass Instance to CreateLogicalDevice or store it
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;


        if (vmaCreateAllocator(&allocatorInfo, &m_Allocator) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create VMA allocator!");
            m_IsValid = false;
            return;
        }

        vkGetDeviceQueue(m_Device, m_Indices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_Indices.PresentFamily.value(), 0, &m_PresentQueue);
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

    bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices = FindQueueFamilies(device);
        bool extensionsSupported = CheckDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported)
        {
            // We technically need the surface to check format support,
            // so we call our helper locally
            // Note: In a pure implementation we might separate this, but this is fine for now.
            // We cast to invoke the helper below which queries m_Surface
            // But wait, 'QuerySwapchainSupport' uses m_PhysicalDevice which isn't set yet.
            // We must implement a local version or refactor.
            // For simplicity, let's check formats manually here:

            uint32_t formatCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);

            uint32_t presentModeCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);

            swapChainAdequate = (formatCount != 0) && (presentModeCount != 0);
        }

        return indices.IsComplete() && extensionsSupported && swapChainAdequate;
    }

    bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());

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

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if (presentSupport)
            {
                indices.PresentFamily = i;
            }

            if (indices.IsComplete()) break;
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
