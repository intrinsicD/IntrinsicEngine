module;

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"
#include "Graphics.SMAALookupTextures.hpp"

module Graphics.Passes.PostProcess.SMAA;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;

import Core.Hash;
import Core.Logging;
import Core.Filesystem;

import RHI.Buffer;
import RHI.CommandUtils;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.Pipeline;
import RHI.Shader;

import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    // =====================================================================
    // UploadTextureViaStaging (file-local helper)
    // =====================================================================
    static void UploadTextureViaStaging(
        RHI::VulkanDevice& device,
        RHI::VulkanImage& dstImage,
        const void* srcData,
        size_t dataSize,
        uint32_t width,
        uint32_t height)
    {
        RHI::VulkanBuffer staging(device, dataSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

        std::memcpy(staging.GetMappedData(), srcData, dataSize);

        VkCommandBuffer cmd = RHI::CommandUtils::BeginSingleTimeCommands(device);

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = dstImage.GetHandle();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging.GetHandle(), dstImage.GetHandle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        vkCmdPipelineBarrier2(cmd, &dep);

        RHI::CommandUtils::EndSingleTimeCommands(device, cmd);
    }

    // =====================================================================
    // InitializeLookupTextures
    // =====================================================================
    void SMAASubPass::InitializeLookupTextures(VkSampler linearSampler)
    {
        using namespace Graphics::SMAA;

        auto areaData = GenerateAreaTexture();
        m_AreaTex = std::make_unique<RHI::VulkanImage>(
            *m_Device,
            kAreaTexWidth, kAreaTexHeight, 1,
            VK_FORMAT_R8G8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        UploadTextureViaStaging(*m_Device, *m_AreaTex,
            areaData.data(), areaData.size(),
            static_cast<uint32_t>(kAreaTexWidth),
            static_cast<uint32_t>(kAreaTexHeight));

        auto searchData = GenerateSearchTexture();
        m_SearchTex = std::make_unique<RHI::VulkanImage>(
            *m_Device,
            kSearchTexWidth, kSearchTexHeight, 1,
            VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        UploadTextureViaStaging(*m_Device, *m_SearchTex,
            searchData.data(), searchData.size(),
            static_cast<uint32_t>(kSearchTexWidth),
            static_cast<uint32_t>(kSearchTexHeight));

        Core::Log::Info("PostProcess: SMAA lookup textures initialized (Area {}x{}, Search {}x{})",
                        kAreaTexWidth, kAreaTexHeight, kSearchTexWidth, kSearchTexHeight);

        // Bind SMAA lookup textures to blend weight descriptor sets.
        VkDevice dev = m_Device->GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        for (auto& set : m_BlendSets)
        {
            UpdateImageDescriptor(dev, set, 1, linearSampler, m_AreaTex->GetView(), readLayout);
            UpdateImageDescriptor(dev, set, 2, linearSampler, m_SearchTex->GetView(), readLayout);
        }
    }

    void SMAASubPass::Initialize(RHI::VulkanDevice& device,
                                 RHI::DescriptorAllocator& descriptorPool,
                                 VkSampler linearSampler,
                                 VkImageView dummyView)
    {
        m_Device = &device;
        VkDevice dev = device.GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        m_EdgeSetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.SMAA.Edge");

        m_BlendSetLayout = CreateMultiSamplerSetLayout(
            dev, 3, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.SMAA.Blend");

        m_ResolveSetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.SMAA.Resolve");

        AllocatePerFrameSets<3>(descriptorPool, m_EdgeSetLayout, m_EdgeSets);
        AllocatePerFrameSets<3>(descriptorPool, m_BlendSetLayout, m_BlendSets);
        AllocatePerFrameSets<3>(descriptorPool, m_ResolveSetLayout, m_ResolveSets);

        for (auto& set : m_EdgeSets)
            UpdateImageDescriptor(dev, set, 0, linearSampler, dummyView, readLayout);

        for (auto& set : m_BlendSets)
        {
            UpdateImageDescriptor(dev, set, 0, linearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 1, linearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 2, linearSampler, dummyView, readLayout);
        }

        for (auto& set : m_ResolveSets)
        {
            UpdateImageDescriptor(dev, set, 0, linearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 1, linearSampler, dummyView, readLayout);
        }

        InitializeLookupTextures(linearSampler);
    }

    std::unique_ptr<RHI::GraphicsPipeline> SMAASubPass::BuildEdgePipeline(VkFormat edgeFormat)
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.SMAA.Edge.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({edgeFormat});
        pb.AddDescriptorSetLayout(m_EdgeSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build SMAA Edge pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    std::unique_ptr<RHI::GraphicsPipeline> SMAASubPass::BuildBlendPipeline(VkFormat weightFormat)
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.SMAA.Blend.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({weightFormat});
        pb.AddDescriptorSetLayout(m_BlendSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build SMAA Blend pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    std::unique_ptr<RHI::GraphicsPipeline> SMAASubPass::BuildResolvePipeline(VkFormat outputFormat)
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.SMAA.Resolve.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({outputFormat});
        pb.AddDescriptorSetLayout(m_ResolveSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build SMAA Resolve pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    void SMAASubPass::AddPasses(RenderPassContext& ctx,
                                RGResourceHandle postLdr,
                                RGResourceHandle sceneColorLdr,
                                const PostProcessSettings& settings)
    {
        if (!m_EdgePipeline)
            m_EdgePipeline = BuildEdgePipeline(VK_FORMAT_R8G8_UNORM);
        if (!m_BlendPipeline)
            m_BlendPipeline = BuildBlendPipeline(VK_FORMAT_R8G8B8A8_UNORM);
        if (!m_ResolvePipeline)
            m_ResolvePipeline = BuildResolvePipeline(ctx.SwapchainFormat);

        if (!m_EdgePipeline || !m_BlendPipeline || !m_ResolvePipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const float edgeThreshold = settings.SMAAEdgeThreshold;
        const int maxSearch = settings.SMAAMaxSearchSteps;
        const int maxSearchDiag = settings.SMAAMaxSearchStepsDiag;

        // --- Pass 1: Edge Detection ---
        struct SMAAEdgeData { RGResourceHandle Src; RGResourceHandle Dst; };
        RGResourceHandle smaaEdges{};

        ctx.Graph.AddPass<SMAAEdgeData>("Post.SMAA.Edge",
            [&](SMAAEdgeData& data, RGBuilder& builder)
            {
                RGTextureDesc edgeDesc{};
                edgeDesc.Width = resolution.width;
                edgeDesc.Height = resolution.height;
                edgeDesc.Format = VK_FORMAT_R8G8_UNORM;
                edgeDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                edgeDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                smaaEdges = builder.CreateTexture("SMAAEdges"_id, edgeDesc);

                data.Src = builder.Read(postLdr,
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorInfo.ClearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                data.Dst = builder.WriteColor(smaaEdges, colorInfo);
            },
            [this, fi, resolution, edgeThreshold]
            (const SMAAEdgeData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_EdgePipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_EdgePipeline->GetLayout(),
                                        0, 1, &m_EdgeSets[fi], 0, nullptr);

                struct SMAAEdgePC {
                    float InvResX, InvResY;
                    float EdgeThreshold;
                    float _pad0;
                };
                static_assert(sizeof(SMAAEdgePC) == 16, "SMAAEdgePC must be 16 bytes");
                SMAAEdgePC pc{
                    1.0f / static_cast<float>(resolution.width),
                    1.0f / static_cast<float>(resolution.height),
                    edgeThreshold, 0.0f
                };
                vkCmdPushConstants(cmd, m_EdgePipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        );

        // --- Pass 2: Blend Weight Calculation ---
        struct SMAABlendData { RGResourceHandle Edges; RGResourceHandle Dst; };
        RGResourceHandle smaaWeights{};

        ctx.Graph.AddPass<SMAABlendData>("Post.SMAA.Blend",
            [&](SMAABlendData& data, RGBuilder& builder)
            {
                RGTextureDesc weightDesc{};
                weightDesc.Width = resolution.width;
                weightDesc.Height = resolution.height;
                weightDesc.Format = VK_FORMAT_R8G8B8A8_UNORM;
                weightDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                weightDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                smaaWeights = builder.CreateTexture("SMAAWeights"_id, weightDesc);

                data.Edges = builder.Read(smaaEdges,
                                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorInfo.ClearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
                data.Dst = builder.WriteColor(smaaWeights, colorInfo);

                m_LastEdgesHandle = data.Edges;
            },
            [this, fi, resolution, maxSearch, maxSearchDiag]
            (const SMAABlendData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_BlendPipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_BlendPipeline->GetLayout(),
                                        0, 1, &m_BlendSets[fi], 0, nullptr);

                struct SMAABlendPC {
                    float InvResX, InvResY;
                    int   MaxSearchSteps;
                    int   MaxSearchStepsDiag;
                };
                static_assert(sizeof(SMAABlendPC) == 16, "SMAABlendPC must be 16 bytes");
                SMAABlendPC pc{
                    1.0f / static_cast<float>(resolution.width),
                    1.0f / static_cast<float>(resolution.height),
                    maxSearch, maxSearchDiag
                };
                vkCmdPushConstants(cmd, m_BlendPipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        );

        // --- Pass 3: Neighborhood Blending ---
        struct SMAAResolveData { RGResourceHandle Src; RGResourceHandle Weights; RGResourceHandle Dst; };

        ctx.Graph.AddPass<SMAAResolveData>("Post.SMAA.Resolve",
            [&](SMAAResolveData& data, RGBuilder& builder)
            {
                data.Src = builder.Read(postLdr,
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                data.Weights = builder.Read(smaaWeights,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Dst = builder.WriteColor(sceneColorLdr, colorInfo);

                m_LastWeightsHandle = data.Weights;
            },
            [this, fi, resolution]
            (const SMAAResolveData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_ResolvePipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_ResolvePipeline->GetLayout(),
                                        0, 1, &m_ResolveSets[fi], 0, nullptr);

                struct SMAAResolvePC {
                    float InvResX, InvResY;
                    float _pad0, _pad1;
                };
                static_assert(sizeof(SMAAResolvePC) == 16, "SMAAResolvePC must be 16 bytes");
                SMAAResolvePC pc{
                    1.0f / static_cast<float>(resolution.width),
                    1.0f / static_cast<float>(resolution.height),
                    0.0f, 0.0f
                };
                vkCmdPushConstants(cmd, m_ResolvePipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        );
    }

    void SMAASubPass::PostCompile(uint32_t frameIndex,
                                  std::span<const RenderGraphDebugImage> debugImages,
                                  VkSampler linearSampler,
                                  RGResourceHandle postLdrHandle)
    {
        if (!m_Device) return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        frameIndex %= kFrames;
        VkDevice dev = m_Device->GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        auto findView = [&](RGResourceHandle handle) -> VkImageView
        {
            if (!handle.IsValid()) return VK_NULL_HANDLE;
            for (const auto& img : debugImages)
            {
                if (img.Resource == handle.ID && img.View != VK_NULL_HANDLE)
                    return img.View;
            }
            return VK_NULL_HANDLE;
        };

        // SMAA Edge: binding 0 = PostLdr.
        if (VkImageView ldrView = findView(postLdrHandle))
            UpdateImageDescriptor(dev, m_EdgeSets[frameIndex], 0, linearSampler, ldrView, readLayout);

        // SMAA Blend: binding 0 = edges texture.
        if (VkImageView edgesView = findView(m_LastEdgesHandle))
            UpdateImageDescriptor(dev, m_BlendSets[frameIndex], 0, linearSampler, edgesView, readLayout);

        // SMAA Resolve: binding 0 = PostLdr, binding 1 = weights.
        if (VkImageView ldrView = findView(postLdrHandle))
            UpdateImageDescriptor(dev, m_ResolveSets[frameIndex], 0, linearSampler, ldrView, readLayout);

        if (VkImageView weightsView = findView(m_LastWeightsHandle))
            UpdateImageDescriptor(dev, m_ResolveSets[frameIndex], 1, linearSampler, weightsView, readLayout);
    }

    void SMAASubPass::Shutdown()
    {
        if (!m_Device) return;
        VkDevice dev = m_Device->GetLogicalDevice();

        m_EdgePipeline.reset();
        m_BlendPipeline.reset();
        m_ResolvePipeline.reset();
        m_AreaTex.reset();
        m_SearchTex.reset();

        if (m_EdgeSetLayout != VK_NULL_HANDLE)    { vkDestroyDescriptorSetLayout(dev, m_EdgeSetLayout, nullptr);    m_EdgeSetLayout = VK_NULL_HANDLE; }
        if (m_BlendSetLayout != VK_NULL_HANDLE)   { vkDestroyDescriptorSetLayout(dev, m_BlendSetLayout, nullptr);   m_BlendSetLayout = VK_NULL_HANDLE; }
        if (m_ResolveSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(dev, m_ResolveSetLayout, nullptr); m_ResolveSetLayout = VK_NULL_HANDLE; }
    }

    void SMAASubPass::OnResize()
    {
        m_EdgePipeline.reset();
        m_BlendPipeline.reset();
        m_ResolvePipeline.reset();
    }
}
