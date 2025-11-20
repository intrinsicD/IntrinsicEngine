module;
#include "RHI.Vulkan.hpp"
#include <fstream>
#include <vector>
#include <filesystem>

module Runtime.RHI.Shader;
import Core.Logging;

namespace Runtime::RHI {

    ShaderModule::ShaderModule(VulkanDevice& device, const std::string& filepath, ShaderStage stage)
        : m_Device(device), m_Stage(stage)
    {
        auto code = ReadFile(filepath);
        if (code.empty()) return;

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        if (vkCreateShaderModule(m_Device.GetLogicalDevice(), &createInfo, nullptr, &m_Module) != VK_SUCCESS) {
            Core::Log::Error("Failed to create shader module: {}", filepath);
        }
    }

    ShaderModule::~ShaderModule() {
        if (m_Module) vkDestroyShaderModule(m_Device.GetLogicalDevice(), m_Module, nullptr);
    }

    VkPipelineShaderStageCreateInfo ShaderModule::GetStageInfo() const {
        VkPipelineShaderStageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = (m_Stage == ShaderStage::Vertex) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        info.module = m_Module;
        info.pName = "main";
        return info;
    }

    std::vector<char> ShaderModule::ReadFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            Core::Log::Error("Failed to open shader file: {}", filename);
            return {};
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }
}