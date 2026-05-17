module;

#include <algorithm>
#include <cassert>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "Vulkan.hpp"
#include <GLFW/glfw3.h>

module Extrinsic.Backends.Vulkan;

import :Device;
import Extrinsic.Core.Logging;

namespace Extrinsic::Backends::Vulkan
{
namespace
{
    std::atomic<std::uint64_t> g_FallbackBindlessAllocationAttempts{0};
    std::atomic<std::uint64_t> g_FallbackTransferUploadAttempts{0};
    std::atomic<std::uint64_t> g_FallbackPipelineCreationAttempts{0};
    std::atomic<std::uint64_t> g_FallbackBeginFrameAttempts{0};
    std::atomic<std::uint64_t> g_FallbackEndFrameAttempts{0};
    std::atomic<std::uint64_t> g_FallbackPresentAttempts{0};
    std::atomic<std::uint64_t> g_FallbackResizeAttempts{0};
    std::atomic<std::uint8_t>  g_LastFallbackPipelineReason{
        static_cast<std::uint8_t>(FallbackPipelineReason::None)};

    // GRAPHICS-033B operational-diagnostics counters. Process-monotonic and
    // never reset across Initialize/Shutdown cycles; ordering across fields
    // is not transactional. Per-reason histogram buckets are owned by the
    // Vulkan backend because the runtime never observes the individual
    // reasons except through this aggregate snapshot.
    std::atomic<std::uint64_t> g_VulkanFallbackToNullCount{0};
    std::atomic<std::uint64_t> g_VulkanInitFailureCount{0};
    std::atomic<std::uint64_t> g_VulkanValidationErrorCount{0};
    std::atomic<std::uint64_t> g_VulkanOperationalGateFailureCount{0};
    std::atomic<std::uint64_t> g_VulkanDeviceLostOperationalDropCount{0};
    std::array<std::atomic<std::uint32_t>, kVulkanOperationalReasonCount>
        g_VulkanOperationalReasonHistogram{};
    std::mutex g_BootstrapDiagnosticsMutex;
    VulkanBootstrapDiagnosticsSnapshot g_BootstrapDiagnostics{};
    std::mutex g_FrameLifecycleDiagnosticsMutex;
    VulkanFrameLifecycleDiagnosticsSnapshot g_FrameLifecycleDiagnostics{};
    std::mutex g_ServiceDiagnosticsMutex;
    VulkanServiceDiagnosticsSnapshot g_ServiceDiagnostics{};
    std::mutex g_PipelineDiagnosticsMutex;
    VulkanPipelineDiagnosticsSnapshot g_PipelineDiagnostics{};
    std::atomic<std::uint64_t> g_SuccessfulPipelineCreations{0};

    constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

    struct QueueFamilyProbe
    {
        std::uint32_t Graphics = 0;
        std::uint32_t Present = 0;
        std::uint32_t Transfer = 0;
        bool GraphicsFound = false;
        bool PresentFound = false;
        bool TransferFound = false;
    };

    struct RequiredDeviceFeatureProbe
    {
        bool SamplerAnisotropySupported = false;
        bool DescriptorIndexingSupported = false;
        bool TimelineSemaphoreSupported = false;
        bool DynamicRenderingSupported = false;
        bool BufferDeviceAddressSupported = false;
        bool Synchronization2Supported = false;
        bool ScalarBlockLayoutSupported = false;
        bool ShaderInt64Supported = false;

        [[nodiscard]] bool AllRequiredSupported() const noexcept
        {
            return DescriptorIndexingSupported && TimelineSemaphoreSupported && DynamicRenderingSupported &&
                   BufferDeviceAddressSupported && Synchronization2Supported && ScalarBlockLayoutSupported &&
                   ShaderInt64Supported;
        }
    };

    void PublishBootstrapDiagnostics(const VulkanBootstrapDiagnosticsSnapshot& snapshot) noexcept
    {
        std::scoped_lock lock{g_BootstrapDiagnosticsMutex};
        g_BootstrapDiagnostics = snapshot;
    }

    void PublishServiceDiagnostics(const VulkanServiceDiagnosticsSnapshot& snapshot) noexcept
    {
        std::scoped_lock lock{g_ServiceDiagnosticsMutex};
        g_ServiceDiagnostics = snapshot;
    }

    void PublishPipelineDiagnostics(const VulkanPipelineDiagnosticsSnapshot& snapshot) noexcept
    {
        std::scoped_lock lock{g_PipelineDiagnosticsMutex};
        g_PipelineDiagnostics = snapshot;
    }

    void RefreshFrameLifecycleAttemptCounters(VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
    {
        snapshot.BeginFrameAttempts = g_FallbackBeginFrameAttempts.load(std::memory_order_relaxed);
        snapshot.EndFrameAttempts = g_FallbackEndFrameAttempts.load(std::memory_order_relaxed);
        snapshot.PresentAttempts = g_FallbackPresentAttempts.load(std::memory_order_relaxed);
        snapshot.ResizeAttempts = g_FallbackResizeAttempts.load(std::memory_order_relaxed);
    }

    template <typename Mutator>
    void MutateFrameLifecycleDiagnostics(Mutator&& mutator) noexcept
    {
        std::scoped_lock lock{g_FrameLifecycleDiagnosticsMutex};
        mutator(g_FrameLifecycleDiagnostics);
        RefreshFrameLifecycleAttemptCounters(g_FrameLifecycleDiagnostics);
    }

    [[nodiscard]] bool HasInstanceLayer(const char* name)
    {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0)
            return false;

        std::vector<VkLayerProperties> layers(count);
        if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS)
            return false;

        for (const VkLayerProperties& layer : layers)
        {
            if (std::strcmp(layer.layerName, name) == 0)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool HasInstanceExtension(const char* name)
    {
        std::uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
            return false;

        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS)
            return false;

        for (const VkExtensionProperties& extension : extensions)
        {
            if (std::strcmp(extension.extensionName, name) == 0)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
    {
        std::uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
            return false;

        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS)
            return false;

        for (const VkExtensionProperties& extension : extensions)
        {
            if (std::strcmp(extension.extensionName, name) == 0)
                return true;
        }
        return false;
    }

    [[nodiscard]] QueueFamilyProbe ProbeQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        QueueFamilyProbe probe{};

        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        if (count == 0)
            return probe;

        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

        for (std::uint32_t index = 0; index < count; ++index)
        {
            const VkQueueFamilyProperties& family = families[index];
            if (!probe.GraphicsFound && (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                probe.Graphics = index;
                probe.GraphicsFound = true;
            }

            VkBool32 presentSupported = VK_FALSE;
            if (surface != VK_NULL_HANDLE &&
                vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupported) == VK_SUCCESS &&
                presentSupported == VK_TRUE && !probe.PresentFound)
            {
                probe.Present = index;
                probe.PresentFound = true;
            }

            if (!probe.TransferFound && (family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 &&
                (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
            {
                probe.Transfer = index;
                probe.TransferFound = true;
            }
        }

        if (!probe.TransferFound && probe.GraphicsFound)
        {
            probe.Transfer = probe.Graphics;
            probe.TransferFound = true;
        }

        return probe;
    }

    [[nodiscard]] bool HasSwapchainSurfaceSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        if (surface == VK_NULL_HANDLE)
            return false;

        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities) != VK_SUCCESS)
            return false;

        std::uint32_t formatCount = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr) != VK_SUCCESS ||
            formatCount == 0)
            return false;

        std::uint32_t presentModeCount = 0;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr) != VK_SUCCESS ||
            presentModeCount == 0)
            return false;

        return true;
    }

    [[nodiscard]] RequiredDeviceFeatureProbe QueryRequiredDeviceFeatures(VkPhysicalDevice physicalDevice)
    {
        RequiredDeviceFeatureProbe probe{};
        if (physicalDevice == VK_NULL_HANDLE)
            return probe;

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

        probe.SamplerAnisotropySupported = features2.features.samplerAnisotropy == VK_TRUE;
        probe.DescriptorIndexingSupported =
            features12.descriptorBindingPartiallyBound == VK_TRUE &&
            features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE;
        probe.TimelineSemaphoreSupported = features12.timelineSemaphore == VK_TRUE;
        probe.DynamicRenderingSupported = features13.dynamicRendering == VK_TRUE;
        probe.BufferDeviceAddressSupported = features12.bufferDeviceAddress == VK_TRUE;
        probe.Synchronization2Supported = features13.synchronization2 == VK_TRUE;
        probe.ScalarBlockLayoutSupported = features12.scalarBlockLayout == VK_TRUE;
        probe.ShaderInt64Supported = features2.features.shaderInt64 == VK_TRUE;
        return probe;
    }

    [[nodiscard]] VkSurfaceFormatKHR ChooseSwapchainSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
    {
        for (const VkSurfaceFormatKHR& format : formats)
        {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }
        return formats.empty()
            ? VkSurfaceFormatKHR{.format = VK_FORMAT_B8G8R8A8_UNORM,
                                 .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
            : formats.front();
    }

    [[nodiscard]] RHI::Format ToRhiBackbufferFormat(const VkFormat format) noexcept
    {
        switch (format)
        {
        case VK_FORMAT_R8G8B8A8_UNORM: return RHI::Format::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return RHI::Format::RGBA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return RHI::Format::BGRA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return RHI::Format::BGRA8_SRGB;
        default: return RHI::Format::RGBA8_UNORM;
        }
    }

    [[nodiscard]] VkExtent2D ChooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                                   const Core::Extent2D framebufferExtent)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
            return capabilities.currentExtent;

        const auto clampDimension = [](const int requested,
                                       const std::uint32_t minimum,
                                       const std::uint32_t maximum)
        {
            std::uint32_t value = requested > 0 ? static_cast<std::uint32_t>(requested) : minimum;
            value = std::max(value, minimum);
            if (maximum >= minimum)
                value = std::min(value, maximum);
            return value;
        };

        return VkExtent2D{
            .width = clampDimension(framebufferExtent.Width,
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width),
            .height = clampDimension(framebufferExtent.Height,
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height),
        };
    }

    [[nodiscard]] std::vector<std::uint32_t> UniqueQueueFamilies(const QueueFamilyProbe& queueProbe)
    {
        std::vector<std::uint32_t> families;
        const auto addUnique = [&families](const std::uint32_t family)
        {
            for (const std::uint32_t existing : families)
            {
                if (existing == family)
                    return;
            }
            families.push_back(family);
        };

        if (queueProbe.GraphicsFound)
            addUnique(queueProbe.Graphics);
        if (queueProbe.PresentFound)
            addUnique(queueProbe.Present);
        if (queueProbe.TransferFound)
            addUnique(queueProbe.Transfer);
        return families;
    }

    [[nodiscard]] VkResult CreateBootstrapLogicalDevice(VkPhysicalDevice physicalDevice,
                                                        const QueueFamilyProbe& queueProbe,
                                                        const RequiredDeviceFeatureProbe& featureProbe,
                                                        VkDevice* outDevice,
                                                        bool* outSamplerAnisotropySupported)
    {
        if (outDevice == nullptr || physicalDevice == VK_NULL_HANDLE)
            return VK_ERROR_INITIALIZATION_FAILED;

        *outDevice = VK_NULL_HANDLE;
        if (outSamplerAnisotropySupported != nullptr)
            *outSamplerAnisotropySupported = false;

        const std::vector<std::uint32_t> uniqueFamilies = UniqueQueueFamilies(queueProbe);
        if (uniqueFamilies.empty())
            return VK_ERROR_INITIALIZATION_FAILED;

        constexpr float kQueuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        queueInfos.reserve(uniqueFamilies.size());
        for (const std::uint32_t family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &kQueuePriority;
            queueInfos.push_back(queueInfo);
        }

        if (!featureProbe.AllRequiredSupported())
            return VK_ERROR_FEATURE_NOT_PRESENT;

        VkPhysicalDeviceVulkan13Features enabled13{};
        enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enabled13.dynamicRendering = VK_TRUE;
        enabled13.synchronization2 = VK_TRUE;

        VkPhysicalDeviceVulkan12Features enabled12{};
        enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enabled12.pNext = &enabled13;
        enabled12.descriptorBindingPartiallyBound = VK_TRUE;
        enabled12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        enabled12.timelineSemaphore = VK_TRUE;
        enabled12.bufferDeviceAddress = VK_TRUE;
        enabled12.scalarBlockLayout = VK_TRUE;

        VkPhysicalDeviceFeatures2 enabledFeatures{};
        enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        enabledFeatures.pNext = &enabled12;
        if (featureProbe.SamplerAnisotropySupported)
        {
            enabledFeatures.features.samplerAnisotropy = VK_TRUE;
            if (outSamplerAnisotropySupported != nullptr)
                *outSamplerAnisotropySupported = true;
        }
        enabledFeatures.features.shaderInt64 = VK_TRUE;

        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = 1u;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        deviceInfo.pNext = &enabledFeatures;

        return vkCreateDevice(physicalDevice, &deviceInfo, nullptr, outDevice);
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL BootstrapDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                          VkDebugUtilsMessageTypeFlagsEXT,
                                                          const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                          void*)
    {
        const char* message = data && data->pMessage ? data->pMessage : "<null>";
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
            Core::Log::Error("[VulkanDevice::Bootstrap] validation: {}", message);
        else
            Core::Log::Warn("[VulkanDevice::Bootstrap] validation: {}", message);
        return VK_FALSE;
    }

    [[nodiscard]] bool HasImageUsage(const VkImageUsageFlags usage, const VkImageUsageFlags bit) noexcept
    {
        return (usage & bit) != 0;
    }

    [[nodiscard]] bool IsDepthStencilFormat(const VkFormat format) noexcept
    {
        return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    [[nodiscard]] bool HasStencilFormat(const VkFormat format) noexcept
    {
        return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    struct SpirvReadResult
    {
        std::vector<std::uint32_t> Words{};
        std::uint64_t Bytes = 0;
    };

    [[nodiscard]] SpirvReadResult ReadSpirvFile(const std::string& path)
    {
        if (path.empty())
            return {};

        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file)
            return {};

        const std::streampos end = file.tellg();
        if (end <= std::streampos{0})
            return {};

        const auto byteCount = static_cast<std::uint64_t>(end);
        if ((byteCount % sizeof(std::uint32_t)) != 0u)
            return {};

        SpirvReadResult result{};
        result.Bytes = byteCount;
        result.Words.resize(static_cast<std::size_t>(byteCount / sizeof(std::uint32_t)));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(result.Words.data()), static_cast<std::streamsize>(byteCount));
        if (!file)
            return {};
        return result;
    }

    [[nodiscard]] VkResult CreateShaderModule(VkDevice device,
                                              const std::vector<std::uint32_t>& spirv,
                                              VkShaderModule& outModule) noexcept
    {
        outModule = VK_NULL_HANDLE;
        if (device == VK_NULL_HANDLE || spirv.empty())
            return VK_ERROR_INITIALIZATION_FAILED;

        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        moduleInfo.pCode = spirv.data();
        return vkCreateShaderModule(device, &moduleInfo, nullptr, &outModule);
    }

    [[nodiscard]] bool ValidatePipelineDesc(const RHI::PipelineDesc& desc) noexcept
    {
        if (desc.PushConstantSize > RHI::MaxPushConstantBytes)
            return false;

        const bool isCompute = !desc.ComputeShaderPath.empty();
        if (isCompute)
            return desc.VertexShaderPath.empty() && desc.FragmentShaderPath.empty();

        if (desc.VertexShaderPath.empty() || desc.ColorTargetCount > RHI::MaxColorTargets)
        {
            return false;
        }

        if (desc.ColorTargetCount == 0u)
        {
            return desc.DepthTargetFormat != RHI::Format::Undefined &&
                   ToVkFormat(desc.DepthTargetFormat) != VK_FORMAT_UNDEFINED;
        }

        if (desc.FragmentShaderPath.empty())
            return false;

        for (std::uint32_t i = 0; i < desc.ColorTargetCount; ++i)
        {
            if (ToVkFormat(desc.ColorTargetFormats[i]) == VK_FORMAT_UNDEFINED)
                return false;
        }

        if (desc.DepthTargetFormat != RHI::Format::Undefined &&
            ToVkFormat(desc.DepthTargetFormat) == VK_FORMAT_UNDEFINED)
        {
            return false;
        }
        return true;
    }

    void SetDebugName(VkDevice device,
                      VkObjectType objectType,
                      std::uint64_t objectHandle,
                      const char* name,
                      bool validationEnabled) noexcept
    {
        if (!validationEnabled || name == nullptr || device == VK_NULL_HANDLE || objectHandle == 0u ||
            vkSetDebugUtilsObjectNameEXT == nullptr)
        {
            return;
        }

        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = objectType;
        nameInfo.objectHandle = objectHandle;
        nameInfo.pObjectName = name;
        vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
    }
}

