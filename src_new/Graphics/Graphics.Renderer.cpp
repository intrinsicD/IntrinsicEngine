module;

#include <cstdint>
#include <memory>
#include <optional>
#include <entt/entity/registry.hpp>

module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.PostProcessSystem;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;

namespace Extrinsic::Graphics
{
    class NullRenderer final : public IRenderer
    {
    public:
        void Initialize(RHI::IDevice& device) override
        {
            m_BufferManager  .emplace(device);
            m_SamplerManager .emplace(device);
            m_TextureManager .emplace(device, device.GetBindlessHeap());
            m_PipelineManager.emplace(device);
            m_GpuWorld.emplace();
            m_GpuWorld->Initialize(device, *m_BufferManager);
            m_MaterialSystem .emplace();
            m_MaterialSystem->Initialize(device, *m_BufferManager);
            m_ColormapSystem.emplace();
            m_ColormapSystem->Initialize(device, *m_TextureManager, *m_SamplerManager);
            m_VisualizationSyncSystem.emplace();
            m_VisualizationSyncSystem->Initialize(*m_MaterialSystem, device);
            m_TransformSyncSystem.emplace();
            m_TransformSyncSystem->Initialize();
            m_GpuWorld->SetMaterialBuffer(
                m_MaterialSystem->GetBuffer(),
                m_MaterialSystem->GetCapacity());
            m_CullingSystem  .emplace();
            m_LightSystem    .emplace();
            m_LightSystem->Initialize();
            m_SelectionSystem.emplace();
            m_SelectionSystem->Initialize();
            m_ForwardSystem.emplace();
            m_ForwardSystem->Initialize();
            m_DeferredSystem.emplace();
            m_DeferredSystem->Initialize();
            m_PostProcessSystem.emplace();
            m_PostProcessSystem->Initialize();
            m_ShadowSystem.emplace();
            m_ShadowSystem->Initialize();
            // CullingSystem::Initialize requires a shader path — concrete
            // renderers supply it.  NullRenderer skips the cull dispatch.
        }

        void Shutdown() override
        {
            if (m_SelectionSystem) m_SelectionSystem->Shutdown();
            if (m_LightSystem)     m_LightSystem->Shutdown();
            if (m_ForwardSystem)   m_ForwardSystem->Shutdown();
            if (m_DeferredSystem)  m_DeferredSystem->Shutdown();
            if (m_PostProcessSystem) m_PostProcessSystem->Shutdown();
            if (m_ShadowSystem)    m_ShadowSystem->Shutdown();
            if (m_CullingSystem)   m_CullingSystem->Shutdown();
            if (m_TransformSyncSystem) m_TransformSyncSystem->Shutdown();
            if (m_VisualizationSyncSystem) m_VisualizationSyncSystem->Shutdown();
            if (m_ColormapSystem)  m_ColormapSystem->Shutdown();
            if (m_GpuWorld)        m_GpuWorld->Shutdown();
            if (m_MaterialSystem)  m_MaterialSystem->Shutdown();

            m_SelectionSystem.reset();
            m_LightSystem    .reset();
            m_ForwardSystem  .reset();
            m_DeferredSystem .reset();
            m_PostProcessSystem.reset();
            m_ShadowSystem   .reset();
            m_CullingSystem  .reset();
            m_TransformSyncSystem.reset();
            m_VisualizationSyncSystem.reset();
            m_ColormapSystem .reset();
            m_GpuWorld       .reset();
            m_MaterialSystem .reset();
            m_PipelineManager.reset();
            m_TextureManager .reset();
            m_SamplerManager .reset();
            m_BufferManager  .reset();
        }

        void Resize(std::uint32_t, std::uint32_t) override {}

        // ── Per-frame phases ──────────────────────────────────────────────

        bool BeginFrame(RHI::FrameHandle&) override
        {
            // NullRenderer has no swapchain; always reports "frame available"
            // so the rest of the loop exercises the extraction/prepare/execute
            // seams even without a real GPU backend.
            return true;
        }

        RenderWorld ExtractRenderWorld(const RenderFrameInput& input) override
        {
            return RenderWorld{
                .Viewport = input.Viewport,
                .Alpha    = input.Alpha,
            };
        }

