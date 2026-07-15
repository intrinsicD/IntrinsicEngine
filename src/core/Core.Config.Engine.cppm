module;

#include <cstdint>
#include <vector>

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

        export enum class ParameterizationStrategyKind : std::uint32_t
        {
            Lscm = 0,
            HarmonicCotangent,
            TutteUniform,
            Bff,
        };

        export enum class ParameterizationBoundaryPolicy : std::uint32_t
        {
            Circle = 0,
            Square,
            Custom,
        };

        export enum class ParameterizationBffBoundaryMode : std::uint32_t
        {
            AutomaticConformal = 0,
            TargetLengths,
            TargetAngles,
        };

        export enum class ParameterizationUvRenderMode : std::uint32_t
        {
            CpuLayout = 0,
            GpuShaded,
        };

        export enum class ParameterizationUvBackgroundMode : std::uint32_t
        {
            Grid = 0,
            Checker,
            TexelDensity,
            Texture,
        };

        export struct ParameterizationViewConfig
        {
            ParameterizationUvRenderMode RenderMode{
                ParameterizationUvRenderMode::CpuLayout};
            ParameterizationUvBackgroundMode BackgroundMode{
                ParameterizationUvBackgroundMode::Grid};
            bool ShowDistortionHeatmap{false};
        };

        export struct ParameterizationUvConfig
        {
            double U{0.0};
            double V{0.0};
        };

        export struct ParameterizationLscmConfig
        {
            bool AutoPins{true};
            std::uint32_t PinVertex0{0u};
            std::uint32_t PinVertex1{1u};
            ParameterizationUvConfig PinUv0{};
            ParameterizationUvConfig PinUv1{1.0, 0.0};
            double SolverTolerance{1.0e-8};
            std::uint32_t MaxSolverIterations{5000u};
        };

        export struct ParameterizationHarmonicConfig
        {
            ParameterizationBoundaryPolicy Boundary{
                ParameterizationBoundaryPolicy::Circle};
            bool ArcLengthSpacing{true};
            bool ClampNonConvexWeights{true};
            std::vector<std::uint32_t> PinnedVertices{};
            std::vector<ParameterizationUvConfig> PinnedUvs{};
        };

        export struct ParameterizationBffConfig
        {
            ParameterizationBffBoundaryMode Mode{
                ParameterizationBffBoundaryMode::AutomaticConformal};
            std::vector<double> BoundaryData{};
            double AngleSumTolerance{1.0e-8};
            double DegeneracyTolerance{1.0e-12};
        };

        export struct ParameterizationConfig
        {
            ParameterizationStrategyKind Strategy{ParameterizationStrategyKind::Lscm};
            ParameterizationLscmConfig Lscm{};
            ParameterizationHarmonicConfig Harmonic{};
            ParameterizationBffConfig Bff{};
            ParameterizationViewConfig View{};
        };

        export struct SandboxConfig
        {
            ProgressivePoissonPlaygroundConfig ProgressivePoisson{};
            ParameterizationConfig Parameterization{};
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