void NoteFallbackBindlessAllocationAttempt()
{
    g_FallbackBindlessAllocationAttempts.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t GetFallbackBindlessAllocationAttemptCount() noexcept
{
    return g_FallbackBindlessAllocationAttempts.load(std::memory_order_relaxed);
}

void NoteFallbackTransferUploadAttempt()
{
    g_FallbackTransferUploadAttempts.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t GetFallbackTransferUploadAttemptCount() noexcept
{
    return g_FallbackTransferUploadAttempts.load(std::memory_order_relaxed);
}

void NoteFallbackPipelineCreationAttempt(FallbackPipelineReason reason)
{
    g_FallbackPipelineCreationAttempts.fetch_add(1, std::memory_order_relaxed);
    g_LastFallbackPipelineReason.store(static_cast<std::uint8_t>(reason),
                                       std::memory_order_relaxed);
}

std::uint64_t GetFallbackPipelineCreationAttemptCount() noexcept
{
    return g_FallbackPipelineCreationAttempts.load(std::memory_order_relaxed);
}

FallbackPipelineReason GetLastFallbackPipelineReason() noexcept
{
    return static_cast<FallbackPipelineReason>(
        g_LastFallbackPipelineReason.load(std::memory_order_relaxed));
}

namespace
{
    // Frame-loop fail-closed breadcrumbs return the new counter value so callers
    // can log only on the first fire. Subsequent fires are still counted by the
    // process-monotonic atomic but do not produce log spam at 60 Hz; the counter
    // itself is always available for CPU diagnostic consumers.
    [[nodiscard]] std::uint64_t NoteFallbackBeginFrameAttempt() noexcept
    {
        return g_FallbackBeginFrameAttempts.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    [[nodiscard]] std::uint64_t NoteFallbackEndFrameAttempt() noexcept
    {
        return g_FallbackEndFrameAttempts.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    [[nodiscard]] std::uint64_t NoteFallbackPresentAttempt() noexcept
    {
        return g_FallbackPresentAttempts.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    void NoteFallbackResizeAttempt() noexcept
    {
        // Resize is event-driven (not per-frame), so no suppression is needed.
        g_FallbackResizeAttempts.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint64_t GetFallbackBeginFrameAttemptCount() noexcept
{
    return g_FallbackBeginFrameAttempts.load(std::memory_order_relaxed);
}

std::uint64_t GetFallbackEndFrameAttemptCount() noexcept
{
    return g_FallbackEndFrameAttempts.load(std::memory_order_relaxed);
}

std::uint64_t GetFallbackPresentAttemptCount() noexcept
{
    return g_FallbackPresentAttempts.load(std::memory_order_relaxed);
}

std::uint64_t GetFallbackResizeAttemptCount() noexcept
{
    return g_FallbackResizeAttempts.load(std::memory_order_relaxed);
}

FallbackDiagnosticsSnapshot GetFallbackDiagnosticsSnapshot() noexcept
{
    FallbackDiagnosticsSnapshot snapshot{};
    snapshot.BindlessAllocationAttempts =
        g_FallbackBindlessAllocationAttempts.load(std::memory_order_relaxed);
    snapshot.TransferUploadAttempts =
        g_FallbackTransferUploadAttempts.load(std::memory_order_relaxed);
    snapshot.PipelineCreationAttempts =
        g_FallbackPipelineCreationAttempts.load(std::memory_order_relaxed);
    snapshot.LastPipelineReason = static_cast<FallbackPipelineReason>(
        g_LastFallbackPipelineReason.load(std::memory_order_relaxed));
    snapshot.BeginFrameAttempts =
        g_FallbackBeginFrameAttempts.load(std::memory_order_relaxed);
    snapshot.EndFrameAttempts =
        g_FallbackEndFrameAttempts.load(std::memory_order_relaxed);
    snapshot.PresentAttempts =
        g_FallbackPresentAttempts.load(std::memory_order_relaxed);
    snapshot.ResizeAttempts =
        g_FallbackResizeAttempts.load(std::memory_order_relaxed);
    return snapshot;
}

void RecordVulkanOperationalFallback(const VulkanOperationalStatus status) noexcept
{
    // The truth table in `src/graphics/vulkan/README.md` records that
    // `Operational` and `NotRequested` rows fire no fallback counters; the
    // remaining five status codes all increment the aggregate fallback count
    // plus the matching reason histogram bucket.
    switch (status.Code)
    {
    case VulkanOperationalStatusCode::Operational:
    case VulkanOperationalStatusCode::NotRequested:
        return;
    case VulkanOperationalStatusCode::NotCompiled:
    case VulkanOperationalStatusCode::RequestedButUnsupported:
        break;
    case VulkanOperationalStatusCode::RequestedButFailedInit:
        g_VulkanInitFailureCount.fetch_add(1, std::memory_order_relaxed);
        break;
    case VulkanOperationalStatusCode::RequestedButValidationFailed:
        g_VulkanValidationErrorCount.fetch_add(1, std::memory_order_relaxed);
        break;
    case VulkanOperationalStatusCode::RequestedButIncompleteGate:
        g_VulkanOperationalGateFailureCount.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    g_VulkanFallbackToNullCount.fetch_add(1, std::memory_order_relaxed);
    const auto reasonIndex = static_cast<std::size_t>(status.Reason);
    if (reasonIndex < kVulkanOperationalReasonCount)
    {
        g_VulkanOperationalReasonHistogram[reasonIndex].fetch_add(
            1, std::memory_order_relaxed);
    }
}

void NoteVulkanOperationalDeviceLostDrop() noexcept
{
    g_VulkanDeviceLostOperationalDropCount.fetch_add(1, std::memory_order_relaxed);
}

VulkanOperationalDiagnosticsSnapshot GetVulkanOperationalDiagnosticsSnapshot() noexcept
{
    VulkanOperationalDiagnosticsSnapshot snapshot{};
    snapshot.VulkanFallbackToNullCount =
        g_VulkanFallbackToNullCount.load(std::memory_order_relaxed);
    snapshot.VulkanInitFailureCount =
        g_VulkanInitFailureCount.load(std::memory_order_relaxed);
    snapshot.VulkanValidationErrorCount =
        g_VulkanValidationErrorCount.load(std::memory_order_relaxed);
    snapshot.VulkanOperationalGateFailureCount =
        g_VulkanOperationalGateFailureCount.load(std::memory_order_relaxed);
    snapshot.VulkanDeviceLostOperationalDropCount =
        g_VulkanDeviceLostOperationalDropCount.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < kVulkanOperationalReasonCount; ++i)
    {
        snapshot.ReasonHistogram[i] =
            g_VulkanOperationalReasonHistogram[i].load(std::memory_order_relaxed);
    }
    return snapshot;
}

VulkanOperationalStatus EvaluateVulkanDeviceOperationalStatus(
    const RHI::IDevice* device) noexcept
{
    if (device == nullptr)
        return {VulkanOperationalStatusCode::NotCompiled, VulkanOperationalReason::None};
    const auto& vulkanDevice = static_cast<const VulkanDevice&>(*device);
    return EvaluateVulkanOperationalStatus(vulkanDevice.BuildOperationalInputs());
}

VulkanOperationalInputs GetVulkanDeviceOperationalInputs(
    const RHI::IDevice* device) noexcept
{
    if (device == nullptr)
        return {};
    const auto& vulkanDevice = static_cast<const VulkanDevice&>(*device);
    return vulkanDevice.BuildOperationalInputs();
}

VulkanBootstrapDiagnosticsSnapshot GetVulkanBootstrapDiagnosticsSnapshot() noexcept
{
    std::scoped_lock lock{g_BootstrapDiagnosticsMutex};
    return g_BootstrapDiagnostics;
}

VulkanFrameLifecycleDiagnosticsSnapshot GetVulkanFrameLifecycleDiagnosticsSnapshot() noexcept
{
    std::scoped_lock lock{g_FrameLifecycleDiagnosticsMutex};
    VulkanFrameLifecycleDiagnosticsSnapshot snapshot = g_FrameLifecycleDiagnostics;
    RefreshFrameLifecycleAttemptCounters(snapshot);
    return snapshot;
}

VulkanServiceDiagnosticsSnapshot GetVulkanServiceDiagnosticsSnapshot() noexcept
{
    std::scoped_lock lock{g_ServiceDiagnosticsMutex};
    return g_ServiceDiagnostics;
}

VulkanPipelineDiagnosticsSnapshot GetVulkanPipelineDiagnosticsSnapshot() noexcept
{
    std::scoped_lock lock{g_PipelineDiagnosticsMutex};
    VulkanPipelineDiagnosticsSnapshot snapshot = g_PipelineDiagnostics;
    snapshot.SuccessfulPipelineCreations = g_SuccessfulPipelineCreations.load(std::memory_order_relaxed);
    return snapshot;
}

// =============================================================================
// §12  Factory
// =============================================================================

std::unique_ptr<RHI::IDevice> CreateVulkanDevice()
{
    // Explicit base-pointer construction — unique_ptr<Derived>→unique_ptr<Base>
    // implicit conversion can confuse Clang's module-purview type resolution.
    return std::unique_ptr<RHI::IDevice>(new VulkanDevice());
}

// =============================================================================
// §11  VulkanDevice — destructor & lifecycle
// (buffer/texture/sampler/pipeline CRUD see upload path summary below)
// =============================================================================

VulkanDevice::~VulkanDevice() = default;

bool VulkanDevice::HasLiveOperationalPrerequisites() const noexcept
{
    if (m_DeviceLost || m_Instance == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE ||
        m_PhysDevice == VK_NULL_HANDLE || m_Device == VK_NULL_HANDLE || m_Vma == VK_NULL_HANDLE ||
        m_GraphicsQueue == VK_NULL_HANDLE || m_PresentQueue == VK_NULL_HANDLE ||
        m_TransferVkQueue == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE ||
        m_GlobalPipelineLayout == VK_NULL_HANDLE || m_SwapchainExtent.width == 0u ||
        m_SwapchainExtent.height == 0u || !m_BindlessHeap || !m_BindlessHeap->IsValid() ||
        !m_TransferQueue || !m_TransferQueue->IsValid())
    {
        return false;
    }

    const std::size_t swapchainImageCount = m_SwapchainImages.size();
    if (swapchainImageCount == 0u || m_SwapchainViews.size() != swapchainImageCount ||
        m_SwapchainHandles.size() != swapchainImageCount)
    {
        return false;
    }

    for (const RHI::TextureHandle handle : m_SwapchainHandles)
    {
        if (!handle.IsValid())
            return false;
    }

    if (m_FrameSlot >= kMaxFramesInFlight)
        return false;

    for (const PerFrame& frame : m_Frames)
    {
        if (frame.CmdPool == VK_NULL_HANDLE || frame.CmdBuffer == VK_NULL_HANDLE ||
            frame.Fence == VK_NULL_HANDLE || frame.ImageAcquired == VK_NULL_HANDLE ||
            frame.RenderDone == VK_NULL_HANDLE)
        {
            return false;
        }
    }

    return true;
}

bool VulkanDevice::HasOperationalSafetyPrerequisites() const noexcept
{
    // GRAPHICS-033F: gate 8 (`PublicServiceReconciled`) is sourced from raw,
    // non-circular preconditions on the live Vulkan handles and bound command
    // contexts. The previous hard-coded `false` was a placeholder for the
    // service-diagnostics path, and the diagnostics block used to derive
    // `PublicBindlessHeapExposed` from `IsOperational()`, which created a
    // definitional cycle through `BuildOperationalInputs()`. This predicate
    // breaks that cycle by checking raw live-handle prerequisites directly.
    if (m_DeviceLost)
        return false;
    if (m_GlobalPipelineLayout == VK_NULL_HANDLE)
        return false;
    if (!m_BindlessHeap || !m_BindlessHeap->IsValid())
        return false;
    if (!m_TransferQueue || !m_TransferQueue->IsValid())
        return false;
    if (m_Swapchain == VK_NULL_HANDLE)
        return false;
    const std::size_t swapchainImageCount = m_SwapchainImages.size();
    if (swapchainImageCount == 0u ||
        m_SwapchainViews.size()   != swapchainImageCount ||
        m_SwapchainHandles.size() != swapchainImageCount)
    {
        return false;
    }
    for (const PerFrame& frame : m_Frames)
    {
        if (frame.CmdPool == VK_NULL_HANDLE || frame.CmdBuffer == VK_NULL_HANDLE ||
            frame.Fence == VK_NULL_HANDLE || frame.ImageAcquired == VK_NULL_HANDLE ||
            frame.RenderDone == VK_NULL_HANDLE)
        {
            return false;
        }
    }
    for (const VulkanCommandContext& cmdContext : m_CmdContexts)
    {
        if (!cmdContext.IsBound())
            return false;
    }
    return true;
}

VulkanOperationalInputs VulkanDevice::BuildOperationalInputs() const noexcept
{
    VulkanOperationalInputs inputs{};

    // The Vulkan backend is, by definition, compiled in when this code runs.
    // Runtime-side request reconciliation lives in `GRAPHICS-033B`; from
    // `VulkanDevice`'s own perspective the request was honored.
    inputs.CompiledIn = true;
    inputs.Requested  = true;

    // Lifecycle loss is checked before host-support / live bring-up so a
    // previously successful bring-up still resolves fail-closed.
    inputs.DeviceLost  = m_DeviceLost;
    inputs.SurfaceLost = false;

    // Host-support pre-init gates. If logical device creation succeeded, the
    // required extension/feature chain was satisfied; future GRAPHICS-033B
    // wiring can replace the proxies with explicit per-capability bits.
    inputs.HostSupportsRequiredInstance   = m_Instance   != VK_NULL_HANDLE;
    inputs.HostSupportsRequiredSurface    = m_Surface    != VK_NULL_HANDLE;
    inputs.HostSupportsPhysicalDevice     = m_PhysDevice != VK_NULL_HANDLE;
    inputs.HostSupportsRequiredExtensions = m_Device     != VK_NULL_HANDLE;
    inputs.HostSupportsRequiredFeatures   = m_Device     != VK_NULL_HANDLE;

    // Live bring-up.
    inputs.LogicalDeviceReady = m_Device != VK_NULL_HANDLE;
    inputs.AllocatorReady     = m_Vma    != VK_NULL_HANDLE;
    inputs.SwapchainReady     = m_Swapchain != VK_NULL_HANDLE &&
                                m_SwapchainExtent.width  != 0u &&
                                m_SwapchainExtent.height != 0u &&
                                !m_SwapchainImages.empty() &&
                                m_SwapchainViews.size() == m_SwapchainImages.size() &&
                                m_SwapchainHandles.size() == m_SwapchainImages.size();

    bool commandSyncReady = m_GlobalPipelineLayout != VK_NULL_HANDLE &&
                            m_GraphicsQueue   != VK_NULL_HANDLE &&
                            m_PresentQueue    != VK_NULL_HANDLE &&
                            m_TransferVkQueue != VK_NULL_HANDLE &&
                            m_BindlessHeap && m_BindlessHeap->IsValid() &&
                            m_TransferQueue && m_TransferQueue->IsValid();
    for (const PerFrame& frame : m_Frames)
    {
        if (frame.CmdPool == VK_NULL_HANDLE || frame.CmdBuffer == VK_NULL_HANDLE ||
            frame.Fence == VK_NULL_HANDLE || frame.ImageAcquired == VK_NULL_HANDLE ||
            frame.RenderDone == VK_NULL_HANDLE)
        {
            commandSyncReady = false;
            break;
        }
    }
    inputs.CommandSyncReady = commandSyncReady;

    // GRAPHICS-033C: the minimal-recipe recording bodies
    // (`MinimalDebugSurfacePass::Execute` and
    // `MinimalDebugPresentPass::Execute`) are present in the renderer and the
    // executor lambda routes the live `VulkanCommandContext` to them whenever
    // `IDevice::IsOperational()` is true and the slot-0 / culling / GpuWorld
    // prerequisites are ready. The presence of the recording bodies is a
    // codebase fact, so this gate flips to `true` here.
    inputs.MinimalRecipeRecordingPresent = true;
    // GRAPHICS-033E: gate 7 is sourced from the renderer-published recipe-aware
    // validation outcome. Cold-start fail-closed (`Initialize()` resets the
    // atomic to `false`); a single `Error`-severity finding flips it back to
    // `false` on the next compile.
    inputs.BarrierValidationClean        = m_LatestRecipeValidationClean.load(std::memory_order_relaxed);
    // GRAPHICS-033F: gate 8 is sourced from the raw, non-circular safety-
    // prereq predicate. The predicate inspects live Vulkan handles directly
    // and never consults `IsOperational()`, so the diagnostics block can
    // safely combine the two without re-entering this evaluator.
    inputs.PublicServiceReconciled       = HasOperationalSafetyPrerequisites();
    inputs.ValidationClean               = true;

    return inputs;
}

bool VulkanDevice::ComputeOperationalPredicate() const noexcept
{
    // Single source of truth: the evaluator. `HasLiveOperationalPrerequisites`
    // is still consumed by individual fail-closed paths (transfer queue,
    // buffer/texture creation) that need finer-grained readiness checks than
    // the binary operational predicate.
    return EvaluateVulkanOperationalStatus(BuildOperationalInputs()).Code ==
           VulkanOperationalStatusCode::Operational;
}

void VulkanDevice::RefreshOperationalState() noexcept
{
    m_Operational = ComputeOperationalPredicate();
}

void VulkanDevice::NoteDeviceLostIfNeeded(const VkResult result) noexcept
{
    if (result == VK_ERROR_DEVICE_LOST)
    {
        m_DeviceLost = true;
        // GRAPHICS-033B: record the operational→non-operational drop exactly
        // once per transition; a stuck-lost device should not keep bumping
        // the counter on every subsequent VK_ERROR_DEVICE_LOST observation.
        if (m_Operational)
        {
            m_Operational = false;
            NoteVulkanOperationalDeviceLostDrop();
        }
    }
}

VkResult VulkanDevice::CreateSwapchainResources(const std::uint32_t requestedWidth,
                                                const std::uint32_t requestedHeight,
                                                const VkSwapchainKHR oldSwapchain,
                                                VulkanSwapchainState& outState)
{
    outState = {};
    if (m_Device == VK_NULL_HANDLE || m_PhysDevice == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE ||
        requestedWidth == 0u || requestedHeight == 0u)
    {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysDevice,
                                                                m_Surface,
                                                                &surfaceCapabilities);
    if (result != VK_SUCCESS)
        return result;

    std::uint32_t surfaceFormatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDevice,
                                                  m_Surface,
                                                  &surfaceFormatCount,
                                                  nullptr);
    if (result != VK_SUCCESS || surfaceFormatCount == 0u)
        return result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result;

    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDevice,
                                                  m_Surface,
                                                  &surfaceFormatCount,
                                                  surfaceFormats.data());
    if (result != VK_SUCCESS || surfaceFormatCount == 0u)
        return result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result;
    surfaceFormats.resize(surfaceFormatCount);

    std::uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDevice,
                                                       m_Surface,
                                                       &presentModeCount,
                                                       nullptr);
    if (result != VK_SUCCESS || presentModeCount == 0u)
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDevice,
                                                       m_Surface,
                                                       &presentModeCount,
                                                       presentModes.data());
    if (result != VK_SUCCESS || presentModeCount == 0u)
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
    presentModes.resize(presentModeCount);

    const VkSurfaceFormatKHR surfaceFormat = ChooseSwapchainSurfaceFormat(surfaceFormats);
    const VkPresentModeKHR presentMode = ToVkPresentMode(m_PresentMode, presentModes);
    const VkExtent2D swapchainExtent = ChooseSwapchainExtent(
        surfaceCapabilities,
        Core::Extent2D{.Width = static_cast<int>(requestedWidth),
                       .Height = static_cast<int>(requestedHeight)});
    if (swapchainExtent.width == 0u || swapchainExtent.height == 0u)
        return VK_ERROR_OUT_OF_DATE_KHR;

    std::uint32_t desiredImageCount = surfaceCapabilities.minImageCount + 1u;
    if (surfaceCapabilities.maxImageCount > 0u && desiredImageCount > surfaceCapabilities.maxImageCount)
        desiredImageCount = surfaceCapabilities.maxImageCount;

    const std::uint32_t queueFamilyIndices[] = {m_GraphicsFamily, m_PresentFamily};
    // GRAPHICS-033D: opt into TRANSFER_SRC for the swapchain images when the
    // surface advertises it. The MinimalDebug backbuffer-to-host readback path
    // records vkCmdCopyImageToBuffer with the backbuffer as the source, which
    // requires `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` on the image. The flag is
    // commonly supported but not guaranteed by the Vulkan spec; when the
    // surface omits it we keep the prior usage set and the renderer's readback
    // hook degrades to a no-op (the gpu;vulkan smoke would then trip its own
    // operational-counter assertion instead of producing undefined results).
    VkImageUsageFlags swapchainImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u)
    {
        swapchainImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = m_Surface;
    swapchainInfo.minImageCount = desiredImageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = swapchainExtent;
    swapchainInfo.imageArrayLayers = 1u;
    swapchainInfo.imageUsage = swapchainImageUsage;
    if (m_GraphicsFamily != m_PresentFamily)
    {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = 2u;
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = oldSwapchain;

    result = vkCreateSwapchainKHR(m_Device, &swapchainInfo, nullptr, &outState.Swapchain);
    if (result != VK_SUCCESS || outState.Swapchain == VK_NULL_HANDLE)
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;

    outState.Format = surfaceFormat.format;
    outState.Extent = swapchainExtent;

    std::uint32_t swapchainImageCount = 0;
    result = vkGetSwapchainImagesKHR(m_Device, outState.Swapchain, &swapchainImageCount, nullptr);
    if (result != VK_SUCCESS || swapchainImageCount == 0u)
    {
        vkDestroySwapchainKHR(m_Device, outState.Swapchain, nullptr);
        outState.Swapchain = VK_NULL_HANDLE;
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
    }

    outState.Images.resize(swapchainImageCount);
    result = vkGetSwapchainImagesKHR(m_Device,
                                     outState.Swapchain,
                                     &swapchainImageCount,
                                     outState.Images.data());
    if (result != VK_SUCCESS || swapchainImageCount == 0u)
    {
        vkDestroySwapchainKHR(m_Device, outState.Swapchain, nullptr);
        outState = {};
        return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
    }
    outState.Images.resize(swapchainImageCount);
    outState.Views.reserve(swapchainImageCount);
    outState.Handles.reserve(swapchainImageCount);

    for (VkImage swapchainImage : outState.Images)
    {
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = outState.Format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0u;
        viewInfo.subresourceRange.levelCount = 1u;
        viewInfo.subresourceRange.baseArrayLayer = 0u;
        viewInfo.subresourceRange.layerCount = 1u;

        result = vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView);
        if (result != VK_SUCCESS || imageView == VK_NULL_HANDLE)
        {
            if (imageView != VK_NULL_HANDLE)
                vkDestroyImageView(m_Device, imageView, nullptr);
            for (VkImageView createdView : outState.Views)
            {
                if (createdView != VK_NULL_HANDLE)
                    vkDestroyImageView(m_Device, createdView, nullptr);
            }
            vkDestroySwapchainKHR(m_Device, outState.Swapchain, nullptr);
            outState = {};
            return result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result;
        }

        outState.Views.push_back(imageView);
    }

    for (std::size_t imageIndex = 0; imageIndex < outState.Images.size(); ++imageIndex)
    {
        VulkanImage importedImage{};
        importedImage.Image = outState.Images[imageIndex];
        importedImage.View = outState.Views[imageIndex];
        importedImage.Format = outState.Format;
        importedImage.RhiFormat = RHI::Format::Undefined;
        importedImage.Dimension = RHI::TextureDimension::Tex2D;
        // GRAPHICS-033D: mirror the swapchain's negotiated usage so the
        // backend-internal `HasImageUsage(image->Usage, VK_IMAGE_USAGE_*)`
        // checks (e.g. the GRAPHICS-033D backbuffer-to-host readback path)
        // honour the live TRANSFER_SRC opt-in from
        // CreateSwapchainResources().
        importedImage.Usage = swapchainImageUsage;
        importedImage.Width = outState.Extent.width;
        importedImage.Height = outState.Extent.height;
        importedImage.Depth = 1u;
        importedImage.MipLevels = 1u;
        importedImage.ArrayLayers = 1u;
        importedImage.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        importedImage.OwnsMemory = false;

        outState.Handles.push_back(m_Images.Add(std::move(importedImage)));
    }

    return VK_SUCCESS;
}

void VulkanDevice::DestroySwapchainState(VulkanSwapchainState& state)
{
    if (m_Device != VK_NULL_HANDLE)
    {
        for (const RHI::TextureHandle handle : state.Handles)
        {
            VulkanImage* image = m_Images.GetIfValid(handle);
            if (!image)
                continue;

            const VkImageView view = image->View;
            image->Image = VK_NULL_HANDLE;
            image->View = VK_NULL_HANDLE;
            image->Allocation = VK_NULL_HANDLE;
            image->CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            m_Images.Remove(handle, m_GlobalFrameNumber);

            if (view != VK_NULL_HANDLE)
                vkDestroyImageView(m_Device, view, nullptr);
        }

        if (state.Swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(m_Device, state.Swapchain, nullptr);
    }

    state = {};
}

void VulkanDevice::AdoptSwapchainState(VulkanSwapchainState&& state)
{
    m_Swapchain = state.Swapchain;
    m_SwapchainFormat = state.Format;
    m_SwapchainExtent = state.Extent;
    m_SwapchainImages = std::move(state.Images);
    m_SwapchainViews = std::move(state.Views);
    m_SwapchainHandles = std::move(state.Handles);

    state.Swapchain = VK_NULL_HANDLE;
    state.Format = VK_FORMAT_UNDEFINED;
    state.Extent = {};
}

void VulkanDevice::ResetFrameAcquisitionState() noexcept
{
    for (PerFrame& frame : m_Frames)
    {
        frame.AcquiredImageIndex = 0u;
        frame.ImageAcquiredForFrame = false;
        frame.SubmittedForPresent = false;
    }
}

bool VulkanDevice::IsOperational() const noexcept
{
    return m_Operational && ComputeOperationalPredicate();
}

void VulkanDevice::NoteRecipeGraphValidation(const bool clean) noexcept
{
    m_LatestRecipeValidationClean.store(clean, std::memory_order_relaxed);
    RefreshOperationalState();
}

RHI::ITransferQueue& VulkanDevice::GetTransferQueue()
{
    if (HasLiveOperationalPrerequisites() && m_TransferQueue && m_TransferQueue->IsValid())
        return *m_TransferQueue;
    return m_FallbackTransferQueue;
}

RHI::IBindlessHeap& VulkanDevice::GetBindlessHeap()
{
    if (IsOperational() && m_BindlessHeap && m_BindlessHeap->IsValid())
        return *m_BindlessHeap;
    return m_FallbackBindlessHeap;
}

void VulkanDevice::Initialize(Platform::IWindow& window,
                              const Core::Config::RenderConfig& config)
{
    m_ValidationEnabled = config.EnableValidation;
    m_Operational       = false;
    m_DeviceLost        = false;
    m_HasPendingResize  = false;
    m_PendingResizeExtent = {};
    m_FrameSlot         = 0;
    m_GlobalFrameNumber = 0;
    // GRAPHICS-033E: cold-start fail-closed until the renderer publishes the
    // first clean recipe-aware validation outcome.
    m_LatestRecipeValidationClean.store(false, std::memory_order_relaxed);

    if (m_Instance != VK_NULL_HANDLE || m_Surface != VK_NULL_HANDLE || m_Device != VK_NULL_HANDLE ||
        m_Swapchain != VK_NULL_HANDLE)
    {
        Shutdown();
        m_ValidationEnabled = config.EnableValidation;
        m_DeviceLost        = false;
        m_HasPendingResize  = false;
        m_PendingResizeExtent = {};
        m_FrameSlot         = 0;
        m_GlobalFrameNumber = 0;
        m_LatestRecipeValidationClean.store(false, std::memory_order_relaxed);
    }

    VulkanBootstrapDiagnosticsSnapshot diagnostics{};
    diagnostics.ValidationRequested = config.EnableValidation;
    VulkanServiceDiagnosticsSnapshot serviceDiagnostics{};
    PublishServiceDiagnostics(serviceDiagnostics);

    auto fail = [this, &diagnostics, &serviceDiagnostics](const VulkanBootstrapStatus status, const VkResult result)
    {
        diagnostics.Status = status;
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        PublishBootstrapDiagnostics(diagnostics);
        if (serviceDiagnostics.Status == VulkanServiceBootstrapStatus::NotStarted)
        {
            serviceDiagnostics.Status = VulkanServiceBootstrapStatus::SkippedNoBootstrap;
            PublishServiceDiagnostics(serviceDiagnostics);
        }
        Shutdown();
    };

    auto* glfwWindow = static_cast<GLFWwindow*>(window.GetNativeHandle());
    diagnostics.NativeWindowAvailable = glfwWindow != nullptr;
    if (!glfwWindow)
    {
        diagnostics.Status = VulkanBootstrapStatus::SkippedNoNativeWindow;
        PublishBootstrapDiagnostics(diagnostics);
        serviceDiagnostics.Status = VulkanServiceBootstrapStatus::SkippedNoBootstrap;
        PublishServiceDiagnostics(serviceDiagnostics);
        Core::Log::Warn("[VulkanDevice::Initialize] No native GLFW window is available; promoted Vulkan bootstrap skipped and device remains non-operational.");
        return;
    }

    const VkResult volkResult = volkInitialize();
    diagnostics.VolkInitialized = volkResult == VK_SUCCESS;
    if (volkResult != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::Initialize] volkInitialize failed; promoted Vulkan device remains non-operational.");
        fail(VulkanBootstrapStatus::FailedVolkInitialize, volkResult);
        return;
    }

    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    diagnostics.RequiredInstanceExtensionCount = glfwExtensionCount;
    if (glfwExtensions == nullptr || glfwExtensionCount == 0)
    {
        Core::Log::Error("[VulkanDevice::Initialize] GLFW did not provide required Vulkan instance extensions.");
        fail(VulkanBootstrapStatus::FailedRequiredInstanceExtensions, VK_ERROR_EXTENSION_NOT_PRESENT);
        return;
    }

    std::vector<const char*> instanceExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (config.EnableValidation)
    {
        diagnostics.ValidationEnabled = HasInstanceLayer(kValidationLayerName);
        if (diagnostics.ValidationEnabled)
        {
            diagnostics.DebugUtilsEnabled = HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            if (diagnostics.DebugUtilsEnabled)
                instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        else
        {
            Core::Log::Warn("[VulkanDevice::Initialize] Vulkan validation requested but VK_LAYER_KHRONOS_validation is unavailable.");
        }
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "IntrinsicEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "IntrinsicEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    const char* enabledLayer = kValidationLayerName;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    instanceInfo.enabledLayerCount = diagnostics.ValidationEnabled ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames = diagnostics.ValidationEnabled ? &enabledLayer : nullptr;

    VkResult result = vkCreateInstance(&instanceInfo, nullptr, &m_Instance);
    if (result != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::Initialize] vkCreateInstance failed; promoted Vulkan device remains non-operational.");
        fail(VulkanBootstrapStatus::FailedInstanceCreation, result);
        return;
    }
    diagnostics.InstanceCreated = true;
    volkLoadInstance(m_Instance);

    if (diagnostics.ValidationEnabled && diagnostics.DebugUtilsEnabled && vkCreateDebugUtilsMessengerEXT)
    {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = BootstrapDebugCallback;
        if (vkCreateDebugUtilsMessengerEXT(m_Instance, &messengerInfo, nullptr, &m_Messenger) != VK_SUCCESS)
            Core::Log::Warn("[VulkanDevice::Initialize] Failed to create Vulkan debug messenger; continuing bootstrap without it.");
    }

    result = glfwCreateWindowSurface(m_Instance, glfwWindow, nullptr, &m_Surface);
    if (result != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::Initialize] glfwCreateWindowSurface failed; promoted Vulkan device remains non-operational.");
        fail(VulkanBootstrapStatus::FailedSurfaceCreation, result);
        return;
    }
    diagnostics.SurfaceCreated = true;

    std::uint32_t physicalDeviceCount = 0;
    result = vkEnumeratePhysicalDevices(m_Instance, &physicalDeviceCount, nullptr);
    diagnostics.PhysicalDeviceCount = physicalDeviceCount;
    if (result != VK_SUCCESS)
    {
        Core::Log::Error("[VulkanDevice::Initialize] vkEnumeratePhysicalDevices failed; promoted Vulkan device remains non-operational.");
        fail(VulkanBootstrapStatus::FailedPhysicalDeviceEnumeration, result);
        return;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    if (physicalDeviceCount > 0)
    {
        result = vkEnumeratePhysicalDevices(m_Instance, &physicalDeviceCount, physicalDevices.data());
        diagnostics.PhysicalDeviceCount = physicalDeviceCount;
        if (result != VK_SUCCESS)
        {
            Core::Log::Error("[VulkanDevice::Initialize] vkEnumeratePhysicalDevices failed while reading devices; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedPhysicalDeviceEnumeration, result);
            return;
        }
    }

    for (VkPhysicalDevice physicalDevice : physicalDevices)
    {
        const QueueFamilyProbe queueProbe = ProbeQueueFamilies(physicalDevice, m_Surface);
        const bool swapchainExtensionSupported = HasDeviceExtension(physicalDevice, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        const bool swapchainSurfaceSupported = swapchainExtensionSupported &&
                                               HasSwapchainSurfaceSupport(physicalDevice, m_Surface);
        const RequiredDeviceFeatureProbe featureProbe = QueryRequiredDeviceFeatures(physicalDevice);
        if (!queueProbe.GraphicsFound || !queueProbe.PresentFound || !queueProbe.TransferFound ||
            !swapchainSurfaceSupported || !featureProbe.AllRequiredSupported())
        {
            continue;
        }

        m_PhysDevice = physicalDevice;
        m_GraphicsFamily = queueProbe.Graphics;
        m_PresentFamily = queueProbe.Present;
        m_TransferFamily = queueProbe.Transfer;

        diagnostics.Status = VulkanBootstrapStatus::ProbedPhysicalDevice;
        diagnostics.PhysicalDeviceSelected = true;
        diagnostics.GraphicsQueueFound = queueProbe.GraphicsFound;
        diagnostics.PresentQueueFound = queueProbe.PresentFound;
        diagnostics.TransferQueueFound = queueProbe.TransferFound;
        diagnostics.GraphicsQueueFamily = queueProbe.Graphics;
        diagnostics.PresentQueueFamily = queueProbe.Present;
        diagnostics.TransferQueueFamily = queueProbe.Transfer;
        diagnostics.SwapchainExtensionSupported = swapchainExtensionSupported;
        diagnostics.SwapchainSurfaceSupported = swapchainSurfaceSupported;
        diagnostics.DescriptorIndexingSupported = featureProbe.DescriptorIndexingSupported;
        diagnostics.TimelineSemaphoreSupported = featureProbe.TimelineSemaphoreSupported;
        diagnostics.DynamicRenderingSupported = featureProbe.DynamicRenderingSupported;
        diagnostics.BufferDeviceAddressSupported = featureProbe.BufferDeviceAddressSupported;
        diagnostics.RequiredDeviceFeaturesSupported = featureProbe.AllRequiredSupported();

        bool samplerAnisotropySupported = false;
        result = CreateBootstrapLogicalDevice(m_PhysDevice,
                                              queueProbe,
                                               featureProbe,
                                              &m_Device,
                                              &samplerAnisotropySupported);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        diagnostics.LogicalDeviceCreated = result == VK_SUCCESS && m_Device != VK_NULL_HANDLE;
        if (!diagnostics.LogicalDeviceCreated)
        {
            Core::Log::Error("[VulkanDevice::Initialize] vkCreateDevice failed for the selected Vulkan physical device; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedLogicalDeviceCreation, result);
            return;
        }

        volkLoadDevice(m_Device);
        m_SamplerAnisotropySupported = samplerAnisotropySupported;
        diagnostics.DescriptorIndexingEnabled = true;
        diagnostics.TimelineSemaphoreEnabled = true;
        diagnostics.DynamicRenderingEnabled = true;
        diagnostics.BufferDeviceAddressEnabled = true;

        vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_PresentFamily, 0, &m_PresentQueue);
        vkGetDeviceQueue(m_Device, m_TransferFamily, 0, &m_TransferVkQueue);

        diagnostics.GraphicsQueueAcquired = m_GraphicsQueue != VK_NULL_HANDLE;
        diagnostics.PresentQueueAcquired = m_PresentQueue != VK_NULL_HANDLE;
        diagnostics.TransferQueueAcquired = m_TransferVkQueue != VK_NULL_HANDLE;
        if (!diagnostics.GraphicsQueueAcquired || !diagnostics.PresentQueueAcquired ||
            !diagnostics.TransferQueueAcquired)
        {
            Core::Log::Error("[VulkanDevice::Initialize] vkGetDeviceQueue did not return all required queues; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedLogicalDeviceCreation, VK_ERROR_INITIALIZATION_FAILED);
            return;
        }

        VmaVulkanFunctions vmaFunctions{};
        vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice = m_PhysDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = m_Instance;
        allocatorInfo.pVulkanFunctions = &vmaFunctions;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        result = vmaCreateAllocator(&allocatorInfo, &m_Vma);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        diagnostics.MemoryAllocatorCreated = result == VK_SUCCESS && m_Vma != VK_NULL_HANDLE;
        if (!diagnostics.MemoryAllocatorCreated)
        {
            Core::Log::Error("[VulkanDevice::Initialize] vmaCreateAllocator failed; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedMemoryAllocatorCreation, result);
            return;
        }

        for (std::uint32_t frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
        {
            PerFrame& frame = m_Frames[frameSlot];

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = m_GraphicsFamily;

            result = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &frame.CmdPool);
            diagnostics.LastVkResult = static_cast<std::int32_t>(result);
            if (result != VK_SUCCESS || frame.CmdPool == VK_NULL_HANDLE)
            {
                Core::Log::Error("[VulkanDevice::Initialize] vkCreateCommandPool failed for per-frame Vulkan resources; promoted Vulkan device remains non-operational.");
                fail(VulkanBootstrapStatus::FailedPerFrameResourceCreation, result);
                return;
            }
            ++diagnostics.FrameCommandPoolCount;

            VkCommandBufferAllocateInfo commandBufferInfo{};
            commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferInfo.commandPool = frame.CmdPool;
            commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferInfo.commandBufferCount = 1u;

            result = vkAllocateCommandBuffers(m_Device, &commandBufferInfo, &frame.CmdBuffer);
            diagnostics.LastVkResult = static_cast<std::int32_t>(result);
            if (result != VK_SUCCESS || frame.CmdBuffer == VK_NULL_HANDLE)
            {
                Core::Log::Error("[VulkanDevice::Initialize] vkAllocateCommandBuffers failed for per-frame Vulkan resources; promoted Vulkan device remains non-operational.");
                fail(VulkanBootstrapStatus::FailedPerFrameResourceCreation, result);
                return;
            }
            ++diagnostics.FrameCommandBufferCount;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            result = vkCreateFence(m_Device, &fenceInfo, nullptr, &frame.Fence);
            diagnostics.LastVkResult = static_cast<std::int32_t>(result);
            if (result != VK_SUCCESS || frame.Fence == VK_NULL_HANDLE)
            {
                Core::Log::Error("[VulkanDevice::Initialize] vkCreateFence failed for per-frame Vulkan resources; promoted Vulkan device remains non-operational.");
                fail(VulkanBootstrapStatus::FailedPerFrameResourceCreation, result);
                return;
            }
            ++diagnostics.FrameFenceCount;

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            result = vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &frame.ImageAcquired);
            diagnostics.LastVkResult = static_cast<std::int32_t>(result);
            if (result != VK_SUCCESS || frame.ImageAcquired == VK_NULL_HANDLE)
            {
                Core::Log::Error("[VulkanDevice::Initialize] vkCreateSemaphore failed for image-acquired per-frame Vulkan resources; promoted Vulkan device remains non-operational.");
                fail(VulkanBootstrapStatus::FailedPerFrameResourceCreation, result);
                return;
            }
            ++diagnostics.FrameImageAcquiredSemaphoreCount;

            result = vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &frame.RenderDone);
            diagnostics.LastVkResult = static_cast<std::int32_t>(result);
            if (result != VK_SUCCESS || frame.RenderDone == VK_NULL_HANDLE)
            {
                Core::Log::Error("[VulkanDevice::Initialize] vkCreateSemaphore failed for render-done per-frame Vulkan resources; promoted Vulkan device remains non-operational.");
                fail(VulkanBootstrapStatus::FailedPerFrameResourceCreation, result);
                return;
            }
            ++diagnostics.FrameRenderDoneSemaphoreCount;

            m_CmdContexts[frameSlot].Bind(m_Device,
                                          frame.CmdBuffer,
                                          m_GlobalPipelineLayout,
                                          VK_NULL_HANDLE,
                                          &m_Buffers,
                                          &m_Images,
                                          &m_Pipelines,
                                          m_GraphicsFamily,
                                          m_PresentFamily,
                                          m_TransferFamily);
        }

        diagnostics.PerFrameResourcesCreated =
            diagnostics.FrameCommandPoolCount == kMaxFramesInFlight &&
            diagnostics.FrameCommandBufferCount == kMaxFramesInFlight &&
            diagnostics.FrameFenceCount == kMaxFramesInFlight &&
            diagnostics.FrameImageAcquiredSemaphoreCount == kMaxFramesInFlight &&
            diagnostics.FrameRenderDoneSemaphoreCount == kMaxFramesInFlight;

        VkSurfaceCapabilitiesKHR surfaceCapabilities{};
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysDevice,
                                                           m_Surface,
                                                           &surfaceCapabilities);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS)
        {
            Core::Log::Error("[VulkanDevice::Initialize] vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed during swapchain bring-up; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainCreation, result);
            return;
        }

        std::uint32_t surfaceFormatCount = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDevice,
                                                      m_Surface,
                                                      &surfaceFormatCount,
                                                      nullptr);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS || surfaceFormatCount == 0)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Failed to query Vulkan swapchain surface formats; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainCreation,
                 result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result);
            return;
        }

        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDevice,
                                                      m_Surface,
                                                      &surfaceFormatCount,
                                                      surfaceFormats.data());
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS || surfaceFormatCount == 0)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Failed to read Vulkan swapchain surface formats; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainCreation,
                 result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result);
            return;
        }
        surfaceFormats.resize(surfaceFormatCount);

        std::uint32_t presentModeCount = 0;
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDevice,
                                                           m_Surface,
                                                           &presentModeCount,
                                                           nullptr);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS || presentModeCount == 0)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Failed to query Vulkan swapchain present modes; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainCreation,
                 result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
            return;
        }

        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDevice,
                                                           m_Surface,
                                                           &presentModeCount,
                                                           presentModes.data());
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS || presentModeCount == 0)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Failed to read Vulkan swapchain present modes; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainCreation,
                 result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
            return;
        }
        presentModes.resize(presentModeCount);

        const VkSurfaceFormatKHR surfaceFormat = ChooseSwapchainSurfaceFormat(surfaceFormats);
        const VkPresentModeKHR presentMode = ToVkPresentMode(m_PresentMode, presentModes);
        const VkExtent2D swapchainExtent = ChooseSwapchainExtent(surfaceCapabilities,
                                                                 window.GetFramebufferExtent());

        std::uint32_t desiredImageCount = surfaceCapabilities.minImageCount + 1u;
        if (surfaceCapabilities.maxImageCount > 0u && desiredImageCount > surfaceCapabilities.maxImageCount)
            desiredImageCount = surfaceCapabilities.maxImageCount;

        const std::uint32_t queueFamilyIndices[] = {m_GraphicsFamily, m_PresentFamily};
        // GRAPHICS-033D: mirror the CreateSwapchainResources() opt-in so the
        // backbuffer-to-host readback path keeps working across the bootstrap
        // swapchain creation site too (kept in sync with the recreation path
        // for symmetry).
        VkImageUsageFlags swapchainImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u)
        {
            swapchainImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = m_Surface;
        swapchainInfo.minImageCount = desiredImageCount;
        swapchainInfo.imageFormat = surfaceFormat.format;
        swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = swapchainExtent;
        swapchainInfo.imageArrayLayers = 1u;
        swapchainInfo.imageUsage = swapchainImageUsage;
        if (m_GraphicsFamily != m_PresentFamily)
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = 2u;
            swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        result = vkCreateSwapchainKHR(m_Device, &swapchainInfo, nullptr, &m_Swapchain);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        diagnostics.SwapchainCreated = result == VK_SUCCESS && m_Swapchain != VK_NULL_HANDLE;
        if (!diagnostics.SwapchainCreated)
        {
            Core::Log::Error("[VulkanDevice::Initialize] vkCreateSwapchainKHR failed; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainCreation, result);
            return;
        }

        diagnostics.Status = VulkanBootstrapStatus::CreatedSwapchain;
        m_SwapchainFormat = surfaceFormat.format;
        m_SwapchainExtent = swapchainExtent;
        diagnostics.SwapchainWidth = swapchainExtent.width;
        diagnostics.SwapchainHeight = swapchainExtent.height;

        std::uint32_t swapchainImageCount = 0;
        result = vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &swapchainImageCount, nullptr);
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS || swapchainImageCount == 0)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Failed to query Vulkan swapchain image count; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainImageEnumeration,
                 result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
            return;
        }

        m_SwapchainImages.resize(swapchainImageCount);
        result = vkGetSwapchainImagesKHR(m_Device,
                                         m_Swapchain,
                                         &swapchainImageCount,
                                         m_SwapchainImages.data());
        diagnostics.LastVkResult = static_cast<std::int32_t>(result);
        if (result != VK_SUCCESS || swapchainImageCount == 0)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Failed to enumerate Vulkan swapchain images; promoted Vulkan device remains non-operational.");
            fail(VulkanBootstrapStatus::FailedSwapchainImageEnumeration,
                 result == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : result);
            return;
        }
        m_SwapchainImages.resize(swapchainImageCount);
        m_SwapchainViews.reserve(swapchainImageCount);
        m_SwapchainHandles.reserve(swapchainImageCount);
        diagnostics.SwapchainImagesEnumerated = true;
        diagnostics.SwapchainImageCount = swapchainImageCount;

        for (VkImage swapchainImage : m_SwapchainImages)
        {
            VkImageView imageView = VK_NULL_HANDLE;
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapchainImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_SwapchainFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0u;
            viewInfo.subresourceRange.levelCount = 1u;
            viewInfo.subresourceRange.baseArrayLayer = 0u;
            viewInfo.subresourceRange.layerCount = 1u;

            result = vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView);
            diagnostics.LastVkResult = static_cast<std::int32_t>(result);
            if (result != VK_SUCCESS || imageView == VK_NULL_HANDLE)
            {
                if (imageView != VK_NULL_HANDLE)
                    vkDestroyImageView(m_Device, imageView, nullptr);
                Core::Log::Error("[VulkanDevice::Initialize] vkCreateImageView failed for a Vulkan swapchain image; promoted Vulkan device remains non-operational.");
                fail(VulkanBootstrapStatus::FailedSwapchainImageViewCreation, result);
                return;
            }

            VulkanImage importedImage{};
            importedImage.Image = swapchainImage;
            importedImage.View = imageView;
            importedImage.Format = m_SwapchainFormat;
            importedImage.RhiFormat = RHI::Format::Undefined;
            importedImage.Dimension = RHI::TextureDimension::Tex2D;
            // GRAPHICS-033D: record the live swapchain usage (kept in sync with
            // the chosen `swapchainImageUsage` above, including the
            // TRANSFER_SRC opt-in when the surface advertises it).
            importedImage.Usage = swapchainImageUsage;
            importedImage.Width = m_SwapchainExtent.width;
            importedImage.Height = m_SwapchainExtent.height;
            importedImage.Depth = 1u;
            importedImage.MipLevels = 1u;
            importedImage.ArrayLayers = 1u;
            importedImage.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            importedImage.OwnsMemory = false;

            m_SwapchainViews.push_back(imageView);
            m_SwapchainHandles.push_back(m_Images.Add(std::move(importedImage)));
            diagnostics.SwapchainImageViewCount = static_cast<std::uint32_t>(m_SwapchainViews.size());
            diagnostics.SwapchainImageHandleCount = static_cast<std::uint32_t>(m_SwapchainHandles.size());
        }

        diagnostics.SwapchainImageViewsCreated = diagnostics.SwapchainImageViewCount == diagnostics.SwapchainImageCount;
        diagnostics.SwapchainImagesRegistered = diagnostics.SwapchainImageHandleCount == diagnostics.SwapchainImageCount;

        m_BindlessHeap = std::make_unique<VulkanBindlessHeap>(m_Device);
        serviceDiagnostics.BindlessHeapCreated = m_BindlessHeap && m_BindlessHeap->IsValid();
        serviceDiagnostics.BindlessCapacity = serviceDiagnostics.BindlessHeapCreated
            ? m_BindlessHeap->GetCapacity()
            : 0u;
        if (!serviceDiagnostics.BindlessHeapCreated)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Vulkan bindless heap creation failed; promoted Vulkan device remains non-operational.");
            diagnostics.Status = VulkanBootstrapStatus::CreatedSwapchain;
            PublishBootstrapDiagnostics(diagnostics);
            serviceDiagnostics.Status = VulkanServiceBootstrapStatus::FailedBindlessHeapCreation;
            serviceDiagnostics.PublicServicesRemainFailClosed = !m_Operational;
            PublishServiceDiagnostics(serviceDiagnostics);
            Shutdown();
            return;
        }
        m_BindlessHeap->SetTextureSamplerResolver(
            [this](const RHI::TextureHandle texture,
                   const RHI::SamplerHandle sampler,
                   VkImageView& outView,
                   VkSampler& outSampler) noexcept -> bool
            {
                const VulkanImage* image = m_Images.GetIfValid(texture);
                const VulkanSampler* vkSampler = m_Samplers.GetIfValid(sampler);
                if (!image || image->View == VK_NULL_HANDLE || !vkSampler || vkSampler->Sampler == VK_NULL_HANDLE)
                    return false;

                outView = image->View;
                outSampler = vkSampler->Sampler;
                return true;
            });

        const VkDescriptorSetLayout bindlessLayout = m_BindlessHeap->GetLayout();
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0u;
        pushConstantRange.size = RHI::MaxPushConstantBytes;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1u;
        pipelineLayoutInfo.pSetLayouts = &bindlessLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1u;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        result = vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_GlobalPipelineLayout);
        serviceDiagnostics.LastVkResult = static_cast<std::int32_t>(result);
        serviceDiagnostics.GlobalPipelineLayoutCreated = result == VK_SUCCESS &&
                                                         m_GlobalPipelineLayout != VK_NULL_HANDLE;
        if (!serviceDiagnostics.GlobalPipelineLayoutCreated)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Vulkan global pipeline layout creation failed; promoted Vulkan device remains non-operational.");
            diagnostics.Status = VulkanBootstrapStatus::CreatedSwapchain;
            PublishBootstrapDiagnostics(diagnostics);
            serviceDiagnostics.Status = VulkanServiceBootstrapStatus::FailedGlobalPipelineLayoutCreation;
            serviceDiagnostics.PublicServicesRemainFailClosed = !m_Operational;
            PublishServiceDiagnostics(serviceDiagnostics);
            Shutdown();
            return;
        }

        VulkanTransferQueue::Config transferConfig{};
        transferConfig.Device = m_Device;
        transferConfig.Vma = m_Vma;
        transferConfig.Queue = m_TransferVkQueue;
        transferConfig.QueueFamily = m_TransferFamily;
        m_TransferQueue = std::make_unique<VulkanTransferQueue>(transferConfig);
        serviceDiagnostics.TransferQueueCreated = m_TransferQueue && m_TransferQueue->IsValid();
        if (!serviceDiagnostics.TransferQueueCreated)
        {
            Core::Log::Error("[VulkanDevice::Initialize] Vulkan transfer queue creation failed; promoted Vulkan device remains non-operational.");
            diagnostics.Status = VulkanBootstrapStatus::CreatedSwapchain;
            PublishBootstrapDiagnostics(diagnostics);
            serviceDiagnostics.Status = VulkanServiceBootstrapStatus::FailedTransferQueueCreation;
            serviceDiagnostics.PublicServicesRemainFailClosed = !m_Operational;
            PublishServiceDiagnostics(serviceDiagnostics);
            Shutdown();
            return;
        }
        m_TransferQueue->m_Buffers = &m_Buffers;
        m_TransferQueue->m_Images = &m_Images;

        for (std::uint32_t frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
        {
            m_CmdContexts[frameSlot].Bind(m_Device,
                                          m_Frames[frameSlot].CmdBuffer,
                                          m_GlobalPipelineLayout,
                                          m_BindlessHeap->GetSet(),
                                          &m_Buffers,
                                          &m_Images,
                                          &m_Pipelines,
                                          m_GraphicsFamily,
                                          m_PresentFamily,
                                          m_TransferFamily);
            ++serviceDiagnostics.CommandContextRebindCount;
        }
        serviceDiagnostics.CommandContextsRebound =
            serviceDiagnostics.CommandContextRebindCount == kMaxFramesInFlight;
        RefreshOperationalState();
        serviceDiagnostics.LiveOperationalPrerequisitesReady = HasLiveOperationalPrerequisites();
        // GRAPHICS-033F: source `PublicBindlessHeapExposed` from
        // `HasOperationalSafetyPrerequisites() && IsOperational()` rather
        // than from `IsOperational()` alone. This is a correctness clarification
        // (the operational predicate already consumes the safety prereqs
        // through gate 8), not a behavior change for fail-closed paths.
        const bool safetyPrereqsReady = HasOperationalSafetyPrerequisites();
        serviceDiagnostics.OperationalSafetyPrerequisitesReady = safetyPrereqsReady;
        serviceDiagnostics.PublicBindlessHeapExposed = safetyPrereqsReady && IsOperational() &&
                                                       m_BindlessHeap && m_BindlessHeap->IsValid();
        serviceDiagnostics.PublicTransferQueueExposed = serviceDiagnostics.LiveOperationalPrerequisitesReady &&
                                                         m_TransferQueue && m_TransferQueue->IsValid();
        serviceDiagnostics.PublicServicesExposed = serviceDiagnostics.PublicBindlessHeapExposed &&
                                                   serviceDiagnostics.PublicTransferQueueExposed;
        serviceDiagnostics.PublicServicesRemainFailClosed = !serviceDiagnostics.PublicBindlessHeapExposed &&
                                                            !serviceDiagnostics.PublicTransferQueueExposed;
        serviceDiagnostics.Status = VulkanServiceBootstrapStatus::Ready;
        PublishServiceDiagnostics(serviceDiagnostics);

        diagnostics.Status = VulkanBootstrapStatus::RegisteredSwapchainImages;
        PublishBootstrapDiagnostics(diagnostics);

        Core::Log::Warn("[VulkanDevice::Initialize] Vulkan bootstrap created logical-device, queue, VMA, per-frame command/sync, swapchain image/view/handle, bindless heap, transfer queue, and global pipeline-layout state; canonical frame execution, resize/device-loss handling, and operational public-service exposure remain predicate blockers, so device remains non-operational.");
        return;
    }

    Core::Log::Error("[VulkanDevice::Initialize] No suitable Vulkan physical device with graphics/present queues and swapchain support was found.");
    serviceDiagnostics.Status = VulkanServiceBootstrapStatus::SkippedNoBootstrap;
    PublishServiceDiagnostics(serviceDiagnostics);
    fail(VulkanBootstrapStatus::FailedNoSuitablePhysicalDevice, VK_ERROR_INITIALIZATION_FAILED);
}

