// Persisted canonical-domain spacing/radius controls and neighborhood backend selection.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.PointSpacingConfig;
export import Extrinsic.Runtime.GeometryAvailability;
export import Geometry.PointCloud.Utils;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kPointSpacingConfigSectionName="sandbox.point_spacing";
    inline constexpr std::string_view kPointSpacingConfigSectionSchemaId="intrinsic.runtime.sandbox.point_spacing";
    enum class PointSpacingBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(PointSpacingBackend) noexcept;
    struct PointSpacingConfig
    {
        std::uint32_t StableEntityId{};
        PointSpacingBackend Backend{PointSpacingBackend::CpuOctree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Radii{.Domain=GeometryElementDomain::Unknown,.Name="radii",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{6}, GpuQueryBatchSize{4096};
        float ScaleFactor{1};
    };
    [[nodiscard]] std::string SerializePointSpacingConfig(const PointSpacingConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    [[nodiscard]] std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig&);
    void SetPointSpacingConfig(Core::Config::EngineConfig&,const PointSpacingConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration();
}
