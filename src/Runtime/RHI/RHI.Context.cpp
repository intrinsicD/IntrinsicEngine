module;

#include <GLFW/glfw3.h> // Only for glfwGetRequiredInstanceExtensions
#include <vector>
#include <iostream>

#include "RHI/RHI.Vulkan.hpp"

module Runtime.RHI.Context;
import Core.Logging;

namespace Runtime::RHI {

    // Validation Layer Name
    const std::vector<const char*> VALIDATION_LAYERS = {
        "VK_LAYER_KHRONOS_validation"
    };

    // Debug Callback Function (Standard Vulkan Boilerplate)
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) 
    {
        // Filter out noisy verbose messages if needed
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            Core::Log::Warn("[Vulkan Validation]: {}", pCallbackData->pMessage);
        } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            Core::Log::Error("[Vulkan Error]: {}", pCallbackData->pMessage);
        }
        return VK_FALSE;
    }

    VulkanContext::VulkanContext(const ContextConfig& config, Core::Windowing::Window& window) {
        // 1. Initialize Volk (Load basic function pointers)
        if (volkInitialize() != VK_SUCCESS) {
            Core::Log::Error("Failed to initialize Volk! Is Vulkan installed?");
            return;
        }

        CreateInstance(config);
        
        // 2. Load Instance functions
        volkLoadInstance(m_Instance);

        if (config.EnableValidation) {
            SetupDebugMessenger();
        }

        Core::Log::Info("Vulkan Instance Initialized.");
    }

    VulkanContext::~VulkanContext() {
        if (m_DebugMessenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
        }
        vkDestroyInstance(m_Instance, nullptr);
    }

    void VulkanContext::CreateInstance(const ContextConfig& config) {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = config.AppName.data();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Intrinsic Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3; // Target Vulkan 1.3

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        // --- Extensions ---
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        
        if (config.EnableValidation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        
        // Portability for MacOS/MoltenVK if we ever port, but good practice generally
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME); 

        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // --- Layers ---
        if (config.EnableValidation) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
            createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
            
            // Setup debug info for the CreateInstance call itself
            VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = DebugCallback;
            
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        if (result != VK_SUCCESS) {
            Core::Log::Error("Failed to create Vulkan Instance! Error Code: {}", (int)result);
        }
    }

    void VulkanContext::SetupDebugMessenger() {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;

        // Since this function is an extension, we must load it manually via vkGetInstanceProcAddr
        // But volk does this for us if we call volkLoadInstance!
        if (vkCreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS) {
            Core::Log::Error("Failed to set up debug messenger!");
        }
    }
}