module;

#include <memory>
#include <span>
#include <string>
#include <cstring>
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
    // Initialize
    // =====================================================================
    void PostProcessPass::Initialize(RHI::VulkanDevice& device,
                                     RHI::DescriptorAllocator& descriptorPool,
                                     RHI::DescriptorLayout&)
    {
        m_Device = &device;

        // Create linear sampler for HDR scene color and LDR intermediate.
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
        CheckVkResult(vkCreateSampler(m_Device->GetLogicalDevice(), &samp, nullptr, &m_LinearSampler),
                      "PostProcess", "vkCreateSampler");

        // Tone map descriptor set layout: 1 combined image sampler for HDR scene color.
        m_ToneMapSetLayout = CreateSamplerDescriptorSetLayout(
            m_Device->GetLogicalDevice(), VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.ToneMap");

        // FXAA descriptor set layout: 1 combined image sampler for LDR input.
        m_FXAASetLayout = CreateSamplerDescriptorSetLayout(
            m_Device->GetLogicalDevice(), VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.FXAA");

        // Allocate per-frame descriptor sets.
        AllocatePerFrameSets<3>(descriptorPool, m_ToneMapSetLayout, m_ToneMapSets);
        AllocatePerFrameSets<3>(descriptorPool, m_FXAASetLayout, m_FXAASets);

        m_DummySampled = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionImageToShaderRead(*m_Device, m_DummySampled->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);

        for (auto& set : m_ToneMapSets)
        {
            UpdateImageDescriptor(m_Device->GetLogicalDevice(), set, 0,
                                  m_LinearSampler, m_DummySampled->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        for (auto& set : m_FXAASets)
        {
            UpdateImageDescriptor(m_Device->GetLogicalDevice(), set, 0,
                                  m_LinearSampler, m_DummySampled->GetView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) + sizeof(int); // Exposure + Operator
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

        // Push constants: InvResolution (vec2) + ContrastThreshold + RelativeThreshold + SubpixelBlending
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

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const bool fxaaEnabled = m_Settings.FXAAEnabled && m_FXAAPipeline;

        // Capture settings for lambda.
        const float exposure = m_Settings.Exposure;
        const int toneOp = static_cast<int>(m_Settings.ToneOperator);
        const float fxaaContrast = m_Settings.FXAAContrastThreshold;
        const float fxaaRelative = m_Settings.FXAARelativeThreshold;
        const float fxaaSubpixel = m_Settings.FXAASubpixelBlending;

        if (fxaaEnabled)
        {
            // --- Two-pass: ToneMap -> PostLdrTemp, FXAA -> SceneColorLDR ---

            // Create transient LDR intermediate.
            RGTextureDesc ldrDesc{};
            ldrDesc.Width = resolution.width;
            ldrDesc.Height = resolution.height;
            ldrDesc.Format = ctx.SwapchainFormat;
            ldrDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ldrDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

            // --- PASS 1: Tone Map ---
            struct ToneMapData
            {
                RGResourceHandle Src;
                RGResourceHandle Dst;
            };

            RGResourceHandle postLdr{};
            ctx.Graph.AddPass<ToneMapData>("Post.ToneMap",
                [&](ToneMapData& data, RGBuilder& builder)
                {
                    postLdr = builder.CreateTexture("PostLdrTemp"_id, ldrDesc);

                    data.Src = builder.Read(sceneColor,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(postLdr, colorInfo);

                    m_LastSceneColorHandle = data.Src;
                    m_LastPostLdrHandle = data.Dst;
                },
                [this, fi, resolution, exposure, toneOp]
                (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    SetViewportScissor(cmd, resolution);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_ToneMapPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_ToneMapPipeline->GetLayout(),
                                            0, 1, &m_ToneMapSets[fi], 0, nullptr);

                    struct { float Exposure; int Operator; } pc{exposure, toneOp};
                    vkCmdPushConstants(cmd, m_ToneMapPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

                    vkCmdDraw(cmd, 3, 1, 0, 0); // Fullscreen triangle
                }
            );

            // --- PASS 2: FXAA ---
            struct FXAAData
            {
                RGResourceHandle Src;
                RGResourceHandle Dst;
            };

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
            struct ToneMapData
            {
                RGResourceHandle Src;
                RGResourceHandle Dst;
            };

            ctx.Graph.AddPass<ToneMapData>("Post.ToneMap",
                [&](ToneMapData& data, RGBuilder& builder)
                {
                    data.Src = builder.Read(sceneColor,
                                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(sceneColorLdr, colorInfo);

                    m_LastSceneColorHandle = data.Src;
                    m_LastPostLdrHandle = {};
                },
                [this, fi, resolution, exposure, toneOp]
                (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    SetViewportScissor(cmd, resolution);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_ToneMapPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_ToneMapPipeline->GetLayout(),
                                            0, 1, &m_ToneMapSets[fi], 0, nullptr);

                    struct { float Exposure; int Operator; } pc{exposure, toneOp};
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

        // Update tone map descriptor: bind SceneColor.
        if (m_LastSceneColorHandle.IsValid())
        {
            for (const auto& img : debugImages)
            {
                if (img.Resource == m_LastSceneColorHandle.ID && img.View != VK_NULL_HANDLE)
                {
                    UpdateImageDescriptor(m_Device->GetLogicalDevice(),
                                          m_ToneMapSets[frameIndex], 0,
                                          m_LinearSampler, img.View,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    break;
                }
            }
        }

        // Update FXAA descriptor: bind PostLdr.
        if (m_LastPostLdrHandle.IsValid())
        {
            for (const auto& img : debugImages)
            {
                if (img.Resource == m_LastPostLdrHandle.ID && img.View != VK_NULL_HANDLE)
                {
                    UpdateImageDescriptor(m_Device->GetLogicalDevice(),
                                          m_FXAASets[frameIndex], 0,
                                          m_LinearSampler, img.View,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
    }

    // =====================================================================
    // OnResize — invalidate cached pipelines (format may change).
    // =====================================================================
    void PostProcessPass::OnResize(uint32_t, uint32_t)
    {
        // Pipelines are format-dependent; reset on resize to rebuild lazily.
        m_ToneMapPipeline.reset();
        m_FXAAPipeline.reset();
    }
}
