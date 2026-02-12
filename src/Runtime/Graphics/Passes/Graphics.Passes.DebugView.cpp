module;

#include <memory>
#include <span>

#include "RHI.Vulkan.hpp"

module Graphics:Passes.DebugView.Impl;

import :Passes.DebugView;
import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import RHI;
import Interface;

using namespace Core::Hash;

namespace Graphics::Passes
{
    static void CheckVk(VkResult r, const char* what)
    {
        if (r != VK_SUCCESS)
            Core::Log::Error("DebugView: {} failed (VkResult={})", what, (int)r);
    }

    void DebugViewPass::Initialize(RHI::VulkanDevice& device,
                                   RHI::DescriptorAllocator& descriptorPool,
                                   RHI::DescriptorLayout&)
    {
        m_Device = &device;

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

        VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        CheckVk(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout),
                "vkCreateDescriptorSetLayout");

        m_DescriptorSets.resize(RHI::VulkanDevice::GetFramesInFlight());
        for (auto& set : m_DescriptorSets) set = descriptorPool.Allocate(m_DescriptorSetLayout);
        m_ImGuiTextureIds.resize(RHI::VulkanDevice::GetFramesInFlight(), nullptr);

        // Dummy textures
        m_DummyFloat = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        m_DummyUint = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R32_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        m_DummyDepth = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);

        // Transition dummy images for sampling.
        {
            VkCommandBuffer cmd = RHI::CommandUtils::BeginSingleTimeCommands(*m_Device);

            VkImageMemoryBarrier2 floatBarrier{};
            floatBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            floatBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            floatBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            floatBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            floatBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            floatBarrier.image = m_DummyFloat->GetHandle();
            floatBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            floatBarrier.subresourceRange.baseMipLevel = 0;
            floatBarrier.subresourceRange.levelCount = 1;
            floatBarrier.subresourceRange.baseArrayLayer = 0;
            floatBarrier.subresourceRange.layerCount = 1;
            floatBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            floatBarrier.srcAccessMask = 0;
            floatBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            floatBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

            VkImageMemoryBarrier2 uintBarrier = floatBarrier;
            uintBarrier.image = m_DummyUint->GetHandle();

            VkImageMemoryBarrier2 depthBarrier = floatBarrier;
            depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthBarrier.image = m_DummyDepth->GetHandle();
            depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            VkImageMemoryBarrier2 barriers[] = {floatBarrier, uintBarrier, depthBarrier};
            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 3;
            depInfo.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(cmd, &depInfo);

            RHI::CommandUtils::EndSingleTimeCommands(*m_Device, cmd);
        }

        // Init descriptor bindings with dummy textures
        for (auto& set : m_DescriptorSets)
        {
            VkDescriptorImageInfo dummyFloatInfo{
                m_Sampler, m_DummyFloat->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            VkDescriptorImageInfo dummyUintInfo{
                m_Sampler, m_DummyUint->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            VkDescriptorImageInfo dummyDepthInfo{
                m_Sampler, m_DummyDepth->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            };

            VkWriteDescriptorSet writes[3] = {};

            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = set;
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &dummyFloatInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = set;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &dummyUintInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = set;
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1;
            writes[2].pImageInfo = &dummyDepthInfo;

            vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 3, writes, 0, nullptr);
        }
    }

    void DebugViewPass::AddPasses(RenderPassContext& ctx)
    {
        if (!ctx.Debug.Enabled)
            return;

        // Lazy pipeline build once we know swapchain format.
        if (!m_Pipeline)
        {
            if (!m_ShaderRegistry)
            {
                Core::Log::Error("DebugView: ShaderRegistry not configured.");
                std::exit(-1);
            }

            const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
                "Debug.Vert"_id);
            const std::string fragPath = Core::Filesystem::ResolveShaderPathOrExit(
                [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
                "Debug.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            // PipelineBuilder API takes std::shared_ptr<VulkanDevice>. We don't own the device here.
            // Use an aliasing shared_ptr with a no-op deleter to satisfy the API without changing ownership.
            std::shared_ptr<RHI::VulkanDevice> deviceAlias(m_Device, [](RHI::VulkanDevice*)
            {
            });
            RHI::PipelineBuilder pb(deviceAlias);
            pb.SetShaders(&vert, &frag);
            pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            pb.DisableDepthTest();
            pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            pb.SetColorFormats({ctx.SwapchainFormat});
            pb.AddDescriptorSetLayout(m_DescriptorSetLayout);

            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pcr.offset = 0;
            pcr.size = sizeof(int) + sizeof(float) * 2;
            pb.AddPushConstantRange(pcr);

            auto built = pb.Build();
            if (built)
                m_Pipeline = std::move(*built);
            else
                Core::Log::Error("DebugView: failed to build pipeline (VkResult={})", (int)built.error());
        }

        // Resolve selected resource by NAME from previous frame list.
        if (ctx.PrevFrameDebugImages.empty())
            return;

        const RenderGraphDebugImage* srcInfo = nullptr;
        for (const auto& img : ctx.PrevFrameDebugImages)
        {
            if (img.Name == ctx.Debug.SelectedResource && (img.Usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
            {
                srcInfo = &img;
                break;
            }
        }

        if (!srcInfo)
        {
            for (const auto& img : ctx.PrevFrameDebugImages)
            {
                if (img.Name == "PickID"_id && (img.Usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
                {
                    srcInfo = &img;
                    ctx.Debug.SelectedResource = img.Name;
                    break;
                }
            }
        }

        if (!srcInfo)
            return;

        // 1. Prepare Intermediate Image (Always needed for UI, and now source for Viewport)
        if (ctx.FrameIndex >= m_PreviewImages.size())
            m_PreviewImages.resize(ctx.FrameIndex + 1);

        auto& dbgImg = m_PreviewImages[ctx.FrameIndex];
        // Recreate if resolution changed
        if (!dbgImg || dbgImg->GetWidth() != ctx.Resolution.width || dbgImg->GetHeight() != ctx.Resolution.height)
        {
            dbgImg = std::make_unique<RHI::VulkanImage>(
                *m_Device,
                ctx.Resolution.width,
                ctx.Resolution.height,
                1,
                ctx.SwapchainFormat,
                // ADDED: TRANSFER_SRC_BIT so we can blit FROM this to the backbuffer
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);

            if (ctx.FrameIndex < m_ImGuiTextureIds.size() && m_ImGuiTextureIds[ctx.FrameIndex])
            {
                Interface::GUI::RemoveTexture(m_ImGuiTextureIds[ctx.FrameIndex]);
                m_ImGuiTextureIds[ctx.FrameIndex] = nullptr;
            }
        }

        // Only transient resources are supported by the current resolve strategy.
        RGTextureDesc srcDesc{};
        srcDesc.Width = srcInfo->Extent.width;
        srcDesc.Height = srcInfo->Extent.height;
        srcDesc.Format = srcInfo->Format;
        srcDesc.Usage = srcInfo->Usage;
        srcDesc.Aspect = srcInfo->Aspect;

        ctx.Graph.AddPass<ResolveData>("DebugViewResolve",
                                       [&](ResolveData& data, RGBuilder& builder)
                                       {
                                           // Blackboard lookup patch logic goes here
                                           RGResourceHandle srcHandle = ctx.Blackboard.Get(srcInfo->Name);
                                           if (!srcHandle.IsValid())
                                               srcHandle = builder.CreateTexture(
                                                   srcInfo->Name, srcDesc);
                                           if (!srcHandle.IsValid()) return;

                                           // Import intermediate
                                           auto dst = builder.ImportTexture(
                                               "DebugViewRGBA"_id,
                                               dbgImg->GetHandle(),
                                               dbgImg->GetView(),
                                               dbgImg->GetFormat(),
                                               ctx.Resolution,
                                               VK_IMAGE_LAYOUT_UNDEFINED);

                                           // Write to Intermediate
                                           RGAttachmentInfo info{};
                                           info.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                           info.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                           data.Dst = builder.WriteColor(dst, info);

                                           // Read Source
                                           // NOTE: We OR-in MEMORY_WRITE to force a barrier if the previous pass ended in a writable layout
                                           // (e.g., COLOR_ATTACHMENT_OPTIMAL for PickID). The RenderGraph currently allows read-after-read
                                           // accesses to skip barriers, which can leave the image in the wrong layout for sampling.
                                           data.Src = builder.Read(srcHandle,
                                                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                                           // Metadata
                                           data.SrcFormat = srcInfo->Format;
                                           data.IsDepth = (srcInfo->Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
                                           m_LastSrcHandle = data.Src;

                                           // Expose to Blackboard
                                           ctx.Blackboard.Add("DebugViewRGBA"_id, data.Dst);
                                       },
                                       [&, this](const ResolveData& data, const RGRegistry&, VkCommandBuffer cmd)
                                       {
                                           if (!m_Pipeline) return;
                                           if (!data.Dst.IsValid()) return;
                                           if (!data.Src.IsValid()) return;

                                           vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                             m_Pipeline->GetHandle());

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

                                           VkDescriptorSet currentSet = GetDescriptorSet(ctx.FrameIndex);
                                           if (currentSet == VK_NULL_HANDLE)
                                               return;

                                           vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                   m_Pipeline->GetLayout(),
                                                                   0, 1, &currentSet,
                                                                   0, nullptr);

                                           struct Push
                                           {
                                               int Mode;
                                               float DepthNear;
                                               float DepthFar;
                                           } push{};

                                           push.DepthNear = ctx.Debug.DepthNear;
                                           push.DepthFar = ctx.Debug.DepthFar;

                                           if (data.IsDepth)
                                               push.Mode = 2;
                                           else if (data.SrcFormat == VK_FORMAT_R32_UINT)
                                               push.Mode = 1;
                                           else
                                               push.Mode = 0;

                                           vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                                              VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push), &push);
                                           vkCmdDraw(cmd, 3, 1, 0, 0);
                                       });

        if (ctx.Debug.ShowInViewport)
        {
            struct BlitData
            {
                RGResourceHandle Src;
                RGResourceHandle Dst;
            };

            ctx.Graph.AddPass<BlitData>("DebugViewBlit",
                                        [&](BlitData& data, RGBuilder& builder)
                                        {
                                            const RGResourceHandle intermediate = ctx.Blackboard.
                                                Get("DebugViewRGBA"_id);
                                            const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);

                                            if (intermediate.IsValid() && backbuffer.IsValid())
                                            {
                                                // Blit requires TRANSFER_READ on source and TRANSFER_WRITE on dest
                                                data.Src = builder.Read(intermediate, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                        VK_ACCESS_2_TRANSFER_READ_BIT);
                                                data.Dst = builder.Write(
                                                    backbuffer, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
                                            }
                                        },
                                        [&](const BlitData& data, const RGRegistry& reg, VkCommandBuffer cmd)
                                        {
                                            if (!data.Src.IsValid() || !data.Dst.IsValid()) return;

                                            VkImageBlit blit{};
                                            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                            blit.srcSubresource.layerCount = 1;
                                            blit.srcOffsets[1] = {
                                                (int32_t)ctx.Resolution.width, (int32_t)ctx.Resolution.height, 1
                                            };

                                            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                            blit.dstSubresource.layerCount = 1;
                                            blit.dstOffsets[1] = {
                                                (int32_t)ctx.Resolution.width, (int32_t)ctx.Resolution.height, 1
                                            };

                                            vkCmdBlitImage(cmd,
                                                           reg.GetImage(data.Src), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                           reg.GetImage(data.Dst), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                           1, &blit, VK_FILTER_NEAREST);
                                        }
            );
        }

        // --- PASS 3: TRANSITION FOR UI (Mandatory) ---
        // Ensure "DebugViewRGBA" ends up in SHADER_READ_ONLY_OPTIMAL for ImGui
        struct TransitionData
        {
            RGResourceHandle Img;
        };
        ctx.Graph.AddPass<TransitionData>("DebugViewBarrier",
                                          [&](TransitionData& data, RGBuilder& builder)
                                          {
                                              auto handle = ctx.Blackboard.Get("DebugViewRGBA"_id);
                                              if (handle.IsValid())
                                              {
                                                  // This read ensures RenderGraph generates a barrier from whatever state it was left in
                                                  // (COLOR_ATTACHMENT if no blit, or TRANSFER_SRC if blitted) -> SHADER_READ_ONLY.
                                                  data.Img = builder.Read(
                                                      handle, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                                              }
                                          },
                                          [](const TransitionData&, const RGRegistry&, VkCommandBuffer)
                                          {
                                          }
        );
    }

    void DebugViewPass::PostCompile(uint32_t frameIndex, std::span<const RenderGraphDebugImage> debugImages)
    {
        if (!m_Device)
            return;

        // Lazily allocate descriptor sets if needed.
        if (frameIndex >= m_DescriptorSets.size())
        {
            // Cannot allocate without a descriptor allocator reference; this should be sized from RenderSystem.
            // For now, keep “null implies disabled”.
            return;
        }

        VkDescriptorSet currentSet = m_DescriptorSets[frameIndex];
        if (currentSet && m_LastSrcHandle.IsValid())
        {
            for (const auto& img : debugImages)
            {
                if (img.Resource == m_LastSrcHandle.ID && img.View != VK_NULL_HANDLE)
                {
                    VkDescriptorImageInfo imageInfo{m_Sampler, img.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

                    uint32_t targetBinding = 0;
                    if (img.Format == VK_FORMAT_R32_UINT)
                        targetBinding = 1;
                    else if ((img.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
                        targetBinding = 2;

                    VkWriteDescriptorSet write{};
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = currentSet;
                    write.dstBinding = targetBinding;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.descriptorCount = 1;
                    write.pImageInfo = &imageInfo;

                    vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);
                    break;
                }
            }
        }

        if (frameIndex >= m_ImGuiTextureIds.size()) m_ImGuiTextureIds.resize(frameIndex + 1, nullptr);

        if (frameIndex < m_PreviewImages.size() && m_PreviewImages[frameIndex])
        {
            // Only create if not already cached.
            // This prevents destroying/recreating the descriptor set every frame (Flickering Fix).
            if (m_ImGuiTextureIds[frameIndex] == nullptr)
            {
                m_ImGuiTextureIds[frameIndex] = Interface::GUI::AddTexture(
                    m_Sampler,
                    m_PreviewImages[frameIndex]->GetView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }

    void DebugViewPass::OnResize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        // Device is idle when this is called from RenderSystem::OnResize

        // 1. Clear ImGui Descriptors
        for (void* texId : m_ImGuiTextureIds)
        {
            if (texId) Interface::GUI::RemoveTexture(texId);
        }
        std::fill(m_ImGuiTextureIds.begin(), m_ImGuiTextureIds.end(), nullptr);

        // 2. Clear Images
        m_PreviewImages.clear();
    }

    void DebugViewPass::Shutdown()
    {
        for (void* texId : m_ImGuiTextureIds)
        {
            if (texId) Interface::GUI::RemoveTexture(texId);
        }
        m_ImGuiTextureIds.clear();

        if (!m_Device) return;
        if (m_DescriptorSetLayout)
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_DescriptorSetLayout,
                                         nullptr);
        if (m_Sampler) vkDestroySampler(m_Device->GetLogicalDevice(), m_Sampler, nullptr);
    }

    VkDescriptorSet DebugViewPass::GetDescriptorSet(uint32_t frameIndex) const
    {
        if (frameIndex >= m_DescriptorSets.size())
            return VK_NULL_HANDLE;
        return m_DescriptorSets[frameIndex];
    }

    void* DebugViewPass::GetImGuiTextureId(uint32_t frameIndex) const
    {
        if (frameIndex >= m_ImGuiTextureIds.size()) return nullptr;
        return m_ImGuiTextureIds[frameIndex];
    }
}
