module;

#include <memory>

export module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.CullingSystem;

namespace Extrinsic::Graphics
{
    export class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        virtual void Initialize(RHI::IDevice& device) = 0;
        virtual void Shutdown() = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        virtual void RenderFrame(const RHI::FrameHandle& frame) = 0;

        // Resource managers — initialised inside Initialize() once IDevice is live.
        // Shutdown() destroys them in dependency order (pipelines → textures →
        // samplers → buffers) so no manager outlives a resource it references.
        [[nodiscard]] virtual RHI::BufferManager&   GetBufferManager()   = 0;
        [[nodiscard]] virtual RHI::TextureManager&  GetTextureManager()  = 0;
        [[nodiscard]] virtual RHI::SamplerManager&  GetSamplerManager()  = 0;
        [[nodiscard]] virtual RHI::PipelineManager& GetPipelineManager() = 0;
        [[nodiscard]] virtual MaterialSystem&        GetMaterialSystem()  = 0;
        [[nodiscard]] virtual CullingSystem&         GetCullingSystem()   = 0;
    };

    export std::unique_ptr<IRenderer> CreateRenderer();
}
