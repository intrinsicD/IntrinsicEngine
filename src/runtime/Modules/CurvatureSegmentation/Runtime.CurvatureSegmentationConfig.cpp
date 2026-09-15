module;

#include <cmath>
#include <array>
#include <cstddef>
#include <string>

module Extrinsic.Runtime.CurvatureSegmentationConfig;

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
        const CurvatureSegmentationConfig defaults;
        const std::array properties{
            &CurvatureSegmentationConfig::Positions,
            &CurvatureSegmentationConfig::Components,
            &CurvatureSegmentationConfig::Regions,
            &CurvatureSegmentationConfig::RegionColors,
            &CurvatureSegmentationConfig::Boundaries,
            &CurvatureSegmentationConfig::BoundaryColors,
            &CurvatureSegmentationConfig::HardFeatures,
            &CurvatureSegmentationConfig::FeatureConfidence,
            &CurvatureSegmentationConfig::BoundaryRoles,
            &CurvatureSegmentationConfig::FeatureColors
        };
        for (std::size_t i = 0; i < properties.size(); ++i)
        {
            const auto& ref = config.*properties[i];
            const auto& expected = defaults.*properties[i];
            if (ref.Domain != expected.Domain || ref.ValueKind != expected.ValueKind ||
                !ref.Name.starts_with(expected.Name.substr(0, 2)) || ref.Name.size() < 3 ||
                ref.Name.find('\0') != std::string::npos || ref.Name.ends_with(":deleted") ||
                ref.Name == "e:v0" || ref.Name == "e:v1" || ref.Name == "f:halfedge") return false;
            for (std::size_t j = 0; j < i; ++j)
                if (ref.Domain == (config.*properties[j]).Domain && ref.Name == (config.*properties[j]).Name) return false;
        }
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
}
