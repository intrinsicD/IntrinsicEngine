module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.CurvatureSegmentationConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view
        kCurvatureSegmentationConfigSectionName =
            "sandbox.curvature_segmentation";
    inline constexpr std::string_view
        kCurvatureSegmentationConfigSectionSchemaId =
            "intrinsic.runtime.sandbox.curvature_segmentation";
    inline constexpr std::uint32_t
        kCurvatureSegmentationConfigSectionSchemaVersion = 1u;

    enum class CurvatureSegmentationSelectionMode : std::uint8_t
    {
        FixedCount = 0,
        Automatic,
    };

    [[nodiscard]] const char* DebugNameForCurvatureSegmentationSelectionMode(
        CurvatureSegmentationSelectionMode mode) noexcept;

    struct CurvatureSegmentationConfig
    {
        CurvatureSegmentationSelectionMode SelectionMode{
            CurvatureSegmentationSelectionMode::Automatic};
        std::uint32_t FixedComponentCount{6u};
        std::uint32_t AutomaticMinComponents{1u};
        std::uint32_t AutomaticMaxComponents{12u};
        double AutomaticFitTolerance{0.35};
        double AutomaticComplexityWeight{1.0};

        std::uint32_t MaxEmIterations{100u};
        double EmRelativeTolerance{1.0e-6};
        double CovarianceFloor{1.0e-5};
        std::uint32_t Seed{42u};

        double SpatialWeight{0.75};
        double FeatureSensitivity{4.0};
        std::uint32_t MaxSpatialIterations{24u};
        std::uint32_t MinimumRegionFaces{2u};
    };

    [[nodiscard]] bool IsValidCurvatureSegmentationConfig(
        const CurvatureSegmentationConfig& config) noexcept;

    [[nodiscard]] std::string SerializeCurvatureSegmentationConfig(
        const CurvatureSegmentationConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateCurvatureSegmentationConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<CurvatureSegmentationConfig>
    GetCurvatureSegmentationConfig(
        const Core::Config::EngineConfig& config);

    void SetCurvatureSegmentationConfig(
        Core::Config::EngineConfig& config,
        const CurvatureSegmentationConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeCurvatureSegmentationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
}
