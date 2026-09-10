// Persisted canonical-domain bilateral point-filter controls and neighborhood backend selection.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.BilateralFilterConfig;
export import Extrinsic.Runtime.GeometryAvailability;
export import Geometry.PointCloud.Utils;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kBilateralFilterConfigSectionName="sandbox.bilateral_filter";
    inline constexpr std::string_view kBilateralFilterConfigSectionSchemaId="intrinsic.runtime.sandbox.bilateral_filter";
    enum class BilateralFilterBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(BilateralFilterBackend) noexcept;
    struct BilateralFilterConfig
    {
        std::uint32_t StableEntityId{};
        BilateralFilterBackend Backend{BilateralFilterBackend::CpuOctree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Normals{.Domain=GeometryElementDomain::Unknown,.Name="v:normal",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Output{.Domain=GeometryElementDomain::Unknown,.Name="filtered_positions",.ValueKind=Geometry::PropertyValueKind::Vec3};
        std::uint32_t KNeighbors{15}, GpuQueryBatchSize{4096};
        float SpatialSigma{}, NormalSigma{.25f};
        std::uint32_t Iterations{1};
    };
    [[nodiscard]] std::string SerializeBilateralFilterConfig(const BilateralFilterConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateBilateralFilterConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    [[nodiscard]] std::optional<BilateralFilterConfig> GetBilateralFilterConfig(const Core::Config::EngineConfig&);
    void SetBilateralFilterConfig(Core::Config::EngineConfig&,const BilateralFilterConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeBilateralFilterConfigSectionRegistration();
}
