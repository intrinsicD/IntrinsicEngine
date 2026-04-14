module;

#include <memory>

export module Extrinsic.Platform.Window;

import Extrinsic.Core.Config.Window;

namespace Extrinsic::Platform
{
    export struct Extent2D
    {
        std::uint32_t Width{0};
        std::uint32_t Height{0};
    };

    export class IWindow
    {
    public:
        virtual ~IWindow() = default;

        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;
        virtual bool IsMinimized() const = 0;
        virtual bool WasResized() const = 0;
        virtual void AcknowledgeResize() = 0;
        virtual Extent2D GetExtent() const = 0;
        virtual void* GetNativeHandle() const = 0;
    };

    export std::unique_ptr<IWindow> CreateWindow(const Core::Config::WindowConfig& config);
}