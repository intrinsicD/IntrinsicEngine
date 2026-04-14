export module Extrinsic.Core.Config;

import <cstdint>;
import <string>;

namespace Extrinsic::Core
{
    export enum class GraphicsBackend : std::uint8_t
    {
        Vulkan
    };

    export struct WindowConfig
    {
        std::string Title{"Extrinsic"};
        std::uint32_t Width{1920};
        std::uint32_t Height{1080};
        bool Resizable{true};
    };

    export struct RenderConfig
    {
        GraphicsBackend Backend{GraphicsBackend::Vulkan};
        bool EnableValidation{true};
        bool EnableVSync{true};
        std::uint32_t FramesInFlight{2};
    };

    export struct ExtrinsicConfig
    {
        WindowConfig Window{};
        RenderConfig Render{};
    };
}