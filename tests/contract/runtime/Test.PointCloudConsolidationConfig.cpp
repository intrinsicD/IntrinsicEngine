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
        .GpuPreviewInterval = 3u,
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
    EXPECT_EQ(decoded->GpuPreviewInterval, 3u);
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
            R"({"gpu_preview_interval":0,"backend":"unknown","strategy":"clop","support_radius_mode":"unknown","support_radius":0,"max_support_neighbors":0,"max_predicted_contributions":0,"max_iterations":7,"normal_refinement_rounds":9,"ear_edge_sensitivity":6,"unknown_field":true})",
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
    EXPECT_EQ(fallback->GpuPreviewInterval, 3u);
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
    EXPECT_EQ(fallback->NormalRefinementRounds, 9u); // CLOP does not consume normal rounds.
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

TEST(PointCloudConsolidationConfig, CachedLopBackendRoundTrips)
{
    Extrinsic::Runtime::PointCloudConsolidationConfig value;
    value.Backend=Extrinsic::Runtime::PointCloudConsolidationBackend::CpuLBVH;
    value.Strategy=Extrinsic::Runtime::PointCloudConsolidationStrategy::Lop;
    const auto json=Extrinsic::Runtime::SerializePointCloudConsolidationConfig(value);
    EXPECT_NE(json.find("cpu_lbvh"),std::string::npos);
    EXPECT_TRUE(Extrinsic::Runtime::ValidatePointCloudConsolidationConfigSection(json,{},"test").Usable());
}

TEST(PointCloudConsolidationConfig, VulkanLbvhControlsRoundTrip)
{
    namespace R=Extrinsic::Runtime;
    for (const auto strategy : {R::PointCloudConsolidationStrategy::Lop,
             R::PointCloudConsolidationStrategy::Wlop,
             R::PointCloudConsolidationStrategy::Clop,
             R::PointCloudConsolidationStrategy::Ear})
    {
        Extrinsic::Core::Config::EngineConfig engine;
        R::PointCloudConsolidationConfig value;
        value.Backend = R::PointCloudConsolidationBackend::VulkanLBVH;
        value.Strategy = strategy;
        value.GpuQueryBatchSize = 17;
        value.GpuRadiusCapacity = 113;
        R::SetPointCloudConsolidationConfig(engine, value);
        const auto read = R::GetPointCloudConsolidationConfig(engine);
        ASSERT_TRUE(read);
        EXPECT_EQ(read->Backend, value.Backend);
        EXPECT_EQ(read->Strategy, strategy);
        EXPECT_EQ(read->GpuQueryBatchSize, 17u);
        EXPECT_EQ(read->GpuRadiusCapacity, 113u);
        EXPECT_TRUE(R::ValidatePointCloudConsolidationConfigSection(
            R::SerializePointCloudConsolidationConfig(value), {}, "test").Usable());
    }
}

// RUNTIME-289: the initial samples' method is part of the section.
TEST(PointCloudConsolidationConfig, InitialSamplingRoundTripsAndInvalidBlocksKeepTheDefault)
{
    namespace R = Extrinsic::Runtime;
    Extrinsic::Core::Config::EngineConfig engine;
    R::PointCloudConsolidationConfig value;
    EXPECT_EQ(value.InitialSampling.Method, R::PointSamplingMethod::Random) << "legacy seeded subsample by default";
    value.InitialSampling.Method = R::PointSamplingMethod::FlatGreedy;
    value.InitialSampling.Beta = 1.3;
    R::SetPointCloudConsolidationConfig(engine, value);
    const auto read = R::GetPointCloudConsolidationConfig(engine);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->InitialSampling.Method, R::PointSamplingMethod::FlatGreedy);
    EXPECT_EQ(read->InitialSampling.Beta, 1.3);
    const auto result = R::ValidatePointCloudConsolidationConfigSection(
        R"({"initial_method": 3, "initial_eta": 1.0, "initial_candidate_cap": 8})", {}, "test");
    EXPECT_FALSE(result.Diagnostics.empty()) << "eta 1 with a cap of 8 is rejected with a warning";
    const auto bad = R::ValidatePointCloudConsolidationConfigSection(R"({"initial_beta": 0.5})", {}, "test");
    EXPECT_FALSE(bad.Diagnostics.empty());
}

TEST(PointCloudConsolidationConfig, ShortLopPreservesUnusedNormalSettings)
{
    Runtime::PointCloudConsolidationConfig config{};
    config.Strategy = Runtime::PointCloudConsolidationStrategy::Lop;
    config.MaxIterations = 1u;
    config.NormalRefinementRounds = 3u;
    const auto validation = Runtime::ValidatePointCloudConsolidationConfigSection(
        Runtime::SerializePointCloudConsolidationConfig(config),
        Runtime::SerializePointCloudConsolidationConfig(Runtime::PointCloudConsolidationConfig{}), "test");
    EXPECT_EQ(validation.State, CoreConfig::EngineConfigState::Valid);
    CoreConfig::EngineConfig document{};
    Runtime::SetPointCloudConsolidationConfig(document, config);
    const auto decoded = Runtime::GetPointCloudConsolidationConfig(document);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->MaxIterations, 1u);
    EXPECT_EQ(decoded->NormalRefinementRounds, 3u);
}
