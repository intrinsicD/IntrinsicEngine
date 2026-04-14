//
// Created by alex on 14.04.26.
//
module Extrinsic.Render.Renderer;

import <memory>;
import Extrinsic.RHI.Device;

namespace Extrinsic::Render
{
    class NullRenderer final : public IRenderer
    {
    public:
        void Initialize(Extrinsic::RHI::IDevice& device) override
        {
            (void)device;
        }

        void Shutdown() override {}

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            (void)width;
            (void)height;
        }

        void RenderFrame(const Extrinsic::RHI::FrameHandle& frame) override
        {
            (void)frame;
        }
    };

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
