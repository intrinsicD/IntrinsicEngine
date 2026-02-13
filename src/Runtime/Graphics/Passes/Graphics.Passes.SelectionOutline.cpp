module;

#include <algorithm>
#include <memory>
#include <span>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.SelectionOutline.Impl;

import :Passes.SelectionOutline;
import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import RHI;
import ECS;

using namespace Core::Hash;

namespace Graphics::Passes
{
    static void CheckVk(VkResult r, const char* what)
    {
        if (r != VK_SUCCESS)
            Core::Log::Error("SelectionOutline: {} failed (VkResult={})", what, (int)r);
    }

    void SelectionOutlinePass::Initialize(RHI::VulkanDevice& device,
                                          RHI::DescriptorAllocator& descriptorPool,
                                          RHI::DescriptorLayout&)
    {
        m_Device = &device;

        // Nearest-neighbor sampler for integer PickID texture
        VkSamplerCreateInfo samp{};
        samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp.magFilter = VK_FILTER_NEAREST;
        samp.minFilter = VK_FILTER_NEAREST;
        samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.minLod = 0.0f;
        samp.maxLod = 0.0f;
        samp.maxAnisotropy = 1.0f;
        CheckVk(vkCreateSampler(m_Device->GetLogicalDevice(), &samp, nullptr, &m_Sampler), "vkCreateSampler");

        // Descriptor set layout: binding 0 = PickID usampler2D
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        CheckVk(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        // Allocate per-frame descriptor sets
        m_DescriptorSets.resize(RHI::VulkanDevice::GetFramesInFlight());
        for (auto& set : m_DescriptorSets)
            set = descriptorPool.Allocate(m_DescriptorSetLayout);

        // Create a dummy 1x1 R32_UINT image for initial descriptor binding
        m_DummyPickId = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R32_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        // Transition dummy to SHADER_READ_ONLY_OPTIMAL
        {
            VkCommandBuffer cmd = RHI::CommandUtils::BeginSingleTimeCommands(*m_Device);
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_DummyPickId->GetHandle();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &depInfo);
            RHI::CommandUtils::EndSingleTimeCommands(*m_Device, cmd);
        }

        // Initialize descriptor bindings with the dummy image
        for (auto& set : m_DescriptorSets)
        {
            VkDescriptorImageInfo imageInfo{
                m_Sampler, m_DummyPickId->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set;
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);
        }

        // Keep the dummy alive until replaced in PostCompile.
    }

    void SelectionOutlinePass::AddPasses(RenderPassContext& ctx)
    {
        // Skip if no entities are selected or hovered
        auto& registry = ctx.Scene.GetRegistry();

        // Count selected entities and collect their PickIDs
        uint32_t selectedCount = 0;
        uint32_t selectedIds[kMaxSelectedIds] = {};
        uint32_t hoveredId = 0;
        bool hasHovered = false;

        auto selectedView = registry.view<
            ECS::Components::Selection::SelectedTag,
            ECS::Components::Selection::PickID>();

        for (auto [entity, pid] : selectedView.each())
        {
            if (selectedCount < kMaxSelectedIds)
                selectedIds[selectedCount++] = pid.Value;
        }

        auto hoveredView = registry.view<
            ECS::Components::Selection::HoveredTag,
            ECS::Components::Selection::PickID>();

        for (auto [entity, pid] : hoveredView.each())
        {
            hoveredId = pid.Value;
            hasHovered = true;
            break; // Only one hovered entity at a time
        }

        // Clear the cached handle unless we add the pass.
        m_LastPickIdHandle = {};

        // Early out: nothing to outline
        if (selectedCount == 0 && !hasHovered)
            return;

        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
            return;
        if (ctx.Resolution.width == ~0u || ctx.Resolution.height == ~0u)
            return;

        // Lazy pipeline build once we know swapchain format
        if (!m_Pipeline)
        {
            if (!m_ShaderRegistry)
            {
                Core::Log::Error("SelectionOutline: ShaderRegistry not configured.");
                return;
            }

            const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
                "Outline.Vert"_id);
            const std::string fragPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
                "Outline.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            std::shared_ptr<RHI::VulkanDevice> deviceAlias(m_Device, [](RHI::VulkanDevice*) {});
            RHI::PipelineBuilder pb(deviceAlias);
            pb.SetShaders(&vert, &frag);
            pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            pb.DisableDepthTest();
            pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            pb.EnableAlphaBlending();
            pb.SetColorFormats({ctx.SwapchainFormat});
            pb.AddDescriptorSetLayout(m_DescriptorSetLayout);

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pcr.offset = 0;
            pcr.size = 112; // 2*vec4 + float + 3*uint + 16*uint = 32 + 16 + 64 = 112
            pb.AddPushConstantRange(pcr);

            auto built = pb.Build();
            if (built)
                m_Pipeline = std::move(*built);
            else
            {
                Core::Log::Error("SelectionOutline: failed to build pipeline (VkResult={})", (int)built.error());
                return;
            }
        }

