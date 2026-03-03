module;

#include <cstddef>
#include <memory>
#include <span>
#include <algorithm>
#include <unordered_set>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

module Graphics:Pipelines.Impl;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import :Geometry;
import :DebugDraw;
import :Passes.Picking;
import :Passes.Surface;
import :Passes.SelectionOutline;
import :Passes.Line;
import :Passes.Point;
import :Passes.DebugView;
import :Passes.ImGui;
import :PipelineLibrary;
import :ShaderRegistry;
import :Pipelines;
import RHI;
import ECS;
import Core.Hash;
import Core.FeatureRegistry;

using namespace Core::Hash;

namespace Graphics
{
    void DefaultPipeline::Shutdown()
    {
        if (m_PickingPass)          m_PickingPass->Shutdown();
        if (m_SurfacePass)          m_SurfacePass->Shutdown();
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->Shutdown();
        if (m_LinePass)             m_LinePass->Shutdown();
        if (m_PointPass)            m_PointPass->Shutdown();
        if (m_DebugViewPass)        m_DebugViewPass->Shutdown();
        if (m_ImGuiPass)            m_ImGuiPass->Shutdown();

        m_PickingPass.reset();
        m_SurfacePass.reset();
        m_SelectionOutlinePass.reset();
        m_LinePass.reset();
        m_PointPass.reset();
        m_DebugViewPass.reset();
        m_ImGuiPass.reset();
    }