        void PrepareFrame(RenderWorld&) override
        {
            // Phase 14.1 sync order contract:
            //  1) commit pipelines
            //  2) flush base materials
            //  3) resolve visualization overrides + write GpuEntityConfig
            //  4) flush override material deltas
            //  5) write instance transforms/material slots/flags/bounds
            //  6) write lights
            //  7) refresh scene table material binding
            //  8) upload GpuWorld dirty ranges
            m_PipelineManager->CommitPending();
            m_MaterialSystem->SyncGpuBuffer();
            m_VisualizationSyncSystem->Sync(
                m_SyncRegistry,
                *m_MaterialSystem,
                *m_ColormapSystem,
                *m_GpuWorld);
            m_MaterialSystem->SyncGpuBuffer();
            m_TransformSyncSystem->SyncGpuBuffer(m_SyncRegistry, *m_GpuWorld, *m_MaterialSystem);
            m_LightSystem->SyncGpuBuffer(m_SyncRegistry, *m_GpuWorld);
            m_GpuWorld->SetMaterialBuffer(
                m_MaterialSystem->GetBuffer(),
                m_MaterialSystem->GetCapacity());
            m_GpuWorld->SyncFrame();
            m_CullingSystem->SyncGpuBuffer();
        }

        void ExecuteFrame(const RHI::FrameHandle&,
                          const RenderWorld&) override
        {
            // Phase 14.2 GPU order is intentionally fixed for concrete
            // backends:
            //   1) culling counter reset
            //   2) culling dispatch
            //   3) depth prepass (optional)
            //   4) gbuffer
            //   5) shadows
            //   6) deferred lighting
            //   7) forward lines
            //   8) forward points
            //   9) selection/outline
            //  10) postprocess/present
            //
            // Null backend records no commands.
        }

        std::uint64_t EndFrame(const RHI::FrameHandle&) override
        {
            // No GPU timeline — report zero so maintenance callers
            // never block waiting for a value that will never arrive.
            return 0;
        }

        // ── Resource managers ─────────────────────────────────────────────

        RHI::BufferManager&   GetBufferManager()   override { return *m_BufferManager;   }
        RHI::TextureManager&  GetTextureManager()  override { return *m_TextureManager;  }
        RHI::SamplerManager&  GetSamplerManager()  override { return *m_SamplerManager;  }
        RHI::PipelineManager& GetPipelineManager() override { return *m_PipelineManager; }
        GpuWorld&             GetGpuWorld()        override { return *m_GpuWorld;        }
        MaterialSystem&        GetMaterialSystem()  override { return *m_MaterialSystem;  }
        ColormapSystem&        GetColormapSystem()  override { return *m_ColormapSystem;  }
        VisualizationSyncSystem& GetVisualizationSyncSystem() override { return *m_VisualizationSyncSystem; }
        CullingSystem&         GetCullingSystem()   override { return *m_CullingSystem;   }
        TransformSyncSystem&   GetTransformSyncSystem() override { return *m_TransformSyncSystem; }
        LightSystem&           GetLightSystem()     override { return *m_LightSystem;     }
        SelectionSystem&       GetSelectionSystem() override { return *m_SelectionSystem; }
        ForwardSystem&         GetForwardSystem()   override { return *m_ForwardSystem;   }
        DeferredSystem&        GetDeferredSystem()  override { return *m_DeferredSystem;  }
        PostProcessSystem&     GetPostProcessSystem() override { return *m_PostProcessSystem; }
        ShadowSystem&          GetShadowSystem()    override { return *m_ShadowSystem;    }

    private:
        std::optional<RHI::BufferManager>   m_BufferManager;
        std::optional<RHI::SamplerManager>  m_SamplerManager;
        std::optional<RHI::TextureManager>  m_TextureManager;
        std::optional<RHI::PipelineManager> m_PipelineManager;
        std::optional<GpuWorld>              m_GpuWorld;
        std::optional<MaterialSystem>        m_MaterialSystem;
        std::optional<ColormapSystem>        m_ColormapSystem;
        std::optional<VisualizationSyncSystem> m_VisualizationSyncSystem;
        std::optional<CullingSystem>         m_CullingSystem;
        std::optional<TransformSyncSystem>   m_TransformSyncSystem;
        std::optional<LightSystem>           m_LightSystem;
        std::optional<SelectionSystem>       m_SelectionSystem;
        std::optional<ForwardSystem>         m_ForwardSystem;
        std::optional<DeferredSystem>        m_DeferredSystem;
        std::optional<PostProcessSystem>     m_PostProcessSystem;
        std::optional<ShadowSystem>          m_ShadowSystem;
        entt::registry                       m_SyncRegistry;
    };

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
