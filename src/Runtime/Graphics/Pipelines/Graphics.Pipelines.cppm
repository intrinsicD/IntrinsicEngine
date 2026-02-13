module;

#include <memory>
#include <span>

export module Graphics:Pipelines;

import :RenderPipeline;
import :RenderPath;
import :RenderGraph;
import :ShaderRegistry;
import :PipelineLibrary;
import :Passes.DebugView;
import :Passes.Forward;
import :Passes.ImGui;
import :Passes.Picking;
import :Passes.SelectionOutline;
import RHI;
import Core.Hash;
import Core.FeatureRegistry;

export namespace Graphics
{
    // Default pipeline that replicates current hard-coded RenderSystem feature order.
    // When a FeatureRegistry is provided, RebuildPath() checks IsEnabled() for each
    // feature, allowing runtime toggling without pipeline recreation.
    class DefaultPipeline final : public RenderPipeline
    {
    public:
        ~DefaultPipeline() override;

        // Provide an optional FeatureRegistry for runtime enable/disable of features.
        void SetFeatureRegistry(const Core::FeatureRegistry* registry) { m_Registry = registry; }

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
        const Core::FeatureRegistry* m_Registry = nullptr;

        std::unique_ptr<Passes::PickingPass> m_PickingPass;
        std::unique_ptr<Passes::ForwardPass> m_ForwardPass;
        std::unique_ptr<Passes::SelectionOutlinePass> m_SelectionOutlinePass;
        std::unique_ptr<Passes::DebugViewPass> m_DebugViewPass;
        std::unique_ptr<Passes::ImGuiPass> m_ImGuiPass;

        // Modern Data-Driven Render Path
        RenderPath m_Path;
        bool m_PathDirty = true;

        // Check if a feature is enabled via the registry (defaults to true if no registry).
        bool IsFeatureEnabled(Core::Hash::StringID id) const;

        void RebuildPath();
    };

    // -------------------------------------------------------------------------
    // Vtable anchor: destructor is defined out-of-line in this TU (the :Pipelines
    // partition) so the vtable for DefaultPipeline is emitted here — same TU as the
    // class. This works around Clang 18's failure to link vtables across module
    // partition boundaries (see ARCHITECTURE_ANALYSIS.md §1.1).
    // -------------------------------------------------------------------------
    DefaultPipeline::~DefaultPipeline() {}
}
