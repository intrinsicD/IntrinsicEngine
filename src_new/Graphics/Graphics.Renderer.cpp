module;

#include <memory>

module Extrinsic.Graphics.Renderer;

import Extrinsic.RHI.Device;

namespace Extrinsic::Graphics
{
    class NullRenderer final : public IRenderer
    {
    public:
        void Initialize(RHI::IDevice& device) override
        {
            (void)device;
        }

        void Shutdown() override {}

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            (void)width;
            (void)height;
        }

        void RenderFrame(const RHI::FrameHandle& frame) override
        {
            (void)frame;
        }
    };

    std::unique_ptr<IRenderer> CreateRenderer()
    {
        return std::make_unique<NullRenderer>();
    }
}
