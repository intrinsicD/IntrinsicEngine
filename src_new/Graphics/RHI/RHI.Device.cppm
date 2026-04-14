export module Extrinsic.RHI.Device;

import <memory>;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.Core.Config;

namespace Extrinsic::RHI
{
    export class IDevice
    {
    public:
        virtual ~IDevice() = default;

        virtual void Initialize(Platform::IWindow& window, const Core::RenderConfig& config) = 0;
        virtual void Shutdown() = 0;

        virtual void WaitIdle() = 0;

        virtual bool BeginFrame(FrameHandle& outFrame) = 0;
        virtual void EndFrame(const FrameHandle& frame) = 0;
        virtual void Present(const FrameHandle& frame) = 0;

        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
        virtual Platform::Extent2D GetBackbufferExtent() const = 0;

        virtual ICommandContext& GetGraphicsContext(std::uint32_t frameIndex) = 0;
    };

}
