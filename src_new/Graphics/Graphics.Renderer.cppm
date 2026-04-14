export module Extrinsic.Render.Renderer;

import <memory>;
import Extrinsic.RHI.Device;

namespace Extrinsic::Graphics
{
    export class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        virtual void Initialize(Extrinsic::RHI::IDevice& device) = 0;
        virtual void Shutdown() = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        virtual void RenderFrame(const Extrinsic::RHI::FrameHandle& frame) = 0;
    };

    export std::unique_ptr<IRenderer> CreateRenderer();
}