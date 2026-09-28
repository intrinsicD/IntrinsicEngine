// Persisted Coherent Point Drift operands and controls (RUNTIME-273) shared by file, agent
// and editor commands. Fields are declared once in a ConfigFieldSpec table that drives
// validation, the generated schema and the panel's hints.
module;
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.CoherentPointDriftConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kCoherentPointDriftConfigSectionName = "sandbox.coherent_point_drift";
    inline constexpr std::string_view kCoherentPointDriftConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.coherent_point_drift";

    enum class CoherentPointDriftMethod : std::uint8_t { Rigid, Affine, Nonrigid };
    // Where a result goes: the source entity's Transform (rigid only; the TRS component
    // cannot hold an affine shear), the source position property itself, or a named vec3
    // displacement property on the source domain (affine and nonrigid).
    enum class CoherentPointDriftOutput : std::uint8_t { SourceTransform, Positions, DisplacementProperty };

    struct CoherentPointDriftConfig
    {
        std::uint32_t SourceStableEntityId{0u}; // moving
        std::uint32_t TargetStableEntityId{0u}; // fixed
        // Unknown domain resolves to the entity's primary point domain.
        GeometryPropertyRef SourcePositions{GeometryElementDomain::Unknown, "v:position", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef TargetPositions{GeometryElementDomain::Unknown, "v:position", Geometry::PropertyValueKind::Vec3};
        CoherentPointDriftMethod Method{CoherentPointDriftMethod::Rigid};
        double OutlierWeight{0.1};
        std::uint32_t MaxIterations{150u};
        double Tolerance{1.0e-5};
        double InitialSigma2{0.0};
        double Sigma2Floor{1.0e-10};
        bool NormalizeInputs{true};
        bool EstimateScale{false};
        bool AllowReflection{false};
        double Beta{2.0};
        double Lambda{3.0};
        CoherentPointDriftOutput Output{CoherentPointDriftOutput::SourceTransform};
        std::string DisplacementName{"cpd_displacement"};
    };

    [[nodiscard]] std::string SerializeCoherentPointDriftConfig(const CoherentPointDriftConfig& config);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateCoherentPointDriftConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    // The validated section, or nullopt when missing or invalid.
    [[nodiscard]] std::optional<CoherentPointDriftConfig> GetCoherentPointDriftConfig(const Core::Config::EngineConfig& config);
    void SetCoherentPointDriftConfig(Core::Config::EngineConfig& config, const CoherentPointDriftConfig& value);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeCoherentPointDriftConfigSectionRegistration();
    [[nodiscard]] std::span<const ConfigFieldSpec> CoherentPointDriftConfigFieldSpecs() noexcept;
    // Payload decoding for callers holding a validated canonical payload.
    [[nodiscard]] std::optional<CoherentPointDriftConfig> DecodeCoherentPointDriftConfig(std::string_view payload);
}
