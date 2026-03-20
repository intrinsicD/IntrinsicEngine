module;

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "RHI.Vulkan.hpp"

module Graphics.Passes.Composition;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Core.Filesystem;
import Core.Hash;
import Core.Logging;
import RHI;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    void CompositionPass::Initialize(RHI::VulkanDevice& device,
                                     RHI::DescriptorAllocator& descriptorPool,
                                     RHI::DescriptorLayout&)
    {
        m_Device = &device;
        VkDevice dev = device.GetLogicalDevice();

        // Linear sampler for G-buffer color textures.
        {
            VkSamplerCreateInfo samp{};
            samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samp.magFilter = VK_FILTER_LINEAR;
            samp.minFilter = VK_FILTER_LINEAR;
            samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samp.maxAnisotropy = 1.0f;
            CheckVkResult(vkCreateSampler(dev, &samp, nullptr, &m_LinearSampler),
                          "Composition", "vkCreateSampler(linear)");
        }

        // Nearest sampler for depth texture.
        m_NearestSampler = CreateNearestSampler(dev, "Composition");

        // Descriptor set layout: 4 combined image samplers.
        m_SetLayout = CreateMultiSamplerSetLayout(dev, 4, VK_SHADER_STAGE_FRAGMENT_BIT, "Composition");

        // Allocate per-frame descriptor sets.
        AllocatePerFrameSets<FRAMES>(descriptorPool, m_SetLayout, m_Sets);

        // Dummy image for safe initial bindings.
        m_DummySampled = std::make_unique<RHI::VulkanImage>(
            device,
            1,
            1,
            1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        TransitionImageToShaderRead(device, m_DummySampled->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageView dummyView = m_DummySampled->GetView();
        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            for (uint32_t b = 0; b < 4; ++b)
            {
                VkSampler samp = (b == 3) ? m_NearestSampler : m_LinearSampler;
                UpdateImageDescriptor(dev, m_Sets[i], b, samp, dummyView,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }

    void CompositionPass::Shutdown()
    {
        if (!m_Device) return;
        VkDevice dev = m_Device->GetLogicalDevice();

        m_DeferredPipeline.reset();
        m_DummySampled.reset();

        if (m_LinearSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(dev, m_LinearSampler, nullptr);
            m_LinearSampler = VK_NULL_HANDLE;
        }
        if (m_NearestSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(dev, m_NearestSampler, nullptr);
            m_NearestSampler = VK_NULL_HANDLE;
        }
        if (m_SetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
    }

    void CompositionPass::AddPasses(RenderPassContext& ctx)
    {
        // Only run for the deferred lighting path.
        if (ctx.Recipe.LightingPath != FrameLightingPath::Deferred)
            return;

        if (!m_ShaderRegistry)
            return;

        // Read G-buffer resources from blackboard.
        const RGResourceHandle normal   = ctx.Blackboard.Get(RenderResource::SceneNormal);
        const RGResourceHandle albedo   = ctx.Blackboard.Get(RenderResource::Albedo);
        const RGResourceHandle material = ctx.Blackboard.Get(RenderResource::Material0);
        const RGResourceHandle depth    = ctx.Blackboard.Get(RenderResource::SceneDepth);
        const RGResourceHandle hdr      = ctx.Blackboard.Get(RenderResource::SceneColorHDR);

        if (!normal.IsValid() || !albedo.IsValid() || !material.IsValid() ||
            !depth.IsValid() || !hdr.IsValid())
        {
            Core::Log::Warn("CompositionPass: Missing G-buffer or HDR target — skipping deferred composition.");
            return;
        }

        // Lazy pipeline creation (needs swapchain format to resolve HDR format).
        if (!m_DeferredPipeline)
        {
            m_DeferredPipeline = BuildDeferredPipeline(ctx.SceneColorFormat);
            if (!m_DeferredPipeline)
                return;
        }

        const uint32_t fi = ctx.FrameIndex % FRAMES;

        // Push constants: inverse VP + clear color.
        struct DeferredPushConstants
        {
            glm::mat4 InvViewProj;
            glm::vec4 ClearColor;
        };

        DeferredPushConstants pc{};
        pc.InvViewProj = glm::inverse(ctx.CameraProj * ctx.CameraView);
        pc.ClearColor = glm::vec4(0.1f, 0.3f, 0.6f, 1.0f); // Match forward clear color.

        struct PassData
        {
            RGResourceHandle Normal;
            RGResourceHandle Albedo;
            RGResourceHandle Material;
            RGResourceHandle Depth;
            RGResourceHandle HDR;
        };

        ctx.Graph.AddPass<PassData>("DeferredLighting",
            [&](PassData& data, RGBuilder& builder)
            {
                data.Normal   = builder.Read(normal,   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                data.Albedo   = builder.Read(albedo,   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                data.Material = builder.Read(material, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                data.Depth    = builder.Read(depth,    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.HDR = builder.WriteColor(hdr, colorInfo);
            },
            [this, fi, resolution = ctx.Resolution, pc]
            (const PassData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DeferredPipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_DeferredPipeline->GetLayout(),
                                        0, 1, &m_Sets[fi], 0, nullptr);

                vkCmdPushConstants(cmd, m_DeferredPipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

                vkCmdDraw(cmd, 3, 1, 0, 0); // Fullscreen triangle.
            });

        // Track handles for PostCompile descriptor update.
        m_LastNormalHandle   = normal;
        m_LastAlbedoHandle   = albedo;
        m_LastMaterialHandle = material;
        m_LastDepthHandle    = depth;
    }

    void CompositionPass::PostCompile(uint32_t frameIndex,
                                      std::span<const RenderGraphDebugImage> debugImages)
    {
        if (!m_Device || !m_LastNormalHandle.IsValid())
            return;

        const uint32_t fi = frameIndex % FRAMES;
        VkDevice dev = m_Device->GetLogicalDevice();

        // Update descriptor sets with actual G-buffer image views.
        auto findView = [&](RGResourceHandle handle) -> VkImageView
        {
            for (const auto& img : debugImages)
            {
                if (handle.IsValid() && img.Resource == handle.ID && img.View != VK_NULL_HANDLE)
                    return img.View;
            }
            return m_DummySampled ? m_DummySampled->GetView() : VK_NULL_HANDLE;
        };

        struct DescBinding { uint32_t binding; RGResourceHandle handle; VkSampler sampler; };
        DescBinding bindings[] = {
            {0, m_LastNormalHandle,   m_LinearSampler},
            {1, m_LastAlbedoHandle,   m_LinearSampler},
            {2, m_LastMaterialHandle, m_LinearSampler},
            {3, m_LastDepthHandle,    m_NearestSampler},
        };

        for (const auto& b : bindings)
        {
            VkImageView view = findView(b.handle);
            if (view != VK_NULL_HANDLE)
            {
                UpdateImageDescriptor(dev, m_Sets[fi], b.binding, b.sampler, view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }
    }

    std::unique_ptr<RHI::GraphicsPipeline>
    CompositionPass::BuildDeferredPipeline(VkFormat outputFormat)
    {
        if (!m_ShaderRegistry || !m_Device)
            return nullptr;

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Deferred.Lighting.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({outputFormat});
        pb.AddDescriptorSetLayout(m_SetLayout); // set = 0: G-buffer samplers

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(glm::mat4) + sizeof(glm::vec4); // 80 bytes
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("CompositionPass: Failed to build deferred pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }

        return std::move(*built);
    }
}
