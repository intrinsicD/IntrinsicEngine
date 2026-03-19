module;

#include <memory>
#include <span>

export module Graphics:Pipelines;

import :RenderPipeline;
import :RenderPath;
import :RenderGraph;
import :ShaderRegistry;
import :PipelineLibrary;
import :FeatureCatalog;
import :Passes.DebugView;
import :Passes.Surface;
import :Passes.ImGui;
import :Passes.Line;
import :Passes.Picking;
import :Passes.Point;
import :Passes.HtexPatchPreview;
import :Passes.SelectionOutline;
import :Passes.SelectionOutlineSettings;
import :Passes.PostProcess;
import :Passes.Composition;
import RHI;
import Core.Hash;
import Core.FeatureRegistry;

export namespace Graphics
{
    struct DefaultPipelineRecipeInputs
    {
        bool PickingPassEnabled = true;
        bool SurfacePassEnabled = true;
        bool LinePassEnabled = true;
        bool PointPassEnabled = true;
        bool PostProcessPassEnabled = true;
        bool SelectionOutlinePassEnabled = true;
        bool DebugViewPassEnabled = true;
        bool ImGuiPassEnabled = true;
        bool CompositionPassEnabled = true;
        bool HasSelectionWork = false;
        bool DebugViewEnabled = false;
        FrameLightingPath RequestedLightingPath = FrameLightingPath::Forward;
        Core::Hash::StringID DebugResource = GetRenderResourceName(RenderResource::EntityId);
    };

    [[nodiscard]] FrameRecipe BuildDefaultPipelineRecipe(const DefaultPipelineRecipeInputs& inputs);

    class DefaultPipeline final : public RenderPipeline
    {
    public:
        ~DefaultPipeline() override;

        void SetFeatureRegistry(const Core::FeatureRegistry* registry) { m_Registry = registry; }

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout,
                        const ShaderRegistry& shaderRegistry,
                        PipelineLibrary& pipelineLibrary) override;

        void Shutdown() override;

        [[nodiscard]] FrameRecipe BuildFrameRecipe(const RenderPassContext& ctx) const override;
        void SetupFrame(RenderPassContext& ctx) override;

        void OnResize(uint32_t width, uint32_t height) override;

        void PostCompile(uint32_t frameIndex,
                         std::span<const RenderGraphDebugImage> debugImages,
                         std::span<const RenderGraphDebugPass> debugPasses) override;

        Passes::SelectionOutlineSettings* GetSelectionOutlineSettings() override
        {
            return m_SelectionOutlinePass ? &m_SelectionOutlinePass->GetSettings() : nullptr;
        }

        Passes::PostProcessSettings* GetPostProcessSettings() override
        {
            return m_PostProcessPass ? &m_PostProcessPass->GetSettings() : nullptr;
        }

        const Passes::HistogramReadback* GetHistogramReadback() const override
        {
            return m_PostProcessPass ? &m_PostProcessPass->GetHistogram() : nullptr;
        }

        [[nodiscard]] RenderPipelineDebugState GetDebugState() const override;
        [[nodiscard]] const Passes::SelectionOutlineDebugState* GetSelectionOutlineDebugState() const override
        {
            return m_SelectionOutlinePass ? &m_SelectionOutlinePass->GetDebugState() : nullptr;
        }

        [[nodiscard]] const Passes::PostProcessDebugState* GetPostProcessDebugState() const override
        {
            return m_PostProcessPass ? &m_PostProcessPass->GetDebugState() : nullptr;
        }

    private:
        const Core::FeatureRegistry* m_Registry = nullptr;

        std::unique_ptr<Passes::PickingPass> m_PickingPass;
        std::unique_ptr<Passes::SurfacePass> m_SurfacePass;
        std::unique_ptr<Passes::SelectionOutlinePass> m_SelectionOutlinePass;
        std::unique_ptr<Passes::LinePass> m_LinePass;
        std::unique_ptr<Passes::PointPass> m_PointPass;
        std::unique_ptr<Passes::HtexPatchPreviewPass> m_HtexPatchPreviewPass;
        std::unique_ptr<Passes::DebugViewPass> m_DebugViewPass;
        std::unique_ptr<Passes::ImGuiPass> m_ImGuiPass;
        std::unique_ptr<Passes::PostProcessPass> m_PostProcessPass;
        std::unique_ptr<Passes::CompositionPass> m_CompositionPass;

        RenderPath m_Path;
        bool m_PathDirty = true;

        [[nodiscard]] bool IsFeatureEnabled(const Core::FeatureDescriptor& descriptor) const;

        void RebuildPath();
    };

    DefaultPipeline::~DefaultPipeline() = default;
}
