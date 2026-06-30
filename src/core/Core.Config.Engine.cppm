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

        export enum class ProgressivePoissonPlaygroundChannel : std::uint32_t
        {
            Level = 0,
            Phase = 1,
            SplatRadius = 2,
            PrefixVisible = 3,
        };

        export enum class ProgressivePoissonPlaygroundBackend : std::uint32_t
        {
            CpuReference = 0,
            VulkanCompute = 1,
        };

        export struct ProgressivePoissonPlaygroundConfig
        {
            std::uint32_t Dimension{3u};
            std::uint32_t GridWidth{4u};
            std::uint32_t MaxLevels{16u};
            double HashLoadFactor{0.25};
            double RadiusAlpha{-1.0};
            bool RandomizeGridOrigin{true};
            std::uint32_t GridOriginSeed{1337u};
            bool ShuffleWithinLevels{true};
            std::uint32_t ShuffleSeed{0x51ed270bu};
            std::uint32_t PrefixCount{0u};
            ProgressivePoissonPlaygroundChannel Channel{
                ProgressivePoissonPlaygroundChannel::Level};
            ProgressivePoissonPlaygroundBackend Backend{
                ProgressivePoissonPlaygroundBackend::CpuReference};
            std::uint32_t MeshSurfaceSampleCount{4096u};
            std::uint32_t MeshSurfaceSampleSeed{1337u};
            double MeshSurfaceMinTriangleArea{1.0e-14};
            bool MeshSurfaceInterpolateNormals{true};
            bool AutoRunOnEdit{true};
            double DebounceSeconds{0.25};
        };

        export struct SandboxConfig
        {
            ProgressivePoissonPlaygroundConfig ProgressivePoisson{};
        };

        export struct EngineConfig
        {
            RenderConfig    Render;
            SimulationConfig Simulation;
            WindowConfig    Window;
            ReferenceSceneConfig ReferenceScene;
            CameraConfig Camera;
            SandboxConfig Sandbox;
        };

}
