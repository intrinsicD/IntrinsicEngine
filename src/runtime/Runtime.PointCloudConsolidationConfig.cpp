module;

#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.PointCloudConsolidationConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.Private.FeatureConfigCodecs;

namespace Extrinsic::Runtime
{
    std::string_view StableToken(
        const PointCloudConsolidationBackend backend) noexcept
    {
        switch (backend)
        {
        case PointCloudConsolidationBackend::None: return "none";
        case PointCloudConsolidationBackend::CpuReference:
            return "cpu_reference";
        case PointCloudConsolidationBackend::VulkanCompute:
            return "gpu_vulkan_compute";
        }
        return {};
    }

    std::string_view StableToken(
        const PointCloudConsolidationStrategy strategy) noexcept
    {
        switch (strategy)
        {
        case PointCloudConsolidationStrategy::Lop: return "lop";
        case PointCloudConsolidationStrategy::Wlop: return "wlop";
        case PointCloudConsolidationStrategy::Clop: return "clop";
        case PointCloudConsolidationStrategy::Ear: return "ear";
        }
        return {};
    }

    std::string_view StableToken(
        const PointCloudConsolidationNormalSource source) noexcept
    {
        switch (source)
        {
        case PointCloudConsolidationNormalSource::AuthoredOrEstimate:
            return "authored_or_estimate";
        case PointCloudConsolidationNormalSource::RequireAuthored:
            return "require_authored";
        }
        return {};
    }

    std::string_view StableToken(
        const PointCloudConsolidationSupportRadiusMode mode) noexcept
    {
        switch (mode)
        {
        case PointCloudConsolidationSupportRadiusMode::Auto: return "auto";
        case PointCloudConsolidationSupportRadiusMode::Manual:
            return "manual";
        }
        return {};
    }

    std::string SerializePointCloudConsolidationConfig(
        const PointCloudConsolidationConfig& config)
    {
        return FeatureConfigDetail::
            SerializePointCloudConsolidationConfigImpl(config);
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidatePointCloudConsolidationConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        return FeatureConfigDetail::
            ValidatePointCloudConsolidationConfigSectionImpl(
                documentPayloadJson,
                referencePayloadJson,
                diagnosticSubject);
    }

    std::optional<PointCloudConsolidationConfig>
    GetPointCloudConsolidationConfig(
        const Core::Config::EngineConfig& config)
    {
        return FeatureConfigDetail::GetPointCloudConsolidationConfigImpl(
            config);
    }

    void SetPointCloudConsolidationConfig(
        Core::Config::EngineConfig& config,
        const PointCloudConsolidationConfig& value)
    {
        FeatureConfigDetail::SetPointCloudConsolidationConfigImpl(
            config, value);
    }

    Core::Config::EngineConfigSectionRegistration
    MakePointCloudConsolidationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return FeatureConfigDetail::
            MakePointCloudConsolidationConfigSectionRegistrationImpl(
                std::move(onChanged));
    }
}
