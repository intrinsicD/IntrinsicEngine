// Persisted mesh curvature controls and typed vertex property bindings.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.MeshCurvatureConfig;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kMeshCurvatureConfigSectionName = "sandbox.mesh_curvature";
    inline constexpr std::string_view kMeshCurvatureConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.mesh_curvature";
    enum class EditorMeshCurvatureOutput : std::uint8_t
    {
        All, Mean, Gaussian, PrincipalDirections,
    };
    struct MeshCurvatureConfig
    {
        std::uint32_t StableEntityId{};
        EditorMeshCurvatureOutput Output{EditorMeshCurvatureOutput::All};
        bool PublishPrincipalDirections{true};
        GeometryPropertyRef Positions{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Mean{GeometryElementDomain::MeshVertex, "v:mean_curvature", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Gaussian{GeometryElementDomain::MeshVertex, "v:gaussian_curvature", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef MinPrincipal{GeometryElementDomain::MeshVertex, "v:min_principal_curvature", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef MaxPrincipal{GeometryElementDomain::MeshVertex, "v:max_principal_curvature", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef Direction1{GeometryElementDomain::MeshVertex, "v:principal_dir1", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Direction2{GeometryElementDomain::MeshVertex, "v:principal_dir2", Geometry::PropertyValueKind::Vec3};
    };
    [[nodiscard]] bool IsValidMeshCurvaturePropertyBindings(const MeshCurvatureConfig& config) noexcept;
    [[nodiscard]] std::string SerializeMeshCurvatureConfig(const MeshCurvatureConfig& config);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateMeshCurvatureConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    [[nodiscard]] std::optional<MeshCurvatureConfig> GetMeshCurvatureConfig(const Core::Config::EngineConfig& config);
    void SetMeshCurvatureConfig(Core::Config::EngineConfig& config, const MeshCurvatureConfig& value);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeMeshCurvatureConfigSectionRegistration();
}
