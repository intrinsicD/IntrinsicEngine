module;
#include <vector>
#include <string>
#include <set>
#include <mutex>
#include <functional>
#include <string_view>
#include <memory>
#include <algorithm>

#include "RHI.Vulkan.hpp"


module RHI:Device.Impl;
import :Device;
import :TransientAllocator;
import Core;

namespace RHI
{
    const std::vector<const char*> DEVICE_EXTENSIONS = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME
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

        // Create graphics timeline semaphore for accurate deferred destruction.
        {
            VkSemaphoreTypeCreateInfo timelineInfo{};
            timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            timelineInfo.initialValue = 0;

            VkSemaphoreCreateInfo semInfo{};
            semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semInfo.pNext = &timelineInfo;

            if (vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_GraphicsTimelineSemaphore) != VK_SUCCESS)
            {
                Core::Log::Error("Failed to create graphics timeline semaphore");
                m_IsValid = false;
                return;
            }
        }

        // Create transient allocator AFTER device creation (it owns VkDeviceMemory pages).
        m_TransientAllocator = new TransientAllocator(*this);

        CreateCommandPool();
    }

    void VulkanDevice::FlushAllDeletionQueues()
    {
        std::lock_guard lock(m_DeletionMutex);
        for (auto& queue : m_DeletionQueue)
        {
            for (auto& fn : queue) fn();
            queue.clear();
        }

        // Drain timeline-based queue too (caller must ensure GPU idle).
        for (auto& item : m_TimelineDeletionQueue)
        {
            if (item.Fn) item.Fn();
        }
        m_TimelineDeletionQueue.clear();
    }

    void VulkanDevice::FlushTimelineDeletionQueueNow()
    {
        std::lock_guard lock(m_DeletionMutex);
        for (auto& item : m_TimelineDeletionQueue)
        {
            if (item.Fn) item.Fn();
        }
        m_TimelineDeletionQueue.clear();
    }

    VulkanDevice::~VulkanDevice()
    {
        // 1) Stop the GPU first.
        if (m_Device) vkDeviceWaitIdle(m_Device);

        // 2) Execute all deferred deletions while the device + VMA allocator are still valid.
        FlushAllDeletionQueues();

        // Also flush timeline-based deletions.
        {
            std::lock_guard lock(m_DeletionMutex);
            for (auto& item : m_TimelineDeletionQueue)
            {
                if (item.Fn) item.Fn();
            }
            m_TimelineDeletionQueue.clear();
        }

        // Destroy timeline semaphore while device is still alive.
        if (m_GraphicsTimelineSemaphore)
        {
            vkDestroySemaphore(m_Device, m_GraphicsTimelineSemaphore, nullptr);
            m_GraphicsTimelineSemaphore = VK_NULL_HANDLE;
        }

        // 3) Destroy transient allocator pages (raw VkDeviceMemory pages).
        delete static_cast<TransientAllocator*>(m_TransientAllocator);
        m_TransientAllocator = nullptr;

        // 4) One more flush in case any destructors enqueued work during step (3).
        FlushAllDeletionQueues();

        // ALSO: if any destructor enqueued SafeDestroy() (timeline-based) during step (3), flush it too.
        {
            std::lock_guard lock(m_DeletionMutex);
            for (auto& item : m_TimelineDeletionQueue)
            {
                if (item.Fn) item.Fn();
            }
            m_TimelineDeletionQueue.clear();
        }

        // 5) Destroy Thread Pools
        {
            std::lock_guard lock(m_ThreadPoolsMutex);
            for (auto pool : m_ThreadCommandPools)
            {
                vkDestroyCommandPool(m_Device, pool, nullptr);
            }
            m_ThreadCommandPools.clear();
        }

        // 6) Destroy Main Resources
        if (m_CommandPool) vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        if (m_Allocator) vmaDestroyAllocator(m_Allocator);

        // 7) Destroy Device
        if (m_Device) vkDestroyDevice(m_Device, nullptr);
    }

    uint64_t VulkanDevice::SignalGraphicsTimeline()
    {
        const uint64_t value = m_GraphicsTimelineNextValue.fetch_add(1, std::memory_order_relaxed);
        m_GraphicsTimelineValue = value;
        return value;
    }

    uint64_t VulkanDevice::GetGraphicsTimelineCompletedValue() const
    {
        if (!m_GraphicsTimelineSemaphore) return 0;
        uint64_t completed = 0;
        VK_CHECK(vkGetSemaphoreCounterValue(m_Device, m_GraphicsTimelineSemaphore, &completed));
        return completed;
    }

    void VulkanDevice::CollectGarbage()
    {
        const uint64_t completed = GetGraphicsTimelineCompletedValue();

        std::lock_guard lock(m_DeletionMutex);

        if (m_TimelineDeletionQueue.empty())
            return;

        // Keep order; destroys are typically small, so a single pass erase is fine.
        std::erase_if(m_TimelineDeletionQueue, [&](DeferredDelete& item)
        {
            if (item.Value <= completed)
            {
                if (item.Fn) item.Fn();
                return true;
            }
            return false;
        });
    }

    void VulkanDevice::SafeDestroyAfter(uint64_t value, std::function<void()>&& deleteFn)
    {
        std::lock_guard lock(m_DeletionMutex);
        m_TimelineDeletionQueue.push_back(DeferredDelete{.Value = value, .Fn = std::move(deleteFn)});
    }

    void VulkanDevice::SafeDestroy(std::function<void()>&& deleteFn)
    {
        // Defer until the *next* graphics submit completes.
        // If no submit has happened yet, fall back to value 1 (first submit).
        const uint64_t target = (m_GraphicsTimelineValue > 0) ? (m_GraphicsTimelineValue + 1) : 1;
        SafeDestroyAfter(target, std::move(deleteFn));
    }

    void VulkanDevice::FlushDeletionQueue(uint32_t frameIndex)
    {
        std::lock_guard lock(m_DeletionMutex);

        m_CurrentSafeDestroyFrame = frameIndex;

        auto& queue = m_DeletionQueue[frameIndex];
        for (auto& fn : queue) fn();
        queue.clear();
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
        m_Indices = FindQueueFamilies(m_PhysicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies;
        if (m_Indices.GraphicsFamily.has_value())
            uniqueQueueFamilies.insert(m_Indices.GraphicsFamily.value());
        if (m_Indices.PresentFamily.has_value())
            uniqueQueueFamilies.insert(m_Indices.PresentFamily.value());
        if (m_Indices.TransferFamily.has_value())
            uniqueQueueFamilies.insert(m_Indices.TransferFamily.value());

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

        // Vulkan 1.1 Features (This is where shaderDrawParameters lives)
        VkPhysicalDeviceVulkan11Features features11{};
        features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        features11.shaderDrawParameters = VK_TRUE;
        features11.pNext = &dynamicState;

        // Vulkan 1.2 Features (BDA lives here)
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.descriptorIndexing = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.pNext = &features11; // Chain 12 -> 11

        // Vulkan 1.3 Features (Dynamic Rendering/Sync2 live here)
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.synchronization2 = VK_TRUE;
        features13.dynamicRendering = VK_TRUE;
        features13.pNext = &features12; // Chain 13 -> 12


        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features13;

        vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);

        // Verify required features (Panic if missing)
        if (!features13.dynamicRendering) Core::Log::Error("Vulkan 1.3 Dynamic Rendering not supported!");
        if (!features13.synchronization2) Core::Log::Error("Vulkan 1.3 Sync2 not supported!");
        if (!features12.bufferDeviceAddress) Core::Log::Error("Vulkan 1.2 Buffer Device Address not supported!");

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
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

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
        if (m_Indices.TransferFamily.has_value())
        {
            vkGetDeviceQueue(m_Device, m_Indices.TransferFamily.value(), 0, &m_TransferQueue);
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

            if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT))
            {
                indices.TransferFamily = i;
            }
            i++;

        }
        if (!indices.TransferFamily.has_value()) {
            i = 0;
            for (const auto& queueFamily : queueFamilies) {
                if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
                    indices.TransferFamily = i;
                    break;
                }
                i++;
            }
        }

        // Final Fallback: Graphics queue implies Transfer support implicitly in Vulkan
        if (!indices.TransferFamily.has_value() && indices.GraphicsFamily.has_value()) {
            indices.TransferFamily = indices.GraphicsFamily;
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

    VkResult VulkanDevice::SubmitToGraphicsQueue(const VkSubmitInfo& submitInfo, VkFence fence)
    {
        if (!m_Device || !m_GraphicsQueue)
            return VK_ERROR_DEVICE_LOST;

        std::scoped_lock lock(m_QueueMutex);
        return vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, fence);
    }

    VkResult VulkanDevice::Present(const VkPresentInfoKHR& presentInfo)
    {
        // Headless / offscreen mode: no surface == no present.
        if (m_Surface == VK_NULL_HANDLE)
            return VK_SUCCESS;

        if (!m_Device || !m_PresentQueue)
            return VK_ERROR_DEVICE_LOST;

        std::scoped_lock lock(m_QueueMutex);
        return vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    }

    void VulkanDevice::RegisterThreadLocalPool(VkCommandPool pool)
    {
        if (pool == VK_NULL_HANDLE || !m_Device)
            return;

        std::lock_guard lock(m_ThreadPoolsMutex);
        m_ThreadCommandPools.push_back(pool);
    }
}
