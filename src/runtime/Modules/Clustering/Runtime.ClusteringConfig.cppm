module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ClusteringConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export import Extrinsic.Runtime.ClusteringModule;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kClusteringConfigSectionName =
        "sandbox.clustering";
    inline constexpr std::string_view kClusteringConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.clustering";
    inline constexpr std::uint32_t kClusteringConfigSectionSchemaVersion = 1u;

    struct ClusteringConfig
    {
        KMeansParameters Parameters{};
        ClusteringBackend Backend{ClusteringBackend::CpuReference};
    };

    [[nodiscard]] RunKMeans MakeConfiguredKMeansRequest(
        std::uint32_t stableEntityId,
        KMeansPropertyRefs properties,
        const ClusteringConfig& config);

    [[nodiscard]] std::string SerializeClusteringConfig(
        const ClusteringConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateClusteringConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<ClusteringConfig>
    GetClusteringConfig(const Core::Config::EngineConfig& config);

    void SetClusteringConfig(
        Core::Config::EngineConfig& config,
        const ClusteringConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeClusteringConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
}
