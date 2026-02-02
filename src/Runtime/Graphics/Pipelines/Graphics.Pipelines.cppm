module;

#include <memory>
#include <span>

export module Graphics:Pipelines;

import :RenderPipeline;
import :Passes.DebugView;
import :Passes.Forward;
import :Passes.ImGui;
import :Passes.Picking;
import RHI;

export namespace Graphics
{
    // Default pipeline that replicates current hard-coded RenderSystem feature order.
    class DefaultPipeline final : public RenderPipeline
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout,
                        const ShaderRegistry& shaderRegistry,
                        PipelineLibrary& pipelineLibrary) override;

        void Shutdown() override;

        void SetupFrame(RenderPassContext& ctx) override;

        void OnResize(uint32_t width, uint32_t height) override;

        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages,
                         std::span<const RenderGraphDebugPass> debugPasses) override;

    private:
        std::unique_ptr<Passes::PickingPass> m_PickingPass;
        std::unique_ptr<Passes::ForwardPass> m_ForwardPass;
        std::unique_ptr<Passes::DebugViewPass> m_DebugViewPass;
        std::unique_ptr<Passes::ImGuiPass> m_ImGuiPass;
    };
}
