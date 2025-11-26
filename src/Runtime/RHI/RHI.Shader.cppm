module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>
#include <memory>

export module Runtime.RHI.Shader;

import Runtime.RHI.Device;

export namespace Runtime::RHI {
    
    enum class ShaderStage { Vertex, Fragment, Compute };

    class ShaderModule {
    public:
        ShaderModule(std::shared_ptr<VulkanDevice> device, const std::string& filepath, ShaderStage stage);
        ~ShaderModule();

        [[nodiscard]] VkShaderModule GetHandle() const { return m_Module; }
        [[nodiscard]] VkPipelineShaderStageCreateInfo GetStageInfo() const;

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkShaderModule m_Module = VK_NULL_HANDLE;
        ShaderStage m_Stage;

        std::vector<char> ReadFile(const std::string& filename);
    };
}