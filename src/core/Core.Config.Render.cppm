module;

#include <cstdint>

export module Extrinsic.Core.Config.Render;

namespace Extrinsic::Core::Config
{
    export enum class GraphicsBackend : std::uint8_t
    {
        Vulkan
    };

    export struct RenderConfig
    {
        GraphicsBackend Backend{GraphicsBackend::Vulkan};
        bool EnableValidation{true};
        bool EnableVSync{true};
        std::uint32_t FramesInFlight{2};
    };
}
