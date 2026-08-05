module;

#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.ProgressivePoissonConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.Private.FeatureConfigCodecs;

namespace Extrinsic::Runtime
{
    std::string SerializeProgressivePoissonPlaygroundConfig(
        const ProgressivePoissonPlaygroundConfig& config)
    {
        return FeatureConfigDetail::
            SerializeProgressivePoissonPlaygroundConfigImpl(config);
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateProgressivePoissonConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        return FeatureConfigDetail::ValidateProgressivePoissonConfigSectionImpl(
            documentPayloadJson, referencePayloadJson, diagnosticSubject);
    }

    std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfig(
        const Core::Config::EngineConfig& config)
    {
        return FeatureConfigDetail::GetProgressivePoissonPlaygroundConfigImpl(
            config);
    }

    void SetProgressivePoissonPlaygroundConfig(
        Core::Config::EngineConfig& config,
        const ProgressivePoissonPlaygroundConfig& value)
    {
        FeatureConfigDetail::SetProgressivePoissonPlaygroundConfigImpl(
            config, value);
    }

    Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return FeatureConfigDetail::
            MakeProgressivePoissonConfigSectionRegistrationImpl(
                std::move(onChanged));
    }
}
