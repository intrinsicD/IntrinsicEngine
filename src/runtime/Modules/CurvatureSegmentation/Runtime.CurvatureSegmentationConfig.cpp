module;

#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

module Extrinsic.Runtime.CurvatureSegmentationConfig;

import Geometry.HalfedgeMesh.CurvatureSegmentation;

#include "Config/internal/Runtime.CurvatureSegmentationParams.hpp"

namespace Extrinsic::Runtime
{
    namespace CurvSeg = Geometry::CurvatureSegmentation;

    extern "C++"
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
            return "Property GMM (curvature or bound features)";
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

    bool IsSegmentationFeatureBinding(const GeometryPropertyRef& ref) noexcept
    {
        const auto width = GeometryPropertyComponentCount(ref.ValueKind);
        return (ref.Domain == GeometryElementDomain::MeshVertex ||
                ref.Domain == GeometryElementDomain::MeshFace) &&
               width >= 1u && width <= 3u && !ref.Name.empty() &&
               ref.Name.find('\0') == std::string::npos;
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
                ref.Name.empty() ||
                ref.Name.find('\0') != std::string::npos) return false;
            if (IsTopologyProperty(ref.Domain, ref.Name)) return false;
            for (std::size_t j = 0; j < i; ++j)
                if (ref.Domain == (config.*properties[j]).Domain && ref.Name == (config.*properties[j]).Name) return false;
        }
        std::uint32_t featureWidth = 0u;
        for (const auto& feature : config.Features)
        {
            if (!IsSegmentationFeatureBinding(feature)) return false;
            featureWidth += GeometryPropertyComponentCount(feature.ValueKind);
            if (featureWidth > 3u) return false;
            for (const auto property : properties)
            {
                if (property == &CurvatureSegmentationConfig::Positions) continue;
                const auto& output = config.*property;
                if (feature.Domain == output.Domain && feature.Name == output.Name) return false;
            }
        }
        if (!config.Features.empty() && config.Method != CurvatureSegmentationMethod::CurvatureGmm)
            return false;
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
