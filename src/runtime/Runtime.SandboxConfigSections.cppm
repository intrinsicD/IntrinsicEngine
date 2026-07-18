module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.SandboxConfigSections;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

export namespace Extrinsic::Runtime
{
    using EngineConfigSectionRegistry =
        Core::Config::EngineConfigSectionRegistry;
    using EngineConfigSectionRegistration =
        Core::Config::EngineConfigSectionRegistration;
    using EngineConfigSectionChangedCallback =
        Core::Config::EngineConfigSectionChangedCallback;

    inline constexpr std::string_view kProgressivePoissonConfigSectionName =
        "sandbox.progressive_poisson";
    inline constexpr std::string_view kProgressivePoissonConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.progressive-poisson";
    inline constexpr std::uint32_t kProgressivePoissonConfigSectionSchemaVersion =
        1u;

    inline constexpr std::string_view kParameterizationConfigSectionName =
        "sandbox.parameterization";
    inline constexpr std::string_view kParameterizationConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.parameterization";
    inline constexpr std::uint32_t kParameterizationConfigSectionSchemaVersion =
        1u;

    enum class ProgressivePoissonPlaygroundChannel : std::uint32_t
    {
        Level = 0,
        Phase = 1,
        SplatRadius = 2,
        PrefixVisible = 3,
    };

    enum class ProgressivePoissonPlaygroundBackend : std::uint32_t
    {
        CpuReference = 0,
        VulkanCompute = 1,
    };

    struct ProgressivePoissonPlaygroundConfig
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

    [[nodiscard]] std::string SerializeProgressivePoissonPlaygroundConfig(
        const ProgressivePoissonPlaygroundConfig& config);
    [[nodiscard]] std::string SerializeParameterizationConfig(
        const ParameterizationConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateProgressivePoissonConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateParameterizationConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfig(
        const Core::Config::EngineConfig& config);
    void SetProgressivePoissonPlaygroundConfig(
        Core::Config::EngineConfig& config,
        const ProgressivePoissonPlaygroundConfig& value);

    [[nodiscard]] std::optional<ParameterizationConfig>
    GetParameterizationConfig(const Core::Config::EngineConfig& config);
    void SetParameterizationConfig(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistration(
        EngineConfigSectionChangedCallback onChanged = {});
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistration(
        EngineConfigSectionChangedCallback onChanged = {});
}
