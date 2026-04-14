module;

#include <memory>

export module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;

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
    };

    export std::unique_ptr<IRenderer> CreateRenderer();
}
