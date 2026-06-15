module;

#include <cstdint>
#include <string>

export module Extrinsic.Core.Config.Window;

namespace Extrinsic::Core::Config
{
    export enum class WindowBackend : std::uint32_t
    {
        Configured = 0,
        Null = 1,
    };

    export struct WindowConfig
    {
        std::string Title{"Extrinsic"};
        int Width{1920};
        int Height{1080};
        bool Resizable{true};
        WindowBackend Backend{WindowBackend::Configured};
    };
}
