module;

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
            // CullingSystem::Initialize requires a shader path — concrete renderers
            // supply it.  NullRenderer skips the cull dispatch (no-op RenderFrame).
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

        void RenderFrame(const RHI::FrameHandle&) override
        {
            m_PipelineManager->CommitPending();
            m_MaterialSystem->SyncGpuBuffer();
            m_CullingSystem->SyncGpuBuffer();
            // DispatchCull() is called by the concrete renderer pass that
            // owns the camera UBO and command context for this frame.
        }

        RHI::BufferManager&   GetBufferManager()   override { return *m_BufferManager;   }
        RHI::TextureManager&  GetTextureManager()  override { return *m_TextureManager;  }
        RHI::SamplerManager&  GetSamplerManager()  override { return *m_SamplerManager;  }
        RHI::PipelineManager& GetPipelineManager() override { return *m_PipelineManager; }
        MaterialSystem&        GetMaterialSystem()  override { return *m_MaterialSystem;  }
        CullingSystem&         GetCullingSystem()   override { return *m_CullingSystem;   }

    private:
        // Constructed lazily in Initialize() once IDevice is live.
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
