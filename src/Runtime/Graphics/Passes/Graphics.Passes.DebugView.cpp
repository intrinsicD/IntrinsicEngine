module;

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"


module Graphics.Passes.DebugView;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;

import Core.Hash;
import Core.Logging;
import Core.Filesystem;

import RHI.Descriptors;
import RHI.Buffer;
import RHI.CommandUtils;
import RHI.Device;
import RHI.Image;
import RHI.Pipeline;
import RHI.Shader;
import Interface;

import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;


namespace Graphics::Passes
{
    void DebugViewPass::ReleaseImGuiTextures() noexcept
    {
        for (void*& texId : m_ImGuiTextureIds)
        {
            if (!texId)
                continue;

            Interface::GUI::RemoveTexture(texId);
            texId = nullptr;
        }
    }

    void DebugViewPass::Initialize(RHI::VulkanDevice& device,
                                   RHI::DescriptorAllocator& descriptorPool,
                                   RHI::DescriptorLayout&)
    {
        m_Device = &device;

        m_Sampler = CreateNearestSampler(m_Device->GetLogicalDevice(), "DebugView");

        VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        CheckVkResult(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout),
                      "DebugView", "vkCreateDescriptorSetLayout");

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
        TransitionImageToShaderRead(*m_Device, m_DummyFloat->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionImageToShaderRead(*m_Device, m_DummyUint->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionImageToShaderRead(*m_Device, m_DummyDepth->GetHandle(), VK_IMAGE_ASPECT_DEPTH_BIT,
                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        // Init descriptor bindings with dummy textures
        for (auto& set : m_DescriptorSets)
        {
            UpdateImageDescriptor(m_Device->GetLogicalDevice(), set, 0,
                                  m_Sampler, m_DummyFloat->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            UpdateImageDescriptor(m_Device->GetLogicalDevice(), set, 1,
                                  m_Sampler, m_DummyUint->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            UpdateImageDescriptor(m_Device->GetLogicalDevice(), set, 2,
                                  m_Sampler, m_DummyDepth->GetView(),
                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }
    }

    void DebugViewPass::AddPasses(RenderPassContext& ctx)
    {
        if (!ctx.Debug.Enabled)
            return;

        m_LastSrcHandle = {};
        m_LastSrcFormat = VK_FORMAT_UNDEFINED;
        m_LastSrcAspect = 0;

        // Lazy pipeline build once we know swapchain format.
        if (!m_Pipeline)
        {
            if (!m_ShaderRegistry)
            {
                Core::Log::Error("DebugView: ShaderRegistry not configured.");
                std::exit(-1);
            }

            const auto [vertPath, fragPath] = ResolveShaderPaths(
                *m_ShaderRegistry,
                "Debug.Vert"_id,
                "Debug.Frag"_id);

            RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
            RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

            RHI::PipelineBuilder pb(MakeDeviceAlias(m_Device));
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

        const auto resolveCurrentHandle = [&](Core::Hash::StringID name) -> RGResourceHandle
        {
            return ctx.Blackboard.Get(name);
        };

        enum class SourceKind
        {
            Float,
            Uint,
            Depth,
        };

        Core::Hash::StringID resolvedName = ctx.Debug.SelectedResource;
        RGResourceHandle resolvedHandle = resolveCurrentHandle(resolvedName);
        RGTextureDesc srcDesc{};
        SourceKind sourceKind = SourceKind::Float;

        if (resolvedName == GetRenderResourceName(RenderResource::SceneDepth))
            sourceKind = SourceKind::Depth;
        else if (resolvedName == GetRenderResourceName(RenderResource::EntityId) ||
                 resolvedName == GetRenderResourceName(RenderResource::PrimitiveId) ||
                 resolvedName == GetRenderResourceName(RenderResource::SelectionMask) ||
                 resolvedName == GetRenderResourceName(RenderResource::SelectionOutline))
            sourceKind = SourceKind::Uint;

        if (const auto canonical = TryGetRenderResourceByName(resolvedName); canonical && resolvedHandle.IsValid())
        {
            const auto def = GetRenderResourceDefinition(*canonical);
            srcDesc.Width = ctx.Resolution.width;
            srcDesc.Height = ctx.Resolution.height;
            srcDesc.Format = ResolveRenderResourceFormat(*canonical, ctx.SwapchainFormat, ctx.DepthFormat);
            srcDesc.Usage = def.Usage;
            srcDesc.Aspect = def.Aspect;
            m_LastSrcFormat = srcDesc.Format;
            m_LastSrcAspect = srcDesc.Aspect;
        }
        else if (resolvedHandle.IsValid())
        {
            // Non-canonical resources should still resolve from the current frame only.
            srcDesc.Width = ctx.Resolution.width;
            srcDesc.Height = ctx.Resolution.height;
            srcDesc.Format = (sourceKind == SourceKind::Depth) ? ctx.DepthFormat
                            : (sourceKind == SourceKind::Uint) ? VK_FORMAT_R32_UINT
                                                              : ctx.SwapchainFormat;
            srcDesc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            srcDesc.Aspect = (sourceKind == SourceKind::Depth) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            m_LastSrcFormat = srcDesc.Format;
            m_LastSrcAspect = srcDesc.Aspect;
        }
        else
        {
            // Current frame does not yet expose the requested source; fall back to a dummy texture
            // of the matching kind instead of sampling stale previous-frame metadata.
            srcDesc.Width = 1;
            srcDesc.Height = 1;
            srcDesc.Format = (sourceKind == SourceKind::Depth) ? VK_FORMAT_D32_SFLOAT
                            : (sourceKind == SourceKind::Uint) ? VK_FORMAT_R32_UINT
                                                              : VK_FORMAT_R8G8B8A8_UNORM;
            srcDesc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            srcDesc.Aspect = (sourceKind == SourceKind::Depth) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            m_LastSrcFormat = srcDesc.Format;
            m_LastSrcAspect = srcDesc.Aspect;
        }

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

        ctx.Graph.AddPass<ResolveData>("DebugViewResolve",
                                       [&](ResolveData& data, RGBuilder& builder)
                                       {
                                           RGResourceHandle srcHandle = resolvedHandle;
                                           if (!srcHandle.IsValid())
                                           {
                                               switch (sourceKind)
                                               {
                                               case SourceKind::Depth:
                                                   srcHandle = builder.ImportTexture("DebugViewDummyDepth"_id,
                                                                                     m_DummyDepth->GetHandle(),
                                                                                     m_DummyDepth->GetView(),
                                                                                     m_DummyDepth->GetFormat(),
                                                                                     {1u, 1u},
                                                                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
                                                   break;
                                               case SourceKind::Uint:
                                                   srcHandle = builder.ImportTexture("DebugViewDummyUint"_id,
                                                                                     m_DummyUint->GetHandle(),
                                                                                     m_DummyUint->GetView(),
                                                                                     m_DummyUint->GetFormat(),
                                                                                     {1u, 1u},
                                                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                                   break;
                                               case SourceKind::Float:
                                                   srcHandle = builder.ImportTexture("DebugViewDummyFloat"_id,
                                                                                     m_DummyFloat->GetHandle(),
                                                                                     m_DummyFloat->GetView(),
                                                                                     m_DummyFloat->GetFormat(),
                                                                                     {1u, 1u},
                                                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                                   break;
                                               }
                                           }
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
                                           data.SrcFormat = srcDesc.Format;
                                           data.IsDepth = (srcDesc.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
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
                                           SetViewportScissor(cmd, ctx.Resolution);
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
                                            const RGResourceHandle intermediate = ctx.Blackboard.Get("DebugViewRGBA"_id);
                                            const RGResourceHandle target = GetPresentationTarget(ctx);

                                            if (intermediate.IsValid() && target.IsValid())
                                            {
                                                // Blit requires TRANSFER_READ on source and TRANSFER_WRITE on dest
                                                // IMPORTANT: OR-in MEMORY_WRITE so the DAG scheduler treats this
                                                // as a write on DebugViewRGBA. Without it, DebugViewBarrier (which
                                                // also reads DebugViewRGBA) has no ordering constraint with this
                                                // pass, and the scheduler can reorder them. CalculateBarriers
                                                // assumes creation order, so reordering causes oldLayout mismatches.
                                                data.Src = builder.Read(intermediate, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                        VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                                                data.Dst = builder.Write(
                                                    target, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
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
        if (currentSet)
        {
            const VkDevice device = m_Device->GetLogicalDevice();
            UpdateImageDescriptor(device, currentSet, 0,
                                  m_Sampler, m_DummyFloat->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            UpdateImageDescriptor(device, currentSet, 1,
                                  m_Sampler, m_DummyUint->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            UpdateImageDescriptor(device, currentSet, 2,
                                  m_Sampler, m_DummyDepth->GetView(),
                                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            if (m_LastSrcHandle.IsValid())
            {
                const uint32_t targetBinding = (m_LastSrcFormat == VK_FORMAT_R32_UINT)
                    ? 1u
                    : (((m_LastSrcAspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0) ? 2u : 0u);

                for (const auto& img : debugImages)
                {
                    if (img.Resource == m_LastSrcHandle.ID && img.View != VK_NULL_HANDLE)
                    {
                        UpdateImageDescriptor(device, currentSet, targetBinding,
                                              m_Sampler, img.View,
                                              targetBinding == 2
                                                  ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                  : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        break;
                    }
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
        ReleaseImGuiTextures();

        // 2. Clear Images
        m_PreviewImages.clear();
    }

    void DebugViewPass::Shutdown()
    {
        ReleaseImGuiTextures();
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
