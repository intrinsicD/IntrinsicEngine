module;

#include <cstdint>

export module Extrinsic.Core.Config.Engine;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Simulation;
import Extrinsic.Core.Config.Window;

namespace Extrinsic::Core::Config
{
        // Selector for the reference-scene provider installed by
        // Runtime::Engine when ReferenceSceneConfig::Enabled is true. The
        // implementation surface lives under Extrinsic.Runtime.ReferenceScene
        // (per GRAPHICS-029 Decisions 1, 8); core only carries the value-type
        // enum so EngineConfig stays free of runtime/graphics imports.
        export enum class ReferenceSceneSelector : std::uint32_t
        {
            Triangle = 0,
        };

        export struct ReferenceSceneConfig
        {
            bool Enabled{false};
            ReferenceSceneSelector Selector{ReferenceSceneSelector::Triangle};
        };

        export enum class CameraControllerKind : std::uint32_t
        {
            Orbit = 0,
            Fly = 1,
            FreeLook = 2,
            TopDown = 3,
        };

        export struct CameraConfig
        {
            bool Enabled{true};
            CameraControllerKind Controller{CameraControllerKind::Orbit};
        };

        export struct EngineConfig
        {
            RenderConfig    Render;
            SimulationConfig Simulation;
            WindowConfig    Window;
            ReferenceSceneConfig ReferenceScene;
            CameraConfig Camera;
        };

}
