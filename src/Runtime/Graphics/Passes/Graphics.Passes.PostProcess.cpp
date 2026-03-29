module;

#include <memory>
#include <span>
#include <array>
#include <string>
#include <string_view>
#include <cstring>
#include <algorithm>
#include <format>
#include <cmath>
#include <utility>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics.Passes.PostProcess;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;

import Core.Hash;
import Core.Logging;
import Core.Filesystem;

import RHI.Buffer;
import RHI.CommandUtils;
import RHI.ComputePipeline;
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
    // Initialize
    // =====================================================================
    void PostProcessPass::Initialize(RHI::VulkanDevice& device,
                                     RHI::DescriptorAllocator& descriptorPool,
                                     RHI::DescriptorLayout&)
    {
        m_Device = &device;
        m_DebugState.Initialized = true;

        VkDevice dev = m_Device->GetLogicalDevice();

        // Create linear sampler (shared by all post-process sub-passes).
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

        // Dummy image for safe default bindings.
        m_DummySampled = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_DebugState.DummySampledAllocated = static_cast<bool>(m_DummySampled);

        TransitionImageToShaderRead(*m_Device, m_DummySampled->GetHandle(), VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageView dummyView = m_DummySampled->GetView();

        // Initialize sub-passes with shared resources.
        m_Bloom.Initialize(device, descriptorPool, m_LinearSampler, dummyView);
        m_ToneMap.Initialize(device, descriptorPool, m_LinearSampler, dummyView);
        m_FXAA.Initialize(device, descriptorPool, m_LinearSampler, dummyView);
        m_SMAA.Initialize(device, descriptorPool, m_LinearSampler, dummyView);
        m_Histogram.Initialize(device, descriptorPool, m_LinearSampler, dummyView);
    }

    // =====================================================================
    // SetShaderRegistry
    // =====================================================================
    void PostProcessPass::SetShaderRegistry(const ShaderRegistry& shaderRegistry)
    {
        m_ShaderRegistry = &shaderRegistry;
        m_DebugState.ShaderRegistryConfigured = true;

        m_Bloom.SetShaderRegistry(m_ShaderRegistry);
        m_ToneMap.SetShaderRegistry(m_ShaderRegistry);
        m_FXAA.SetShaderRegistry(m_ShaderRegistry);
        m_SMAA.SetShaderRegistry(m_ShaderRegistry);
        m_Histogram.SetShaderRegistry(m_ShaderRegistry);
    }

    // =====================================================================
    // AddPasses
    // =====================================================================
    void PostProcessPass::AddPasses(RenderPassContext& ctx)
    {
        m_DebugState.LastFrameIndex = ctx.FrameIndex;
        m_DebugState.LastResolutionWidth = ctx.Resolution.width;
        m_DebugState.LastResolutionHeight = ctx.Resolution.height;
        m_DebugState.LastOutputFormat = ctx.SwapchainFormat;
        m_DebugState.BloomEnabled = m_Settings.BloomEnabled;
        m_DebugState.HistogramEnabled = m_Settings.HistogramEnabled;
        m_DebugState.FXAAEnabled = m_Settings.AntiAliasingMode == AAMode::FXAA;
        m_DebugState.SMAAEnabled = m_Settings.AntiAliasingMode == AAMode::SMAA;
        m_DebugState.LastSceneColorHandleValid = false;
        m_DebugState.LastPostLdrHandleValid = false;
        m_DebugState.LastBloomMip0HandleValid = false;
        m_DebugState.LastSMAAEdgesHandleValid = false;
        m_DebugState.LastSMAAWeightsHandleValid = false;
        m_DebugState.LastSceneColorHandle = 0;
        m_DebugState.LastPostLdrHandle = 0;
        m_DebugState.LastBloomMip0Handle = 0;
        m_DebugState.LastSMAAEdgesHandle = 0;
        m_DebugState.LastSMAAWeightsHandle = 0;
        m_DebugState.LastBloomDownHandles.fill(0u);
        m_DebugState.LastBloomUpSourceHandles.fill(0u);

        if (!m_ShaderRegistry)
        {
            Core::Log::Error("PostProcess: ShaderRegistry not configured.");
            return;
        }

        const RGResourceHandle sceneColor = ctx.Blackboard.Get(RenderResource::SceneColorHDR);
        const RGResourceHandle sceneColorLdr = ctx.Blackboard.Get(RenderResource::SceneColorLDR);
        m_DebugState.LastSceneColorHandleValid = sceneColor.IsValid();
        m_DebugState.LastSceneColorHandle = sceneColor.IsValid() ? sceneColor.ID : 0u;
        if (!sceneColor.IsValid() || !sceneColorLdr.IsValid())
            return;

        // --- Histogram (reads HDR scene color) ---
        if (m_Settings.HistogramEnabled)
            m_Histogram.AddPass(ctx, sceneColor, m_Settings);

        // --- Bloom (operates in HDR space before tone mapping) ---
        RGResourceHandle bloomResult{};
        if (m_Settings.BloomEnabled)
        {
            m_Bloom.AddPasses(ctx, sceneColor, m_Settings);
            bloomResult = m_Bloom.GetBloomResult();
        }

        m_DebugState.LastBloomMip0HandleValid = bloomResult.IsValid();
        m_DebugState.LastBloomMip0Handle = bloomResult.IsValid() ? bloomResult.ID : 0u;

        const AAMode aaMode = m_Settings.AntiAliasingMode;
        const bool fxaaEnabled = (aaMode == AAMode::FXAA);
        const bool smaaEnabled = (aaMode == AAMode::SMAA);
        m_DebugState.FXAAEnabled = fxaaEnabled;
        m_DebugState.SMAAEnabled = smaaEnabled;

        if (fxaaEnabled || smaaEnabled)
        {
            // --- Multi-pass: ToneMap -> PostLdrTemp, then AA -> SceneColorLDR ---
            RGTextureDesc ldrDesc{};
            ldrDesc.Width = ctx.Resolution.width;
            ldrDesc.Height = ctx.Resolution.height;
            ldrDesc.Format = ctx.SwapchainFormat;
            ldrDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ldrDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

            // Create PostLdrTemp as a transient texture via a setup pass capture.
            RGResourceHandle postLdr{};
            struct PostLdrSetupData { RGResourceHandle Tex; };
            ctx.Graph.AddPass<PostLdrSetupData>("Post.ToneMap.Setup",
                [&](PostLdrSetupData& data, RGBuilder& builder)
                {
                    postLdr = builder.CreateTexture("PostLdrTemp"_id, ldrDesc);
                    data.Tex = postLdr;
                },
                [](const PostLdrSetupData&, const RGRegistry&, VkCommandBuffer) {}
            );

            m_ToneMap.AddPass(ctx, sceneColor, bloomResult, postLdr, m_Settings);

            m_LastSceneColorHandle = m_ToneMap.GetLastSceneColorHandle();
            m_LastPostLdrHandle = m_ToneMap.GetLastOutputHandle();
            m_DebugState.LastSceneColorHandleValid = m_LastSceneColorHandle.IsValid();
            m_DebugState.LastSceneColorHandle = m_LastSceneColorHandle.IsValid() ? m_LastSceneColorHandle.ID : 0u;
            m_DebugState.LastPostLdrHandleValid = m_LastPostLdrHandle.IsValid();
            m_DebugState.LastPostLdrHandle = m_LastPostLdrHandle.IsValid() ? m_LastPostLdrHandle.ID : 0u;

            if (smaaEnabled)
                m_SMAA.AddPasses(ctx, postLdr, sceneColorLdr, m_Settings);
            else
                m_FXAA.AddPass(ctx, postLdr, sceneColorLdr, m_Settings);
        }
        else
        {
            // --- Single pass: ToneMap -> SceneColorLDR (no AA) ---
            m_ToneMap.AddPass(ctx, sceneColor, bloomResult, sceneColorLdr, m_Settings);

            m_LastSceneColorHandle = m_ToneMap.GetLastSceneColorHandle();
            m_LastPostLdrHandle = {};
            m_DebugState.LastSceneColorHandleValid = m_LastSceneColorHandle.IsValid();
            m_DebugState.LastSceneColorHandle = m_LastSceneColorHandle.IsValid() ? m_LastSceneColorHandle.ID : 0u;
            m_DebugState.LastPostLdrHandleValid = false;
            m_DebugState.LastPostLdrHandle = 0u;
        }

        // Update debug state from sub-passes.
        m_DebugState.ToneMapPipelineBuilt = m_ToneMap.IsPipelineBuilt();
        m_DebugState.FXAAPipelineBuilt = m_FXAA.IsPipelineBuilt();
        m_DebugState.BloomDownPipelineBuilt = m_Bloom.IsDownPipelineBuilt();
        m_DebugState.BloomUpPipelineBuilt = m_Bloom.IsUpPipelineBuilt();
        m_DebugState.HistogramPipelineBuilt = m_Histogram.IsPipelineBuilt();
        m_DebugState.SMAAEdgePipelineBuilt = m_SMAA.IsEdgePipelineBuilt();
        m_DebugState.SMAABlendPipelineBuilt = m_SMAA.IsBlendPipelineBuilt();
        m_DebugState.SMAAResolvePipelineBuilt = m_SMAA.IsResolvePipelineBuilt();

        m_DebugState.LastSMAAEdgesHandleValid = m_SMAA.GetLastEdgesHandle().IsValid();
        m_DebugState.LastSMAAEdgesHandle = m_SMAA.GetLastEdgesHandle().IsValid() ? m_SMAA.GetLastEdgesHandle().ID : 0u;
        m_DebugState.LastSMAAWeightsHandleValid = m_SMAA.GetLastWeightsHandle().IsValid();
        m_DebugState.LastSMAAWeightsHandle = m_SMAA.GetLastWeightsHandle().IsValid() ? m_SMAA.GetLastWeightsHandle().ID : 0u;

        const auto& bloomDownHandles = m_Bloom.GetLastDownHandles();
        const auto& bloomUpSrcHandles = m_Bloom.GetLastUpSrcHandles();
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            m_DebugState.LastBloomDownHandles[mip] = bloomDownHandles[mip].IsValid() ? bloomDownHandles[mip].ID : 0u;
            m_DebugState.LastBloomUpSourceHandles[mip] = bloomUpSrcHandles[mip].IsValid() ? bloomUpSrcHandles[mip].ID : 0u;
        }
    }

    // =====================================================================
    // PostCompile
    // =====================================================================
    void PostProcessPass::PostCompile(uint32_t frameIndex,
                                      std::span<const RenderGraphDebugImage> debugImages)
    {
        m_DebugState.LastFrameIndex = frameIndex;

        if (!m_Device || !m_LinearSampler)
            return;

        VkImageView dummyView = m_DummySampled ? m_DummySampled->GetView() : VK_NULL_HANDLE;

        m_ToneMap.PostCompile(frameIndex, debugImages, m_LinearSampler, dummyView);
        m_Bloom.PostCompile(frameIndex, debugImages, m_LinearSampler);

        if (m_Settings.AntiAliasingMode == AAMode::FXAA)
            m_FXAA.PostCompile(frameIndex, debugImages, m_LinearSampler, m_LastPostLdrHandle);
        else if (m_Settings.AntiAliasingMode == AAMode::SMAA)
            m_SMAA.PostCompile(frameIndex, debugImages, m_LinearSampler, m_LastPostLdrHandle);

        if (m_Settings.HistogramEnabled)
            m_Histogram.PostCompile(frameIndex, debugImages, m_LinearSampler, m_LastSceneColorHandle);
    }

    // =====================================================================
    // Shutdown
    // =====================================================================
    void PostProcessPass::Shutdown()
    {
        m_DebugState.Initialized = false;
        m_DebugState.DummySampledAllocated = false;

        m_Bloom.Shutdown();
        m_ToneMap.Shutdown();
        m_FXAA.Shutdown();
        m_SMAA.Shutdown();
        m_Histogram.Shutdown();

        m_DummySampled.reset();

        if (m_Device)
        {
            VkDevice dev = m_Device->GetLogicalDevice();
            if (m_LinearSampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(dev, m_LinearSampler, nullptr);
                m_LinearSampler = VK_NULL_HANDLE;
            }
        }
    }

    // =====================================================================
    // OnResize
    // =====================================================================
    void PostProcessPass::OnResize(uint32_t width, uint32_t height)
    {
        ++m_DebugState.ResizeCount;
        m_DebugState.LastResizeWidth = width;
        m_DebugState.LastResizeHeight = height;

        m_Bloom.OnResize();
        m_ToneMap.OnResize();
        m_FXAA.OnResize();
        m_SMAA.OnResize();
        m_Histogram.OnResize();

        m_DebugState.ToneMapPipelineBuilt = false;
        m_DebugState.FXAAPipelineBuilt = false;
        m_DebugState.SMAAEdgePipelineBuilt = false;
        m_DebugState.SMAABlendPipelineBuilt = false;
        m_DebugState.SMAAResolvePipelineBuilt = false;
        m_DebugState.BloomDownPipelineBuilt = false;
        m_DebugState.BloomUpPipelineBuilt = false;
        m_DebugState.HistogramPipelineBuilt = false;
    }
}
