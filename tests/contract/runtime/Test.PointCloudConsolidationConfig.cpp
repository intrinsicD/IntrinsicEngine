#include <gtest/gtest.h>

#include <string>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.PointCloudConsolidationConfig;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;

TEST(PointCloudConsolidationConfig, RoundTripsAndFallsBackPerField)
{
    Runtime::PointCloudConsolidationConfig configured{
        .Backend = Runtime::PointCloudConsolidationBackend::VulkanCompute,
        .Strategy = Runtime::PointCloudConsolidationStrategy::Ear,
        .SupportRadiusMode = Runtime::
            PointCloudConsolidationSupportRadiusMode::Manual,
        .SupportRadius = 0.75,
        .MaxSupportNeighbors = 2'048u,
        .MaxPredictedContributions = 12'345'678u,
        .RepulsionWeight = 0.2,
        .MaxIterations = 12u,
        .ConvergenceTolerance = 1.0e-3,
        .TargetPointCount = 31u,
        .Seed = 91u,
        .WlopAnisotropic = true,
        .NormalSource = Runtime::PointCloudConsolidationNormalSource::
            RequireAuthored,
        .NormalAngleRadians = 0.4,
        .NormalRefinementRounds = 4u,
        .ClopMixtureComponentCount = 9u,
        .ClopMixtureMaxIterations = 73u,
        .ClopMixtureRelativeTolerance = 2.0e-5,
        .ClopCovarianceFloor = 3.0e-5,
        .EarEdgeSensitivity = 7.0,
    };

    CoreConfig::EngineConfig document{};
    Runtime::SetPointCloudConsolidationConfig(document, configured);
    const auto decoded =
        Runtime::GetPointCloudConsolidationConfig(document);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->Backend, configured.Backend);
    EXPECT_EQ(decoded->Strategy, configured.Strategy);
    EXPECT_EQ(decoded->SupportRadiusMode, configured.SupportRadiusMode);
    EXPECT_DOUBLE_EQ(decoded->SupportRadius, configured.SupportRadius);
    EXPECT_EQ(decoded->MaxSupportNeighbors,
              configured.MaxSupportNeighbors);
    EXPECT_EQ(decoded->MaxPredictedContributions,
              configured.MaxPredictedContributions);
    EXPECT_EQ(decoded->TargetPointCount, configured.TargetPointCount);
    EXPECT_EQ(decoded->NormalSource, configured.NormalSource);
    EXPECT_EQ(decoded->ClopMixtureMaxIterations,
              configured.ClopMixtureMaxIterations);
    EXPECT_DOUBLE_EQ(decoded->EarEdgeSensitivity,
                     configured.EarEdgeSensitivity);

    const CoreConfig::EngineConfigSectionValidationResult validation =
        Runtime::ValidatePointCloudConsolidationConfigSection(
            R"({"backend":"unknown","strategy":"clop","support_radius_mode":"unknown","support_radius":0,"max_support_neighbors":0,"max_predicted_contributions":0,"max_iterations":7,"normal_refinement_rounds":9,"ear_edge_sensitivity":6,"unknown_field":true})",
            Runtime::SerializePointCloudConsolidationConfig(configured),
            "app.sections.sandbox.point_cloud_consolidation.payload");
    EXPECT_EQ(validation.State, CoreConfig::EngineConfigState::FallbackApplied);
    EXPECT_FALSE(validation.Diagnostics.empty());

    CoreConfig::EngineConfig canonical{};
    CoreConfig::UpsertEngineConfigSection(
        canonical.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = std::string{
                Runtime::kPointCloudConsolidationConfigSectionName},
            .SchemaId = std::string{
                Runtime::kPointCloudConsolidationConfigSectionSchemaId},
            .SchemaVersion =
                Runtime::kPointCloudConsolidationConfigSectionSchemaVersion,
            .PayloadJson = validation.CanonicalPayloadJson,
        });
    const auto fallback =
        Runtime::GetPointCloudConsolidationConfig(canonical);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->Backend, configured.Backend);
    EXPECT_EQ(fallback->Strategy,
              Runtime::PointCloudConsolidationStrategy::Clop);
    EXPECT_EQ(fallback->SupportRadiusMode, configured.SupportRadiusMode);
    EXPECT_DOUBLE_EQ(fallback->SupportRadius, configured.SupportRadius);
    EXPECT_EQ(fallback->MaxSupportNeighbors,
              configured.MaxSupportNeighbors);
    EXPECT_EQ(fallback->MaxPredictedContributions,
              configured.MaxPredictedContributions);
    EXPECT_EQ(fallback->MaxIterations, 7u);
    EXPECT_EQ(fallback->NormalRefinementRounds, 4u);
    EXPECT_DOUBLE_EQ(fallback->EarEdgeSensitivity, 6.0);

    CoreConfig::EngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(
        Runtime::MakePointCloudConsolidationConfigSectionRegistration()));
    CoreConfig::EngineConfig defaults{};
    CoreConfig::PopulateEngineConfigSectionDefaults(defaults, registry);
    EXPECT_TRUE(Runtime::GetPointCloudConsolidationConfig(defaults).has_value());

    const auto legacyValidation =
        Runtime::ValidatePointCloudConsolidationConfigSection(
            R"({"strategy":"lop","support_radius":0.5,"repulsion_weight":0.1,"max_iterations":5,"convergence_tolerance":0.001,"target_point_count":0,"seed":7,"wlop_anisotropic":false,"normal_source":"authored_or_estimate","normal_angle_radians":0.2,"normal_refinement_rounds":2,"clop_mixture_component_count":4,"clop_mixture_max_iterations":10,"clop_mixture_relative_tolerance":0.0001,"clop_covariance_floor":0.000001,"ear_edge_sensitivity":5})",
            Runtime::SerializePointCloudConsolidationConfig(
                Runtime::PointCloudConsolidationConfig{}),
            "legacy.point_cloud_consolidation");
    EXPECT_EQ(legacyValidation.State, CoreConfig::EngineConfigState::Valid);
    CoreConfig::EngineConfig migrated{};
    CoreConfig::UpsertEngineConfigSection(
        migrated.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = std::string{
                Runtime::kPointCloudConsolidationConfigSectionName},
            .SchemaId = std::string{
                Runtime::kPointCloudConsolidationConfigSectionSchemaId},
            .SchemaVersion =
                Runtime::kPointCloudConsolidationConfigSectionSchemaVersion,
            .PayloadJson = legacyValidation.CanonicalPayloadJson,
        });
    const auto legacy = Runtime::GetPointCloudConsolidationConfig(migrated);
    ASSERT_TRUE(legacy.has_value());
    EXPECT_EQ(legacy->Backend,
              Runtime::PointCloudConsolidationBackend::CpuReference);
    EXPECT_EQ(legacy->SupportRadiusMode,
              Runtime::PointCloudConsolidationSupportRadiusMode::Auto);
    EXPECT_DOUBLE_EQ(legacy->SupportRadius, 0.5);
    EXPECT_EQ(legacy->MaxSupportNeighbors, 4'096u);
    EXPECT_EQ(legacy->MaxPredictedContributions, 100'000'000u);

    EXPECT_EQ(Runtime::StableToken(
                  Runtime::PointCloudConsolidationBackend::None),
              "none");
    EXPECT_EQ(Runtime::StableToken(
                  Runtime::PointCloudConsolidationBackend::CpuReference),
              "cpu_reference");
    EXPECT_EQ(Runtime::StableToken(
                  Runtime::PointCloudConsolidationBackend::VulkanCompute),
              "gpu_vulkan_compute");
}