void VulkanDevice::Shutdown()
{
    WaitIdle();

    // Shutdown invariant: deferred-deletion queues own resources already
    // removed from m_Buffers/m_Images/m_Samplers/m_Pipelines. Any handle still
    // present in a resource pool below has not been queued for destruction, so
    // pool drain is responsible for releasing it exactly once.
    for (uint32_t frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
        FlushDeletionQueue(frameSlot);

    m_TransferQueue.reset();
    m_Profiler.reset();
    m_BindlessHeap.reset();

    const VkDevice device = m_Device;
    const VmaAllocator vma = m_Vma;

    m_Pipelines.ForEach([device](RHI::PipelineHandle, VulkanPipeline& pipeline)
    {
        if (device != VK_NULL_HANDLE)
        {
            if (pipeline.Pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, pipeline.Pipeline, nullptr);
            if (pipeline.OwnsLayout && pipeline.Layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, pipeline.Layout, nullptr);
        }
        pipeline.Pipeline = VK_NULL_HANDLE;
        pipeline.Layout = VK_NULL_HANDLE;
    });
    m_Pipelines.Clear();

    m_Samplers.ForEach([device](RHI::SamplerHandle, VulkanSampler& sampler)
    {
        if (device != VK_NULL_HANDLE && sampler.Sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, sampler.Sampler, nullptr);
        sampler.Sampler = VK_NULL_HANDLE;
    });
    m_Samplers.Clear();

    m_Images.ForEach([device, vma](RHI::TextureHandle, VulkanImage& image)
    {
        if (device != VK_NULL_HANDLE && image.View != VK_NULL_HANDLE)
            vkDestroyImageView(device, image.View, nullptr);
        if (image.OwnsMemory && vma != VK_NULL_HANDLE && image.Image != VK_NULL_HANDLE &&
            image.Allocation != VK_NULL_HANDLE)
            vmaDestroyImage(vma, image.Image, image.Allocation);
        image.Image = VK_NULL_HANDLE;
        image.View = VK_NULL_HANDLE;
        image.Allocation = VK_NULL_HANDLE;
    });
    m_Images.Clear();

    m_Buffers.ForEach([vma](RHI::BufferHandle, VulkanBuffer& buffer)
    {
        if (vma != VK_NULL_HANDLE && buffer.Buffer != VK_NULL_HANDLE &&
            buffer.Allocation != VK_NULL_HANDLE)
            vmaDestroyBuffer(vma, buffer.Buffer, buffer.Allocation);
        buffer.Buffer = VK_NULL_HANDLE;
        buffer.Allocation = VK_NULL_HANDLE;
        buffer.MappedPtr = nullptr;
    });
    m_Buffers.Clear();

    m_SwapchainHandles.clear();
    if (device != VK_NULL_HANDLE)
    {
        // Swapchain image views are registered in m_Images as non-owning-memory
        // VulkanImage records so the resource-pool drain above destroys each
        // view exactly once. m_SwapchainViews is only a raw mirror for future
        // swapchain diagnostics/acquire paths.
        if (m_Swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
        if (m_GlobalPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, m_GlobalPipelineLayout, nullptr);
    }
    m_SwapchainViews.clear();
    m_SwapchainImages.clear();
    m_Swapchain        = VK_NULL_HANDLE;
    m_SwapchainFormat  = VK_FORMAT_UNDEFINED;
    m_SwapchainExtent  = {};
    m_PendingResizeExtent = {};
    m_HasPendingResize = false;
    m_DefaultSamplerHandle = {};
    m_GlobalPipelineLayout = VK_NULL_HANDLE;
    m_SamplerAnisotropySupported = false;

    for (std::uint32_t frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
    {
        PerFrame& frame = m_Frames[frameSlot];
        if (device != VK_NULL_HANDLE)
        {
            if (frame.RenderDone != VK_NULL_HANDLE)
                vkDestroySemaphore(device, frame.RenderDone, nullptr);
            if (frame.ImageAcquired != VK_NULL_HANDLE)
                vkDestroySemaphore(device, frame.ImageAcquired, nullptr);
            if (frame.Fence != VK_NULL_HANDLE)
                vkDestroyFence(device, frame.Fence, nullptr);
            if (frame.CmdPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(device, frame.CmdPool, nullptr);
        }

        frame.CmdBuffer = VK_NULL_HANDLE;
        frame.CmdPool = VK_NULL_HANDLE;
        frame.Fence = VK_NULL_HANDLE;
        frame.ImageAcquired = VK_NULL_HANDLE;
        frame.RenderDone = VK_NULL_HANDLE;
        frame.AcquiredImageIndex = 0;
        frame.ImageAcquiredForFrame = false;
        frame.SubmittedForPresent = false;
        frame.DeletionQueue.clear();

        m_CmdContexts[frameSlot].Bind(VK_NULL_HANDLE,
                                      VK_NULL_HANDLE,
                                      VK_NULL_HANDLE,
                                      VK_NULL_HANDLE,
                                      nullptr,
                                      nullptr,
                                      nullptr);
    }

    if (m_Vma != VK_NULL_HANDLE)
        vmaDestroyAllocator(m_Vma);
    m_Vma = VK_NULL_HANDLE;

    if (m_Device != VK_NULL_HANDLE)
        vkDestroyDevice(m_Device, nullptr);
    m_Device = VK_NULL_HANDLE;
    m_GraphicsQueue = VK_NULL_HANDLE;
    m_PresentQueue = VK_NULL_HANDLE;
    m_TransferVkQueue = VK_NULL_HANDLE;
    m_GraphicsFamily = 0;
    m_PresentFamily = 0;
    m_TransferFamily = 0;

    if (m_Surface != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    m_Surface = VK_NULL_HANDLE;

    if (m_Messenger != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT)
        vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
    m_Messenger = VK_NULL_HANDLE;

    m_PhysDevice = VK_NULL_HANDLE;
    if (m_Instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_Instance, nullptr);
    m_Instance = VK_NULL_HANDLE;
    m_Operational      = false;
    m_DeviceLost       = false;
}

void VulkanDevice::WaitIdle()
{
    if (m_Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_Device);
}

bool VulkanDevice::BeginFrame(RHI::FrameHandle& outFrame)
{
    outFrame = {};

    const bool lifecycleReady = !m_DeviceLost && m_Device != VK_NULL_HANDLE && m_Swapchain != VK_NULL_HANDLE &&
        !m_SwapchainHandles.empty() && m_GraphicsQueue != VK_NULL_HANDLE &&
        m_PresentQueue != VK_NULL_HANDLE && m_FrameSlot < kMaxFramesInFlight &&
        m_Frames[m_FrameSlot].CmdPool != VK_NULL_HANDLE &&
        m_Frames[m_FrameSlot].CmdBuffer != VK_NULL_HANDLE &&
        m_Frames[m_FrameSlot].Fence != VK_NULL_HANDLE &&
        m_Frames[m_FrameSlot].ImageAcquired != VK_NULL_HANDLE &&
        m_Frames[m_FrameSlot].RenderDone != VK_NULL_HANDLE;

    if (!lifecycleReady)
    {
        const std::uint64_t count = NoteFallbackBeginFrameAttempt();
        MutateFrameLifecycleDiagnostics([this](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = (m_Device == VK_NULL_HANDLE || m_DeviceLost)
                ? VulkanFrameBeginStatus::SkippedNotOperational
                : (m_Swapchain == VK_NULL_HANDLE
                    ? VulkanFrameBeginStatus::SkippedNoSwapchain
                    : VulkanFrameBeginStatus::SkippedNoSwapchainImages);
            snapshot.LastVkResult = 0;
            snapshot.LastFrameIndex = 0;
            snapshot.LastSwapchainImageIndex = 0;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        if (count == 1)
            Core::Log::Warn("[VulkanDevice::BeginFrame] device non-operational; returning fail-closed (no frame produced)");
        return false;
    }

    PerFrame& frame = m_Frames[m_FrameSlot];
    if (frame.ImageAcquiredForFrame)
    {
        MutateFrameLifecycleDiagnostics([this, &frame](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = VulkanFrameBeginStatus::FailedAcquire;
            snapshot.LastVkResult = static_cast<std::int32_t>(VK_NOT_READY);
            snapshot.LastFrameIndex = m_FrameSlot;
            snapshot.LastSwapchainImageIndex = frame.AcquiredImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Warn("[VulkanDevice::BeginFrame] previous guarded Vulkan frame is still in flight; acquire skipped");
        return false;
    }

    VkResult result = vkWaitForFences(m_Device, 1u, &frame.Fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        MutateFrameLifecycleDiagnostics([this, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = VulkanFrameBeginStatus::FailedAcquire;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastFrameIndex = m_FrameSlot;
            snapshot.LastSwapchainImageIndex = 0;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Error("[VulkanDevice::BeginFrame] vkWaitForFences failed; guarded Vulkan frame acquire skipped");
        return false;
    }

    result = vkResetCommandPool(m_Device, frame.CmdPool, 0u);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        MutateFrameLifecycleDiagnostics([this, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = VulkanFrameBeginStatus::FailedAcquire;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastFrameIndex = m_FrameSlot;
            snapshot.LastSwapchainImageIndex = 0;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Error("[VulkanDevice::BeginFrame] vkResetCommandPool failed before acquire; guarded frame skipped");
        return false;
    }

    std::uint32_t imageIndex = 0;
    result = vkAcquireNextImageKHR(m_Device,
                                   m_Swapchain,
                                   UINT64_MAX,
                                   frame.ImageAcquired,
                                   VK_NULL_HANDLE,
                                   &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_HasPendingResize = true;
        m_PendingResizeExtent = m_SwapchainExtent;
        MutateFrameLifecycleDiagnostics([this, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = VulkanFrameBeginStatus::OutOfDate;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastFrameIndex = m_FrameSlot;
            snapshot.LastSwapchainImageIndex = 0;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        Core::Log::Warn("[VulkanDevice::BeginFrame] vkAcquireNextImageKHR reported out-of-date swapchain; guarded frame skipped");
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        NoteDeviceLostIfNeeded(result);
        MutateFrameLifecycleDiagnostics([this, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = VulkanFrameBeginStatus::FailedAcquire;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastFrameIndex = m_FrameSlot;
            snapshot.LastSwapchainImageIndex = 0;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Error("[VulkanDevice::BeginFrame] vkAcquireNextImageKHR failed; guarded frame skipped");
        return false;
    }
    if (imageIndex >= m_SwapchainHandles.size())
    {
        MutateFrameLifecycleDiagnostics([this, imageIndex](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.BeginStatus = VulkanFrameBeginStatus::FailedAcquire;
            snapshot.LastVkResult = static_cast<std::int32_t>(VK_ERROR_INITIALIZATION_FAILED);
            snapshot.LastFrameIndex = m_FrameSlot;
            snapshot.LastSwapchainImageIndex = imageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Error("[VulkanDevice::BeginFrame] acquired swapchain image index is outside registered handle range; guarded frame skipped");
        return false;
    }

    frame.AcquiredImageIndex = imageIndex;
    frame.ImageAcquiredForFrame = true;
    frame.SubmittedForPresent = false;

    m_CmdContexts[m_FrameSlot].Bind(m_Device,
                                    frame.CmdBuffer,
                                    m_GlobalPipelineLayout,
                                    m_BindlessHeap ? m_BindlessHeap->GetSet() : VK_NULL_HANDLE,
                                    &m_Buffers,
                                    &m_Images,
                                    &m_Pipelines,
                                    m_GraphicsFamily,
                                    m_PresentFamily,
                                    m_TransferFamily);

    outFrame.FrameIndex = m_FrameSlot;
    outFrame.SwapchainImageIndex = imageIndex;
    MutateFrameLifecycleDiagnostics([this, &outFrame, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
    {
        snapshot.BeginStatus = result == VK_SUBOPTIMAL_KHR
            ? VulkanFrameBeginStatus::Suboptimal
            : VulkanFrameBeginStatus::Acquired;
        snapshot.LastVkResult = static_cast<std::int32_t>(result);
        snapshot.LastFrameIndex = outFrame.FrameIndex;
        snapshot.LastSwapchainImageIndex = outFrame.SwapchainImageIndex;
        snapshot.DeviceOperational = m_Operational;
        snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
        snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
    });
    return true;
}

void VulkanDevice::EndFrame(const RHI::FrameHandle& frame)
{
    const bool lifecycleReady = !m_DeviceLost && m_Device != VK_NULL_HANDLE && m_GraphicsQueue != VK_NULL_HANDLE &&
        frame.FrameIndex < kMaxFramesInFlight;

    if (!lifecycleReady)
    {
        const std::uint64_t count = NoteFallbackEndFrameAttempt();
        MutateFrameLifecycleDiagnostics([this, &frame](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.EndStatus = VulkanFrameEndStatus::SkippedNotOperational;
            snapshot.LastVkResult = 0;
            snapshot.LastFrameIndex = frame.FrameIndex;
            snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        if (count == 1)
            Core::Log::Warn("[VulkanDevice::EndFrame] device non-operational; ignoring frame end (no rotation)");
        return;
    }

    PerFrame& perFrame = m_Frames[frame.FrameIndex];
    if (!perFrame.ImageAcquiredForFrame || perFrame.CmdBuffer == VK_NULL_HANDLE ||
        perFrame.ImageAcquired == VK_NULL_HANDLE || perFrame.RenderDone == VK_NULL_HANDLE ||
        perFrame.Fence == VK_NULL_HANDLE)
    {
        MutateFrameLifecycleDiagnostics([this, &frame](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.EndStatus = VulkanFrameEndStatus::FailedSubmit;
            snapshot.LastVkResult = static_cast<std::int32_t>(VK_NOT_READY);
            snapshot.LastFrameIndex = frame.FrameIndex;
            snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Warn("[VulkanDevice::EndFrame] guarded Vulkan frame has no acquired image or command buffer; submit skipped");
        return;
    }

    VkResult result = vkResetFences(m_Device, 1u, &perFrame.Fence);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        MutateFrameLifecycleDiagnostics([this, &frame, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.EndStatus = VulkanFrameEndStatus::FailedSubmit;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastFrameIndex = frame.FrameIndex;
            snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Error("[VulkanDevice::EndFrame] vkResetFences failed; guarded submit skipped");
        return;
    }

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1u;
    submitInfo.pWaitSemaphores = &perFrame.ImageAcquired;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &perFrame.CmdBuffer;
    submitInfo.signalSemaphoreCount = 1u;
    submitInfo.pSignalSemaphores = &perFrame.RenderDone;

    {
        std::scoped_lock lock{m_QueueMutex};
        result = vkQueueSubmit(m_GraphicsQueue, 1u, &submitInfo, perFrame.Fence);
    }
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        perFrame.SubmittedForPresent = false;
        MutateFrameLifecycleDiagnostics([this, &frame, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.EndStatus = VulkanFrameEndStatus::FailedSubmit;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastFrameIndex = frame.FrameIndex;
            snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Error("[VulkanDevice::EndFrame] vkQueueSubmit failed for guarded Vulkan frame");
        return;
    }

    perFrame.SubmittedForPresent = true;
    // Rotate from the slot returned by BeginFrame, then advance the global
    // post-EndFrame counter consumed by renderer maintenance/deferred-delete
    // code.
    m_FrameSlot = (frame.FrameIndex + 1u) % kMaxFramesInFlight;
    ++m_GlobalFrameNumber;
    MutateFrameLifecycleDiagnostics([this, &frame](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
    {
        snapshot.EndStatus = VulkanFrameEndStatus::Submitted;
        snapshot.LastVkResult = 0;
        snapshot.LastFrameIndex = frame.FrameIndex;
        snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
        snapshot.DeviceOperational = m_Operational;
        snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
        snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
    });
}

void VulkanDevice::Present(const RHI::FrameHandle& frame)
{
    const bool lifecycleReady = !m_DeviceLost && m_Device != VK_NULL_HANDLE && m_PresentQueue != VK_NULL_HANDLE &&
        m_Swapchain != VK_NULL_HANDLE && frame.FrameIndex < kMaxFramesInFlight;

    if (!lifecycleReady)
    {
        const std::uint64_t count = NoteFallbackPresentAttempt();
        MutateFrameLifecycleDiagnostics([this, &frame](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.PresentStatus = (m_Device == VK_NULL_HANDLE || m_DeviceLost)
                ? VulkanFramePresentStatus::SkippedNotOperational
                : VulkanFramePresentStatus::SkippedNoSwapchain;
            snapshot.LastVkResult = 0;
            snapshot.LastFrameIndex = frame.FrameIndex;
            snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        if (count == 1)
            Core::Log::Warn("[VulkanDevice::Present] device or swapchain non-operational; skipping presentation");
        return;
    }

    PerFrame& perFrame = m_Frames[frame.FrameIndex];
    if (!perFrame.ImageAcquiredForFrame || !perFrame.SubmittedForPresent ||
        frame.SwapchainImageIndex >= m_SwapchainHandles.size())
    {
        MutateFrameLifecycleDiagnostics([this, &frame](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.PresentStatus = VulkanFramePresentStatus::FailedPresent;
            snapshot.LastVkResult = static_cast<std::int32_t>(VK_NOT_READY);
            snapshot.LastFrameIndex = frame.FrameIndex;
            snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        });
        Core::Log::Warn("[VulkanDevice::Present] guarded Vulkan frame was not submitted or has an invalid image index; present skipped");
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &perFrame.RenderDone;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &perFrame.AcquiredImageIndex;

    VkResult result = VK_SUCCESS;
    {
        std::scoped_lock lock{m_QueueMutex};
        result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    }

    perFrame.ImageAcquiredForFrame = false;
    perFrame.SubmittedForPresent = false;

    const VulkanFramePresentStatus presentStatus = result == VK_SUCCESS
        ? VulkanFramePresentStatus::Presented
        : (result == VK_SUBOPTIMAL_KHR
            ? VulkanFramePresentStatus::Suboptimal
            : (result == VK_ERROR_OUT_OF_DATE_KHR
                ? VulkanFramePresentStatus::OutOfDate
                : VulkanFramePresentStatus::FailedPresent));

    if (presentStatus == VulkanFramePresentStatus::Suboptimal ||
        presentStatus == VulkanFramePresentStatus::OutOfDate)
    {
        m_HasPendingResize = true;
        m_PendingResizeExtent = m_SwapchainExtent;
    }
    else if (presentStatus == VulkanFramePresentStatus::FailedPresent)
    {
        NoteDeviceLostIfNeeded(result);
    }

    MutateFrameLifecycleDiagnostics([this, &frame, result, presentStatus](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
    {
        snapshot.PresentStatus = presentStatus;
        snapshot.LastVkResult = static_cast<std::int32_t>(result);
        snapshot.LastFrameIndex = frame.FrameIndex;
        snapshot.LastSwapchainImageIndex = frame.SwapchainImageIndex;
        snapshot.DeviceOperational = m_Operational;
        snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
        snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        snapshot.DeviceLost = m_DeviceLost;
        snapshot.ResizePending = m_HasPendingResize;
    });

    if (presentStatus == VulkanFramePresentStatus::FailedPresent)
    {
        Core::Log::Error("[VulkanDevice::Present] vkQueuePresentKHR failed for guarded Vulkan frame");
    }
}

void VulkanDevice::Resize(uint32_t width, uint32_t height)
{
    m_PendingResizeExtent = VkExtent2D{.width = width, .height = height};
    m_HasPendingResize = true;

    const bool lifecycleReady = !m_DeviceLost && m_Device != VK_NULL_HANDLE && m_PhysDevice != VK_NULL_HANDLE &&
        m_Surface != VK_NULL_HANDLE && m_Swapchain != VK_NULL_HANDLE && m_GraphicsQueue != VK_NULL_HANDLE &&
        m_PresentQueue != VK_NULL_HANDLE;

    if (!lifecycleReady)
    {
        NoteFallbackResizeAttempt();
        MutateFrameLifecycleDiagnostics([this, width, height](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.ResizeStatus = (m_Device == VK_NULL_HANDLE || m_DeviceLost)
                ? VulkanFrameResizeStatus::RecordedPendingNotOperational
                : VulkanFrameResizeStatus::RecordedPendingNoSwapchain;
            snapshot.LastVkResult = 0;
            snapshot.LastRequestedWidth = width;
            snapshot.LastRequestedHeight = height;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        Core::Log::Warn("[VulkanDevice::Resize] device or swapchain non-operational; recording pending extent only");
        if (m_Swapchain == VK_NULL_HANDLE)
            m_SwapchainExtent = m_PendingResizeExtent;
        return;
    }

    if (width == 0u || height == 0u)
    {
        MutateFrameLifecycleDiagnostics([this, width, height](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.ResizeStatus = VulkanFrameResizeStatus::RecordedPendingRecreate;
            snapshot.LastVkResult = 0;
            snapshot.LastRequestedWidth = width;
            snapshot.LastRequestedHeight = height;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        Core::Log::Warn("[VulkanDevice::Resize] zero-sized Vulkan resize requested; deferring swapchain recreation");
        return;
    }

    VkResult result = vkDeviceWaitIdle(m_Device);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        MutateFrameLifecycleDiagnostics([this, width, height, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.ResizeStatus = VulkanFrameResizeStatus::FailedRecreate;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastRequestedWidth = width;
            snapshot.LastRequestedHeight = height;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        Core::Log::Error("[VulkanDevice::Resize] vkDeviceWaitIdle failed before swapchain recreation");
        return;
    }

    VulkanSwapchainState newState{};
    result = CreateSwapchainResources(width, height, m_Swapchain, newState);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        DestroySwapchainState(newState);
        MutateFrameLifecycleDiagnostics([this, width, height, result](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
        {
            snapshot.ResizeStatus = VulkanFrameResizeStatus::FailedRecreate;
            snapshot.LastVkResult = static_cast<std::int32_t>(result);
            snapshot.LastRequestedWidth = width;
            snapshot.LastRequestedHeight = height;
            snapshot.DeviceOperational = m_Operational;
            snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
            snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
            snapshot.DeviceLost = m_DeviceLost;
            snapshot.ResizePending = m_HasPendingResize;
        });
        Core::Log::Error("[VulkanDevice::Resize] Vulkan swapchain recreation failed");
        return;
    }

    VulkanSwapchainState oldState{};
    oldState.Swapchain = m_Swapchain;
    oldState.Format = m_SwapchainFormat;
    oldState.Extent = m_SwapchainExtent;
    oldState.Images = std::move(m_SwapchainImages);
    oldState.Views = std::move(m_SwapchainViews);
    oldState.Handles = std::move(m_SwapchainHandles);

    AdoptSwapchainState(std::move(newState));
    DestroySwapchainState(oldState);
    ResetFrameAcquisitionState();
    m_FrameSlot = 0u;
    m_HasPendingResize = false;
    m_PendingResizeExtent = {};
    RefreshOperationalState();

    MutateFrameLifecycleDiagnostics([this, width, height](VulkanFrameLifecycleDiagnosticsSnapshot& snapshot) noexcept
    {
        snapshot.ResizeStatus = VulkanFrameResizeStatus::Recreated;
        snapshot.LastVkResult = 0;
        snapshot.LastRequestedWidth = width;
        snapshot.LastRequestedHeight = height;
        snapshot.DeviceOperational = m_Operational;
        snapshot.SwapchainAvailable = m_Swapchain != VK_NULL_HANDLE;
        snapshot.SwapchainImagesAvailable = !m_SwapchainHandles.empty();
        snapshot.DeviceLost = m_DeviceLost;
        snapshot.ResizePending = m_HasPendingResize;
    });
}

Core::Extent2D VulkanDevice::GetBackbufferExtent() const
{
    if (m_Swapchain == VK_NULL_HANDLE && m_HasPendingResize)
    {
        return Core::Extent2D{.Width = static_cast<int>(m_PendingResizeExtent.width),
                              .Height = static_cast<int>(m_PendingResizeExtent.height)};
    }

    return Core::Extent2D{.Width = static_cast<int>(m_SwapchainExtent.width),
                          .Height = static_cast<int>(m_SwapchainExtent.height)};
}

RHI::Format VulkanDevice::GetBackbufferFormat() const
{
    return ToRhiBackbufferFormat(m_SwapchainFormat);
}

void VulkanDevice::SetPresentMode(RHI::PresentMode mode)
{
    m_PresentMode = mode;
}

RHI::TextureHandle VulkanDevice::GetBackbufferHandle(const RHI::FrameHandle& frame) const
{
    if (frame.SwapchainImageIndex >= m_SwapchainHandles.size())
        return {};

    return m_SwapchainHandles[frame.SwapchainImageIndex];
}

RHI::ICommandContext& VulkanDevice::GetGraphicsContext(uint32_t frameIndex)
{
    return m_CmdContexts[frameIndex % kMaxFramesInFlight];
}

// =============================================================================
// §11a  VulkanDevice — buffer subsystem
//
// Upload path summary
// -------------------
// Host-visible buffers (HostVisible = true):
//   VMA maps them persistently into CPU address space at creation time.
//   WriteBuffer → memcpy directly into MappedPtr + offset.
//   Zero GPU work, zero command buffer, zero synchronisation needed.
//   Used for: per-entity dynamic geometry, uniform ring-buffers, staging.
//
// Device-local buffers (HostVisible = false):
//   Reside in VRAM (VMA_MEMORY_USAGE_GPU_ONLY), inaccessible by the CPU.
//   WriteBuffer → allocate a temporary host-visible staging VkBuffer,
//   memcpy the data, record vkCmdCopyBuffer in a one-shot command buffer,
//   submit to the graphics queue, and vkQueueWaitIdle to synchronise.
//   This is intentionally blocking — it is the scene-load path, not the
//   per-frame render path.  For non-blocking streaming use
//   IDevice::GetTransferQueue().UploadBuffer() which returns a TransferToken
//   and uses the StagingBelt ring-buffer on the dedicated transfer queue.
//
// BDA note: Storage buffers always get VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
// via ToVkBufferUsage.  GetBufferDeviceAddress queries
// vkGetBufferDeviceAddress and caches nothing; the BDA is stable for the
// lifetime of the VkBuffer.
// =============================================================================

namespace
{
    [[nodiscard]] VkImageType ToVkImageType(const RHI::TextureDimension dimension)
    {
        switch (dimension)
        {
        case RHI::TextureDimension::Tex1D: return VK_IMAGE_TYPE_1D;
        case RHI::TextureDimension::Tex2D: return VK_IMAGE_TYPE_2D;
        case RHI::TextureDimension::Tex3D: return VK_IMAGE_TYPE_3D;
        case RHI::TextureDimension::TexCube: return VK_IMAGE_TYPE_2D;
        }
        return VK_IMAGE_TYPE_2D;
    }

    [[nodiscard]] VkImageViewType ToVkImageViewType(const RHI::TextureDimension dimension)
    {
        switch (dimension)
        {
        case RHI::TextureDimension::Tex1D: return VK_IMAGE_VIEW_TYPE_1D;
        case RHI::TextureDimension::Tex2D: return VK_IMAGE_VIEW_TYPE_2D;
        case RHI::TextureDimension::Tex3D: return VK_IMAGE_VIEW_TYPE_3D;
        case RHI::TextureDimension::TexCube: return VK_IMAGE_VIEW_TYPE_CUBE;
        }
        return VK_IMAGE_VIEW_TYPE_2D;
    }

    [[nodiscard]] VkSampleCountFlagBits ToVkSampleCount(const std::uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    [[nodiscard]] std::uint32_t MipExtent(const std::uint32_t extent, const std::uint32_t mipLevel)
    {
        const std::uint32_t shifted = extent >> mipLevel;
        return shifted == 0 ? 1u : shifted;
    }

    [[nodiscard]] std::uint32_t FormatBlockByteSize(const VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_UNORM: return 2;
        case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32_SFLOAT: return 12;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        case VK_FORMAT_D16_UNORM: return 2;
        case VK_FORMAT_D32_SFLOAT: return 4;
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return 0;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return 8;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK: return 16;
        default: return 0;
        }
    }

    [[nodiscard]] bool IsBlockCompressedFormat(const VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] std::uint64_t RequiredUploadBytes(const VulkanImage& image,
                                                    const std::uint32_t mipLevel)
    {
        const std::uint32_t blockBytes = FormatBlockByteSize(image.Format);
        if (blockBytes == 0)
            return 0;

        const std::uint32_t width = MipExtent(image.Width, mipLevel);
        const std::uint32_t height = MipExtent(image.Height, mipLevel);
        const std::uint32_t depth = MipExtent(image.Depth, mipLevel);

        if (IsBlockCompressedFormat(image.Format))
        {
            const std::uint64_t blocksWide = (static_cast<std::uint64_t>(width) + 3u) / 4u;
            const std::uint64_t blocksHigh = (static_cast<std::uint64_t>(height) + 3u) / 4u;
            return blocksWide * blocksHigh * depth * blockBytes;
        }

        return static_cast<std::uint64_t>(width) * height * depth * blockBytes;
    }

    void ImageBarrier(VkCommandBuffer cmd,
                      VkImage image,
                      VkImageAspectFlags aspectMask,
                      std::uint32_t mipLevel,
                      std::uint32_t arrayLayer,
                      VkImageLayout oldLayout,
                      VkImageLayout newLayout,
                      VkPipelineStageFlags2 srcStage,
                      VkAccessFlags2 srcAccess,
                      VkPipelineStageFlags2 dstStage,
                      VkAccessFlags2 dstAccess)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = image;
        barrier.subresourceRange = VkImageSubresourceRange{.aspectMask = aspectMask,
                                                           .baseMipLevel = mipLevel,
                                                           .levelCount = 1,
                                                           .baseArrayLayer = arrayLayer,
                                                           .layerCount = 1};

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }
}

RHI::BufferHandle VulkanDevice::CreateBuffer(const RHI::BufferDesc& desc)
{
    if (!HasLiveOperationalPrerequisites() || desc.SizeBytes == 0)
        return {};

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = desc.SizeBytes;
    bci.usage = ToVkBufferUsage(desc.Usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    // TransferSrc always present so WriteBuffer's staging copy (dst→src) works.
    bci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    // GRAPHICS-018 §4 queue-family handling: if the transfer queue lives on a
    // different family from graphics, declare CONCURRENT sharing so transfer
    // uploads and graphics reads can touch the same buffer without explicit
    // queue-family ownership-transfer barriers between the two queues. The
    // simpler EXCLUSIVE path is preserved when families coincide.
    std::uint32_t bufferQueueFamilies[3] = {m_GraphicsFamily, m_TransferFamily, m_PresentFamily};
    std::uint32_t bufferQueueFamilyCount = 0;
    auto pushUniqueFamily = [&bufferQueueFamilies, &bufferQueueFamilyCount](std::uint32_t family)
    {
        for (std::uint32_t index = 0; index < bufferQueueFamilyCount; ++index)
        {
            if (bufferQueueFamilies[index] == family) return;
        }
        bufferQueueFamilies[bufferQueueFamilyCount++] = family;
    };
    bufferQueueFamilyCount = 0;
    pushUniqueFamily(m_GraphicsFamily);
    if (m_TransferFamily != m_GraphicsFamily) pushUniqueFamily(m_TransferFamily);
    if (bufferQueueFamilyCount > 1)
    {
        bci.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bci.queueFamilyIndexCount = bufferQueueFamilyCount;
        bci.pQueueFamilyIndices = bufferQueueFamilies;
    }
    else
    {
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VmaAllocationCreateInfo aci{};
    VmaAllocationInfo info{};

    if (desc.HostVisible)
    {
        // Persistently mapped, coherent where possible.
        aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                  | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else
    {
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    }

    VulkanBuffer buf{};
    buf.SizeBytes   = desc.SizeBytes;
    buf.HostVisible = desc.HostVisible;
    buf.HasBDA      = RHI::HasUsage(desc.Usage, RHI::BufferUsage::Storage);

    VkResult result = vmaCreateBuffer(m_Vma, &bci, &aci,
                                      &buf.Buffer, &buf.Allocation, &info);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        return {};
    }

    if (desc.HostVisible)
        buf.MappedPtr = info.pMappedData;

    if (desc.DebugName && m_ValidationEnabled)
    {
        VkDebugUtilsObjectNameInfoEXT nm{};
        nm.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nm.objectType   = VK_OBJECT_TYPE_BUFFER;
        nm.objectHandle = reinterpret_cast<uint64_t>(buf.Buffer);
        nm.pObjectName  = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &nm);
    }

    return m_Buffers.Add(std::move(buf));
}

void VulkanDevice::DestroyBuffer(RHI::BufferHandle handle)
{
    VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf) return;

    // Move the Vulkan objects out so the pool slot can be reclaimed.
    VkBuffer      vkBuf   = buf->Buffer;
    VmaAllocation vkAlloc = buf->Allocation;
    buf->Buffer     = VK_NULL_HANDLE;
    buf->Allocation = VK_NULL_HANDLE;
    buf->MappedPtr  = nullptr;

    m_Buffers.Remove(handle, m_GlobalFrameNumber);

    // Defer the actual VMA destroy until this frame's resources are safe to release.
    VmaAllocator vma = m_Vma;
    if (vma == VK_NULL_HANDLE || vkBuf == VK_NULL_HANDLE || vkAlloc == VK_NULL_HANDLE)
        return;

    DeferDelete([vma, vkBuf, vkAlloc]() mutable
    {
        vmaDestroyBuffer(vma, vkBuf, vkAlloc);
    });
}

void VulkanDevice::WriteBuffer(RHI::BufferHandle handle, const void* data,
                                uint64_t size, uint64_t offset)
{
    if (!HasLiveOperationalPrerequisites())
        return;

    if (!data || size == 0) return;
    VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf) return;
    if (offset > buf->SizeBytes || size > buf->SizeBytes - offset) return;

    if (buf->HostVisible)
    {
        // ----------------------------------------------------------------
        // Fast path: direct memcpy into the persistently-mapped pointer.
        // No GPU work, no synchronisation.
        // ----------------------------------------------------------------
        assert(buf->MappedPtr && "HostVisible buffer has null MappedPtr");
        std::memcpy(static_cast<char*>(buf->MappedPtr) + offset, data, size);
        // Note: VMA_MEMORY_USAGE_CPU_TO_GPU selects HOST_COHERENT memory when
        // available (integrated GPUs and most desktops).  On some discrete GPUs
        // that lack HOST_COHERENT the mapping is write-combined; a coherent
        // flush would be needed.  For strict portability:
        //   vmaFlushAllocation(m_Vma, buf->Allocation, offset, size);
        // This is left as a TODO for production hardening.
        return;
    }

    // ----------------------------------------------------------------
    // Slow path: device-local buffer — upload via temporary staging buffer.
    // This is synchronous (vkQueueWaitIdle).  Only used for scene loading
    // and rare CPU→GPU writes (e.g. CullingSystem::SyncGpuBuffer).
    // For async streaming use IDevice::GetTransferQueue().UploadBuffer().
    // ----------------------------------------------------------------

    // 1. Create a temporary host-visible staging buffer.
    VkBufferCreateInfo stagingCI{};
    stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size  = size;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingACI{};
    stagingACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stagingACI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                     | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer      stagingBuf{};
    VmaAllocation stagingAlloc{};
    VmaAllocationInfo stagingInfo{};

    VkResult result = vmaCreateBuffer(m_Vma, &stagingCI, &stagingACI,
                                      &stagingBuf, &stagingAlloc, &stagingInfo);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        Core::Log::Error("[VulkanDevice::WriteBuffer] Failed to allocate staging buffer");
        return;
    }

    // 2. Copy data into the staging buffer.
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    // 3. Record and submit a one-shot vkCmdCopyBuffer.
    VkCommandBuffer cmd = BeginOneShot();
    if (cmd == VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_Vma, stagingBuf, stagingAlloc);
        return;
    }
    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size      = size;
    vkCmdCopyBuffer(cmd, stagingBuf, buf->Buffer, 1, &region);
    (void)EndOneShot(cmd);  // submits + vkQueueWaitIdle → GPU work is complete when true

    // 4. The GPU has finished reading from the staging buffer — safe to destroy.
    vmaDestroyBuffer(m_Vma, stagingBuf, stagingAlloc);
}

// GRAPHICS-033D — host-visible buffer drain used by the opt-in
// `gpu;vulkan` MinimalDebug visible-triangle smoke. Mirrors the host-visible
// fast path of WriteBuffer in reverse: WaitIdle so any prior
// `vkCmdCopyImageToBuffer` recorded into the in-flight command buffer has
// finished, then memcpy out of the persistently-mapped pointer. Device-local
// buffers are not supported by this slice (would need a staging round-trip via
// a temporary host-visible buffer + EndOneShot); we log once and return so
// callers see the fallback behaviour rather than silent garbage. The smoke
// drives the only known caller and supplies a HostVisible buffer.
void VulkanDevice::ReadBuffer(RHI::BufferHandle handle, void* data,
                               uint64_t size, uint64_t offset)
{
    if (!HasLiveOperationalPrerequisites())
        return;

    if (!data || size == 0) return;
    VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf) return;
    if (offset > buf->SizeBytes || size > buf->SizeBytes - offset) return;

    if (!buf->HostVisible)
    {
        Core::Log::Warn("[VulkanDevice::ReadBuffer] device-local buffer readback is not supported in this slice; skipping");
        return;
    }

    assert(buf->MappedPtr && "HostVisible buffer has null MappedPtr");

    // WaitIdle bounds the readback to GPU-completed bytes. The in-flight
    // command buffer that wrote into this buffer is submitted by EndFrame and
    // signals through the present semaphore chain; vkDeviceWaitIdle is the
    // most defensive synchronisation primitive available without exposing
    // per-frame fences here. The smoke calls ReadBuffer once after Run() so
    // the throughput cost is irrelevant.
    if (m_Device != VK_NULL_HANDLE)
    {
        std::scoped_lock lock{m_QueueMutex};
        (void)vkDeviceWaitIdle(m_Device);
    }

    // Invalidate the non-coherent CPU cache before memcpy so the host sees
    // the GPU writes the prior copy produced. vkDeviceWaitIdle proves the
    // copy completed device-side; on memory types that lack HOST_COHERENT
    // (typical on discrete GPUs once the allocator can no longer satisfy
    // CPU_TO_GPU with a HOST_COHERENT type) the mapped pointer can still
    // reflect stale cache lines without this call. vmaInvalidateAllocation
    // is a documented no-op when the underlying memory type is
    // HOST_COHERENT, so this stays correct on the integrated/desktop hosts
    // where VMA selects HOST_COHERENT today.
    if (m_Vma != VK_NULL_HANDLE && buf->Allocation != VK_NULL_HANDLE)
    {
        const VkResult invalidateResult =
            vmaInvalidateAllocation(m_Vma, buf->Allocation, offset, size);
        if (invalidateResult != VK_SUCCESS)
        {
            Core::Log::Warn("[VulkanDevice::ReadBuffer] vmaInvalidateAllocation reported VkResult={}; readback bytes may reflect stale CPU cache on non-coherent memory.",
                            static_cast<int>(invalidateResult));
        }
    }

    std::memcpy(data, static_cast<const char*>(buf->MappedPtr) + offset, size);
}

uint64_t VulkanDevice::GetBufferDeviceAddress(RHI::BufferHandle handle) const
{
    if (!HasLiveOperationalPrerequisites())
        return 0;

    const VulkanBuffer* buf = m_Buffers.GetIfValid(handle);
    if (!buf || !buf->HasBDA) return 0;

    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buf->Buffer;
    return vkGetBufferDeviceAddress(m_Device, &info);
}

RHI::TextureHandle VulkanDevice::CreateTexture(const RHI::TextureDesc& desc)
{
    if (!HasLiveOperationalPrerequisites())
        return {};

    const VkFormat format = ToVkFormat(desc.Fmt);
    const VkImageUsageFlags usage = ToVkTextureUsage(desc.Usage);
    if (format == VK_FORMAT_UNDEFINED || usage == 0 || desc.Width == 0 || desc.Height == 0 ||
        desc.DepthOrArrayLayers == 0 || desc.MipLevels == 0)
    {
        return {};
    }

    if (desc.Dimension == RHI::TextureDimension::TexCube && desc.DepthOrArrayLayers != 6)
        return {};

    // RHI::TextureDesc::DepthOrArrayLayers maps to depth for Tex3D and to
    // array-layer count for Tex1D/Tex2D/TexCube. Cubes require exactly six
    // layers in the current RHI contract; 2D array textures are represented by
    // Tex2D with DepthOrArrayLayers > 1.
    VulkanImage image{};
    image.Format = format;
    image.RhiFormat = desc.Fmt;
    image.Dimension = desc.Dimension;
    image.Usage = usage;
    image.Width = desc.Width;
    image.Height = desc.Height;
    image.Depth = desc.Dimension == RHI::TextureDimension::Tex3D ? desc.DepthOrArrayLayers : 1u;
    image.MipLevels = desc.MipLevels;
    image.ArrayLayers = desc.Dimension == RHI::TextureDimension::Tex3D ? 1u : desc.DepthOrArrayLayers;
    image.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.OwnsMemory = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = ToVkImageType(desc.Dimension);
    imageInfo.format = format;
    imageInfo.extent = VkExtent3D{.width = desc.Width,
                                  .height = desc.Height,
                                  .depth = desc.Dimension == RHI::TextureDimension::Tex3D
                                               ? desc.DepthOrArrayLayers
                                               : 1u};
    imageInfo.mipLevels = desc.MipLevels;
    imageInfo.arrayLayers = image.ArrayLayers;
    imageInfo.samples = ToVkSampleCount(desc.SampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    // GRAPHICS-018 §4 queue-family handling: when transfer queue lives on a
    // different family from graphics, declare CONCURRENT sharing so transfer
    // uploads (TRANSFER_DST → SHADER_READ_ONLY) and graphics reads can touch
    // the same image without explicit queue-family ownership-transfer
    // barriers. EXCLUSIVE is preserved when families coincide.
    std::uint32_t imageQueueFamilies[2] = {m_GraphicsFamily, m_TransferFamily};
    if (m_TransferFamily != m_GraphicsFamily)
    {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = 2u;
        imageInfo.pQueueFamilyIndices = imageQueueFamilies;
    }
    else
    {
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.Dimension == RHI::TextureDimension::TexCube)
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkResult result = vmaCreateImage(m_Vma, &imageInfo, &allocationInfo, &image.Image, &image.Allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        Core::Log::Error("[VulkanDevice::CreateTexture] Failed to allocate image");
        return {};
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.Image;
    viewInfo.viewType = ToVkImageViewType(desc.Dimension);
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = AspectFromFormat(format);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.MipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = image.ArrayLayers;

    result = vkCreateImageView(m_Device, &viewInfo, nullptr, &image.View);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        Core::Log::Error("[VulkanDevice::CreateTexture] Failed to create image view");
        vmaDestroyImage(m_Vma, image.Image, image.Allocation);
        return {};
    }

    if (desc.DebugName && m_ValidationEnabled && vkSetDebugUtilsObjectNameEXT)
    {
        VkDebugUtilsObjectNameInfoEXT imageName{};
        imageName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        imageName.objectType = VK_OBJECT_TYPE_IMAGE;
        imageName.objectHandle = reinterpret_cast<std::uint64_t>(image.Image);
        imageName.pObjectName = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &imageName);

        VkDebugUtilsObjectNameInfoEXT viewName{};
        viewName.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        viewName.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
        viewName.objectHandle = reinterpret_cast<std::uint64_t>(image.View);
        viewName.pObjectName = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &viewName);
    }

    return m_Images.Add(std::move(image));
}

void VulkanDevice::DestroyTexture(RHI::TextureHandle handle)
{
    for (const RHI::TextureHandle swapchainHandle : m_SwapchainHandles)
    {
        if (swapchainHandle == handle)
        {
            // Backbuffer handles are owned by the swapchain, not by callers of
            // IDevice::DestroyTexture. Keep the registered image/view live until
            // Shutdown or a future internal swapchain-recreation path drains it.
            return;
        }
    }

    VulkanImage* image = m_Images.GetIfValid(handle);
    if (!image)
        return;

    const VkDevice device = m_Device;
    const VmaAllocator vma = m_Vma;
    const VkImage vkImage = image->Image;
    const VkImageView view = image->View;
    const VmaAllocation allocation = image->Allocation;
    const bool ownsMemory = image->OwnsMemory;

    image->Image = VK_NULL_HANDLE;
    image->View = VK_NULL_HANDLE;
    image->Allocation = VK_NULL_HANDLE;
    image->CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_Images.Remove(handle, m_GlobalFrameNumber);

    if (device == VK_NULL_HANDLE)
        return;

    DeferDelete([device, vma, vkImage, view, allocation, ownsMemory]() mutable
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
        if (ownsMemory && vma != VK_NULL_HANDLE && vkImage != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE)
            vmaDestroyImage(vma, vkImage, allocation);
    });
}

void VulkanDevice::WriteTexture(RHI::TextureHandle handle,
                                const void* data,
                                uint64_t dataSizeBytes,
                                uint32_t mipLevel,
                                uint32_t arrayLayer)
{
    if (!HasLiveOperationalPrerequisites())
        return;

    if (!data || dataSizeBytes == 0)
        return;

    VulkanImage* image = m_Images.GetIfValid(handle);
    if (!image || image->Image == VK_NULL_HANDLE)
        return;

    if (!HasImageUsage(image->Usage, VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Skipping upload for texture without sampled usage");
        return;
    }

    if (!HasImageUsage(image->Usage, VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Skipping upload for texture without transfer-dst usage");
        return;
    }

    if (IsDepthStencilFormat(image->Format))
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Depth-stencil uploads are not supported by the current one-shot path");
        return;
    }

    if (mipLevel >= image->MipLevels || arrayLayer >= image->ArrayLayers)
        return;

    const std::uint64_t requiredBytes = RequiredUploadBytes(*image, mipLevel);
    if (requiredBytes == 0 || dataSizeBytes != requiredBytes)
    {
        Core::Log::Warn("[VulkanDevice::WriteTexture] Upload size mismatch: expected={} actual={}",
                        requiredBytes,
                        dataSizeBytes);
        return;
    }

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = requiredBytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT
                         | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocationInfo{};

    VkResult result = vmaCreateBuffer(m_Vma,
                                      &stagingInfo,
                                      &allocationInfo,
                                      &stagingBuffer,
                                      &stagingAllocation,
                                      &stagingAllocationInfo);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        Core::Log::Error("[VulkanDevice::WriteTexture] Failed to allocate staging buffer");
        return;
    }

    std::memcpy(stagingAllocationInfo.pMappedData, data, static_cast<std::size_t>(requiredBytes));

    VkCommandBuffer cmd = BeginOneShot();
    if (cmd == VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_Vma, stagingBuffer, stagingAllocation);
        return;
    }

    const VkImageAspectFlags aspectMask = AspectFromFormat(image->Format);
    ImageBarrier(cmd,
                 image->Image,
                 aspectMask,
                 mipLevel,
                 arrayLayer,
                 image->CurrentLayout,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 image->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE
                                                                   : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 image->CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0
                                                                   : VK_ACCESS_2_MEMORY_WRITE_BIT |
                                                                         VK_ACCESS_2_MEMORY_READ_BIT,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);

    const std::uint32_t mipWidth = MipExtent(image->Width, mipLevel);
    const std::uint32_t mipHeight = MipExtent(image->Height, mipLevel);
    const std::uint32_t mipDepth = MipExtent(image->Depth, mipLevel);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.imageSubresource = VkImageSubresourceLayers{.aspectMask = aspectMask,
                                                       .mipLevel = mipLevel,
                                                       .baseArrayLayer = arrayLayer,
                                                       .layerCount = 1};
    region.imageExtent = VkExtent3D{.width = mipWidth, .height = mipHeight, .depth = mipDepth};
    vkCmdCopyBufferToImage(cmd,
                           stagingBuffer,
                           image->Image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    ImageBarrier(cmd,
                 image->Image,
                 aspectMask,
                 mipLevel,
                 arrayLayer,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_READ_BIT);

    if (EndOneShot(cmd))
    {
        image->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vmaDestroyBuffer(m_Vma, stagingBuffer, stagingAllocation);
}

RHI::SamplerHandle VulkanDevice::CreateSampler(const RHI::SamplerDesc& desc)
{
    if (!HasLiveOperationalPrerequisites())
        return {};

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = ToVkFilter(desc.MagFilter);
    samplerInfo.minFilter = ToVkFilter(desc.MinFilter);
    samplerInfo.mipmapMode = ToVkMipmapMode(desc.MipFilter);
    samplerInfo.addressModeU = ToVkAddressMode(desc.AddressU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.AddressV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.AddressW);
    samplerInfo.mipLodBias = desc.MipLodBias;
    samplerInfo.minLod = desc.MinLod;
    samplerInfo.maxLod = desc.MaxLod;
    samplerInfo.borderColor = ToVkBorderColor(desc.BorderColor);
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = desc.CompareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.Compare);

    const bool enableAnisotropy = m_SamplerAnisotropySupported && desc.MaxAnisotropy > 1.0f;
    samplerInfo.anisotropyEnable = enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = enableAnisotropy ? desc.MaxAnisotropy : 1.0f;

    VulkanSampler sampler{};
    VkResult result = vkCreateSampler(m_Device, &samplerInfo, nullptr, &sampler.Sampler);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        Core::Log::Error("[VulkanDevice::CreateSampler] Failed to create sampler");
        return {};
    }

    if (desc.DebugName && m_ValidationEnabled && vkSetDebugUtilsObjectNameEXT)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo{};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
        nameInfo.objectHandle = reinterpret_cast<std::uint64_t>(sampler.Sampler);
        nameInfo.pObjectName = desc.DebugName;
        vkSetDebugUtilsObjectNameEXT(m_Device, &nameInfo);
    }

    return m_Samplers.Add(std::move(sampler));
}

void VulkanDevice::DestroySampler(RHI::SamplerHandle handle)
{
    VulkanSampler* sampler = m_Samplers.GetIfValid(handle);
    if (!sampler)
        return;

    const VkDevice device = m_Device;
    const VkSampler vkSampler = sampler->Sampler;
    sampler->Sampler = VK_NULL_HANDLE;
    m_Samplers.Remove(handle, m_GlobalFrameNumber);

    if (device == VK_NULL_HANDLE || vkSampler == VK_NULL_HANDLE)
        return;

    DeferDelete([device, vkSampler]() mutable
    {
        vkDestroySampler(device, vkSampler, nullptr);
    });
}

RHI::PipelineHandle VulkanDevice::CreatePipeline(const RHI::PipelineDesc& desc)
{
    auto publish = [this, &desc](VulkanPipelineCreationStatus status,
                                 VkResult result,
                                 std::uint64_t shaderBytes = 0u) noexcept
    {
        VulkanPipelineDiagnosticsSnapshot snapshot{};
        snapshot.Status = status;
        snapshot.LastVkResult = static_cast<std::int32_t>(result);
        snapshot.ColorTargetCount = desc.ColorTargetCount;
        snapshot.PushConstantSize = desc.PushConstantSize;
        snapshot.ShaderBytesRead = shaderBytes;
        snapshot.SuccessfulPipelineCreations = g_SuccessfulPipelineCreations.load(std::memory_order_relaxed);
        snapshot.DeviceAvailable = m_Device != VK_NULL_HANDLE;
        snapshot.DeviceOperational = m_Operational;
        snapshot.GlobalPipelineLayoutAvailable = m_GlobalPipelineLayout != VK_NULL_HANDLE;
        snapshot.ComputePipeline = !desc.ComputeShaderPath.empty();
        PublishPipelineDiagnostics(snapshot);
    };

    if (m_Device == VK_NULL_HANDLE || m_GlobalPipelineLayout == VK_NULL_HANDLE)
    {
        NoteFallbackPipelineCreationAttempt(FallbackPipelineReason::PreBringUp);
        publish(VulkanPipelineCreationStatus::SkippedPreBringUp, VK_SUCCESS);
        Core::Log::Warn("[VulkanDevice] CreatePipeline rejected; device or global pipeline layout is not available");
        return {};
    }

    if (!ValidatePipelineDesc(desc))
    {
        NoteFallbackPipelineCreationAttempt(FallbackPipelineReason::ShaderMissing);
        publish(VulkanPipelineCreationStatus::FailedInvalidDescription, VK_SUCCESS);
        Core::Log::Warn("[VulkanDevice] CreatePipeline rejected invalid pipeline description");
        return {};
    }

    const bool isCompute = !desc.ComputeShaderPath.empty();

    if (isCompute)
    {
        const SpirvReadResult computeSpirv = ReadSpirvFile(desc.ComputeShaderPath);
        if (computeSpirv.Words.empty())
        {
            NoteFallbackPipelineCreationAttempt(FallbackPipelineReason::ShaderMissing);
            publish(VulkanPipelineCreationStatus::FailedShaderRead, VK_SUCCESS);
            Core::Log::Warn("[VulkanDevice] CreatePipeline failed to read compute SPIR-V: {}", desc.ComputeShaderPath);
            return {};
        }

        VkShaderModule computeModule = VK_NULL_HANDLE;
        VkResult result = CreateShaderModule(m_Device, computeSpirv.Words, computeModule);
        if (result != VK_SUCCESS || computeModule == VK_NULL_HANDLE)
        {
            NoteDeviceLostIfNeeded(result);
            publish(VulkanPipelineCreationStatus::FailedShaderModuleCreation, result, computeSpirv.Bytes);
            Core::Log::Error("[VulkanDevice] vkCreateShaderModule failed for compute pipeline");
            return {};
        }

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = computeModule;
        stage.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stage;
        pipelineInfo.layout = m_GlobalPipelineLayout;

        VulkanPipeline pipeline{};
        pipeline.Layout = m_GlobalPipelineLayout;
        pipeline.BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        result = vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipeline.Pipeline);
        vkDestroyShaderModule(m_Device, computeModule, nullptr);

        if (result != VK_SUCCESS || pipeline.Pipeline == VK_NULL_HANDLE)
        {
            NoteDeviceLostIfNeeded(result);
            publish(VulkanPipelineCreationStatus::FailedPipelineCreation, result, computeSpirv.Bytes);
            Core::Log::Error("[VulkanDevice] vkCreateComputePipelines failed");
            return {};
        }

        SetDebugName(m_Device,
                     VK_OBJECT_TYPE_PIPELINE,
                     reinterpret_cast<std::uint64_t>(pipeline.Pipeline),
                     desc.DebugName,
                     m_ValidationEnabled);
        g_SuccessfulPipelineCreations.fetch_add(1u, std::memory_order_relaxed);
        publish(VulkanPipelineCreationStatus::CreatedCompute, VK_SUCCESS, computeSpirv.Bytes);
        return m_Pipelines.Add(std::move(pipeline));
    }

    const SpirvReadResult vertexSpirv = ReadSpirvFile(desc.VertexShaderPath);
    const SpirvReadResult fragmentSpirv = desc.FragmentShaderPath.empty()
        ? SpirvReadResult{}
        : ReadSpirvFile(desc.FragmentShaderPath);
    const std::uint64_t shaderBytes = vertexSpirv.Bytes + fragmentSpirv.Bytes;
    if (vertexSpirv.Words.empty() || (!desc.FragmentShaderPath.empty() && fragmentSpirv.Words.empty()))
    {
        NoteFallbackPipelineCreationAttempt(FallbackPipelineReason::ShaderMissing);
        publish(VulkanPipelineCreationStatus::FailedShaderRead, VK_SUCCESS, shaderBytes);
        Core::Log::Warn("[VulkanDevice] CreatePipeline failed to read graphics SPIR-V: '{}' '{}'",
                        desc.VertexShaderPath,
                        desc.FragmentShaderPath);
        return {};
    }

    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    VkResult result = CreateShaderModule(m_Device, vertexSpirv.Words, vertexModule);
    if (result == VK_SUCCESS && !desc.FragmentShaderPath.empty())
        result = CreateShaderModule(m_Device, fragmentSpirv.Words, fragmentModule);
    if (result != VK_SUCCESS || vertexModule == VK_NULL_HANDLE ||
        (!desc.FragmentShaderPath.empty() && fragmentModule == VK_NULL_HANDLE))
    {
        NoteDeviceLostIfNeeded(result);
        if (vertexModule != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_Device, vertexModule, nullptr);
        if (fragmentModule != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_Device, fragmentModule, nullptr);
        publish(VulkanPipelineCreationStatus::FailedShaderModuleCreation, result, shaderBytes);
        Core::Log::Error("[VulkanDevice] vkCreateShaderModule failed for graphics pipeline");
        return {};
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    std::uint32_t stageCount = 1u;
    if (fragmentModule != VK_NULL_HANDLE)
    {
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";
        stageCount = 2u;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ToVkTopology(desc.PrimitiveTopology);

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1u;
    viewportState.scissorCount = 1u;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = ToVkFillMode(desc.Rasterizer.Fill);
    rasterizer.cullMode = ToVkCullMode(desc.Rasterizer.Culling);
    rasterizer.frontFace = ToVkFrontFace(desc.Rasterizer.Winding);
    rasterizer.depthBiasEnable = (desc.Rasterizer.DepthBiasConstant != 0.0f ||
                                  desc.Rasterizer.DepthBiasSlope != 0.0f) ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = desc.Rasterizer.DepthBiasConstant;
    rasterizer.depthBiasSlopeFactor = desc.Rasterizer.DepthBiasSlope;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.DepthStencil.DepthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.DepthStencil.DepthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ToVkDepthOp(desc.DepthStencil.DepthFunc);
    depthStencil.stencilTestEnable = desc.DepthStencil.StencilEnable ? VK_TRUE : VK_FALSE;

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(desc.ColorTargetCount);
    for (std::uint32_t i = 0; i < desc.ColorTargetCount; ++i)
    {
        const RHI::ColorBlendDesc& blend = desc.ColorBlend[i];
        VkPipelineColorBlendAttachmentState& attachment = colorBlendAttachments[i];
        attachment.blendEnable = blend.Enable ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = ToVkBlendFactor(blend.SrcColorFactor);
        attachment.dstColorBlendFactor = ToVkBlendFactor(blend.DstColorFactor);
        attachment.colorBlendOp = ToVkBlendOp(blend.ColorOp);
        attachment.srcAlphaBlendFactor = ToVkBlendFactor(blend.SrcAlphaFactor);
        attachment.dstAlphaBlendFactor = ToVkBlendFactor(blend.DstAlphaFactor);
        attachment.alphaBlendOp = ToVkBlendOp(blend.AlphaOp);
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<std::uint32_t>(colorBlendAttachments.size());
    colorBlend.pAttachments = colorBlendAttachments.data();

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2u;
    dynamicState.pDynamicStates = dynamicStates;

    std::vector<VkFormat> colorFormats(desc.ColorTargetCount);
    for (std::uint32_t i = 0; i < desc.ColorTargetCount; ++i)
        colorFormats[i] = ToVkFormat(desc.ColorTargetFormats[i]);
    const VkFormat depthFormat = ToVkFormat(desc.DepthTargetFormat);

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat;
    renderingInfo.stencilAttachmentFormat = HasStencilFormat(depthFormat) ? depthFormat : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = stageCount;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_GlobalPipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    VulkanPipeline pipeline{};
    pipeline.Layout = m_GlobalPipelineLayout;
    pipeline.BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    result = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &pipeline.Pipeline);
    if (fragmentModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_Device, fragmentModule, nullptr);
    vkDestroyShaderModule(m_Device, vertexModule, nullptr);

    if (result != VK_SUCCESS || pipeline.Pipeline == VK_NULL_HANDLE)
    {
        NoteDeviceLostIfNeeded(result);
        publish(VulkanPipelineCreationStatus::FailedPipelineCreation, result, shaderBytes);
        Core::Log::Error("[VulkanDevice] vkCreateGraphicsPipelines failed");
        return {};
    }

    SetDebugName(m_Device,
                 VK_OBJECT_TYPE_PIPELINE,
                 reinterpret_cast<std::uint64_t>(pipeline.Pipeline),
                 desc.DebugName,
                 m_ValidationEnabled);
    g_SuccessfulPipelineCreations.fetch_add(1u, std::memory_order_relaxed);
    publish(VulkanPipelineCreationStatus::CreatedGraphics, VK_SUCCESS, shaderBytes);
    return m_Pipelines.Add(std::move(pipeline));
}

void VulkanDevice::DestroyPipeline(RHI::PipelineHandle handle)
{
    VulkanPipeline* pipeline = m_Pipelines.GetIfValid(handle);
    if (!pipeline)
        return;

    const VkDevice device = m_Device;
    const VkPipeline vkPipeline = pipeline->Pipeline;
    const VkPipelineLayout layout = pipeline->Layout;
    const bool ownsLayout = pipeline->OwnsLayout;
    pipeline->Pipeline = VK_NULL_HANDLE;
    pipeline->Layout = VK_NULL_HANDLE;
    m_Pipelines.Remove(handle, m_GlobalFrameNumber);

    if (device == VK_NULL_HANDLE)
        return;

    if (!m_Operational)
    {
        if (vkPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, vkPipeline, nullptr);
        if (ownsLayout && layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, layout, nullptr);
        return;
    }

    DeferDelete([device, vkPipeline, layout, ownsLayout]() mutable
    {
        if (vkPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, vkPipeline, nullptr);
        if (ownsLayout && layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, layout, nullptr);
    });
}

VkCommandBuffer VulkanDevice::BeginOneShot()
{
    if (!HasLiveOperationalPrerequisites() || m_FrameSlot >= kMaxFramesInFlight)
        return VK_NULL_HANDLE;

    const VkCommandBuffer cmd = m_Frames[m_FrameSlot].CmdBuffer;
    if (cmd == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult result = vkBeginCommandBuffer(cmd, &beginInfo);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

bool VulkanDevice::EndOneShot(VkCommandBuffer cmd)
{
    if (!HasLiveOperationalPrerequisites() || cmd == VK_NULL_HANDLE)
        return false;

    VkResult result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS)
    {
        NoteDeviceLostIfNeeded(result);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    if (m_GraphicsQueue != VK_NULL_HANDLE)
    {
        result = vkQueueSubmit(m_GraphicsQueue, 1, &submit, VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
        {
            NoteDeviceLostIfNeeded(result);
            return false;
        }
        result = vkQueueWaitIdle(m_GraphicsQueue);
        if (result != VK_SUCCESS)
        {
            NoteDeviceLostIfNeeded(result);
            return false;
        }
        return true;
    }
    return false;
}

void VulkanDevice::DeferDelete(VulkanDeferredDelete fn)
{
    if (!fn)
        return;

    if (!m_Operational || m_FrameSlot >= kMaxFramesInFlight)
    {
        fn();
        return;
    }

    m_Frames[m_FrameSlot].DeletionQueue.push_back(std::move(fn));
}

void VulkanDevice::FlushDeletionQueue(uint32_t frameSlot)
{
    if (frameSlot >= kMaxFramesInFlight)
        return;

    auto& queue = m_Frames[frameSlot].DeletionQueue;
    for (auto& fn : queue)
    {
        if (fn)
            fn();
    }
    queue.clear();
}

} // namespace Extrinsic::Backends::Vulkan

