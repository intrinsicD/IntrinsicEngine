module;

#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.ParameterizationConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.Private.FeatureConfigCodecs;

namespace Extrinsic::Runtime
{
    std::string SerializeParameterizationConfig(
        const ParameterizationConfig& config)
    {
        return FeatureConfigDetail::SerializeParameterizationConfigImpl(config);
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateParameterizationConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        return FeatureConfigDetail::ValidateParameterizationConfigSectionImpl(
            documentPayloadJson, referencePayloadJson, diagnosticSubject);
    }

    std::optional<ParameterizationConfig> GetParameterizationConfig(
        const Core::Config::EngineConfig& config)
    {
        return FeatureConfigDetail::GetParameterizationConfigImpl(config);
    }

    void SetParameterizationConfig(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value)
    {
        FeatureConfigDetail::SetParameterizationConfigImpl(config, value);
    }

    Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return FeatureConfigDetail::
            MakeParameterizationConfigSectionRegistrationImpl(
                std::move(onChanged));
    }
}
