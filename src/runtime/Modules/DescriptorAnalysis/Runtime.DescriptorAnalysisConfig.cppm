// Persisted FPFH analysis with canonical position/normal inputs and 33 float output columns.
module;
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.DescriptorAnalysisConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kDescriptorAnalysisConfigSectionName="sandbox.descriptor_analysis";
    inline constexpr std::string_view kDescriptorAnalysisConfigSectionSchemaId="intrinsic.runtime.sandbox.descriptor_analysis";
    enum class DescriptorAnalysisBackend : std::uint8_t { CpuKDTree, CpuLBVH, VulkanLBVH };
    extern "C++" [[nodiscard]] const char* ToString(DescriptorAnalysisBackend) noexcept;
    extern "C++" [[nodiscard]] std::array<GeometryPropertyRef,33> MakeDescriptorOutputProperties(
        GeometryElementDomain domain=GeometryElementDomain::Unknown,std::string_view prefix="fpfh");
    struct DescriptorAnalysisConfig
    {
        std::uint32_t StableEntityId{};
        DescriptorAnalysisBackend Backend{DescriptorAnalysisBackend::CpuKDTree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Normals{.Domain=GeometryElementDomain::Unknown,.Name="v:normal",.ValueKind=Geometry::PropertyValueKind::Vec3};
        std::array<GeometryPropertyRef,33> Outputs{MakeDescriptorOutputProperties()};
        std::uint32_t MaxNeighbors{}, GpuQueryBatchSize{4096}, GpuRadiusCapacity{256};
        float FeatureRadius{};
    };
    extern "C++" [[nodiscard]] std::string SerializeDescriptorAnalysisConfig(const DescriptorAnalysisConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateDescriptorAnalysisConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    extern "C++" [[nodiscard]] std::optional<DescriptorAnalysisConfig> GetDescriptorAnalysisConfig(const Core::Config::EngineConfig&);
    extern "C++" void SetDescriptorAnalysisConfig(Core::Config::EngineConfig&,const DescriptorAnalysisConfig&);
    extern "C++" [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeDescriptorAnalysisConfigSectionRegistration();
}
