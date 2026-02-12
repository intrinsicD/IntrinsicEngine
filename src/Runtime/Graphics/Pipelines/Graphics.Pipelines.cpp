module;

#include <memory>
#include <span>

module Graphics:Pipelines.Impl;

import :RenderPipeline;
import :RenderGraph;
import :Passes.Picking;
import :Passes.Forward;
import :Passes.DebugView;
import :Passes.ImGui;
import :PipelineLibrary;
import :ShaderRegistry;
import :Pipelines;
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
        
        m_PathDirty = true;
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

    void DefaultPipeline::RebuildPath()
    {
        m_Path.Clear();

        // 1. Picking (Readback)
        m_Path.AddFeature("Picking", m_PickingPass.get());

        // 2. Forward (Main Scene)
        m_Path.AddFeature("Forward", m_ForwardPass.get());

        // 3. Debug View (Conditional)
        m_Path.AddStage("DebugView", [this](RenderPassContext& ctx)
        {
            if (m_DebugViewPass && ctx.Debug.Enabled)
            {
                m_DebugViewPass->AddPasses(ctx);
            }
        });

        // 4. ImGui (Overlay)
        m_Path.AddFeature("ImGui", m_ImGuiPass.get());
    }

    void DefaultPipeline::SetupFrame(RenderPassContext& ctx)
    {
        if (m_PathDirty)
        {
            RebuildPath();
            m_PathDirty = false;
        }

        m_Path.Execute(ctx);
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
