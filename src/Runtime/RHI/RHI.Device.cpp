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
        CreateLogicalDevice(context);
        CreateCommandPool();
    }

    VulkanDevice::~VulkanDevice()
    {
        // 1. Wait for GPU to stop
        if (m_Device) vkDeviceWaitIdle(m_Device);

        // 2. Flush ALL Deletion Queues (Break the resource cycle)
        // We iterate all frame buckets because we are shutting down.
        {
            std::lock_guard lock(m_DeletionMutex);
            for (auto& queue : m_DeletionQueue)
            {
                for (auto& fn : queue)
                {
                    fn();
                }
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

        // 5. Destroy Device (Now safe because children are gone)
        if (m_Device) vkDestroyDevice(m_Device, nullptr);
    }

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
        // Update current frame index so SafeDestroy knows where to push
        m_CurrentFrameIndex = frameIndex;

        // Execute all deleters pending for THIS frame slot.
        // Because we cycled back to this index and passed the Fence,
        // the GPU is done with the resources that were used 'MAX_FRAMES' ago.
        std::lock_guard lock(m_DeletionMutex);
        auto& queue = m_DeletionQueue[frameIndex];
        for (auto& fn : queue)
        {
            fn();
        }
        queue.clear();
    }

    void VulkanDevice::SafeDestroy(std::function<void()>&& deleteFn)
    {
        std::lock_guard lock(m_DeletionMutex);
        // Push to the CURRENT frame bucket.
        // It will be cleared when we hit this frame index again in the next cycle.
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

        // --- FEATURES CHAIN SETUP ---
        // We use one set of structs for both Querying and Enabling.
        // This ensures that if the driver says "TRUE", we ask for "TRUE".

        // 1. Dynamic State (Viewport, Scissor, Topology)
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

        // 2. Vulkan 1.3 (Dynamic Rendering, Sync2)
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.pNext = &dynamicState;

        // 3. Vulkan 1.2 (Bindless / Descriptor Indexing)
        // Note: UpdateAfterBind is part of Vulkan 1.2 core, so it lives here.
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;

        // 4. Features2 (Base features like Anisotropy)
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;

        // --- QUERY SUPPORT ---
        // This fills the structs with what the GPU *actually* supports.
        vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);

        // --- VERIFY REQUIREMENTS ---
        // If the GPU doesn't support something we need, we should error out or disable it.
        // For this engine, we require these features:

        if (!features13.dynamicRendering) Core::Log::Error("Vulkan 1.3 Dynamic Rendering not supported!");
        if (!features13.synchronization2) Core::Log::Error("Vulkan 1.3 Sync2 not supported!");

        // The Critical Fix for your Validation Error:
        if (!features12.descriptorBindingSampledImageUpdateAfterBind)
            Core::Log::Error("Bindless: UpdateAfterBind not supported!");
        if (!features12.descriptorBindingPartiallyBound)
            Core::Log::Error("Bindless: PartiallyBound not supported!");
        if (!features12.runtimeDescriptorArray)
            Core::Log::Error("Bindless: RuntimeArray not supported!");

        // --- CREATE DEVICE ---
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        // Pass the whole chain (which now contains the TRUE values from the query)
        createInfo.pNext = &features2;

        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();

        createInfo.enabledExtensionCount = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
        createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create logical device!");
            m_IsValid = false;
            return;
        }

        volkLoadDevice(m_Device);

        // ... (VMA Initialization remains the same) ...
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
