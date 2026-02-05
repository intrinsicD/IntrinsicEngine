module;

#include <cstdlib>
#include <string>
#include <optional>
#include <memory>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:PipelineLibrary.Impl;

import :PipelineLibrary;

import Core;
import RHI;

using namespace Core::Hash;

namespace Graphics
{
    PipelineLibrary::PipelineLibrary(std::shared_ptr<RHI::VulkanDevice> device,
                                     RHI::BindlessDescriptorSystem& bindless,
                                     RHI::DescriptorLayout& globalSetLayout)
        : m_DeviceOwner(std::move(device)),
          m_Device(m_DeviceOwner.get()),
          m_Bindless(bindless),
          m_GlobalSetLayout(globalSetLayout)
    {
    }

    PipelineLibrary::~PipelineLibrary()
    {
        if (!m_Device) return;

        VkDevice logicalDevice = m_Device->GetLogicalDevice();
        if (logicalDevice == VK_NULL_HANDLE) return;

        // Clear pipelines first (they reference the layouts).
        m_Pipelines.clear();
        m_CullPipeline.reset();
        m_SceneUpdatePipeline.reset();

        // Destroy descriptor set layouts created in BuildDefaults().
        if (m_Stage1InstanceSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(logicalDevice, m_Stage1InstanceSetLayout, nullptr);
            m_Stage1InstanceSetLayout = VK_NULL_HANDLE;
        }

        if (m_CullSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(logicalDevice, m_CullSetLayout, nullptr);
            m_CullSetLayout = VK_NULL_HANDLE;
        }

        if (m_SceneUpdateSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(logicalDevice, m_SceneUpdateSetLayout, nullptr);
            m_SceneUpdateSetLayout = VK_NULL_HANDLE;
        }
    }

    void PipelineLibrary::BuildDefaults(const ShaderRegistry& shaderRegistry,
                                       VkFormat swapchainFormat,
                                       VkFormat depthFormat)
    {
        // ---------------------------------------------------------------------
        // Forward pipeline (Textured + BDA)
        // ---------------------------------------------------------------------
        {
            const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); },
                "Forward.Vert"_id);
            const std::string fragPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); },
                "Forward.Frag"_id);

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

            // Stage 1: per-frame instance/visibility SSBOs (set = 2).
            // Create this layout once and reuse it (must match the pipeline layout exactly).
            if (m_Stage1InstanceSetLayout == VK_NULL_HANDLE)
            {
                VkDescriptorSetLayoutBinding bindings[2]{};
                bindings[0].binding = 0;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

                bindings[1].binding = 1;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 2;
                layoutInfo.pBindings = bindings;

                VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr,
                                                    &m_Stage1InstanceSetLayout));

                // NOTE: Do NOT SafeDestroy() this layout. SafeDestroy schedules deletion a frame later,
                // which will invalidate descriptor allocations while the pipeline (and ForwardPass) are still live.
            }

            builder.AddDescriptorSetLayout(m_Stage1InstanceSetLayout);

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
            const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); },
                "Picking.Vert"_id);
            const std::string fragPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); },
                "Picking.Frag"_id);

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

        // ---------------------------------------------------------------------
        // GPUScene: Scatter update pipeline
        // ---------------------------------------------------------------------
        {
            if (m_SceneUpdateSetLayout == VK_NULL_HANDLE)
            {
                VkDescriptorSetLayoutBinding bindings[3]{};

                // binding 0: Updates
                bindings[0].binding = 0;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 1: Scene instances (SSBO)
                bindings[1].binding = 1;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 2: Bounds (SSBO)
                bindings[2].binding = 2;
                bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[2].descriptorCount = 1;
                bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 3;
                layoutInfo.pBindings = bindings;

                VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_SceneUpdateSetLayout));
            }

            if (!m_SceneUpdatePipeline)
            {
                const std::string compPath = Core::Filesystem::ResolveShaderPathOrExit(
                    [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); },
                    "SceneUpdate.Comp"_id);

                RHI::ShaderModule comp(*m_Device, compPath, RHI::ShaderStage::Compute);

                RHI::ComputePipelineBuilder cb(m_DeviceOwner);
                cb.SetShader(&comp);
                cb.AddDescriptorSetLayout(m_SceneUpdateSetLayout);

                VkPushConstantRange pcr{};
                pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pcr.offset = 0;
                pcr.size = sizeof(uint32_t) * 4; // UpdateCount + padding
                cb.AddPushConstantRange(pcr);

                auto built = cb.Build();
                if (!built)
                {
                    Core::Log::Error("Failed to build SceneUpdate compute pipeline: {}", (int)built.error());
                    std::exit(1);
                }
                m_SceneUpdatePipeline = std::move(*built);
            }
        }

        // ---------------------------------------------------------------------
        // Stage 3: Compute culling pipeline
        // ---------------------------------------------------------------------
        {
            if (m_CullSetLayout == VK_NULL_HANDLE)
            {
                VkDescriptorSetLayoutBinding bindings[7]{};
                // binding 1: Instances
                bindings[0].binding = 1;
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[0].descriptorCount = 1;
                bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 2: Bounds
                bindings[1].binding = 2;
                bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[1].descriptorCount = 1;
                bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 3: GeometryIndexCount table
                bindings[2].binding = 3;
                bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[2].descriptorCount = 1;
                bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 4: HandleToDense routing table
                bindings[3].binding = 4;
                bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[3].descriptorCount = 1;
                bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 5: IndirectOut
                bindings[4].binding = 5;
                bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[4].descriptorCount = 1;
                bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 6: VisibilityOut
                bindings[5].binding = 6;
                bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[5].descriptorCount = 1;
                bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                // binding 7: DrawCounts
                bindings[6].binding = 7;
                bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[6].descriptorCount = 1;
                bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

                VkDescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layoutInfo.bindingCount = 7;
                layoutInfo.pBindings = bindings;

                VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_CullSetLayout));
            }

            if (!m_CullPipeline)
            {
                const std::string compPath = Core::Filesystem::ResolveShaderPathOrExit(
                    [&](Core::Hash::StringID id) { return shaderRegistry.Get(id); },
                    "Cull.Comp"_id);

                RHI::ShaderModule comp(*m_Device, compPath, RHI::ShaderStage::Compute);

                RHI::ComputePipelineBuilder cb(m_DeviceOwner);
                cb.SetShader(&comp);
                cb.AddDescriptorSetLayout(m_CullSetLayout);

                VkPushConstantRange pcr{};
                pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pcr.offset = 0;
                pcr.size = sizeof(glm::vec4) * 6 + sizeof(uint32_t) * 4;
                cb.AddPushConstantRange(pcr);

                auto built = cb.Build();
                if (!built)
                {
                    Core::Log::Error("Failed to build Cull compute pipeline: {}", (int)built.error());
                    std::exit(1);
                }
                m_CullPipeline = std::move(*built);
            }
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
