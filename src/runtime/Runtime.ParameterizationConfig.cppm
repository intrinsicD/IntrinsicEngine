module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.ParameterizationConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kParameterizationConfigSectionName =
        "sandbox.parameterization";
    inline constexpr std::string_view kParameterizationConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.parameterization";
    inline constexpr std::uint32_t kParameterizationConfigSectionSchemaVersion =
        1u;

    enum class ParameterizationStrategyKind : std::uint32_t
    {
        Lscm = 0,
        HarmonicCotangent,
        TutteUniform,
        Bff,
    };

    enum class ParameterizationBoundaryPolicy : std::uint32_t
    {
        Circle = 0,
        Square,
        Custom,
    };

    enum class ParameterizationBffBoundaryMode : std::uint32_t
    {
        AutomaticConformal = 0,
        TargetLengths,
        TargetAngles,
    };

    enum class ParameterizationUvRenderMode : std::uint32_t
    {
        CpuLayout = 0,
        GpuShaded,
    };

    enum class ParameterizationUvBackgroundMode : std::uint32_t
    {
        Grid = 0,
        Checker,
        TexelDensity,
        Texture,
    };

    struct ParameterizationViewConfig
    {
        ParameterizationUvRenderMode RenderMode{
            ParameterizationUvRenderMode::CpuLayout};
        ParameterizationUvBackgroundMode BackgroundMode{
            ParameterizationUvBackgroundMode::Grid};
        bool ShowDistortionHeatmap{false};
    };

    struct ParameterizationUvConfig
    {
        double U{0.0};
        double V{0.0};
    };

    struct ParameterizationLscmConfig
    {
        bool AutoPins{true};
        std::uint32_t PinVertex0{0u};
        std::uint32_t PinVertex1{1u};
        ParameterizationUvConfig PinUv0{};
        ParameterizationUvConfig PinUv1{1.0, 0.0};
        double SolverTolerance{1.0e-8};
        std::uint32_t MaxSolverIterations{5000u};
    };

    struct ParameterizationHarmonicConfig
    {
        ParameterizationBoundaryPolicy Boundary{
            ParameterizationBoundaryPolicy::Circle};
        bool ArcLengthSpacing{true};
        bool ClampNonConvexWeights{true};
        std::vector<std::uint32_t> PinnedVertices{};
        std::vector<ParameterizationUvConfig> PinnedUvs{};
    };

    struct ParameterizationBffConfig
    {
        ParameterizationBffBoundaryMode Mode{
            ParameterizationBffBoundaryMode::AutomaticConformal};
        std::vector<double> BoundaryData{};
        double AngleSumTolerance{1.0e-8};
        double DegeneracyTolerance{1.0e-12};
    };

    struct ParameterizationConfig
    {
        ParameterizationStrategyKind Strategy{ParameterizationStrategyKind::Lscm};
        ParameterizationLscmConfig Lscm{};
        ParameterizationHarmonicConfig Harmonic{};
        ParameterizationBffConfig Bff{};
        ParameterizationViewConfig View{};
    };

    [[nodiscard]] std::string SerializeParameterizationConfig(
        const ParameterizationConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateParameterizationConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<ParameterizationConfig>
    GetParameterizationConfig(const Core::Config::EngineConfig& config);

    void SetParameterizationConfig(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
}
