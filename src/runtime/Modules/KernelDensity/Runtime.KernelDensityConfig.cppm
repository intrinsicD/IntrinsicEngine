// Persisted canonical-domain local Gaussian density controls and backend selection.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.KernelDensityConfig;
export import Extrinsic.Runtime.GeometryAvailability;
export import Geometry.PointCloud.Utils;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kKernelDensityConfigSectionName="sandbox.kernel_density";
    inline constexpr std::string_view kKernelDensityConfigSectionSchemaId="intrinsic.runtime.sandbox.kernel_density";
    enum class KernelDensityBackend : std::uint8_t { CpuOctree, CpuLBVH, VulkanLBVH };
    [[nodiscard]] const char* ToString(KernelDensityBackend) noexcept;
    struct KernelDensityConfig
    {
        std::uint32_t StableEntityId{};
        KernelDensityBackend Backend{KernelDensityBackend::CpuOctree};
        GeometryPropertyRef Positions{.Domain=GeometryElementDomain::Unknown,.Name="v:position",.ValueKind=Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Density{.Domain=GeometryElementDomain::Unknown,.Name="density",.ValueKind=Geometry::PropertyValueKind::Float};
        std::uint32_t KNeighbors{15}, GpuQueryBatchSize{4096};
        float Bandwidth{0};
    };
    [[nodiscard]] std::string SerializeKernelDensityConfig(const KernelDensityConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateKernelDensityConfigSection(
        std::string_view payload,std::string_view reference,std::string_view subject);
    [[nodiscard]] std::optional<KernelDensityConfig> GetKernelDensityConfig(const Core::Config::EngineConfig&);
    void SetKernelDensityConfig(Core::Config::EngineConfig&,const KernelDensityConfig&);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeKernelDensityConfigSectionRegistration();
}
