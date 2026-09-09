// Persisted normal method, canonical bindings and numerical controls for UI and agents.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <glm/glm.hpp>
export module Extrinsic.Runtime.NormalEstimationConfig;
export import Extrinsic.Runtime.GeometryAvailability;
export import Geometry.PointCloud.Normals;
export import Geometry.HalfedgeMesh.Vertices.Normals;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kNormalEstimationConfigSectionName = "sandbox.normal_estimation";
    inline constexpr std::string_view kNormalEstimationConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.normal_estimation";
    enum class NormalEstimationMethod : std::uint8_t
    {
        PointSetPCA,
        MeshFaceWeighted,
        GraphNeighborhood,
        MeshFaceNormals
    };
    enum class NormalEstimationBackend : std::uint8_t
    {
        CpuKDTree,
        CpuLBVH
    };
    [[nodiscard]] const char *ToString(NormalEstimationMethod method) noexcept;
    [[nodiscard]] const char *ToString(NormalEstimationBackend backend) noexcept;
    struct NormalEstimationConfig
    {
        std::uint32_t StableEntityId{};
        NormalEstimationMethod Method{NormalEstimationMethod::PointSetPCA};
        NormalEstimationBackend Backend{NormalEstimationBackend::CpuKDTree};
        GeometryPropertyRef Positions{.Domain = GeometryElementDomain::Unknown,
                                      .Name = "v:position",
                                      .ValueKind = Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Output{.Domain = GeometryElementDomain::Unknown,
                                   .Name = "v:normal",
                                   .ValueKind = Geometry::PropertyValueKind::Vec3};
        std::uint32_t KNeighbors{15}, MinimumNeighbors{2};
        bool UseRadiusSearch{};
        float Radius{};
        Geometry::PointCloud::Normals::OrientationMode Orientation{
            Geometry::PointCloud::Normals::OrientationMode::MinimumSpanningTree};
        glm::vec3 FallbackNormal{0, 0, 1};
        double DegenerateNormalLengthEpsilon{1e-12}, CollinearEigenvalueRatioEpsilon{1e-5};
        Geometry::HalfedgeMesh::VertexNormals::AveragingMode Weighting{
            Geometry::HalfedgeMesh::VertexNormals::AveragingMode::AreaWeighted};
        bool OrientTowardFallback{true};
    };
    [[nodiscard]] std::string SerializeNormalEstimationConfig(const NormalEstimationConfig &config);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateNormalEstimationConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    [[nodiscard]] std::optional<NormalEstimationConfig> GetNormalEstimationConfig(
        const Core::Config::EngineConfig &config);
    void SetNormalEstimationConfig(Core::Config::EngineConfig &config, const NormalEstimationConfig &value);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeNormalEstimationConfigSectionRegistration();
} // namespace Extrinsic::Runtime
