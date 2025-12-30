module;

#include <string_view>
#include "RHI.Vulkan.hpp"

export module RHI:Context;

import Core; // We need this to know required extensions

namespace RHI {

    export struct ContextConfig {
        std::string_view AppName = "Intrinsic Engine";
        bool EnableValidation = true;
        bool Headless = false;
    };

    export class VulkanContext {
    public:
        explicit VulkanContext(const ContextConfig& config);
        ~VulkanContext();

        // No copy
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        [[nodiscard]] VkInstance GetInstance() const { return m_Instance; }

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        
        // Internal helpers
        void CreateInstance(const ContextConfig& config);
        void SetupDebugMessenger();
    };
}