module;

#include <cstdlib>
#include <string>
#include <optional>
#include <memory>
#include "RHI.Vulkan.hpp"

module Graphics:PipelineLibrary.Impl;

import :PipelineLibrary;

import Core;
import RHI;

using namespace Core::Hash;

namespace Graphics
{
    static std::string ResolveShaderPathOrExit(const ShaderRegistry& registry, StringID key)
    {
        auto rel = registry.Get(key);
        if (!rel)
        {
            Core::Log::Error("CRITICAL: Missing shader configuration for ID: 0x{:08X}", key.Value);
            std::exit(-1);
        }
        return Core::Filesystem::GetShaderPath(*rel);
    }

    PipelineLibrary::PipelineLibrary(std::shared_ptr<RHI::VulkanDevice> device,
                                     RHI::BindlessDescriptorSystem& bindless,
                                     RHI::DescriptorLayout& globalSetLayout)
        : m_DeviceOwner(std::move(device)),
          m_Device(m_DeviceOwner.get()),
          m_Bindless(bindless),
          m_GlobalSetLayout(globalSetLayout)
    {
    }

    void PipelineLibrary::BuildDefaults(const ShaderRegistry& shaderRegistry,
                                       VkFormat swapchainFormat,
                                       VkFormat depthFormat)
    {
        // ---------------------------------------------------------------------
        // Forward pipeline (Textured + BDA)
        // ---------------------------------------------------------------------
        {
            const std::string vertPath = ResolveShaderPathOrExit(shaderRegistry, "Forward.Vert"_id);
            const std::string fragPath = ResolveShaderPathOrExit(shaderRegistry, "Forward.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {}; // Empty input layout due to BDA.
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({swapchainFormat});
            builder.SetDepthFormat(depthFormat);
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());
            builder.AddDescriptorSetLayout(m_Bindless.GetLayout());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build Forward pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_Forward] = std::move(*pipelineResult);
        }

        // ---------------------------------------------------------------------
        // Picking pipeline (ID buffer + BDA)
        // ---------------------------------------------------------------------
        {
            const std::string vertPath = ResolveShaderPathOrExit(shaderRegistry, "Picking.Vert"_id);
            const std::string fragPath = ResolveShaderPathOrExit(shaderRegistry, "Picking.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::VertexInputDescription inputLayout = {};
            RHI::PipelineBuilder builder(m_DeviceOwner);
            builder.SetShaders(&vert, &frag);
            builder.SetInputLayout(inputLayout);
            builder.SetColorFormats({VK_FORMAT_R32_UINT});
            builder.SetDepthFormat(depthFormat);
            builder.AddDescriptorSetLayout(m_GlobalSetLayout.GetHandle());

            VkPushConstantRange pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = sizeof(RHI::MeshPushConstants);
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            builder.AddPushConstantRange(pushConstant);

            auto pipelineResult = builder.Build();
            if (!pipelineResult)
            {
                Core::Log::Error("Failed to build Picking pipeline: {}", (int)pipelineResult.error());
                std::exit(1);
            }

            m_Pipelines[kPipeline_Picking] = std::move(*pipelineResult);
        }
    }

    std::optional<std::reference_wrapper<RHI::GraphicsPipeline>>
    PipelineLibrary::TryGet(Core::Hash::StringID name)
    {
        auto it = m_Pipelines.find(name);
        if (it == m_Pipelines.end() || !it->second)
            return std::nullopt;
        return *it->second;
    }

    std::optional<std::reference_wrapper<const RHI::GraphicsPipeline>>
    PipelineLibrary::TryGet(Core::Hash::StringID name) const
    {
        auto it = m_Pipelines.find(name);
        if (it == m_Pipelines.end() || !it->second)
            return std::nullopt;
        return *it->second;
    }

    RHI::GraphicsPipeline& PipelineLibrary::GetOrDie(Core::Hash::StringID name)
    {
        auto p = TryGet(name);
        if (!p)
        {
            Core::Log::Error("CRITICAL: Missing pipeline configuration for ID: 0x{:08X}", name.Value);
            std::exit(-1);
        }
        return p->get();
    }

    const RHI::GraphicsPipeline& PipelineLibrary::GetOrDie(Core::Hash::StringID name) const
    {
        auto p = TryGet(name);
        if (!p)
        {
            Core::Log::Error("CRITICAL: Missing pipeline configuration for ID: 0x{:08X}", name.Value);
            std::exit(-1);
        }
        return p->get();
    }
}
