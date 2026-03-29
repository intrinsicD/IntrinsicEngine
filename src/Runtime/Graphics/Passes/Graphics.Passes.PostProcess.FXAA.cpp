module;

#include <memory>
#include <span>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics.Passes.PostProcess.FXAA;

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
import RHI.Pipeline;
import RHI.Shader;

import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    void FXAASubPass::Initialize(RHI::VulkanDevice& device,
                                 RHI::DescriptorAllocator& descriptorPool,
                                 VkSampler linearSampler,
                                 VkImageView dummyView)
    {
        m_Device = &device;
        VkDevice dev = device.GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        m_SetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.FXAA");

        AllocatePerFrameSets<3>(descriptorPool, m_SetLayout, m_Sets);

        for (auto& set : m_Sets)
            UpdateImageDescriptor(dev, set, 0, linearSampler, dummyView, readLayout);
    }

    std::unique_ptr<RHI::GraphicsPipeline> FXAASubPass::BuildPipeline(VkFormat outputFormat)
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.FXAA.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({outputFormat});
        pb.AddDescriptorSetLayout(m_SetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 5;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build FXAA pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    void FXAASubPass::AddPass(RenderPassContext& ctx,
                               RGResourceHandle postLdr,
                               RGResourceHandle sceneColorLdr,
                               const PostProcessSettings& settings)
    {
        if (!m_Pipeline)
            m_Pipeline = BuildPipeline(ctx.SwapchainFormat);
        if (!m_Pipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const float fxaaContrast = settings.FXAAContrastThreshold;
        const float fxaaRelative = settings.FXAARelativeThreshold;
        const float fxaaSubpixel = settings.FXAASubpixelBlending;

        struct FXAAData { RGResourceHandle Src; RGResourceHandle Dst; };

        ctx.Graph.AddPass<FXAAData>("Post.FXAA",
            [&](FXAAData& data, RGBuilder& builder)
            {
                data.Src = builder.Read(postLdr,
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Dst = builder.WriteColor(sceneColorLdr, colorInfo);
            },
            [this, fi, resolution, fxaaContrast, fxaaRelative, fxaaSubpixel]
            (const FXAAData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_Pipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_Pipeline->GetLayout(),
                                        0, 1, &m_Sets[fi], 0, nullptr);

                struct FXAAPC {
                    float InvResX, InvResY;
                    float ContrastThreshold;
                    float RelativeThreshold;
                    float SubpixelBlending;
                };
                static_assert(sizeof(FXAAPC) == 20, "FXAAPC must be 20 bytes");
                FXAAPC pc{
                    1.0f / static_cast<float>(resolution.width),
                    1.0f / static_cast<float>(resolution.height),
                    fxaaContrast, fxaaRelative, fxaaSubpixel
                };
                vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        );
    }

    void FXAASubPass::PostCompile(uint32_t frameIndex,
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

        if (VkImageView ldrView = findView(postLdrHandle))
            UpdateImageDescriptor(dev, m_Sets[frameIndex], 0, linearSampler, ldrView, readLayout);
    }

    void FXAASubPass::Shutdown()
    {
        if (!m_Device) return;
        VkDevice dev = m_Device->GetLogicalDevice();

        m_Pipeline.reset();

        if (m_SetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
    }

    void FXAASubPass::OnResize()
    {
        m_Pipeline.reset();
    }
}
