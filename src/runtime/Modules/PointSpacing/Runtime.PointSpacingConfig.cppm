// Persisted canonical-domain spacing/radius controls and neighborhood backend selection.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.PointSpacingConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kPointSpacingConfigSectionName="sandbox.point_spacing";
    inline constexpr std::string_view kPointSpacingConfigSectionSchemaId="intrinsic.runtime.sandbox.point_spacing";
    enum class PointSpacingBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    extern "C++" [[nodiscard]] const char* ToString(PointSpacingBackend) noexcept;
    struct PointSpacingConfig
    {
        std::uint32_t StableEntityId{};
        PointSpacingBackend Backend{PointSpacingBackend::CpuOctree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Radii{.Domain=GeometryElementDomain::Unknown,.Name="radii",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{6}, GpuQueryBatchSize{4096};
        float ScaleFactor{1};
    };
    extern "C++" [[nodiscard]] std::string SerializePointSpacingConfig(const PointSpacingConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidatePointSpacingConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    extern "C++" [[nodiscard]] std::optional<PointSpacingConfig> GetPointSpacingConfig(const Core::Config::EngineConfig&);
    extern "C++" void SetPointSpacingConfig(Core::Config::EngineConfig&,const PointSpacingConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakePointSpacingConfigSectionRegistration();
}
