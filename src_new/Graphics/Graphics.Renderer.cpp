module;

#include <cstdint>
#include <memory>
#include <optional>

module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.CullingSystem;
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
            m_MaterialSystem .emplace();
            m_MaterialSystem->Initialize(device, *m_BufferManager);
            m_CullingSystem  .emplace();
            // CullingSystem::Initialize requires a shader path — concrete
            // renderers supply it.  NullRenderer skips the cull dispatch.
        }

        void Shutdown() override
        {
            m_CullingSystem  .reset();
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
            // Sync CPU-side GPU resources (pipeline cache, material SSBO,
            // GPU scene SSBO).  No actual culling without a camera UBO.
            m_PipelineManager->CommitPending();
            m_MaterialSystem->SyncGpuBuffer();
            m_CullingSystem->SyncGpuBuffer();
        }

        void ExecuteFrame(const RHI::FrameHandle&,
                          const RenderWorld&) override
        {
            // No command recording in the null backend.
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
        MaterialSystem&        GetMaterialSystem()  override { return *m_MaterialSystem;  }
        CullingSystem&         GetCullingSystem()   override { return *m_CullingSystem;   }

    private:
        std::optional<RHI::BufferManager>   m_BufferManager;
        std::optional<RHI::SamplerManager>  m_SamplerManager;
        std::optional<RHI::TextureManager>  m_TextureManager;
        std::optional<RHI::PipelineManager> m_PipelineManager;
        std::optional<MaterialSystem>        m_MaterialSystem;
        std::optional<CullingSystem>         m_CullingSystem;
    };

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