        const RGResourceHandle pickId = ctx.Blackboard.Get("PickID"_id);
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);

        if (!pickId.IsValid() || !backbuffer.IsValid())
            return;

        // Capture selection state for the execute lambda
        struct SelectionState
        {
            uint32_t SelectedCount;
            uint32_t HoveredId;
            uint32_t SelectedIds[kMaxSelectedIds];
        };

        SelectionState selState{};
        selState.SelectedCount = selectedCount;
        selState.HoveredId = hoveredId;
        std::copy_n(selectedIds, kMaxSelectedIds, selState.SelectedIds);

        ctx.Graph.AddPass<OutlinePassData>("SelectionOutline",
            [&](OutlinePassData& data, RGBuilder& builder)
            {
                // Read the PickID buffer as a sampled texture
                data.PickID = builder.Read(pickId,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                // Write (alpha-blend) onto the backbuffer, preserving existing content
                RGAttachmentInfo info{};
                info.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                info.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Backbuffer = builder.WriteColor(backbuffer, info);

                m_LastPickIdHandle = data.PickID;
            },
            [&, selState, pipeline = m_Pipeline.get()](
                const OutlinePassData& data, const RGRegistry&, VkCommandBuffer cmd)
            {
                if (!pipeline) return;
                if (!data.PickID.IsValid() || !data.Backbuffer.IsValid()) return;
                if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

                VkViewport vp{};
                vp.x = 0.0f;
                vp.y = 0.0f;
                vp.width = (float)ctx.Resolution.width;
                vp.height = (float)ctx.Resolution.height;
                vp.minDepth = 0.0f;
                vp.maxDepth = 1.0f;
                VkRect2D sc{{0, 0}, ctx.Resolution};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                VkDescriptorSet currentSet = VK_NULL_HANDLE;
                if (ctx.FrameIndex < m_DescriptorSets.size())
                    currentSet = m_DescriptorSets[ctx.FrameIndex];
                if (currentSet == VK_NULL_HANDLE) return;

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->GetLayout(),
                    0, 1, &currentSet,
                    0, nullptr);

                // Push constants matching the shader layout
                struct PushConstants
                {
                    glm::vec4 OutlineColor;
                    glm::vec4 HoverColor;
                    float OutlineWidth;
                    uint32_t SelectedCount;
                    uint32_t HoveredId;
                    uint32_t _pad;
                    uint32_t SelectedIds[kMaxSelectedIds];
                } push{};

                push.OutlineColor = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange
                push.HoverColor = glm::vec4(0.3f, 0.7f, 1.0f, 0.8f);   // Light blue, slightly transparent
                push.OutlineWidth = 2.0f;
                push.SelectedCount = selState.SelectedCount;
                push.HoveredId = selState.HoveredId;
                push._pad = 0;
                std::copy_n(selState.SelectedIds, kMaxSelectedIds, push.SelectedIds);

                vkCmdPushConstants(cmd, pipeline->GetLayout(),
                    VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);

                // Fullscreen triangle (3 vertices, no buffers)
                vkCmdDraw(cmd, 3, 1, 0, 0);
            });
    }

    void SelectionOutlinePass::PostCompile(uint32_t frameIndex,
                                           std::span<const RenderGraphDebugImage> debugImages)
    {
        if (!m_Device) return;
        if (frameIndex >= m_DescriptorSets.size()) return;
        if (!m_LastPickIdHandle.IsValid()) return;

        VkDescriptorSet currentSet = m_DescriptorSets[frameIndex];
        if (!currentSet) return;

        for (const auto& img : debugImages)
        {
            if (img.Resource == m_LastPickIdHandle.ID && img.View != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{
                    m_Sampler, img.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = currentSet;
                write.dstBinding = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfo;

                vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);
                break;
            }
        }
    }

    void SelectionOutlinePass::Shutdown()
    {
        if (!m_Device) return;
        m_DummyPickId.reset();
        if (m_DescriptorSetLayout)
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_DescriptorSetLayout, nullptr);
        if (m_Sampler)
            vkDestroySampler(m_Device->GetLogicalDevice(), m_Sampler, nullptr);
    }
}
