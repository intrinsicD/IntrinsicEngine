module;

#include <memory>
#include <span>
#include <array>
#include <string>
#include <cstring>
#include <algorithm>
#include <format>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics:Passes.PostProcess.Impl;

import :Passes.PostProcess;
import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import RHI;

using namespace Core::Hash;

#include "Graphics.PassUtils.hpp"

namespace Graphics::Passes
{
    // =====================================================================
    // Helper: create a descriptor set layout with N combined image samplers.
    // =====================================================================
    static VkDescriptorSetLayout CreateMultiSamplerSetLayout(
        VkDevice device, uint32_t bindingCount, VkShaderStageFlags stages, std::string_view passName)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings(bindingCount);
        for (uint32_t i = 0; i < bindingCount; ++i)
        {
            bindings[i] = {};
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = stages;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindingCount;
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        CheckVkResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout),
                      passName, "vkCreateDescriptorSetLayout");
        return layout;
    }

    // =====================================================================
    // Initialize
    // =====================================================================
    void PostProcessPass::Initialize(RHI::VulkanDevice& device,
                                     RHI::DescriptorAllocator& descriptorPool,
                                     RHI::DescriptorLayout&)
    {
        m_Device = &device;
        VkDevice dev = m_Device->GetLogicalDevice();

        // Create linear sampler (shared by all post-process stages).
        VkSamplerCreateInfo samp{};
        samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp.magFilter = VK_FILTER_LINEAR;
        samp.minFilter = VK_FILTER_LINEAR;
        samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.minLod = 0.0f;
        samp.maxLod = 0.0f;
        samp.maxAnisotropy = 1.0f;
        CheckVkResult(vkCreateSampler(dev, &samp, nullptr, &m_LinearSampler),
                      "PostProcess", "vkCreateSampler");

        // Tone map: 2 bindings (scene color + bloom).
        m_ToneMapSetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.ToneMap");

        // FXAA: 1 binding.
        m_FXAASetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.FXAA");

        // Bloom downsample: 1 binding (source mip).
        m_BloomDownSetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.BloomDown");

        // Bloom upsample: 2 bindings (coarser mip + current downsample).
        m_BloomUpSetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.BloomUp");

        // Allocate per-frame descriptor sets.
        AllocatePerFrameSets<3>(descriptorPool, m_ToneMapSetLayout, m_ToneMapSets);
        AllocatePerFrameSets<3>(descriptorPool, m_FXAASetLayout, m_FXAASets);

        for (uint32_t fi = 0; fi < 3; ++fi)
        {
            for (uint32_t m = 0; m < kBloomMipCount; ++m)
            {
                m_BloomDownSets[fi][m] = descriptorPool.Allocate(m_BloomDownSetLayout);
                m_BloomUpSets[fi][m]   = descriptorPool.Allocate(m_BloomUpSetLayout);
            }
        }

        // Dummy image for safe default bindings.
        m_DummySampled = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionImageToShaderRead(*m_Device, m_DummySampled->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageView dummyView = m_DummySampled->GetView();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        for (auto& set : m_ToneMapSets)
        {
            UpdateImageDescriptor(dev, set, 0, m_LinearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 1, m_LinearSampler, dummyView, readLayout);
        }
        for (auto& set : m_FXAASets)
            UpdateImageDescriptor(dev, set, 0, m_LinearSampler, dummyView, readLayout);

        for (uint32_t fi = 0; fi < 3; ++fi)
        {
            for (uint32_t m = 0; m < kBloomMipCount; ++m)
            {
                UpdateImageDescriptor(dev, m_BloomDownSets[fi][m], 0, m_LinearSampler, dummyView, readLayout);
                UpdateImageDescriptor(dev, m_BloomUpSets[fi][m], 0, m_LinearSampler, dummyView, readLayout);
                UpdateImageDescriptor(dev, m_BloomUpSets[fi][m], 1, m_LinearSampler, dummyView, readLayout);
            }
        }
    }

    // =====================================================================
    // BuildToneMapPipeline
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildToneMapPipeline(VkFormat outputFormat)
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
        pb.AddDescriptorSetLayout(m_ToneMapSetLayout); // set = 0

        // Push constants: Exposure (float) + Operator (int) + BloomIntensity (float) + pad
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4; // Exposure, Operator(as int), BloomIntensity, pad
        pb.AddPushConstantRange(pcr);

        auto built = pb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build ToneMap pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    // =====================================================================
    // BuildFXAAPipeline
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildFXAAPipeline(VkFormat outputFormat)
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
        pb.AddDescriptorSetLayout(m_FXAASetLayout); // set = 0

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

    // =====================================================================
    // BuildBloomDownsamplePipeline
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildBloomDownsamplePipeline()
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
        pb.AddDescriptorSetLayout(m_BloomDownSetLayout); // set = 0

        // Push constants: InvSrcResolution (vec2) + Threshold (float) + IsFirstMip (int)
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

    // =====================================================================
    // BuildBloomUpsamplePipeline
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildBloomUpsamplePipeline()
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
        pb.AddDescriptorSetLayout(m_BloomUpSetLayout); // set = 0

        // Push constants: InvCoarserResolution (vec2) + FilterRadius (float) + pad
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

    // =====================================================================
    // AddBloomPasses — downsample + upsample chain before tone mapping.
    // =====================================================================
    void PostProcessPass::AddBloomPasses(RenderPassContext& ctx, RGResourceHandle sceneColor)
    {
        if (!m_BloomDownPipeline)
            m_BloomDownPipeline = BuildBloomDownsamplePipeline();
        if (!m_BloomUpPipeline)
            m_BloomUpPipeline = BuildBloomUpsamplePipeline();

        if (!m_BloomDownPipeline || !m_BloomUpPipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const float threshold = m_Settings.BloomThreshold;
        const float filterRadius = m_Settings.BloomFilterRadius;

        // Compute mip dimensions (each mip is half the previous).
        struct MipInfo { uint32_t w; uint32_t h; };
        std::array<MipInfo, kBloomMipCount> mips{};
        mips[0] = { std::max(1u, ctx.Resolution.width / 2), std::max(1u, ctx.Resolution.height / 2) };
        for (uint32_t i = 1; i < kBloomMipCount; ++i)
            mips[i] = { std::max(1u, mips[i - 1].w / 2), std::max(1u, mips[i - 1].h / 2) };

        // Create transient bloom mip textures.
        std::array<RGResourceHandle, kBloomMipCount> bloomMips{};
        // We also need separate upsample targets (can't read+write same resource in one pass).
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
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(bloomMips[capMip], colorInfo);

                    m_LastBloomDownHandles[capMip] = data.Src;
                },
                [this, fi, srcW, srcH, dstW, dstH, threshold, isFirst, capMip]
                (const BloomDownData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    VkExtent2D ext{dstW, dstH};
                    SetViewportScissor(cmd, ext);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_BloomDownPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_BloomDownPipeline->GetLayout(),
                                            0, 1, &m_BloomDownSets[fi][capMip], 0, nullptr);

                    struct {
                        float InvSrcResX, InvSrcResY;
                        float Threshold;
                        int   IsFirstMip;
                    } pc{
                        1.0f / static_cast<float>(srcW),
                        1.0f / static_cast<float>(srcH),
                        threshold,
                        isFirst ? 1 : 0
                    };
                    vkCmdPushConstants(cmd, m_BloomDownPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );

            prevSrc = bloomMips[mip];
        }

        // ---- Upsample chain (from coarsest to finest) ----
        // Start at the coarsest mip; upsample + accumulate back up.
        // We need new textures for upsample outputs to avoid read+write aliasing.
        RGResourceHandle prevUp = bloomMips[kBloomMipCount - 1]; // Start from smallest mip
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
                                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                    data.CurrentSrc = builder.Read(currentDown,
                                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(bloomUps[capMip], colorInfo);

                    m_LastBloomUpSrcHandles[capMip] = data.CoarserSrc;
                    // Store the currentDown handle so we can bind it in PostCompile
                    // We reuse m_LastBloomDownHandles for the current downsample read
                },
                [this, fi, coarserW, coarserH, dstW, dstH, filterRadius, capMip]
                (const BloomUpData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    VkExtent2D ext{dstW, dstH};
                    SetViewportScissor(cmd, ext);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_BloomUpPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_BloomUpPipeline->GetLayout(),
                                            0, 1, &m_BloomUpSets[fi][capMip], 0, nullptr);

                    struct {
                        float InvCoarserResX, InvCoarserResY;
                        float FilterRadius;
                        float _pad0;
                    } pc{
                        1.0f / static_cast<float>(coarserW),
                        1.0f / static_cast<float>(coarserH),
                        filterRadius,
                        0.0f
                    };
                    vkCmdPushConstants(cmd, m_BloomUpPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );

            prevUp = bloomUps[mip];
        }

        // Store final bloom result handle for tone map compositing.
        m_LastBloomMip0Handle = prevUp;
    }

    // =====================================================================
    // AddPasses
    // =====================================================================
    void PostProcessPass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("PostProcess: ShaderRegistry not configured.");
            return;
        }

        const RGResourceHandle sceneColor = ctx.Blackboard.Get(RenderResource::SceneColorHDR);
        const RGResourceHandle sceneColorLdr = ctx.Blackboard.Get(RenderResource::SceneColorLDR);
        if (!sceneColor.IsValid() || !sceneColorLdr.IsValid())
            return;

        // Lazy pipeline creation once we know swapchain format.
        if (!m_ToneMapPipeline)
            m_ToneMapPipeline = BuildToneMapPipeline(ctx.SwapchainFormat);
        if (!m_FXAAPipeline)
            m_FXAAPipeline = BuildFXAAPipeline(ctx.SwapchainFormat);

        if (!m_ToneMapPipeline)
            return;

        // Add bloom passes before tone mapping (operates in HDR space).
        const bool bloomEnabled = m_Settings.BloomEnabled;
        if (bloomEnabled)
            AddBloomPasses(ctx, sceneColor);
        else
            m_LastBloomMip0Handle = {};

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const bool fxaaEnabled = m_Settings.FXAAEnabled && m_FXAAPipeline;

        // Capture settings for lambda.
        const float exposure = m_Settings.Exposure;
        const int toneOp = static_cast<int>(m_Settings.ToneOperator);
        const float bloomIntensity = bloomEnabled ? m_Settings.BloomIntensity : 0.0f;
        const float fxaaContrast = m_Settings.FXAAContrastThreshold;
        const float fxaaRelative = m_Settings.FXAARelativeThreshold;
        const float fxaaSubpixel = m_Settings.FXAASubpixelBlending;

        // Capture bloom handle for read dependency.
        const RGResourceHandle bloomResult = m_LastBloomMip0Handle;

        if (fxaaEnabled)
        {
            // --- Two-pass: ToneMap -> PostLdrTemp, FXAA -> SceneColorLDR ---
            RGTextureDesc ldrDesc{};
            ldrDesc.Width = resolution.width;
            ldrDesc.Height = resolution.height;
            ldrDesc.Format = ctx.SwapchainFormat;
            ldrDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ldrDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

            struct ToneMapData { RGResourceHandle Src; RGResourceHandle Bloom; RGResourceHandle Dst; };

            RGResourceHandle postLdr{};
            ctx.Graph.AddPass<ToneMapData>("Post.ToneMap",
                [&](ToneMapData& data, RGBuilder& builder)
                {
                    postLdr = builder.CreateTexture("PostLdrTemp"_id, ldrDesc);

                    data.Src = builder.Read(sceneColor,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                    if (bloomResult.IsValid())
                    {
                        data.Bloom = builder.Read(bloomResult,
                                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                    }

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(postLdr, colorInfo);

                    m_LastSceneColorHandle = data.Src;
                    m_LastPostLdrHandle = data.Dst;
                },
                [this, fi, resolution, exposure, toneOp, bloomIntensity]
                (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    SetViewportScissor(cmd, resolution);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_ToneMapPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_ToneMapPipeline->GetLayout(),
                                            0, 1, &m_ToneMapSets[fi], 0, nullptr);

                    struct { float Exposure; int Operator; float BloomIntensity; float _pad; } pc{
                        exposure, toneOp, bloomIntensity, 0.0f};
                    vkCmdPushConstants(cmd, m_ToneMapPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );

            // --- PASS 2: FXAA ---
            struct FXAAData { RGResourceHandle Src; RGResourceHandle Dst; };

            ctx.Graph.AddPass<FXAAData>("Post.FXAA",
                [&](FXAAData& data, RGBuilder& builder)
                {
                    data.Src = builder.Read(postLdr,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

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
                                      m_FXAAPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_FXAAPipeline->GetLayout(),
                                            0, 1, &m_FXAASets[fi], 0, nullptr);

                    struct {
                        float InvResX, InvResY;
                        float ContrastThreshold;
                        float RelativeThreshold;
                        float SubpixelBlending;
                    } pc{
                        1.0f / static_cast<float>(resolution.width),
                        1.0f / static_cast<float>(resolution.height),
                        fxaaContrast, fxaaRelative, fxaaSubpixel
                    };
                    vkCmdPushConstants(cmd, m_FXAAPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );
        }
        else
        {
            // --- Single pass: ToneMap -> SceneColorLDR ---
            struct ToneMapData { RGResourceHandle Src; RGResourceHandle Bloom; RGResourceHandle Dst; };

            ctx.Graph.AddPass<ToneMapData>("Post.ToneMap",
                [&](ToneMapData& data, RGBuilder& builder)
                {
                    data.Src = builder.Read(sceneColor,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                    if (bloomResult.IsValid())
                    {
                        data.Bloom = builder.Read(bloomResult,
                                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                    }

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(sceneColorLdr, colorInfo);

                    m_LastSceneColorHandle = data.Src;
                    m_LastPostLdrHandle = {};
                },
                [this, fi, resolution, exposure, toneOp, bloomIntensity]
                (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    SetViewportScissor(cmd, resolution);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_ToneMapPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_ToneMapPipeline->GetLayout(),
                                            0, 1, &m_ToneMapSets[fi], 0, nullptr);

                    struct { float Exposure; int Operator; float BloomIntensity; float _pad; } pc{
                        exposure, toneOp, bloomIntensity, 0.0f};
                    vkCmdPushConstants(cmd, m_ToneMapPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );
        }
    }

    // =====================================================================
    // PostCompile — update descriptor bindings after RenderGraph resolves resources.
    // =====================================================================
    void PostProcessPass::PostCompile(uint32_t frameIndex,
                                      std::span<const RenderGraphDebugImage> debugImages)
    {
        if (!m_Device || !m_LinearSampler)
            return;

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

        // Update tone map descriptor: binding 0 = SceneColor, binding 1 = Bloom.
        if (VkImageView sceneView = findView(m_LastSceneColorHandle))
            UpdateImageDescriptor(dev, m_ToneMapSets[frameIndex], 0, m_LinearSampler, sceneView, readLayout);

        if (VkImageView bloomView = findView(m_LastBloomMip0Handle))
            UpdateImageDescriptor(dev, m_ToneMapSets[frameIndex], 1, m_LinearSampler, bloomView, readLayout);
        else
            UpdateImageDescriptor(dev, m_ToneMapSets[frameIndex], 1, m_LinearSampler, m_DummySampled->GetView(), readLayout);

        // Update FXAA descriptor: bind PostLdr.
        if (VkImageView ldrView = findView(m_LastPostLdrHandle))
            UpdateImageDescriptor(dev, m_FXAASets[frameIndex], 0, m_LinearSampler, ldrView, readLayout);

        // Update bloom downsample descriptors.
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            if (VkImageView view = findView(m_LastBloomDownHandles[mip]))
                UpdateImageDescriptor(dev, m_BloomDownSets[frameIndex][mip], 0, m_LinearSampler, view, readLayout);
        }

        // Update bloom upsample descriptors.
        // Each upsample pass reads: binding 0 = coarser upsampled, binding 1 = current downsample.
        // We need to find the actual image views from the render graph debug images.
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            if (VkImageView coarserView = findView(m_LastBloomUpSrcHandles[mip]))
                UpdateImageDescriptor(dev, m_BloomUpSets[frameIndex][mip], 0, m_LinearSampler, coarserView, readLayout);

            // Binding 1: the downsample mip at this level.
            // The downsample handle at 'mip' was the SRC read of the next downsample pass.
            // We need the actual bloom mip resource. Search for it in debug images by matching
            // the resource created for BloomMip{mip}.
            // For simplicity, we search for any image whose resource ID matches the downsample
            // output at this mip level. The output of downsample[mip] = m_LastBloomDownHandles[mip]
            // but that's the *input* read handle. We need the written texture.
            // Since we can't easily track the written texture handle here, we look for any
            // resource named "BloomMip{mip}" in the debug images.
            std::string mipName = std::format("BloomMip{}", mip);
            Core::Hash::StringID mipId{mipName.c_str()};
            for (const auto& img : debugImages)
            {
                if (img.Name == mipId && img.View != VK_NULL_HANDLE)
                {
                    UpdateImageDescriptor(dev, m_BloomUpSets[frameIndex][mip], 1, m_LinearSampler, img.View, readLayout);
                    break;
                }
            }
        }
    }

    // =====================================================================
    // Shutdown
    // =====================================================================
    void PostProcessPass::Shutdown()
    {
        if (!m_Device) return;

        VkDevice dev = m_Device->GetLogicalDevice();

        m_ToneMapPipeline.reset();
        m_FXAAPipeline.reset();
        m_BloomDownPipeline.reset();
        m_BloomUpPipeline.reset();
        m_DummySampled.reset();

        if (m_LinearSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(dev, m_LinearSampler, nullptr);
            m_LinearSampler = VK_NULL_HANDLE;
        }
        if (m_ToneMapSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_ToneMapSetLayout, nullptr);
            m_ToneMapSetLayout = VK_NULL_HANDLE;
        }
        if (m_FXAASetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_FXAASetLayout, nullptr);
            m_FXAASetLayout = VK_NULL_HANDLE;
        }
        if (m_BloomDownSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_BloomDownSetLayout, nullptr);
            m_BloomDownSetLayout = VK_NULL_HANDLE;
        }
        if (m_BloomUpSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_BloomUpSetLayout, nullptr);
            m_BloomUpSetLayout = VK_NULL_HANDLE;
        }
    }

    // =====================================================================
    // OnResize — invalidate cached pipelines (format may change).
    // =====================================================================
    void PostProcessPass::OnResize(uint32_t, uint32_t)
    {
        m_ToneMapPipeline.reset();
        m_FXAAPipeline.reset();
        m_BloomDownPipeline.reset();
        m_BloomUpPipeline.reset();
    }
}
