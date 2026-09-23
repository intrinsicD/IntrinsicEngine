// Persisted parameterization strategies, typed mesh bindings, and UV view controls.
module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.ParameterizationConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Geometry.UvAtlas.Types;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kParameterizationConfigSectionName =
        "sandbox.parameterization";
    inline constexpr std::string_view kParameterizationConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.parameterization";
    inline constexpr std::uint32_t kParameterizationConfigSectionSchemaVersion =
        3u;

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
        bool SplitEnabled{true};
        bool AtlasOnLeft{false};
        float SplitRatio{0.5f};
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

    struct ParameterizationAtlasConfig
    {
        Geometry::UvAtlas::UvAtlasMethod Method{Geometry::UvAtlas::UvAtlasMethod::FastStaged};
        Geometry::UvAtlas::UvAtlasDistortion Distortion{Geometry::UvAtlas::UvAtlasDistortion::Both};
        // Null selects geometry-only charting; any scalar mesh vertex/face
        // property may guide segmentation regardless of its name or provenance.
        std::optional<GeometryPropertyRef> Guide{};
        std::uint32_t RegionCount{0u};
        std::uint32_t Resolution{1024u};
        std::uint32_t Padding{2u};
        float TexelsPerUnit{0.0f};
        double MaxConformalDistortion{10.0};
        double MaxAreaDistortion{10.0};
        std::uint32_t MaxCharts{16384u};
        std::uint32_t MaxIterations{40u};
        bool AllowXAtlasFallback{true};
    };

    struct ParameterizationConfig
    {
        ParameterizationStrategyKind Strategy{ParameterizationStrategyKind::Lscm};
        ParameterizationLscmConfig Lscm{};
        ParameterizationHarmonicConfig Harmonic{};
        ParameterizationBffConfig Bff{};
        ParameterizationViewConfig View{};
        ParameterizationAtlasConfig Atlas{};
        GeometryPropertyRef Positions{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Texcoords{GeometryElementDomain::MeshVertex, "v:texcoord", Geometry::PropertyValueKind::Vec2};
        // Null preserves corner storage; a bound property is retired with undo.
        std::optional<GeometryPropertyRef> CornerTexcoordsToRetire{};
    };

    // Defined by the shared config codec translation unit, which compiles the
    // JSON dependency once for every feature family.
    extern "C++"
    {
        [[nodiscard]] std::optional<std::string> ValidateParameterizationAtlasConfig(
            const ParameterizationAtlasConfig& config);

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
}
