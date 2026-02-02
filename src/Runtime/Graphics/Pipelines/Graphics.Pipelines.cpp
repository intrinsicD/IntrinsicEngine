module;

#include <memory>
#include <span>

// This is a standard implementation unit for the named module `Graphics.Pipelines`.
module Graphics:Pipelines.Impl;

import Graphics;
import RHI;
import Core;

using namespace Core::Hash;

namespace Graphics
{
    void DefaultPipeline::Initialize(RHI::VulkanDevice& device,
                                    RHI::DescriptorAllocator& descriptorPool,
                                    RHI::DescriptorLayout& globalLayout,
                                    const ShaderRegistry& shaderRegistry,
                                    PipelineLibrary& pipelineLibrary)
    {
        m_PickingPass = std::make_unique<Passes::PickingPass>();
        m_ForwardPass = std::make_unique<Passes::ForwardPass>();
        m_DebugViewPass = std::make_unique<Passes::DebugViewPass>();
        m_ImGuiPass = std::make_unique<Passes::ImGuiPass>();

        m_PickingPass->Initialize(device, descriptorPool, globalLayout);
        m_ForwardPass->Initialize(device, descriptorPool, globalLayout);
        m_DebugViewPass->Initialize(device, descriptorPool, globalLayout);
        m_ImGuiPass->Initialize(device, descriptorPool, globalLayout);

        m_PickingPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Picking));

        m_ForwardPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Forward));
        m_ForwardPass->SetInstanceSetLayout(pipelineLibrary.GetStage1InstanceSetLayout());
        m_ForwardPass->SetCullPipeline(pipelineLibrary.GetCullPipeline());
        m_ForwardPass->SetCullSetLayout(pipelineLibrary.GetCullSetLayout());

        m_DebugViewPass->SetShaderRegistry(shaderRegistry);
    }

    void DefaultPipeline::Shutdown()
    {
        if (m_PickingPass) m_PickingPass->Shutdown();
        if (m_ForwardPass) m_ForwardPass->Shutdown();
        if (m_DebugViewPass) m_DebugViewPass->Shutdown();
        if (m_ImGuiPass) m_ImGuiPass->Shutdown();

        m_PickingPass.reset();
        m_ForwardPass.reset();
        m_DebugViewPass.reset();
        m_ImGuiPass.reset();
    }

    void DefaultPipeline::SetupFrame(RenderPassContext& ctx)
    {
        if (m_PickingPass) m_PickingPass->AddPasses(ctx);
        if (m_ForwardPass) m_ForwardPass->AddPasses(ctx);

        if (m_DebugViewPass && ctx.Debug.Enabled && ctx.Debug.ShowInViewport)
            m_DebugViewPass->AddPasses(ctx);

        if (m_ImGuiPass) m_ImGuiPass->AddPasses(ctx);

        if (m_DebugViewPass && ctx.Debug.Enabled && !ctx.Debug.ShowInViewport)
            m_DebugViewPass->AddPasses(ctx);
    }

    void DefaultPipeline::OnResize(uint32_t width, uint32_t height)
    {
        if (m_PickingPass) m_PickingPass->OnResize(width, height);
        if (m_ForwardPass) m_ForwardPass->OnResize(width, height);
        if (m_DebugViewPass) m_DebugViewPass->OnResize(width, height);
        if (m_ImGuiPass) m_ImGuiPass->OnResize(width, height);
    }

    void DefaultPipeline::PostCompile(uint32_t frameIndex,
                                     std::span<const RenderGraphDebugImage> debugImages,
                                     std::span<const RenderGraphDebugPass>)
    {
        if (m_DebugViewPass)
            m_DebugViewPass->PostCompile(frameIndex, debugImages);
    }
}
