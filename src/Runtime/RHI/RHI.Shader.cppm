module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>

export module RHI:Shader;

import :Device;

export namespace RHI {
    
    enum class ShaderStage { Vertex, Fragment, Compute };

    class ShaderModule {
    public:
        ShaderModule(VulkanDevice& device, const std::string& filepath, ShaderStage stage);
        ~ShaderModule();

        [[nodiscard]] VkShaderModule GetHandle() const { return m_Module; }
        [[nodiscard]] VkPipelineShaderStageCreateInfo GetStageInfo() const;

    private:
        VulkanDevice& m_Device;
        VkShaderModule m_Module = VK_NULL_HANDLE;
        ShaderStage m_Stage;

        std::vector<char> ReadFile(const std::string& filename);
    };
}