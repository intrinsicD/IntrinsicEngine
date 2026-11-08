// Persisted ICP operands and controls shared by file, agent and editor commands.
module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.RegistrationConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    extern "C++" [[nodiscard]] std::span<const ConfigFieldSpec> RegistrationConfigFieldSpecs() noexcept;
    inline constexpr std::string_view kRegistrationConfigSectionName = "sandbox.registration";
    inline constexpr std::string_view kRegistrationConfigSectionSchemaId = "intrinsic.runtime.sandbox.registration";
    // The Vulkan backends share one correspondence path and differ only in the query strategy.
    enum class RegistrationBackend : std::uint8_t
    {
        CpuKDTree,
        CpuLBVH,
        VulkanLBVH,
        VulkanBruteForce
    };
    extern "C++" [[nodiscard]] const char* ToString(RegistrationBackend backend) noexcept;
    enum class EditorICPVariant : std::uint8_t
    {
        PointToPoint,
        PointToPlane,
    };

    // A run's step log holds its whole iteration cap, so the cap is bounded.
    inline constexpr std::uint32_t kMaxRegistrationIterations = 100'000u;
    // One step is one ICP iteration. A configured run computes at once and accepts at convergence or the cap, as
    // the one-shot command did; start=ready and auto_accept=never make it interactive.
    inline constexpr StepRunExecution kDefaultRegistrationExecution{
        .IterationCap = 50u, .AutoAccept = StepRunAutoAccept::End, .Start = StepRunStart::Run};

    struct RegistrationConfig
    {
        std::uint32_t SourceStableEntityId{0u};
        std::uint32_t TargetStableEntityId{0u};
        EditorICPVariant Variant{EditorICPVariant::PointToPoint};
        StepRunExecution Execution{kDefaultRegistrationExecution};
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

    extern "C++" [[nodiscard]] std::string SerializeRegistrationConfig(const RegistrationConfig& config);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateRegistrationConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    extern "C++" [[nodiscard]] std::optional<RegistrationConfig> GetRegistrationConfig(const Core::Config::EngineConfig& config);
    extern "C++" void SetRegistrationConfig(Core::Config::EngineConfig& config, const RegistrationConfig& value);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeRegistrationConfigSectionRegistration();
}
