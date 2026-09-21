// Persisted centroid-PCA keypoint detection with typed mask and saliency outputs.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.KeypointAnalysisConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kKeypointAnalysisConfigSectionName="sandbox.keypoint_analysis";
    inline constexpr std::string_view kKeypointAnalysisConfigSectionSchemaId="intrinsic.runtime.sandbox.keypoint_analysis";
    enum class KeypointAnalysisBackend : std::uint8_t { CpuKDTree, CpuLBVH, VulkanLBVH, VulkanCompute };
    extern "C++" [[nodiscard]] const char* ToString(KeypointAnalysisBackend) noexcept;
    struct KeypointAnalysisConfig
    {
        std::uint32_t StableEntityId{};
        KeypointAnalysisBackend Backend{KeypointAnalysisBackend::CpuKDTree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Mask{.Domain=GeometryElementDomain::Unknown,.Name="keypoint_mask",.ValueKind=Geometry::PropertyValueKind::UInt32};
        GeometryPropertyRef Score{.Domain=GeometryElementDomain::Unknown,.Name="keypoint_saliency",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t MinimumNeighbors{5}, GpuQueryBatchSize{4096}, GpuRadiusCapacity{256};
        float SalientRadius{}, NonMaxRadius{};
        double Gamma21{.975}, Gamma32{.975};
    };
    extern "C++" [[nodiscard]] std::string SerializeKeypointAnalysisConfig(const KeypointAnalysisConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateKeypointAnalysisConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    extern "C++" [[nodiscard]] std::optional<KeypointAnalysisConfig> GetKeypointAnalysisConfig(const Core::Config::EngineConfig&);
    extern "C++" void SetKeypointAnalysisConfig(Core::Config::EngineConfig&,const KeypointAnalysisConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeKeypointAnalysisConfigSectionRegistration();
}
