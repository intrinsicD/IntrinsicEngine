module;

#include <string>

export module Extrinsic.Core.Config.Window;

namespace Extrinsic::Core::Config
{
    export struct WindowConfig
    {
        std::string Title{"Extrinsic"};
        int Width{1920};
        int Height{1080};
        bool Resizable{true};
    };
}
