// Persisted point-to-mesh/graph construction and canonical input bindings.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.PointConstructionConfig;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kPointConstructionConfigSectionName =
        "sandbox.point_construction";
    inline constexpr std::string_view kPointConstructionConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.point_construction";
    enum class PointConstructionMethod : std::uint8_t
    {
        Hoppe,
        KnnGraph
    };
    enum class PointConstructionBackend : std::uint8_t
    {
        CpuReference,
        CpuLBVH,
        VulkanLBVH
    };
    [[nodiscard]] const char* ToString(PointConstructionMethod) noexcept;
    [[nodiscard]] const char* ToString(PointConstructionBackend) noexcept;
    struct PointConstructionConfig
    {
        std::uint32_t StableEntityId{};
        PointConstructionMethod Method{PointConstructionMethod::Hoppe};
        PointConstructionBackend Backend{PointConstructionBackend::CpuReference};
        GeometryPropertyRef Positions{.Domain = GeometryElementDomain::Unknown,
                                      .Name = "v:position",
                                      .ValueKind = Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Normals{.Domain = GeometryElementDomain::Unknown,
                                    .Name = "v:normal",
                                    .ValueKind = Geometry::PropertyValueKind::Vec3};
        std::string OutputName{"Constructed geometry"};
        bool EstimateNormals{true}, Mutual{};
        std::uint32_t Resolution{32}, KNeighbors{8}, NormalKNeighbors{15};
        std::uint32_t GpuQueryBatchSize{4096}, MaxGridVertices{1u << 22};
        float BoundingBoxPadding{0.1f}, NormalAgreementPower{2}, KernelSigmaScale{2};
        float MinDistanceEpsilon{1e-12f};
    };
    [[nodiscard]] std::string SerializePointConstructionConfig(const PointConstructionConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidatePointConstructionConfigSection(std::string_view payload, std::string_view reference,
                                           std::string_view subject);
    [[nodiscard]] std::optional<PointConstructionConfig>
    GetPointConstructionConfig(const Core::Config::EngineConfig&);
    void SetPointConstructionConfig(Core::Config::EngineConfig&, const PointConstructionConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakePointConstructionConfigSectionRegistration();
} // namespace Extrinsic::Runtime
