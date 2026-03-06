module;

#include <memory>
#include <span>

export module Graphics:Pipelines;

import :RenderPipeline;
import :RenderPath;
import :RenderGraph;
import :ShaderRegistry;
import :PipelineLibrary;
import :CompositionStrategy;
import :Passes.DebugView;
import :Passes.Surface;
import :Passes.ImGui;
import :Passes.Line;
import :Passes.Picking;
import :Passes.Point;
import :Passes.SelectionOutline;
import :Passes.SelectionOutlineSettings;
import :Passes.PostProcess;
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
        bool HasSelectionWork = false;
        bool DebugViewEnabled = false;
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

        // Returns the active composition strategy (non-null when geometry
        // passes are enabled). Passes may query this to determine which
        // canonical resource to write their color output to.
        [[nodiscard]] const ICompositionStrategy* GetCompositionStrategy() const
        {
            return m_Composition.get();
        }

    private:
        const Core::FeatureRegistry* m_Registry = nullptr;

        std::unique_ptr<Passes::PickingPass> m_PickingPass;
        std::unique_ptr<Passes::SurfacePass> m_SurfacePass;
        std::unique_ptr<Passes::SelectionOutlinePass> m_SelectionOutlinePass;
        std::unique_ptr<Passes::LinePass> m_LinePass;
        std::unique_ptr<Passes::PointPass> m_PointPass;
        std::unique_ptr<Passes::DebugViewPass> m_DebugViewPass;
        std::unique_ptr<Passes::ImGuiPass> m_ImGuiPass;
        std::unique_ptr<Passes::PostProcessPass> m_PostProcessPass;

        // Active lighting/composition strategy. Created by RebuildPath()
        // based on the FrameLightingPath determined by the recipe.
        std::unique_ptr<ICompositionStrategy> m_Composition;

        RenderPath m_Path;
        bool m_PathDirty = true;

        [[nodiscard]] bool IsFeatureEnabled(Core::Hash::StringID id) const;

        void RebuildPath();
    };

    DefaultPipeline::~DefaultPipeline() = default;
}
