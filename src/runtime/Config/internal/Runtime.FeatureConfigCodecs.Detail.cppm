module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.Private.FeatureConfigCodecs;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;

// Private codec implementation seam. The feature modules above own every
// public schema and function declaration; this module only lets their
// implementation units share the JSON parser/validation machinery.
export namespace Extrinsic::Runtime::FeatureConfigDetail
{
    using Runtime::ClusteringConfig;
    using Runtime::CurvatureSegmentationConfig;
    using Runtime::CurvatureSegmentationSelectionMode;
    using Runtime::KMeansPropertyRefs;
    using Runtime::ParameterizationBffBoundaryMode;
    using Runtime::ParameterizationBoundaryPolicy;
    using Runtime::ParameterizationConfig;
    using Runtime::ParameterizationStrategyKind;
    using Runtime::ParameterizationUvBackgroundMode;
    using Runtime::ParameterizationUvConfig;
    using Runtime::ParameterizationUvRenderMode;
    using Runtime::PointCloudConsolidationConfig;
    using Runtime::PointCloudConsolidationBackend;
    using Runtime::PointCloudConsolidationNormalSource;
    using Runtime::PointCloudConsolidationStrategy;
    using Runtime::ProgressivePoissonPlaygroundBackend;
    using Runtime::ProgressivePoissonPlaygroundChannel;
    using Runtime::ProgressivePoissonPlaygroundConfig;
    using Runtime::RunKMeans;

    using Runtime::kClusteringConfigSectionName;
    using Runtime::kClusteringConfigSectionSchemaId;
    using Runtime::kClusteringConfigSectionSchemaVersion;
    using Runtime::kCurvatureSegmentationConfigSectionName;
    using Runtime::kCurvatureSegmentationConfigSectionSchemaId;
    using Runtime::kCurvatureSegmentationConfigSectionSchemaVersion;
    using Runtime::kParameterizationConfigSectionName;
    using Runtime::kParameterizationConfigSectionSchemaId;
    using Runtime::kParameterizationConfigSectionSchemaVersion;
    using Runtime::kPointCloudConsolidationConfigSectionName;
    using Runtime::kPointCloudConsolidationConfigSectionSchemaId;
    using Runtime::kPointCloudConsolidationConfigSectionSchemaVersion;
    using Runtime::kProgressivePoissonConfigSectionName;
    using Runtime::kProgressivePoissonConfigSectionSchemaId;
    using Runtime::kProgressivePoissonConfigSectionSchemaVersion;

    [[nodiscard]] RunKMeans MakeConfiguredKMeansRequestImpl(
        std::uint32_t stableEntityId,
        KMeansPropertyRefs properties,
        const ClusteringConfig& config);

    [[nodiscard]] std::string SerializeClusteringConfigImpl(
        const ClusteringConfig& config);
    [[nodiscard]] std::string SerializeCurvatureSegmentationConfigImpl(
        const CurvatureSegmentationConfig& config);
    [[nodiscard]] std::string SerializeProgressivePoissonPlaygroundConfigImpl(
        const ProgressivePoissonPlaygroundConfig& config);
    [[nodiscard]] std::string SerializeParameterizationConfigImpl(
        const ParameterizationConfig& config);
    [[nodiscard]] std::string SerializePointCloudConsolidationConfigImpl(
        const PointCloudConsolidationConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateClusteringConfigSectionImpl(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateCurvatureSegmentationConfigSectionImpl(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateProgressivePoissonConfigSectionImpl(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateParameterizationConfigSectionImpl(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidatePointCloudConsolidationConfigSectionImpl(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<ClusteringConfig>
    GetClusteringConfigImpl(const Core::Config::EngineConfig& config);
    void SetClusteringConfigImpl(
        Core::Config::EngineConfig& config,
        const ClusteringConfig& value);

    [[nodiscard]] std::optional<CurvatureSegmentationConfig>
    GetCurvatureSegmentationConfigImpl(
        const Core::Config::EngineConfig& config);
    void SetCurvatureSegmentationConfigImpl(
        Core::Config::EngineConfig& config,
        const CurvatureSegmentationConfig& value);

    [[nodiscard]] std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfigImpl(
        const Core::Config::EngineConfig& config);
    void SetProgressivePoissonPlaygroundConfigImpl(
        Core::Config::EngineConfig& config,
        const ProgressivePoissonPlaygroundConfig& value);

    [[nodiscard]] std::optional<ParameterizationConfig>
    GetParameterizationConfigImpl(const Core::Config::EngineConfig& config);
    void SetParameterizationConfigImpl(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value);

    [[nodiscard]] std::optional<PointCloudConsolidationConfig>
    GetPointCloudConsolidationConfigImpl(
        const Core::Config::EngineConfig& config);
    void SetPointCloudConsolidationConfigImpl(
        Core::Config::EngineConfig& config,
        const PointCloudConsolidationConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeClusteringConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeCurvatureSegmentationConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakePointCloudConsolidationConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
}
