module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.PointCloudConsolidationConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view
        kPointCloudConsolidationConfigSectionName =
            "sandbox.point_cloud_consolidation";
    inline constexpr std::string_view
        kPointCloudConsolidationConfigSectionSchemaId =
            "intrinsic.runtime.sandbox.point-cloud-consolidation";
    inline constexpr std::uint32_t
        kPointCloudConsolidationConfigSectionSchemaVersion = 1u;

    enum class PointCloudConsolidationStrategy : std::uint32_t
    {
        Lop = 0u,
        Wlop,
        Clop,
        Ear,
    };

    enum class PointCloudConsolidationNormalSource : std::uint32_t
    {
        AuthoredOrEstimate = 0u,
        RequireAuthored,
    };

    struct PointCloudConsolidationConfig
    {
        PointCloudConsolidationStrategy Strategy{
            PointCloudConsolidationStrategy::Wlop};
        double SupportRadius{1.0};
        double RepulsionWeight{0.45};
        std::uint32_t MaxIterations{20u};
        double ConvergenceTolerance{1.0e-4};
        std::uint32_t TargetPointCount{0u};
        std::uint32_t Seed{42u};

        bool WlopAnisotropic{false};
        PointCloudConsolidationNormalSource NormalSource{
            PointCloudConsolidationNormalSource::AuthoredOrEstimate};
        double NormalAngleRadians{0.26179938779914943654};
        std::uint32_t NormalRefinementRounds{3u};

        std::uint32_t ClopMixtureComponentCount{16u};
        std::uint32_t ClopMixtureMaxIterations{100u};
        double ClopMixtureRelativeTolerance{1.0e-6};
        double ClopCovarianceFloor{1.0e-6};

        double EarEdgeSensitivity{5.0};
    };

    [[nodiscard]] std::string_view StableToken(
        PointCloudConsolidationStrategy strategy) noexcept;
    [[nodiscard]] std::string_view StableToken(
        PointCloudConsolidationNormalSource source) noexcept;

    [[nodiscard]] std::string SerializePointCloudConsolidationConfig(
        const PointCloudConsolidationConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidatePointCloudConsolidationConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<PointCloudConsolidationConfig>
    GetPointCloudConsolidationConfig(
        const Core::Config::EngineConfig& config);

    void SetPointCloudConsolidationConfig(
        Core::Config::EngineConfig& config,
        const PointCloudConsolidationConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakePointCloudConsolidationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
}
