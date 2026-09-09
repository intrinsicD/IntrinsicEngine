// Persisted statistical/radius/distance-ratio detection and explicit marked-point removal controls.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.OutlierAnalysisConfig;
export import Extrinsic.Runtime.GeometryAvailability;
export import Geometry.PointCloud.Utils;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kOutlierAnalysisConfigSectionName="sandbox.outlier_analysis";
    inline constexpr std::string_view kOutlierAnalysisConfigSectionSchemaId="intrinsic.runtime.sandbox.outlier_analysis";
    enum class OutlierAnalysisMethod : std::uint8_t { Statistical, Radius, LocalDistanceRatio };
    enum class OutlierAnalysisBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    enum class OutlierAnalysisOperation : std::uint8_t { Analyze, RemoveMarked };
    [[nodiscard]] const char* ToString(OutlierAnalysisMethod) noexcept;
    [[nodiscard]] const char* ToString(OutlierAnalysisBackend) noexcept;
    [[nodiscard]] const char* ToString(OutlierAnalysisOperation) noexcept;
    struct OutlierAnalysisConfig
    {
        std::uint32_t StableEntityId{};
        OutlierAnalysisMethod Method{OutlierAnalysisMethod::Statistical};
        OutlierAnalysisBackend Backend{OutlierAnalysisBackend::CpuOctree};
        OutlierAnalysisOperation Operation{OutlierAnalysisOperation::Analyze};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Mask{.Domain=GeometryElementDomain::Unknown,.Name="outlier_mask",.ValueKind=Geometry::PropertyValueKind::UInt32};
        GeometryPropertyRef Score{.Domain=GeometryElementDomain::Unknown,.Name="outlier_score",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{16}, MinimumNeighbors{4}, GpuQueryBatchSize{4096};
        float StdDevMultiplier{1}, Radius{1}, ScoreThreshold{2};
    };
    [[nodiscard]] std::string SerializeOutlierAnalysisConfig(const OutlierAnalysisConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateOutlierAnalysisConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    [[nodiscard]] std::optional<OutlierAnalysisConfig> GetOutlierAnalysisConfig(const Core::Config::EngineConfig&);
    void SetOutlierAnalysisConfig(Core::Config::EngineConfig&,const OutlierAnalysisConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeOutlierAnalysisConfigSectionRegistration();
}
