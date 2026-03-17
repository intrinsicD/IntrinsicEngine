module;

#include <memory>
#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Composition;

import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import RHI;
import Core.Hash;

export namespace Graphics::Passes
{
    // =========================================================================
    // CompositionPass — deferred lighting composition.
    //
    // Reads G-buffer (SceneNormal, Albedo, Material0) + SceneDepth and writes
    // SceneColorHDR via a fullscreen deferred lighting pass.
    //
    // In Forward mode this pass is a no-op (geometry writes SceneColorHDR directly).
    // =========================================================================
    class CompositionPass final : public IRenderFeature
    {
    public:
        CompositionPass() = default;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout) override;

        void Shutdown() override;
        void AddPasses(RenderPassContext& ctx) override;
        void OnResize(uint32_t width, uint32_t height) override { (void)width; (void)height; }

        void SetShaderRegistry(const ShaderRegistry& reg) { m_ShaderRegistry = &reg; }

        // Called after render graph compilation to update descriptor sets
        // with the actual G-buffer image views for the current frame.
        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages);

    private:
        RHI::VulkanDevice* m_Device = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;

        // Deferred lighting pipeline (fullscreen triangle).
        std::unique_ptr<RHI::GraphicsPipeline> m_DeferredPipeline;

        // Descriptor set layout: 4 combined image samplers
        // (normal, albedo, material, depth).
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;

        // Per-frame descriptor sets (one per frame in flight).
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();
        VkDescriptorSet m_Sets[FRAMES] = {};

        // Samplers.
        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        VkSampler m_NearestSampler = VK_NULL_HANDLE;

        // Dummy image for safe initial descriptor bindings.
        std::unique_ptr<RHI::VulkanImage> m_DummySampled;

        // Track G-buffer resource handles from last AddPasses for PostCompile.
        RGResourceHandle m_LastNormalHandle{};
        RGResourceHandle m_LastAlbedoHandle{};
        RGResourceHandle m_LastMaterialHandle{};
        RGResourceHandle m_LastDepthHandle{};

        // Lazy pipeline build.
        [[nodiscard]] std::unique_ptr<RHI::GraphicsPipeline>
        BuildDeferredPipeline(VkFormat outputFormat);
    };
}
