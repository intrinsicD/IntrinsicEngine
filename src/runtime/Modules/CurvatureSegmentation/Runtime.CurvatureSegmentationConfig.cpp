module;

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Extrinsic.Runtime.CurvatureSegmentationConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.Private.FeatureConfigCodecs;

namespace Extrinsic::Runtime
{
    const char* DebugNameForCurvatureSegmentationSelectionMode(
        const CurvatureSegmentationSelectionMode mode) noexcept
    {
        switch (mode)
        {
        case CurvatureSegmentationSelectionMode::FixedCount:
            return "Fixed count";
        case CurvatureSegmentationSelectionMode::Automatic:
            return "Automatic";
        }
        return "Unknown";
    }

    bool IsValidCurvatureSegmentationConfig(
        const CurvatureSegmentationConfig& config) noexcept
    {
        const bool validMode =
            config.SelectionMode ==
                CurvatureSegmentationSelectionMode::FixedCount ||
            config.SelectionMode ==
                CurvatureSegmentationSelectionMode::Automatic;
        return validMode &&
               config.FixedComponentCount > 0u &&
               config.AutomaticMinComponents > 0u &&
               config.AutomaticMaxComponents >=
                   config.AutomaticMinComponents &&
               std::isfinite(config.AutomaticFitTolerance) &&
               config.AutomaticFitTolerance > 0.0 &&
               std::isfinite(config.AutomaticComplexityWeight) &&
               config.AutomaticComplexityWeight >= 0.0 &&
               config.MaxEmIterations > 0u &&
               std::isfinite(config.EmRelativeTolerance) &&
               config.EmRelativeTolerance >= 0.0 &&
               std::isfinite(config.CovarianceFloor) &&
               config.CovarianceFloor > 0.0 &&
               std::isfinite(config.SpatialWeight) &&
               config.SpatialWeight >= 0.0 &&
               std::isfinite(config.FeatureSensitivity) &&
               config.FeatureSensitivity >= 0.0 &&
               config.MaxSpatialIterations > 0u &&
               config.MinimumRegionFaces > 0u;
    }

    std::string SerializeCurvatureSegmentationConfig(
        const CurvatureSegmentationConfig& config)
    {
        return FeatureConfigDetail::
            SerializeCurvatureSegmentationConfigImpl(config);
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateCurvatureSegmentationConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        return FeatureConfigDetail::
            ValidateCurvatureSegmentationConfigSectionImpl(
                documentPayloadJson,
                referencePayloadJson,
                diagnosticSubject);
    }

    std::optional<CurvatureSegmentationConfig>
    GetCurvatureSegmentationConfig(
        const Core::Config::EngineConfig& config)
    {
        return FeatureConfigDetail::
            GetCurvatureSegmentationConfigImpl(config);
    }

    void SetCurvatureSegmentationConfig(
        Core::Config::EngineConfig& config,
        const CurvatureSegmentationConfig& value)
    {
        FeatureConfigDetail::SetCurvatureSegmentationConfigImpl(
            config, value);
    }

    Core::Config::EngineConfigSectionRegistration
    MakeCurvatureSegmentationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return FeatureConfigDetail::
            MakeCurvatureSegmentationConfigSectionRegistrationImpl(
                std::move(onChanged));
    }
}
