// Persisted ICP operands and controls shared by file, agent and editor commands.
module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.RegistrationConfig;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kRegistrationConfigSectionName = "sandbox.registration";
    inline constexpr std::string_view kRegistrationConfigSectionSchemaId = "intrinsic.runtime.sandbox.registration";
    enum class RegistrationBackend : std::uint8_t { CpuKDTree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(RegistrationBackend backend) noexcept;
    enum class EditorICPVariant : std::uint8_t
    {
        PointToPoint,
        PointToPlane,
    };

    struct RegistrationConfig
    {
        std::uint32_t SourceStableEntityId{0u};
        std::uint32_t TargetStableEntityId{0u};
        EditorICPVariant Variant{EditorICPVariant::PointToPoint};
        std::uint32_t MaxIterations{50u};
        // Entity-transformed distance cutoff; <= 0 uses the legacy 1e6 default.
        double MaxCorrespondenceDistance{0.0};
        double InlierRatio{0.9};
        // Trajectory step whose cumulative source->target pose is written to the
        // source entity Transform. 0 = identity (un-registered start); values at
        // or beyond the completed iteration count clamp to the converged pose.
        std::size_t TrajectoryStep{0u};
        // Unknown domain is the legacy default vertex/node domain; explicit bindings are preserved.
        GeometryPropertyRef SourcePositions{.Domain = GeometryElementDomain::Unknown, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef TargetPositions{.Domain = GeometryElementDomain::Unknown, .Name = "v:position", .ValueKind = Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef TargetNormals{.Domain = GeometryElementDomain::Unknown, .Name = "v:normal", .ValueKind = Geometry::PropertyValueKind::Vec3};
        RegistrationBackend Backend{RegistrationBackend::CpuKDTree};
        double ConvergenceThreshold{1e-6};
    };

    [[nodiscard]] std::string SerializeRegistrationConfig(const RegistrationConfig& config);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateRegistrationConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    [[nodiscard]] std::optional<RegistrationConfig> GetRegistrationConfig(const Core::Config::EngineConfig& config);
    void SetRegistrationConfig(Core::Config::EngineConfig& config, const RegistrationConfig& value);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeRegistrationConfigSectionRegistration();
}
