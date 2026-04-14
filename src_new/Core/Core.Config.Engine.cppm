export module Extrinsic.Core.Config.Engine;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Window;

namespace Extrinsic::Core::Config
{
        export struct EngineConfig
        {
            RenderConfig Render;
            WindowConfig Window;
        };

}