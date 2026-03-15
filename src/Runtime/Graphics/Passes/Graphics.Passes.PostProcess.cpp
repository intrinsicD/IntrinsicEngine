module;

#include <memory>
#include <span>
#include <array>
#include <string>
#include <cstring>
#include <algorithm>
#include <format>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"
#include "Graphics.SMAALookupTextures.hpp"

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
        m_DebugState.Initialized = true;

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

        // SMAA Edge Detection: 1 binding (LDR input).
        m_SMAAEdgeSetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.SMAA.Edge");

        // SMAA Blend Weight: 3 bindings (edges, area tex, search tex).
        m_SMAABlendSetLayout = CreateMultiSamplerSetLayout(
            dev, 3, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.SMAA.Blend");

        // SMAA Neighborhood Blending: 2 bindings (LDR input, blend weights).
        m_SMAAResolveSetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.SMAA.Resolve");

        // Bloom downsample: 1 binding (source mip).
        m_BloomDownSetLayout = CreateSamplerDescriptorSetLayout(
            dev, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.BloomDown");

        // Bloom upsample: 2 bindings (coarser mip + current downsample).
        m_BloomUpSetLayout = CreateMultiSamplerSetLayout(
            dev, 2, VK_SHADER_STAGE_FRAGMENT_BIT, "PostProcess.BloomUp");

        // Allocate per-frame descriptor sets.
        AllocatePerFrameSets<3>(descriptorPool, m_ToneMapSetLayout, m_ToneMapSets);
        AllocatePerFrameSets<3>(descriptorPool, m_FXAASetLayout, m_FXAASets);
        AllocatePerFrameSets<3>(descriptorPool, m_SMAAEdgeSetLayout, m_SMAAEdgeSets);
        AllocatePerFrameSets<3>(descriptorPool, m_SMAABlendSetLayout, m_SMAABlendSets);
        AllocatePerFrameSets<3>(descriptorPool, m_SMAAResolveSetLayout, m_SMAAResolveSets);

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
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_DebugState.DummySampledAllocated = static_cast<bool>(m_DummySampled);

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

        for (auto& set : m_SMAAEdgeSets)
            UpdateImageDescriptor(dev, set, 0, m_LinearSampler, dummyView, readLayout);

        for (auto& set : m_SMAABlendSets)
        {
            UpdateImageDescriptor(dev, set, 0, m_LinearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 1, m_LinearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 2, m_LinearSampler, dummyView, readLayout);
        }

        for (auto& set : m_SMAAResolveSets)
        {
            UpdateImageDescriptor(dev, set, 0, m_LinearSampler, dummyView, readLayout);
            UpdateImageDescriptor(dev, set, 1, m_LinearSampler, dummyView, readLayout);
        }

        for (uint32_t fi = 0; fi < 3; ++fi)
        {
            for (uint32_t m = 0; m < kBloomMipCount; ++m)
            {
                UpdateImageDescriptor(dev, m_BloomDownSets[fi][m], 0, m_LinearSampler, dummyView, readLayout);
                UpdateImageDescriptor(dev, m_BloomUpSets[fi][m], 0, m_LinearSampler, dummyView, readLayout);
                UpdateImageDescriptor(dev, m_BloomUpSets[fi][m], 1, m_LinearSampler, dummyView, readLayout);
            }
        }

        // Histogram: descriptor layout with 1 combined-image-sampler + 1 SSBO.
        {
            VkDescriptorSetLayoutBinding bindings[2] = {};
            bindings[0].binding = 0;
            bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            bindings[1].binding = 1;
            bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 2;
            layoutInfo.pBindings = bindings;

            CheckVkResult(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_HistogramSetLayout),
                          "PostProcess.Histogram", "vkCreateDescriptorSetLayout");
        }

        // Allocate per-frame histogram descriptor sets and SSBO buffers.
        constexpr size_t kHistogramBytes = kHistogramBinCount * sizeof(uint32_t);
        for (uint32_t fi = 0; fi < 3; ++fi)
        {
            m_HistogramSets[fi] = descriptorPool.Allocate(m_HistogramSetLayout);

            m_HistogramBuffers[fi] = std::make_unique<RHI::VulkanBuffer>(
                device, kHistogramBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_TO_CPU);

            // Bind the SSBO to the histogram descriptor set.
            UpdateSSBODescriptor(dev, m_HistogramSets[fi], 1,
                                 m_HistogramBuffers[fi]->GetHandle(), kHistogramBytes);

            // Bind dummy image for the sampler binding.
            UpdateImageDescriptor(dev, m_HistogramSets[fi], 0, m_LinearSampler, dummyView, readLayout);
        }

        // SMAA lookup textures (area + search).
        InitializeSMAALookupTextures();

        // Bind SMAA lookup textures to blend weight descriptor sets.
        if (m_SMAAAreaTex && m_SMAASearchTex)
        {
            for (auto& set : m_SMAABlendSets)
            {
                UpdateImageDescriptor(dev, set, 1, m_LinearSampler, m_SMAAAreaTex->GetView(), readLayout);
                UpdateImageDescriptor(dev, set, 2, m_LinearSampler, m_SMAASearchTex->GetView(), readLayout);
            }
        }
    }

    // =====================================================================
    // UploadTextureViaStaging — one-shot CPU→GPU upload with layout
    // transitions (UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY).
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
    // InitializeSMAALookupTextures
    // =====================================================================
    void PostProcessPass::InitializeSMAALookupTextures()
    {
        using namespace Graphics::SMAA;

        // Area texture (160x560, RG8).
        auto areaData = GenerateAreaTexture();
        m_SMAAAreaTex = std::make_unique<RHI::VulkanImage>(
            *m_Device,
            kAreaTexWidth, kAreaTexHeight, 1,
            VK_FORMAT_R8G8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        UploadTextureViaStaging(*m_Device, *m_SMAAAreaTex,
            areaData.data(), areaData.size(),
            static_cast<uint32_t>(kAreaTexWidth),
            static_cast<uint32_t>(kAreaTexHeight));

        // Search texture (66x33, R8).
        auto searchData = GenerateSearchTexture();
        m_SMAASearchTex = std::make_unique<RHI::VulkanImage>(
            *m_Device,
            kSearchTexWidth, kSearchTexHeight, 1,
            VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        UploadTextureViaStaging(*m_Device, *m_SMAASearchTex,
            searchData.data(), searchData.size(),
            static_cast<uint32_t>(kSearchTexWidth),
            static_cast<uint32_t>(kSearchTexHeight));

        Core::Log::Info("PostProcess: SMAA lookup textures initialized (Area {}x{}, Search {}x{})",
                        kAreaTexWidth, kAreaTexHeight, kSearchTexWidth, kSearchTexHeight);
    }

    // =====================================================================
    // BuildToneMapPipeline
    // Descriptor sets: set 0 = m_ToneMapSetLayout (HDR input sampler).
    // Push constants (fragment, 80B): ToneMapParams (16B) + ColorGrading (64B).
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

        // Push constants: ToneMap base (16B) + ColorGrading (64B) = 80 bytes.
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

    // =====================================================================
    // BuildFXAAPipeline
    // Descriptor sets: set 0 = m_FXAASetLayout (LDR input sampler).
    // Push constants (fragment, 20B): InvResolution (vec2) + QualitySubpix,
    //   QualityEdgeThreshold, QualityEdgeThresholdMin (3× float).
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
    // BuildSMAAEdgePipeline
    // Descriptor sets: set 0 = m_SMAAEdgeSetLayout (LDR input sampler).
    // Push constants (fragment, 16B): InvResolution (vec2) + EdgeThreshold
    //   (float) + pad (float).
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildSMAAEdgePipeline(VkFormat edgeFormat)
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
        pb.AddDescriptorSetLayout(m_SMAAEdgeSetLayout); // set = 0

        // Push constants: InvResolution (vec2) + EdgeThreshold (float) + pad (float) = 16B
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

    // =====================================================================
    // BuildSMAABlendPipeline
    // Descriptor sets: set 0 = m_SMAABlendSetLayout (edge tex + area tex +
    //   search tex samplers).
    // Push constants (fragment, 16B): InvResolution (vec2) + MaxSearchSteps
    //   (int) + MaxSearchStepsDiag (int).
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildSMAABlendPipeline(VkFormat weightFormat)
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
        pb.AddDescriptorSetLayout(m_SMAABlendSetLayout); // set = 0

        // Push constants: InvResolution (vec2) + MaxSearchSteps (int) + MaxSearchStepsDiag (int) = 16B
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

    // =====================================================================
    // BuildSMAAResolvePipeline
    // Descriptor sets: set 0 = m_SMAAResolveSetLayout (blend weight tex +
    //   original LDR input samplers).
    // Push constants (fragment, 16B): InvResolution (vec2) + 2× pad (float).
    // =====================================================================
    std::unique_ptr<RHI::GraphicsPipeline> PostProcessPass::BuildSMAAResolvePipeline(VkFormat outputFormat)
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
        pb.AddDescriptorSetLayout(m_SMAAResolveSetLayout); // set = 0

        // Push constants: InvResolution (vec2) + 2 pad (float) = 16B
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

    // =====================================================================
    // BuildBloomDownsamplePipeline
    // Descriptor sets: set 0 = m_BloomDownSetLayout (source mip sampler).
    // Push constants (fragment, 16B): InvSrcResolution (vec2) + Threshold
    //   (float) + IsFirstMip (int).
    // Output: R16G16B16A16_SFLOAT.
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
    // Descriptor sets: set 0 = m_BloomUpSetLayout (coarser mip + current mip
    //   samplers).
    // Push constants (fragment, 16B): InvCoarserResolution (vec2) +
    //   FilterRadius (float) + pad (float).
    // Output: R16G16B16A16_SFLOAT.
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
                                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Dst = builder.WriteColor(bloomMips[capMip], colorInfo);

                    m_LastBloomDownHandles[capMip] = data.Src;
                    m_LastBloomMipWriteHandles[capMip] = data.Dst;
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
                                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
                    data.CurrentSrc = builder.Read(currentDown,
                                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

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
    // BuildHistogramPipeline
    // Descriptor sets: set 0 = m_HistogramSetLayout (HDR input image +
    //   histogram SSBO).
    // Push constants (compute, 16B): InvWidth (float), InvHeight (float),
    //   MinLogLum (float), RangeLogLum (float).
    // =====================================================================
    std::unique_ptr<RHI::ComputePipeline> PostProcessPass::BuildHistogramPipeline()
    {
        auto compPathOpt = m_ShaderRegistry->Get("Post.Histogram.Comp"_id);
        if (!compPathOpt)
        {
            Core::Log::Error("PostProcess: histogram compute shader not registered.");
            return nullptr;
        }

        std::string compPath = *compPathOpt;

        RHI::ShaderModule comp(*m_Device, compPath, RHI::ShaderStage::Compute);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::ComputePipelineBuilder cb(deviceAlias);
        cb.SetShader(&comp);
        cb.AddDescriptorSetLayout(m_HistogramSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4; // InvWidth, InvHeight, MinLogLum, RangeLogLum
        cb.AddPushConstantRange(pcr);

        auto built = cb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build Histogram pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    // =====================================================================
    // AddHistogramPass — compute shader to build luminance histogram.
    // =====================================================================
    void PostProcessPass::AddHistogramPass(RenderPassContext& ctx, RGResourceHandle sceneColor)
    {
        if (!m_HistogramPipeline)
            m_HistogramPipeline = BuildHistogramPipeline();
        if (!m_HistogramPipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const float minEV = m_Settings.HistogramMinEV;
        const float maxEV = m_Settings.HistogramMaxEV;
        const float rangeEV = (maxEV - minEV);
        const float invRange = (rangeEV > 1e-6f) ? (1.0f / rangeEV) : 1.0f;

        // Read back the previous frame's histogram before we overwrite it.
        // We read from the current frame-in-flight buffer which was last written
        // 3 frames ago (ring buffer with 3 frames in flight).
        if (m_HistogramBuffers[fi])
        {
            const auto* mappedData = static_cast<const uint32_t*>(m_HistogramBuffers[fi]->GetMappedData());
            if (mappedData)
            {
                std::memcpy(m_HistogramReadback.Bins, mappedData,
                            kHistogramBinCount * sizeof(uint32_t));

                // Compute average luminance from histogram.
                uint64_t totalPixels = 0;
                double weightedSum = 0.0;
                for (uint32_t i = 0; i < kHistogramBinCount; ++i)
                {
                    if (m_HistogramReadback.Bins[i] > 0)
                    {
                        totalPixels += m_HistogramReadback.Bins[i];
                        float binCenter = minEV + (static_cast<float>(i) + 0.5f) / 254.0f * rangeEV;
                        weightedSum += static_cast<double>(m_HistogramReadback.Bins[i]) * binCenter;
                    }
                }
                if (totalPixels > 0)
                    m_HistogramReadback.AverageLuminance = std::exp2(
                        static_cast<float>(weightedSum / static_cast<double>(totalPixels)));
                else
                    m_HistogramReadback.AverageLuminance = 0.0f;

                m_HistogramReadback.Valid = true;
            }
        }

        struct HistogramData { RGResourceHandle Src; RGResourceHandle Buf; };

        ctx.Graph.AddPass<HistogramData>("Post.Histogram",
            [&, sceneColor, fi](HistogramData& data, RGBuilder& builder)
            {
                data.Src = builder.Read(sceneColor,
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                data.Buf = builder.ImportBuffer("HistogramSSBO"_id, *m_HistogramBuffers[fi]);
                builder.Write(data.Buf,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                // Cache the read handle so PostCompile can patch the descriptor.
                m_LastSceneColorHandle = data.Src;
            },
            [this, fi, resolution, minEV, invRange]
            (const HistogramData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                constexpr size_t kHistogramBytes = kHistogramBinCount * sizeof(uint32_t);

                // Clear the histogram buffer to zero before accumulation.
                vkCmdFillBuffer(cmd, m_HistogramBuffers[fi]->GetHandle(),
                                0, kHistogramBytes, 0);

                // Barrier: transfer write -> compute read/write.
                VkBufferMemoryBarrier2 clearBarrier{};
                clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                clearBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                clearBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                clearBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                clearBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                clearBarrier.buffer = m_HistogramBuffers[fi]->GetHandle();
                clearBarrier.offset = 0;
                clearBarrier.size = kHistogramBytes;

                VkDependencyInfo dep{};
                dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.bufferMemoryBarrierCount = 1;
                dep.pBufferMemoryBarriers = &clearBarrier;
                vkCmdPipelineBarrier2(cmd, &dep);

                // Bind and dispatch.
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                  m_HistogramPipeline->GetHandle());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        m_HistogramPipeline->GetLayout(),
                                        0, 1, &m_HistogramSets[fi], 0, nullptr);

                struct HistogramPC {
                    uint32_t Width;
                    uint32_t Height;
                    float    MinLogLum;
                    float    RangeLogLum;
                };
                static_assert(sizeof(HistogramPC) == 16, "HistogramPC must be 16 bytes");
                HistogramPC pc{
                    resolution.width,
                    resolution.height,
                    minEV,
                    invRange
                };
                vkCmdPushConstants(cmd, m_HistogramPipeline->GetLayout(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t groupsX = (resolution.width + 15) / 16;
                const uint32_t groupsY = (resolution.height + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            }
        );
    }

    // =====================================================================
    // AddSMAAPasses — 3-pass SMAA: edge detection, blend weight, resolve.
    // =====================================================================
    void PostProcessPass::AddSMAAPasses(RenderPassContext& ctx,
                                         RGResourceHandle postLdr,
                                         RGResourceHandle sceneColorLdr)
    {
        if (!m_SMAAEdgePipeline)
            m_SMAAEdgePipeline = BuildSMAAEdgePipeline(VK_FORMAT_R8G8_UNORM);
        if (!m_SMAABlendPipeline)
            m_SMAABlendPipeline = BuildSMAABlendPipeline(VK_FORMAT_R8G8B8A8_UNORM);
        if (!m_SMAAResolvePipeline)
            m_SMAAResolvePipeline = BuildSMAAResolvePipeline(ctx.SwapchainFormat);

        if (!m_SMAAEdgePipeline || !m_SMAABlendPipeline || !m_SMAAResolvePipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const float edgeThreshold = m_Settings.SMAAEdgeThreshold;
        const int maxSearch = m_Settings.SMAAMaxSearchSteps;
        const int maxSearchDiag = m_Settings.SMAAMaxSearchStepsDiag;

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
                                  m_SMAAEdgePipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_SMAAEdgePipeline->GetLayout(),
                                        0, 1, &m_SMAAEdgeSets[fi], 0, nullptr);

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
                vkCmdPushConstants(cmd, m_SMAAEdgePipeline->GetLayout(),
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

                m_LastSMAAEdgesHandle = data.Edges;
            },
            [this, fi, resolution, maxSearch, maxSearchDiag]
            (const SMAABlendData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_SMAABlendPipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_SMAABlendPipeline->GetLayout(),
                                        0, 1, &m_SMAABlendSets[fi], 0, nullptr);

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
                vkCmdPushConstants(cmd, m_SMAABlendPipeline->GetLayout(),
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

                m_LastSMAAWeightsHandle = data.Weights;
            },
            [this, fi, resolution]
            (const SMAAResolveData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                SetViewportScissor(cmd, resolution);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_SMAAResolvePipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_SMAAResolvePipeline->GetLayout(),
                                        0, 1, &m_SMAAResolveSets[fi], 0, nullptr);

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
                vkCmdPushConstants(cmd, m_SMAAResolvePipeline->GetLayout(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        );
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

        // Lazy pipeline creation once we know swapchain format.
        if (!m_ToneMapPipeline)
            m_ToneMapPipeline = BuildToneMapPipeline(ctx.SwapchainFormat);
        if (!m_FXAAPipeline)
            m_FXAAPipeline = BuildFXAAPipeline(ctx.SwapchainFormat);
        m_DebugState.ToneMapPipelineBuilt = static_cast<bool>(m_ToneMapPipeline);
        m_DebugState.FXAAPipelineBuilt = static_cast<bool>(m_FXAAPipeline);

        if (!m_ToneMapPipeline)
            return;

        // Add histogram compute pass (reads HDR scene color, builds luminance histogram).
        if (m_Settings.HistogramEnabled)
            AddHistogramPass(ctx, sceneColor);

        // Add bloom passes before tone mapping (operates in HDR space).
        const bool bloomEnabled = m_Settings.BloomEnabled;
        if (bloomEnabled)
            AddBloomPasses(ctx, sceneColor);
        else
            m_LastBloomMip0Handle = {};

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;

        const AAMode aaMode = m_Settings.AntiAliasingMode;
        const bool fxaaEnabled = (aaMode == AAMode::FXAA) && m_FXAAPipeline;
        const bool smaaEnabled = (aaMode == AAMode::SMAA);
        m_DebugState.FXAAEnabled = fxaaEnabled;
        m_DebugState.SMAAEnabled = smaaEnabled;

        // Capture settings for lambda.
        const float exposure = m_Settings.Exposure;
        const int toneOp = static_cast<int>(m_Settings.ToneOperator);
        const float bloomIntensity = bloomEnabled ? m_Settings.BloomIntensity : 0.0f;
        const float fxaaContrast = m_Settings.FXAAContrastThreshold;
        const float fxaaRelative = m_Settings.FXAARelativeThreshold;
        const float fxaaSubpixel = m_Settings.FXAASubpixelBlending;

        // Color grading push constant block (must match shader layout, 80B total).
        struct ToneMapPC
        {
            float     Exposure;        // offset  0
            int       Operator;        // offset  4
            float     BloomIntensity;  // offset  8
            int       ColorGradingOn;  // offset 12
            float     Saturation;      // offset 16
            float     Contrast;        // offset 20
            float     ColorTempOffset; // offset 24
            float     TintOffset;      // offset 28
            glm::vec3 Lift;  float _pad0; // offset 32
            glm::vec3 Gamma; float _pad1; // offset 48
            glm::vec3 Gain;  float _pad2; // offset 64
        };
        static_assert(sizeof(ToneMapPC) == 80, "ToneMapPC must be 80 bytes");

        const bool cgEnabled = m_Settings.ColorGradingEnabled;
        ToneMapPC toneMapPC{};
        toneMapPC.Exposure        = exposure;
        toneMapPC.Operator        = toneOp;
        toneMapPC.BloomIntensity  = bloomIntensity;
        toneMapPC.ColorGradingOn  = cgEnabled ? 1 : 0;
        toneMapPC.Saturation      = m_Settings.Saturation;
        toneMapPC.Contrast        = m_Settings.Contrast;
        toneMapPC.ColorTempOffset = m_Settings.ColorTempOffset;
        toneMapPC.TintOffset      = m_Settings.TintOffset;
        toneMapPC.Lift             = m_Settings.Lift;
        toneMapPC.Gamma            = m_Settings.Gamma;
        toneMapPC.Gain             = m_Settings.Gain;

        // Capture bloom handle for read dependency.
        const RGResourceHandle bloomResult = m_LastBloomMip0Handle;
        m_DebugState.LastBloomMip0HandleValid = bloomResult.IsValid();
        m_DebugState.LastBloomMip0Handle = bloomResult.IsValid() ? bloomResult.ID : 0u;

        if (fxaaEnabled || smaaEnabled)
        {
            // --- Multi-pass: ToneMap -> PostLdrTemp, then AA -> SceneColorLDR ---
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
                    data.Dst = builder.WriteColor(postLdr, colorInfo);

                    m_LastSceneColorHandle = data.Src;
                    m_LastPostLdrHandle = data.Dst;
                    m_DebugState.LastSceneColorHandleValid = data.Src.IsValid();
                    m_DebugState.LastSceneColorHandle = data.Src.IsValid() ? data.Src.ID : 0u;
                    m_DebugState.LastPostLdrHandleValid = data.Dst.IsValid();
                    m_DebugState.LastPostLdrHandle = data.Dst.IsValid() ? data.Dst.ID : 0u;
                },
                [this, fi, resolution, toneMapPC]
                (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    SetViewportScissor(cmd, resolution);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_ToneMapPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_ToneMapPipeline->GetLayout(),
                                            0, 1, &m_ToneMapSets[fi], 0, nullptr);

                    vkCmdPushConstants(cmd, m_ToneMapPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapPC), &toneMapPC);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );

            if (smaaEnabled)
            {
                // --- SMAA 3-pass ---
                AddSMAAPasses(ctx, postLdr, sceneColorLdr);
            }
            else
            {
                // --- FXAA single pass ---
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
                                          m_FXAAPipeline->GetHandle());
                        vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_FXAAPipeline->GetLayout(),
                                                0, 1, &m_FXAASets[fi], 0, nullptr);

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
                        vkCmdPushConstants(cmd, m_FXAAPipeline->GetLayout(),
                                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                        vkCmdDraw(cmd, 3, 1, 0, 0);
                    }
                );
            }
        }
        else
        {
            // --- Single pass: ToneMap -> SceneColorLDR (no AA) ---
            struct ToneMapData { RGResourceHandle Src; RGResourceHandle Bloom; RGResourceHandle Dst; };

            ctx.Graph.AddPass<ToneMapData>("Post.ToneMap",
                [&](ToneMapData& data, RGBuilder& builder)
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
                    data.Dst = builder.WriteColor(sceneColorLdr, colorInfo);

                    m_LastSceneColorHandle = data.Src;
                    m_LastPostLdrHandle = {};
                    m_DebugState.LastSceneColorHandleValid = data.Src.IsValid();
                    m_DebugState.LastSceneColorHandle = data.Src.IsValid() ? data.Src.ID : 0u;
                    m_DebugState.LastPostLdrHandleValid = false;
                    m_DebugState.LastPostLdrHandle = 0u;
                },
                [this, fi, resolution, toneMapPC]
                (const ToneMapData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    SetViewportScissor(cmd, resolution);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_ToneMapPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_ToneMapPipeline->GetLayout(),
                                            0, 1, &m_ToneMapSets[fi], 0, nullptr);

                    vkCmdPushConstants(cmd, m_ToneMapPipeline->GetLayout(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapPC), &toneMapPC);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                }
            );
        }

        m_DebugState.BloomDownPipelineBuilt = static_cast<bool>(m_BloomDownPipeline);
        m_DebugState.BloomUpPipelineBuilt = static_cast<bool>(m_BloomUpPipeline);
        m_DebugState.HistogramPipelineBuilt = static_cast<bool>(m_HistogramPipeline);
        m_DebugState.SMAAEdgePipelineBuilt = static_cast<bool>(m_SMAAEdgePipeline);
        m_DebugState.SMAABlendPipelineBuilt = static_cast<bool>(m_SMAABlendPipeline);
        m_DebugState.SMAAResolvePipelineBuilt = static_cast<bool>(m_SMAAResolvePipeline);
        m_DebugState.LastSMAAEdgesHandleValid = m_LastSMAAEdgesHandle.IsValid();
        m_DebugState.LastSMAAEdgesHandle = m_LastSMAAEdgesHandle.IsValid() ? m_LastSMAAEdgesHandle.ID : 0u;
        m_DebugState.LastSMAAWeightsHandleValid = m_LastSMAAWeightsHandle.IsValid();
        m_DebugState.LastSMAAWeightsHandle = m_LastSMAAWeightsHandle.IsValid() ? m_LastSMAAWeightsHandle.ID : 0u;
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            m_DebugState.LastBloomDownHandles[mip] = m_LastBloomDownHandles[mip].IsValid() ? m_LastBloomDownHandles[mip].ID : 0u;
            m_DebugState.LastBloomUpSourceHandles[mip] = m_LastBloomUpSrcHandles[mip].IsValid() ? m_LastBloomUpSrcHandles[mip].ID : 0u;
        }
    }

    // =====================================================================
    // PostCompile — update descriptor bindings after RenderGraph resolves resources.
    // =====================================================================
    void PostProcessPass::PostCompile(uint32_t frameIndex,
                                      std::span<const RenderGraphDebugImage> debugImages)
    {
        m_DebugState.LastFrameIndex = frameIndex;

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

        // Update SMAA descriptors.
        if (m_Settings.SMAAEnabled())
        {
            // SMAA Edge: binding 0 = PostLdr.
            if (VkImageView ldrView = findView(m_LastPostLdrHandle))
                UpdateImageDescriptor(dev, m_SMAAEdgeSets[frameIndex], 0, m_LinearSampler, ldrView, readLayout);

            // SMAA Blend: binding 0 = edges texture (cached handle from blend pass setup).
            if (VkImageView edgesView = findView(m_LastSMAAEdgesHandle))
                UpdateImageDescriptor(dev, m_SMAABlendSets[frameIndex], 0, m_LinearSampler, edgesView, readLayout);
            // Bindings 1 and 2 (area/search textures) are set once in Initialize.

            // SMAA Resolve: binding 0 = PostLdr, binding 1 = weights (cached handle from resolve pass setup).
            if (VkImageView ldrView = findView(m_LastPostLdrHandle))
                UpdateImageDescriptor(dev, m_SMAAResolveSets[frameIndex], 0, m_LinearSampler, ldrView, readLayout);

            if (VkImageView weightsView = findView(m_LastSMAAWeightsHandle))
                UpdateImageDescriptor(dev, m_SMAAResolveSets[frameIndex], 1, m_LinearSampler, weightsView, readLayout);
        }

        // Update bloom downsample descriptors.
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            if (VkImageView view = findView(m_LastBloomDownHandles[mip]))
                UpdateImageDescriptor(dev, m_BloomDownSets[frameIndex][mip], 0, m_LinearSampler, view, readLayout);
        }

        // Update histogram descriptor: binding 0 = SceneColor sampler.
        if (m_Settings.HistogramEnabled)
        {
            if (VkImageView sceneView = findView(m_LastSceneColorHandle))
                UpdateImageDescriptor(dev, m_HistogramSets[frameIndex], 0, m_LinearSampler, sceneView, readLayout);
        }

        // Update bloom upsample descriptors.
        // Each upsample pass reads: binding 0 = coarser upsampled, binding 1 = current downsample.
        // We need to find the actual image views from the render graph debug images.
        for (uint32_t mip = 0; mip < kBloomMipCount; ++mip)
        {
            if (VkImageView coarserView = findView(m_LastBloomUpSrcHandles[mip]))
                UpdateImageDescriptor(dev, m_BloomUpSets[frameIndex][mip], 0, m_LinearSampler, coarserView, readLayout);

            // Binding 1: the downsample mip at this level (cached write handle from downsample pass).
            if (VkImageView mipView = findView(m_LastBloomMipWriteHandles[mip]))
                UpdateImageDescriptor(dev, m_BloomUpSets[frameIndex][mip], 1, m_LinearSampler, mipView, readLayout);
        }
    }

    // =====================================================================
    // Shutdown
    // =====================================================================
    void PostProcessPass::Shutdown()
    {
        m_DebugState.Initialized = false;
        m_DebugState.DummySampledAllocated = false;

        if (!m_Device) return;

        VkDevice dev = m_Device->GetLogicalDevice();

        m_ToneMapPipeline.reset();
        m_FXAAPipeline.reset();
        m_SMAAEdgePipeline.reset();
        m_SMAABlendPipeline.reset();
        m_SMAAResolvePipeline.reset();
        m_SMAAAreaTex.reset();
        m_SMAASearchTex.reset();
        m_BloomDownPipeline.reset();
        m_BloomUpPipeline.reset();
        m_HistogramPipeline.reset();
        m_DummySampled.reset();

        for (auto& buf : m_HistogramBuffers)
            buf.reset();

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
        if (m_SMAAEdgeSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_SMAAEdgeSetLayout, nullptr);
            m_SMAAEdgeSetLayout = VK_NULL_HANDLE;
        }
        if (m_SMAABlendSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_SMAABlendSetLayout, nullptr);
            m_SMAABlendSetLayout = VK_NULL_HANDLE;
        }
        if (m_SMAAResolveSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_SMAAResolveSetLayout, nullptr);
            m_SMAAResolveSetLayout = VK_NULL_HANDLE;
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
        if (m_HistogramSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_HistogramSetLayout, nullptr);
            m_HistogramSetLayout = VK_NULL_HANDLE;
        }
    }

    // =====================================================================
    // OnResize — invalidate cached pipelines (format may change).
    // =====================================================================
    void PostProcessPass::OnResize(uint32_t width, uint32_t height)
    {
        ++m_DebugState.ResizeCount;
        m_DebugState.LastResizeWidth = width;
        m_DebugState.LastResizeHeight = height;
        m_ToneMapPipeline.reset();
        m_FXAAPipeline.reset();
        m_SMAAEdgePipeline.reset();
        m_SMAABlendPipeline.reset();
        m_SMAAResolvePipeline.reset();
        m_BloomDownPipeline.reset();
        m_BloomUpPipeline.reset();
        m_HistogramPipeline.reset();
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
