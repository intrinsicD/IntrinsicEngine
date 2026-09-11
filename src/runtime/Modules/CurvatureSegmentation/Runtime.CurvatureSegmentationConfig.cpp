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
import Geometry.HalfedgeMesh.CurvatureSegmentation;

namespace Extrinsic::Runtime
{
    namespace CurvSeg = Geometry::CurvatureSegmentation;

    [[nodiscard]] CurvSeg::CurvatureSegmentationParams
    MakeCurvatureSegmentationParams(
        const CurvatureSegmentationConfig& config)
    {
        return CurvSeg::CurvatureSegmentationParams{
            .SelectionMode =
                config.SelectionMode ==
                        CurvatureSegmentationSelectionMode::FixedCount
                    ? CurvSeg::ComponentSelectionMode::FixedCount
                    : CurvSeg::ComponentSelectionMode::Automatic,
            .FixedComponentCount = config.FixedComponentCount,
            .AutomaticMinComponents =
                config.AutomaticMinComponents,
            .AutomaticMaxComponents =
                config.AutomaticMaxComponents,
            .AutomaticFitTolerance =
                config.AutomaticFitTolerance,
            .AutomaticComplexityWeight =
                config.AutomaticComplexityWeight,
            .MaxEmIterations = config.MaxEmIterations,
            .EmRelativeTolerance = config.EmRelativeTolerance,
            .CovarianceFloor = config.CovarianceFloor,
            .Seed = config.Seed,
            .SpatialWeight = config.SpatialWeight,
            .FeatureSensitivity = config.FeatureSensitivity,
            .MaxSpatialIterations =
                config.MaxSpatialIterations,
            .MinimumRegionFaces = config.MinimumRegionFaces,
        };
    }

    const char* DebugNameForCurvatureSegmentationMethod(
        const CurvatureSegmentationMethod method) noexcept
    {
        switch (method)
        {
        case CurvatureSegmentationMethod::CurvatureGmm:
            return "Curvature GMM (METHOD-037)";
        case CurvatureSegmentationMethod::FeatureAlignedPatches:
            return "Feature-aligned patches (METHOD-039)";
        case CurvatureSegmentationMethod::FeatureBoundaryCurves:
            return "Feature boundaries (METHOD-040, experimental curves_v1)";
        }
        return "Unknown";
    }

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
        const bool validMethod =
            config.Method == CurvatureSegmentationMethod::CurvatureGmm ||
            config.Method ==
                CurvatureSegmentationMethod::FeatureAlignedPatches ||
            config.Method == CurvatureSegmentationMethod::FeatureBoundaryCurves;
        const bool validMode =
            config.SelectionMode ==
                CurvatureSegmentationSelectionMode::FixedCount ||
            config.SelectionMode ==
                CurvatureSegmentationSelectionMode::Automatic;
        return validMethod && validMode &&
               CurvSeg::IsValidSegmentationParams(
                   MakeCurvatureSegmentationParams(config)) &&
               std::isfinite(config.FeatureBaseRadiusRatio) &&
               config.FeatureBaseRadiusRatio > 0.0 &&
               config.FeatureBaseRadiusRatio <= 1.0 &&
               std::isfinite(config.HardDihedralThresholdDegrees) &&
               config.HardDihedralThresholdDegrees >= 0.0 &&
               config.HardDihedralThresholdDegrees <= 180.0 &&
               std::isfinite(config.PatchComplexityCost) &&
               config.PatchComplexityCost >= 0.0 &&
               config.PatchComplexityCost <= 1.0e12;
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
