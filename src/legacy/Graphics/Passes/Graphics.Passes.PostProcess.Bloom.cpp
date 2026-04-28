module;

#include <memory>
#include <span>
#include <array>
#include <string>
#include <vector>
#include <format>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics.Passes.PostProcess.Bloom;

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
    void BloomSubPass::Initialize(RHI::VulkanDevice& device,
                                  RHI::DescriptorAllocator& descriptorPool,
                                  VkSampler linearSampler,
                                  VkImageView dummyView)
    {
        m_Device = &device;
        VkDevice dev = device.GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        m_DownSetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.BloomDown");

        m_UpSetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.BloomUp");

        for (uint32_t fi = 0; fi < 3; ++fi)
        {
            for (uint32_t m = 0; m < kBloomMipCount; ++m)
            {
                m_DownSets[fi][m] = descriptorPool.Allocate(m_DownSetLayout);
                m_UpSets[fi][m]   = descriptorPool.Allocate(m_UpSetLayout);

                UpdateImageDescriptor(dev, m_DownSets[fi][m], 0, linearSampler, dummyView, readLayout);
                UpdateImageDescriptor(dev, m_UpSets[fi][m], 0, linearSampler, dummyView, readLayout);
                UpdateImageDescriptor(dev, m_UpSets[fi][m], 1, linearSampler, dummyView, readLayout);
            }
        }
    }

    std::unique_ptr<RHI::GraphicsPipeline> BloomSubPass::BuildDownsamplePipeline()
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.BloomDown.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({VK_FORMAT_R16G16B16A16_SFLOAT});
        pb.AddDescriptorSetLayout(m_DownSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build BloomDownsample pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    std::unique_ptr<RHI::GraphicsPipeline> BloomSubPass::BuildUpsamplePipeline()
    {
        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                        "Post.Fullscreen.Vert"_id,
                                                        "Post.BloomUp.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.DisableDepthTest();
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.SetColorFormats({VK_FORMAT_R16G16B16A16_SFLOAT});
        pb.AddDescriptorSetLayout(m_UpSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build BloomUpsample pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    void BloomSubPass::AddPasses(RenderPassContext& ctx,
                                 RGResourceHandle sceneColor,
                                 const PostProcessSettings& settings)
    {
        if (!m_DownPipeline)
            m_DownPipeline = BuildDownsamplePipeline();
        if (!m_UpPipeline)
            m_UpPipeline = BuildUpsamplePipeline();

        if (!m_DownPipeline || !m_UpPipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const float threshold = settings.BloomThreshold;
        const float filterRadius = settings.BloomFilterRadius;

        struct MipInfo { uint32_t w; uint32_t h; };
        std::array<MipInfo, kBloomMipCount> mips{};
        mips[0] = { std::max(1u, ctx.Resolution.width / 2), std::max(1u, ctx.Resolution.height / 2) };
        for (uint32_t i = 1; i < kBloomMipCount; ++i)
            mips[i] = { std::max(1u, mips[i - 1].w / 2), std::max(1u, mips[i - 1].h / 2) };

        std::array<RGResourceHandle, kBloomMipCount> bloomMips{};
        std::array<RGResourceHandle, kBloomMipCount> bloomUps{};

        // ---- Downsample chain ----
        RGResourceHandle prevSrc = sceneColor;
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            const uint32_t srcW = (mip == 0) ? ctx.Resolution.width : mips[mip - 1].w;
            const uint32_t srcH = (mip == 0) ? ctx.Resolution.height : mips[mip - 1].h;
            const uint32_t dstW = mips[mip].w;
            const uint32_t dstH = mips[mip].h;
            const bool isFirst = (mip == 0);

            struct BloomDownData { RGResourceHandle Src; RGResourceHandle Dst; };

            const std::string passName = std::format("Post.BloomDown.{}", mip);
            const std::string texName  = std::format("BloomMip{}", mip);

            RGResourceHandle src = prevSrc;
            const uint32_t capMip = mip;

            ctx.Graph.AddPass<BloomDownData>(passName.c_str(),
                [&, src, dstW, dstH, texName, capMip](BloomDownData& data, RGBuilder& builder)
                {
                    RGTextureDesc desc{};
                    desc.Width  = dstW;
                    desc.Height = dstH;
                    desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
                    desc.Usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    desc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                    bloomMips[capMip] = builder.CreateTexture(Core::Hash::StringID{texName.c_str()}, desc);

                    data.Src = builder.Read(src,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(bloomMips[capMip], colorInfo);

                    m_LastDownHandles[capMip] = data.Src;
                    m_LastMipWriteHandles[capMip] = data.Dst;
                },
                [this, fi, srcW, srcH, dstW, dstH, threshold, isFirst, capMip]
                (const BloomDownData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    VkExtent2D ext{dstW, dstH};
                    SetViewportScissor(cmd, ext);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_DownPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_DownPipeline->GetLayout(),
                                            0, 1, &m_DownSets[fi][capMip], 0, nullptr);

                    struct BloomDownPC {
                        float InvSrcResX, InvSrcResY;
                        float Threshold;
                        int   IsFirstMip;
                    };
                    static_assert(sizeof(BloomDownPC) == 16, "BloomDownPC must be 16 bytes");
                    BloomDownPC pc{
                        1.0f / static_cast<float>(srcW),
                        1.0f / static_cast<float>(srcH),
                        threshold,
                        isFirst ? 1 : 0
                    };
                    vkCmdPushConstants(cmd, m_DownPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );

            prevSrc = bloomMips[mip];
        }

        // ---- Upsample chain ----
        RGResourceHandle prevUp = bloomMips[kBloomMipCount - 1];
        for (int mip = static_cast<int>(kBloomMipCount) - 2; mip >= 0; --mip)
        {
            const uint32_t coarserW = mips[mip + 1].w;
            const uint32_t coarserH = mips[mip + 1].h;
            const uint32_t dstW = mips[mip].w;
            const uint32_t dstH = mips[mip].h;
            const uint32_t capMip = static_cast<uint32_t>(mip);

            struct BloomUpData { RGResourceHandle CoarserSrc; RGResourceHandle CurrentSrc; RGResourceHandle Dst; };

            const std::string passName = std::format("Post.BloomUp.{}", mip);
            const std::string texName  = std::format("BloomUp{}", mip);

            RGResourceHandle coarserSrc = prevUp;
            RGResourceHandle currentDown = bloomMips[mip];

            ctx.Graph.AddPass<BloomUpData>(passName.c_str(),
                [&, coarserSrc, currentDown, dstW, dstH, texName, capMip](BloomUpData& data, RGBuilder& builder)
                {
                    RGTextureDesc desc{};
                    desc.Width  = dstW;
                    desc.Height = dstH;
                    desc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
                    desc.Usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    desc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                    bloomUps[capMip] = builder.CreateTexture(Core::Hash::StringID{texName.c_str()}, desc);

                    data.CoarserSrc = builder.Read(coarserSrc,
                                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                    data.CurrentSrc = builder.Read(currentDown,
                                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(bloomUps[capMip], colorInfo);

                    m_LastUpSrcHandles[capMip] = data.CoarserSrc;
                },
                [this, fi, coarserW, coarserH, dstW, dstH, filterRadius, capMip]
                (const BloomUpData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    VkExtent2D ext{dstW, dstH};
                    SetViewportScissor(cmd, ext);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_UpPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_UpPipeline->GetLayout(),
                                            0, 1, &m_UpSets[fi][capMip], 0, nullptr);

                    struct BloomUpPC {
                        float InvCoarserResX, InvCoarserResY;
                        float FilterRadius;
                        float _pad0;
                    };
                    static_assert(sizeof(BloomUpPC) == 16, "BloomUpPC must be 16 bytes");
                    BloomUpPC pc{
                        1.0f / static_cast<float>(coarserW),
                        1.0f / static_cast<float>(coarserH),
                        filterRadius,
                        0.0f
                    };
                    vkCmdPushConstants(cmd, m_UpPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );

            prevUp = bloomUps[mip];
        }

        m_LastBloomMip0Handle = prevUp;
    }

    void BloomSubPass::PostCompile(uint32_t frameIndex,
                                   std::span<const RenderGraphDebugImage> debugImages,
                                   VkSampler linearSampler)
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

        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            if (VkImageView view = findView(m_LastDownHandles[mip]))
                UpdateImageDescriptor(dev, m_DownSets[frameIndex][mip], 0, linearSampler, view, readLayout);
        }

        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            if (VkImageView coarserView = findView(m_LastUpSrcHandles[mip]))
                UpdateImageDescriptor(dev, m_UpSets[frameIndex][mip], 0, linearSampler, coarserView, readLayout);

            if (VkImageView mipView = findView(m_LastMipWriteHandles[mip]))
                UpdateImageDescriptor(dev, m_UpSets[frameIndex][mip], 1, linearSampler, mipView, readLayout);
        }
    }

    void BloomSubPass::Shutdown()
    {
        if (!m_Device) return;
        VkDevice dev = m_Device->GetLogicalDevice();

        m_DownPipeline.reset();
        m_UpPipeline.reset();

        if (m_DownSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_DownSetLayout, nullptr);
            m_DownSetLayout = VK_NULL_HANDLE;
        }
        if (m_UpSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_UpSetLayout, nullptr);
            m_UpSetLayout = VK_NULL_HANDLE;
        }
    }

    void BloomSubPass::OnResize()
    {
        m_DownPipeline.reset();
        m_UpPipeline.reset();
    }
}
