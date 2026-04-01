module;

#include <memory>
#include <span>

export module Graphics.Pipelines;

import Graphics.RenderPipeline;
import Graphics.RenderPath;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;
import Graphics.PipelineLibrary;
import Graphics.FeatureCatalog;
import RHI.Descriptors;
import RHI.Device;
import Core.Hash;
import Core.FeatureRegistry;

export namespace Graphics
{
    struct DefaultPipelineRecipeInputs
    {
        bool PickingPassEnabled = true;
        bool SurfacePassEnabled = true;
        bool DepthPrepassEnabled = true;
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
        DefaultPipeline();
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

        Passes::SelectionOutlineSettings* GetSelectionOutlineSettings() override;
        Passes::PostProcessSettings* GetPostProcessSettings() override;
        const Passes::HistogramReadback* GetHistogramReadback() const override;

        [[nodiscard]] RenderPipelineDebugState GetDebugState() const override;
        [[nodiscard]] const Passes::SelectionOutlineDebugState* GetSelectionOutlineDebugState() const override;
        [[nodiscard]] const Passes::PostProcessDebugState* GetPostProcessDebugState() const override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        const Core::FeatureRegistry* m_Registry = nullptr;

        [[nodiscard]] bool IsFeatureEnabled(const Core::FeatureDescriptor& descriptor) const;

        void RebuildPath();
    };

}
