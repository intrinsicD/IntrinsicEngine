module;

#include <cstdint>
#include <string>

export module Extrinsic.Core.Config.Window;

namespace Extrinsic::Core::Config
{
    export struct WindowConfig
    {
        std::string Title{"Extrinsic"};
        std::uint32_t Width{1920};
        std::uint32_t Height{1080};
        bool Resizable{true};
    };
}
