// Persisted compact-support density weights and canonical property bindings.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.DensityWeightConfig;
export import Extrinsic.Runtime.GeometryAvailability;
export import Geometry.PointCloud.Kernels;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kDensityWeightConfigSectionName="sandbox.density_weights";
    inline constexpr std::string_view kDensityWeightConfigSectionSchemaId="intrinsic.runtime.sandbox.density_weights";
    enum class DensityWeightBackend : std::uint8_t { CpuKDTree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(DensityWeightBackend) noexcept;
    struct DensityWeightConfig
    {
        std::uint32_t StableEntityId{};
        DensityWeightBackend Backend{DensityWeightBackend::CpuKDTree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Weights{.Domain=GeometryElementDomain::Unknown,.Name="density_weight",.ValueKind=Geometry::PropertyValueKind::Float};
        double SupportRadius{1};
        Geometry::PointCloud::Kernels::KernelType Kernel{Geometry::PointCloud::Kernels::KernelType::ThetaLop};
        Geometry::PointCloud::Kernels::DensityWeightMode Mode{Geometry::PointCloud::Kernels::DensityWeightMode::Direct};
        std::uint32_t GpuQueryBatchSize{4096},GpuRadiusCapacity{256};
    };
    [[nodiscard]] std::string SerializeDensityWeightConfig(const DensityWeightConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateDensityWeightConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    [[nodiscard]] std::optional<DensityWeightConfig> GetDensityWeightConfig(const Core::Config::EngineConfig&);
    void SetDensityWeightConfig(Core::Config::EngineConfig&,const DensityWeightConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeDensityWeightConfigSectionRegistration();
}
