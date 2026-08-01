module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.ClusteringConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.Private.FeatureConfigCodecs;

namespace Extrinsic::Runtime
{
    RunKMeans MakeConfiguredKMeansRequest(
        const std::uint32_t stableEntityId,
        KMeansPropertyRefs properties,
        const ClusteringConfig& config)
    {
        return FeatureConfigDetail::MakeConfiguredKMeansRequestImpl(
            stableEntityId, std::move(properties), config);
    }

    std::string SerializeClusteringConfig(const ClusteringConfig& config)
    {
        return FeatureConfigDetail::SerializeClusteringConfigImpl(config);
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateClusteringConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        return FeatureConfigDetail::ValidateClusteringConfigSectionImpl(
            documentPayloadJson, referencePayloadJson, diagnosticSubject);
    }

    std::optional<ClusteringConfig> GetClusteringConfig(
        const Core::Config::EngineConfig& config)
    {
        return FeatureConfigDetail::GetClusteringConfigImpl(config);
    }

    void SetClusteringConfig(
        Core::Config::EngineConfig& config,
        const ClusteringConfig& value)
    {
        FeatureConfigDetail::SetClusteringConfigImpl(config, value);
    }

    Core::Config::EngineConfigSectionRegistration
    MakeClusteringConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return FeatureConfigDetail::MakeClusteringConfigSectionRegistrationImpl(
            std::move(onChanged));
    }
}
