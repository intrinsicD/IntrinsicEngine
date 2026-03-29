module;

#include <memory>
#include <span>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics.Passes.PostProcess.ToneMap;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;

import Core.Hash;
import Core.Logging;
import Core.Filesystem;

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
    void ToneMapSubPass::Initialize(RHI::VulkanDevice& device,
                                    RHI::DescriptorAllocator& descriptorPool,
                                    VkSampler linearSampler,
                                    VkImageView dummyView)
    {
        m_Device = &device;
        VkDevice dev = device.GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        m_SetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.ToneMap");

        AllocatePerFrameSets<3>(descriptorPool, m_SetLayout, m_Sets);

        for (auto& set : m_Sets)
        {
            UpdateImageDescriptor(dev, set, 0, linearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 1, linearSampler, dummyView, readLayout);
        }
    }

    std::unique_ptr<RHI::GraphicsPipeline> ToneMapSubPass::BuildPipeline(VkFormat outputFormat)
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.ToneMap.Frag"_id);

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
        pcr.size = 80;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build ToneMap pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    RGResourceHandle ToneMapSubPass::AddPass(RenderPassContext& ctx,
                                              RGResourceHandle sceneColor,
                                              RGResourceHandle bloomResult,
                                              RGResourceHandle outputTarget,
                                              const PostProcessSettings& settings)
    {
        if (!m_Pipeline)
            m_Pipeline = BuildPipeline(ctx.SwapchainFormat);
        if (!m_Pipeline)
            return {};

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;

        const bool bloomEnabled = bloomResult.IsValid();
        const float bloomIntensity = bloomEnabled ? settings.BloomIntensity : 0.0f;

        struct ToneMapPC
        {
            float     Exposure;
            int       Operator;
            float     BloomIntensity;
            int       ColorGradingOn;
            float     Saturation;
            float     Contrast;
            float     ColorTempOffset;
            float     TintOffset;
            glm::vec3 Lift;  float _pad0;
            glm::vec3 Gamma; float _pad1;
            glm::vec3 Gain;  float _pad2;
        };
        static_assert(sizeof(ToneMapPC) == 80, "ToneMapPC must be 80 bytes");

        ToneMapPC toneMapPC{};
        toneMapPC.Exposure        = settings.Exposure;
        toneMapPC.Operator        = static_cast<int>(settings.ToneOperator);
        toneMapPC.BloomIntensity  = bloomIntensity;
        toneMapPC.ColorGradingOn  = settings.ColorGradingEnabled ? 1 : 0;
        toneMapPC.Saturation      = settings.Saturation;
        toneMapPC.Contrast        = settings.Contrast;
        toneMapPC.ColorTempOffset = settings.ColorTempOffset;
        toneMapPC.TintOffset      = settings.TintOffset;
        toneMapPC.Lift             = settings.Lift;
        toneMapPC.Gamma            = settings.Gamma;
        toneMapPC.Gain             = settings.Gain;

        struct ToneMapData { RGResourceHandle Src; RGResourceHandle Bloom; RGResourceHandle Dst; };

        RGResourceHandle outputHandle{};

        ctx.Graph.AddPass<ToneMapData>("Post.ToneMap",
            [&, sceneColor, bloomResult, outputTarget](ToneMapData& data, RGBuilder& builder)
            {
                data.Src = builder.Read(sceneColor,
                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                if (bloomResult.IsValid())
                {
                    data.Bloom = builder.Read(bloomResult,
                                               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                }

                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Dst = builder.WriteColor(outputTarget, colorInfo);

                m_LastSceneColorHandle = data.Src;
                m_LastBloomHandle = bloomResult;
                m_LastOutputHandle = data.Dst;
                outputHandle = data.Dst;
            },
            [this, fi, resolution, toneMapPC]
            (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_Pipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_Pipeline->GetLayout(),
                                        0, 1, &m_Sets[fi], 0, nullptr);

                vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapPC), &toneMapPC);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        );

        return outputHandle;
    }

    void ToneMapSubPass::PostCompile(uint32_t frameIndex,
                                     std::span<const RenderGraphDebugImage> debugImages,
                                     VkSampler linearSampler,
                                     VkImageView dummyView)
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

        if (VkImageView sceneView = findView(m_LastSceneColorHandle))
            UpdateImageDescriptor(dev, m_Sets[frameIndex], 0, linearSampler, sceneView, readLayout);

        if (VkImageView bloomView = findView(m_LastBloomHandle))
            UpdateImageDescriptor(dev, m_Sets[frameIndex], 1, linearSampler, bloomView, readLayout);
        else
            UpdateImageDescriptor(dev, m_Sets[frameIndex], 1, linearSampler, dummyView, readLayout);
    }

    void ToneMapSubPass::Shutdown()
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

    void ToneMapSubPass::OnResize()
    {
        m_Pipeline.reset();
    }
}