    void DefaultPipeline::Initialize(RHI::VulkanDevice& device,
                                    RHI::DescriptorAllocator& descriptorPool,
                                    RHI::DescriptorLayout& globalLayout,
                                    const ShaderRegistry& shaderRegistry,
                                    PipelineLibrary& pipelineLibrary)
    {
        m_PickingPass          = std::make_unique<Passes::PickingPass>();
        m_SurfacePass          = std::make_unique<Passes::SurfacePass>();
        m_SelectionOutlinePass = std::make_unique<Passes::SelectionOutlinePass>();
        m_LinePass             = std::make_unique<Passes::LinePass>();
        m_PointPass            = std::make_unique<Passes::PointPass>();
        m_DebugViewPass        = std::make_unique<Passes::DebugViewPass>();
        m_ImGuiPass            = std::make_unique<Passes::ImGuiPass>();

        m_PickingPass->Initialize(device, descriptorPool, globalLayout);
        m_SurfacePass->Initialize(device, descriptorPool, globalLayout);
        m_SelectionOutlinePass->Initialize(device, descriptorPool, globalLayout);
        m_LinePass->Initialize(device, descriptorPool, globalLayout);
        m_PointPass->Initialize(device, descriptorPool, globalLayout);
        m_DebugViewPass->Initialize(device, descriptorPool, globalLayout);
        m_ImGuiPass->Initialize(device, descriptorPool, globalLayout);

        m_PickingPass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Picking));

        m_SurfacePass->SetPipeline(&pipelineLibrary.GetOrDie(kPipeline_Surface));
        m_SurfacePass->SetLinePipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfaceLines));
        m_SurfacePass->SetPointPipeline(&pipelineLibrary.GetOrDie(kPipeline_SurfacePoints));
        m_SurfacePass->SetInstanceSetLayout(pipelineLibrary.GetStage1InstanceSetLayout());
        m_SurfacePass->SetCullPipeline(pipelineLibrary.GetCullPipeline());
        m_SurfacePass->SetCullSetLayout(pipelineLibrary.GetCullSetLayout());

        m_SelectionOutlinePass->SetShaderRegistry(shaderRegistry);
        m_LinePass->SetShaderRegistry(shaderRegistry);
        m_PointPass->SetShaderRegistry(shaderRegistry);
        m_DebugViewPass->SetShaderRegistry(shaderRegistry);

        m_PathDirty = true;
    }

    bool DefaultPipeline::IsFeatureEnabled(Core::Hash::StringID id) const
    {
        if (!m_Registry) return true; // No registry → all features enabled

        // Render-path resilience: when a new pass is added but not yet registered
        // in the FeatureRegistry catalog, keep the pass enabled by default rather
        // than silently dropping it from the pipeline.
        const Core::FeatureInfo* info = m_Registry->Find(id);
        if (!info)
            return true;

        return info->Enabled;
    }

    void DefaultPipeline::RebuildPath()
    {
        m_Path.Clear();

        // ==================================================================
        // 1. Picking (Readback) — entity/primitive ID for click queries.
        // ==================================================================
        if (m_PickingPass && IsFeatureEnabled("PickingPass"_id))
            m_Path.AddFeature("Picking", m_PickingPass.get());

        // ==================================================================
        // 2. Surface Pass — face rendering via SurfacePass (triangles / lines /
        //    point geometry).
        // ==================================================================
        if (m_SurfacePass && IsFeatureEnabled("SurfacePass"_id))
            m_Path.AddFeature("MeshPass.Surface", m_SurfacePass.get());

        // ==================================================================
        // 3. LinePass — unified BDA-based line rendering.
        //    Consolidates retained wireframe/graph edges and transient DebugDraw
        //    lines into a single pass. Always present when the pass exists so
        //    debug-line output is not silently dropped.
        // ==================================================================
        if (m_LinePass)
        {
            m_Path.AddStage("LinePass", [this](RenderPassContext& ctx)
            {
                m_LinePass->SetGeometryStorage(&ctx.GeometryStorage);
                m_LinePass->SetDebugDraw(ctx.DebugDrawPtr);
                m_LinePass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 4. PointPass — unified BDA-based point rendering.
        //    Iterates ECS::Point::Component for retained draws and
        //    DebugDraw::GetPoints() for transient markers.
        // ==================================================================
        if (m_PointPass && IsFeatureEnabled("PointPass"_id))
        {
            m_Path.AddStage("Points", [this](RenderPassContext& ctx)
            {
                m_PointPass->SetGeometryStorage(&ctx.GeometryStorage);
                m_PointPass->SetDebugDraw(ctx.DebugDrawPtr);
                m_PointPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 5. Selection Outline — post-process overlay for selected entities.
        // ==================================================================
        if (m_SelectionOutlinePass && IsFeatureEnabled("SelectionOutlinePass"_id))
            m_Path.AddFeature("SelectionOutline", m_SelectionOutlinePass.get());

        // ==================================================================
        // 6. Debug View — conditional texture inspector overlay.
        // ==================================================================
        if (m_DebugViewPass && IsFeatureEnabled("DebugViewPass"_id))
        {
            m_Path.AddStage("DebugView", [this](RenderPassContext& ctx)
            {
                if (ctx.Debug.Enabled)
                    m_DebugViewPass->AddPasses(ctx);
            });
        }

        // ==================================================================
        // 7. ImGui — editor UI overlay.
        // ==================================================================
        if (m_ImGuiPass && IsFeatureEnabled("ImGuiPass"_id))
            m_Path.AddFeature("ImGui", m_ImGuiPass.get());
    }

    void DefaultPipeline::SetupFrame(RenderPassContext& ctx)
    {
        // When a FeatureRegistry is connected, rebuild every frame so that
        // runtime enable/disable changes take effect immediately.
        if (m_PathDirty || m_Registry)
        {
            RebuildPath();
            m_PathDirty = false;
        }

        m_Path.Execute(ctx);
    }

    void DefaultPipeline::OnResize(uint32_t width, uint32_t height)
    {
        if (m_PickingPass)          m_PickingPass->OnResize(width, height);
        if (m_SurfacePass)          m_SurfacePass->OnResize(width, height);
        if (m_SelectionOutlinePass) m_SelectionOutlinePass->OnResize(width, height);
        if (m_LinePass)             m_LinePass->OnResize(width, height);
        if (m_PointPass)            m_PointPass->OnResize(width, height);
        if (m_DebugViewPass)        m_DebugViewPass->OnResize(width, height);
        if (m_ImGuiPass)            m_ImGuiPass->OnResize(width, height);
    }

    void DefaultPipeline::PostCompile(uint32_t frameIndex,
                                     std::span<const RenderGraphDebugImage> debugImages,
                                     std::span<const RenderGraphDebugPass>)
    {
        if (m_SelectionOutlinePass)
            m_SelectionOutlinePass->PostCompile(frameIndex, debugImages);
        if (m_DebugViewPass)
            m_DebugViewPass->PostCompile(frameIndex, debugImages);
    }
}
