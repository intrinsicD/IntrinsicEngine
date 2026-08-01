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
        .Strategy = Runtime::PointCloudConsolidationStrategy::Ear,
        .SupportRadius = 0.75,
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
    EXPECT_EQ(decoded->Strategy, configured.Strategy);
    EXPECT_DOUBLE_EQ(decoded->SupportRadius, configured.SupportRadius);
    EXPECT_EQ(decoded->TargetPointCount, configured.TargetPointCount);
    EXPECT_EQ(decoded->NormalSource, configured.NormalSource);
    EXPECT_EQ(decoded->ClopMixtureMaxIterations,
              configured.ClopMixtureMaxIterations);
    EXPECT_DOUBLE_EQ(decoded->EarEdgeSensitivity,
                     configured.EarEdgeSensitivity);

    const CoreConfig::EngineConfigSectionValidationResult validation =
        Runtime::ValidatePointCloudConsolidationConfigSection(
            R"({"strategy":"clop","support_radius":0,"max_iterations":7,"normal_refinement_rounds":9,"ear_edge_sensitivity":6,"unknown_field":true})",
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
    EXPECT_EQ(fallback->Strategy,
              Runtime::PointCloudConsolidationStrategy::Clop);
    EXPECT_DOUBLE_EQ(fallback->SupportRadius, configured.SupportRadius);
    EXPECT_EQ(fallback->MaxIterations, 7u);
    EXPECT_EQ(fallback->NormalRefinementRounds, 4u);
    EXPECT_DOUBLE_EQ(fallback->EarEdgeSensitivity, 6.0);

    CoreConfig::EngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(
        Runtime::MakePointCloudConsolidationConfigSectionRegistration()));
    CoreConfig::EngineConfig defaults{};
    CoreConfig::PopulateEngineConfigSectionDefaults(defaults, registry);
    EXPECT_TRUE(Runtime::GetPointCloudConsolidationConfig(defaults).has_value());
}
