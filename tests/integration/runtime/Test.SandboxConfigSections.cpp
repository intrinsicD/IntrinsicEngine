#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.MeshCurvatureConfig;
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PhysicsModule;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Sandbox.ConfigSections;
import Extrinsic.Runtime.BilateralFilterConfig;
import Extrinsic.Runtime.KernelDensityConfig;
import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.OutlierAnalysisConfig;
import Extrinsic.Runtime.KeypointAnalysisConfig;
import Extrinsic.Runtime.DescriptorAnalysisConfig;
import Extrinsic.Runtime.DensityWeightConfig;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;
namespace Sandbox = Extrinsic::Sandbox;

namespace
{
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            engine.RequestExit();
        }
        void Shutdown() override {}
    };

    class ScopedConfigFile final
    {
    public:
        explicit ScopedConfigFile(std::string document)
            : m_Path{
                  std::filesystem::temp_directory_path() /
                  "intrinsic_sandbox_config_sections.json"}
        {
            std::ofstream out{m_Path, std::ios::binary | std::ios::trunc};
            EXPECT_TRUE(out.is_open());
            out << document;
        }

        ~ScopedConfigFile()
        {
            std::error_code error{};
            std::filesystem::remove(m_Path, error);
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept
        {
            return m_Path;
        }

    private:
        std::filesystem::path m_Path{};
    };

    class ConfigControlHarness final
    {
    public:
        ConfigControlHarness()
        {
            auto registry = Sandbox::CreateSandboxConfigSectionRegistry();
            CoreConfig::EngineConfig config =
                Runtime::CreateReferenceEngineConfig(registry);
            config.Simulation.WorkerThreadCount = 1u;
            config.ReferenceScene.Enabled = false;
            config.Camera.Enabled = false;
            config.Window.Backend = CoreConfig::WindowBackend::Null;
            config.Render.EnablePromotedVulkanDevice = false;
            config.Render.DefaultRecipeConfigPath.clear();
            m_Engine =
                std::make_unique<Intrinsic::Tests::RuntimeTestKernel>(
                    std::move(config),
                    std::make_unique<OneFrameApplication>());
            m_Engine->EmplaceModule<Runtime::EngineConfigControl>(
                std::move(registry));
            m_Engine->Initialize();
            m_Control =
                m_Engine->Services().Find<Runtime::EngineConfigControl>();
            EXPECT_NE(m_Control, nullptr);
        }

        ~ConfigControlHarness()
        {
            if (m_Engine != nullptr)
                m_Engine->Shutdown();
        }

        [[nodiscard]] Runtime::EngineConfigControl& Control() const
        {
            return *m_Control;
        }

    private:
        std::unique_ptr<Intrinsic::Tests::RuntimeTestKernel> m_Engine{};
        Runtime::EngineConfigControl* m_Control{};
    };
}

TEST(SandboxConfigSections, BootAndLiveApplyUseTheAppOwnedRegistryThroughNullRun)
{
    std::uint32_t clusteringChanges = 0u;
    std::uint32_t curvatureSegmentationChanges = 0u;
    std::uint32_t progressivePoissonChanges = 0u;
    std::uint32_t parameterizationChanges = 0u;
    std::uint32_t pointCloudConsolidationChanges = 0u;
    std::uint32_t physicsChanges = 0u;
    Runtime::PhysicsModuleConfig lastPhysicsConfig{};
    auto configControl = std::make_unique<Runtime::EngineConfigControl>(
        Sandbox::CreateSandboxConfigSectionRegistry(
            Sandbox::SandboxConfigSectionCallbacks{
                .Clustering =
                    [&](const auto&, const auto&)
                    {
                        ++clusteringChanges;
                    },
                .CurvatureSegmentation =
                    [&](const auto&, const auto&)
                    {
                        ++curvatureSegmentationChanges;
                    },
                .ProgressivePoisson =
                    [&](const auto&, const auto&)
                    {
                        ++progressivePoissonChanges;
                    },
                .Parameterization =
                    [&](const auto&, const auto&)
                    {
                        ++parameterizationChanges;
                    },
                .PointCloudConsolidation =
                    [&](const auto&, const auto&)
                    {
                        ++pointCloudConsolidationChanges;
                    },
                .Physics =
                    [&](const Runtime::PhysicsModuleConfig& value)
                    {
                        ++physicsChanges;
                        lastPhysicsConfig = value;
                    },
            }));

    CoreConfig::EngineConfig fileConfig =
        Runtime::CreateReferenceEngineConfig(
            configControl->SectionRegistry());
    fileConfig.Simulation.WorkerThreadCount = 1u;
    fileConfig.Window.Backend = CoreConfig::WindowBackend::Null;
    fileConfig.Render.EnablePromotedVulkanDevice = false;
    fileConfig.Render.DefaultRecipeConfigPath.clear();
    fileConfig.ReferenceScene.Enabled = false;
    fileConfig.Camera.Enabled = false;

    Runtime::ClusteringConfig clustering{};
    clustering.Parameters.ClusterCount = 13u;
    clustering.Parameters.MaxIterations = 77u;
    clustering.Parameters.Seed = 0xf1234567u;
    clustering.Parameters.Initialization =
        Runtime::KMeansInitialization::Random;
    clustering.Backend = Runtime::ClusteringBackend::VulkanCompute;
    Runtime::SetClusteringConfig(fileConfig, clustering);

    Runtime::CurvatureSegmentationConfig curvatureSegmentation{};
    curvatureSegmentation.SelectionMode =
        Runtime::CurvatureSegmentationSelectionMode::FixedCount;
    curvatureSegmentation.FixedComponentCount = 4u;
    curvatureSegmentation.SpatialWeight = 1.25;
    curvatureSegmentation.FeatureSensitivity = 6.5;
    Runtime::SetCurvatureSegmentationConfig(
        fileConfig,
        curvatureSegmentation);

    Runtime::ProgressivePoissonPlaygroundConfig progressivePoisson{};
    progressivePoisson.Dimension = 2u;
    progressivePoisson.GridWidth = 7u;
    Runtime::SetProgressivePoissonPlaygroundConfig(
        fileConfig,
        progressivePoisson);

    Runtime::ParameterizationConfig parameterization{};
    parameterization.View.BackgroundMode =
        Runtime::ParameterizationUvBackgroundMode::Checker;
    Runtime::SetParameterizationConfig(fileConfig, parameterization);

    Runtime::PointCloudConsolidationConfig pointCloudConsolidation{};
    pointCloudConsolidation.Strategy =
        Runtime::PointCloudConsolidationStrategy::Ear;
    pointCloudConsolidation.SupportRadiusMode = Runtime::
        PointCloudConsolidationSupportRadiusMode::Manual;
    pointCloudConsolidation.SupportRadius = 0.25;
    pointCloudConsolidation.MaxSupportNeighbors = 1'024u;
    pointCloudConsolidation.MaxPredictedContributions = 9'876'543u;
    pointCloudConsolidation.TargetPointCount = 17u;
    pointCloudConsolidation.EarEdgeSensitivity = 7.5;
    Runtime::SetPointCloudConsolidationConfig(
        fileConfig,
        pointCloudConsolidation);

    Runtime::PhysicsModuleConfig physics{};
    physics.Enabled = true;
    physics.FixedDeltaSeconds = 0.02f;
    physics.MaxAccumulatedSeconds = 0.2f;
    physics.MaxStepsPerFrame = 3u;
    physics.Gravity = glm::vec3{0.0f, -4.0f, 0.0f};
    Runtime::SetPhysicsModuleConfig(fileConfig, physics);

    const ScopedConfigFile file{CoreConfig::SerializeEngineConfig(fileConfig)};
    const std::string filePath = file.Path().string();
    const std::array<std::string_view, 3u> args{
        "IntrinsicEditorIntegrationTests",
        "--engine-config",
        filePath,
    };
    Runtime::EngineConfigBootResult boot =
        Runtime::ResolveEngineConfigForBoot(
            args,
            configControl->SectionRegistry());
    ASSERT_TRUE(boot.LoadedFile);
    ASSERT_FALSE(boot.UsedReferenceFallback);

    const auto bootClustering =
        Runtime::GetClusteringConfig(boot.Config);
    ASSERT_TRUE(bootClustering.has_value());
    EXPECT_EQ(bootClustering->Parameters.ClusterCount, 13u);
    EXPECT_EQ(bootClustering->Parameters.MaxIterations, 77u);
    EXPECT_EQ(bootClustering->Parameters.Seed, 0xf1234567u);
    EXPECT_EQ(
        bootClustering->Parameters.Initialization,
        Runtime::KMeansInitialization::Random);
    EXPECT_EQ(
        bootClustering->Backend,
        Runtime::ClusteringBackend::VulkanCompute);
    const auto bootCurvatureSegmentation =
        Runtime::GetCurvatureSegmentationConfig(boot.Config);
    ASSERT_TRUE(bootCurvatureSegmentation.has_value());
    EXPECT_EQ(
        bootCurvatureSegmentation->SelectionMode,
        Runtime::CurvatureSegmentationSelectionMode::FixedCount);
    EXPECT_EQ(bootCurvatureSegmentation->FixedComponentCount, 4u);
    EXPECT_DOUBLE_EQ(bootCurvatureSegmentation->SpatialWeight, 1.25);
    EXPECT_DOUBLE_EQ(
        bootCurvatureSegmentation->FeatureSensitivity,
        6.5);
    const auto bootProgressivePoisson =
        Runtime::GetProgressivePoissonPlaygroundConfig(boot.Config);
    ASSERT_TRUE(bootProgressivePoisson.has_value());
    EXPECT_EQ(bootProgressivePoisson->Dimension, 2u);
    EXPECT_EQ(bootProgressivePoisson->GridWidth, 7u);
    const auto bootParameterization =
        Runtime::GetParameterizationConfig(boot.Config);
    ASSERT_TRUE(bootParameterization.has_value());
    EXPECT_EQ(
        bootParameterization->View.BackgroundMode,
        Runtime::ParameterizationUvBackgroundMode::Checker);
    const auto bootPointCloudConsolidation =
        Runtime::GetPointCloudConsolidationConfig(boot.Config);
    ASSERT_TRUE(bootPointCloudConsolidation.has_value());
    EXPECT_EQ(
        bootPointCloudConsolidation->Strategy,
        Runtime::PointCloudConsolidationStrategy::Ear);
    EXPECT_EQ(
        bootPointCloudConsolidation->SupportRadiusMode,
        Runtime::PointCloudConsolidationSupportRadiusMode::Manual);
    EXPECT_DOUBLE_EQ(bootPointCloudConsolidation->SupportRadius, 0.25);
    EXPECT_EQ(bootPointCloudConsolidation->MaxSupportNeighbors, 1'024u);
    EXPECT_EQ(
        bootPointCloudConsolidation->MaxPredictedContributions,
        9'876'543u);
    EXPECT_EQ(bootPointCloudConsolidation->TargetPointCount, 17u);
    EXPECT_DOUBLE_EQ(
        bootPointCloudConsolidation->EarEdgeSensitivity,
        7.5);
    const auto bootPhysics =
        Runtime::GetPhysicsModuleConfig(boot.Config);
    ASSERT_TRUE(bootPhysics.has_value());
    EXPECT_TRUE(bootPhysics->Enabled);
    EXPECT_FLOAT_EQ(bootPhysics->FixedDeltaSeconds, 0.02f);
    EXPECT_FLOAT_EQ(bootPhysics->MaxAccumulatedSeconds, 0.2f);
    EXPECT_EQ(bootPhysics->MaxStepsPerFrame, 3u);
    EXPECT_FLOAT_EQ(bootPhysics->Gravity.y, -4.0f);
    EXPECT_EQ(clusteringChanges, 0u);
    EXPECT_EQ(curvatureSegmentationChanges, 0u);
    EXPECT_EQ(progressivePoissonChanges, 0u);
    EXPECT_EQ(parameterizationChanges, 0u);
    EXPECT_EQ(pointCloudConsolidationChanges, 0u);
    EXPECT_EQ(physicsChanges, 0u);

    Runtime::EngineConfigControl* const expectedConfigControl =
        configControl.get();
    Intrinsic::Tests::RuntimeTestKernel engine{std::move(boot.Config),
                                               std::make_unique<OneFrameApplication>()};
    engine.AddModule(std::move(configControl));
    engine.Initialize();
    Runtime::EngineConfigControl* const control =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_EQ(control, expectedConfigControl);

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    auto liveClustering = Runtime::GetClusteringConfig(candidate);
    ASSERT_TRUE(liveClustering.has_value());
    liveClustering->Parameters.ClusterCount = 5u;
    liveClustering->Parameters.MaxIterations = 19u;
    liveClustering->Parameters.Seed = 0xffffffffu;
    liveClustering->Parameters.Initialization =
        Runtime::KMeansInitialization::Hierarchical;
    liveClustering->Backend = Runtime::ClusteringBackend::CpuReference;
    Runtime::SetClusteringConfig(candidate, *liveClustering);

    auto liveCurvatureSegmentation =
        Runtime::GetCurvatureSegmentationConfig(candidate);
    ASSERT_TRUE(liveCurvatureSegmentation.has_value());
    liveCurvatureSegmentation->SelectionMode =
        Runtime::CurvatureSegmentationSelectionMode::Automatic;
    liveCurvatureSegmentation->AutomaticMinComponents = 2u;
    liveCurvatureSegmentation->AutomaticMaxComponents = 7u;
    liveCurvatureSegmentation->AutomaticFitTolerance = 0.21;
    liveCurvatureSegmentation->Seed = 0x2468ace0u;
    Runtime::SetCurvatureSegmentationConfig(
        candidate,
        *liveCurvatureSegmentation);

    auto liveParameterization =
        Runtime::GetParameterizationConfig(candidate);
    ASSERT_TRUE(liveParameterization.has_value());
    liveParameterization->View.ShowDistortionHeatmap = true;
    Runtime::SetParameterizationConfig(candidate, *liveParameterization);

    auto livePointCloudConsolidation =
        Runtime::GetPointCloudConsolidationConfig(candidate);
    ASSERT_TRUE(livePointCloudConsolidation.has_value());
    livePointCloudConsolidation->Strategy =
        Runtime::PointCloudConsolidationStrategy::Wlop;
    livePointCloudConsolidation->MaxIterations = 31u;
    livePointCloudConsolidation->Seed = 0xfedcba98u;
    Runtime::SetPointCloudConsolidationConfig(
        candidate,
        *livePointCloudConsolidation);

    auto livePhysics = Runtime::GetPhysicsModuleConfig(candidate);
    ASSERT_TRUE(livePhysics.has_value());
    livePhysics->MaxStepsPerFrame = 5u;
    livePhysics->Gravity = glm::vec3{0.0f, -6.0f, 0.0f};
    Runtime::SetPhysicsModuleConfig(candidate, *livePhysics);

    const CoreConfig::EngineConfigLoadResult preview =
        control->PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "sandbox-config-sections-live.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));
    const Runtime::RuntimeEngineConfigApplyResult apply =
        control->ApplyEngineConfigHotSubset(
            preview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(apply.Succeeded());
    EXPECT_TRUE(apply.SectionChanged(
        Runtime::kClusteringConfigSectionName));
    EXPECT_TRUE(apply.SectionChanged(
        Runtime::kCurvatureSegmentationConfigSectionName));
    EXPECT_TRUE(apply.SectionChanged(
        Runtime::kParameterizationConfigSectionName));
    EXPECT_TRUE(apply.SectionChanged(
        Runtime::kPointCloudConsolidationConfigSectionName));
    EXPECT_FALSE(apply.SectionChanged(
        Runtime::kProgressivePoissonConfigSectionName));
    EXPECT_TRUE(apply.SectionChanged(
        Runtime::kPhysicsModuleConfigSectionName));
    EXPECT_EQ(clusteringChanges, 1u);
    EXPECT_EQ(curvatureSegmentationChanges, 1u);
    EXPECT_EQ(progressivePoissonChanges, 0u);
    EXPECT_EQ(parameterizationChanges, 1u);
    EXPECT_EQ(pointCloudConsolidationChanges, 1u);
    EXPECT_EQ(physicsChanges, 1u);
    EXPECT_EQ(lastPhysicsConfig.MaxStepsPerFrame, 5u);
    EXPECT_FLOAT_EQ(lastPhysicsConfig.Gravity.y, -6.0f);

    const auto activeParameterization =
        Runtime::GetParameterizationConfig(engine.GetEngineConfig());
    ASSERT_TRUE(activeParameterization.has_value());
    EXPECT_TRUE(activeParameterization->View.ShowDistortionHeatmap);

    const auto activeClustering =
        Runtime::GetClusteringConfig(engine.GetEngineConfig());
    ASSERT_TRUE(activeClustering.has_value());
    const auto activeCurvatureSegmentation =
        Runtime::GetCurvatureSegmentationConfig(
            engine.GetEngineConfig());
    ASSERT_TRUE(activeCurvatureSegmentation.has_value());
    EXPECT_EQ(
        activeCurvatureSegmentation->SelectionMode,
        Runtime::CurvatureSegmentationSelectionMode::Automatic);
    EXPECT_EQ(
        activeCurvatureSegmentation->AutomaticMinComponents,
        2u);
    EXPECT_EQ(
        activeCurvatureSegmentation->AutomaticMaxComponents,
        7u);
    EXPECT_DOUBLE_EQ(
        activeCurvatureSegmentation->AutomaticFitTolerance,
        0.21);
    EXPECT_EQ(activeCurvatureSegmentation->Seed, 0x2468ace0u);
    const auto activePointCloudConsolidation =
        Runtime::GetPointCloudConsolidationConfig(engine.GetEngineConfig());
    ASSERT_TRUE(activePointCloudConsolidation.has_value());
    EXPECT_EQ(
        activePointCloudConsolidation->Strategy,
        Runtime::PointCloudConsolidationStrategy::Wlop);
    EXPECT_EQ(activePointCloudConsolidation->MaxIterations, 31u);
    EXPECT_EQ(activePointCloudConsolidation->Seed, 0xfedcba98u);
    const auto activePhysics =
        Runtime::GetPhysicsModuleConfig(engine.GetEngineConfig());
    ASSERT_TRUE(activePhysics.has_value());
    EXPECT_TRUE(activePhysics->Enabled);
    EXPECT_EQ(activePhysics->MaxStepsPerFrame, 5u);
    EXPECT_FLOAT_EQ(activePhysics->Gravity.y, -6.0f);
    Runtime::KMeansPropertyRefs properties = Runtime::MakeKMeansPropertyRefs(
        Runtime::GeometryElementDomain::MeshVertex);
    properties.OutputLabels.Name = "v:configured_cluster";
    const Runtime::RunKMeans command = Runtime::MakeConfiguredKMeansRequest(
        41u,
        properties,
        *activeClustering);
    EXPECT_EQ(command.StableEntityId, 41u);
    EXPECT_EQ(command.Properties.InputPositions.Domain,
              Runtime::GeometryElementDomain::MeshVertex);
    EXPECT_EQ(command.Properties.OutputLabels.Name, "v:configured_cluster");
    EXPECT_EQ(command.Parameters.ClusterCount, 5u);
    EXPECT_EQ(command.Parameters.MaxIterations, 19u);
    EXPECT_EQ(command.Parameters.Seed, 0xffffffffu);
    EXPECT_EQ(command.Parameters.Initialization,
              Runtime::KMeansInitialization::Hierarchical);
    EXPECT_EQ(command.Backend, Runtime::ClusteringBackend::CpuReference);

    engine.Run();
    engine.Shutdown();
}

TEST(SandboxConfigSections,
     PhysicsSourcesProduceIdenticalValidatedState)
{
    constexpr std::array sources{
        Runtime::RuntimeConfigControlSource::Editor,
        Runtime::RuntimeConfigControlSource::AgentCli,
        Runtime::RuntimeConfigControlSource::Programmatic,
    };
    Runtime::PhysicsModuleConfig requested{};
    requested.Enabled = true;
    requested.FixedDeltaSeconds = 0.005f;
    requested.MaxAccumulatedSeconds = 0.125f;
    requested.MaxStepsPerFrame = 17u;
    requested.Gravity = glm::vec3{1.0f, -3.0f, 2.0f};

    std::optional<std::string> referenceSerialized{};
    for (const Runtime::RuntimeConfigControlSource source : sources)
    {
        ConfigControlHarness harness{};
        Runtime::EngineConfigControl& control = harness.Control();
        CoreConfig::EngineConfig candidate =
            control.GetEngineConfigControlState().ActiveConfig;
        Runtime::SetPhysicsModuleConfig(candidate, requested);
        const CoreConfig::EngineConfigLoadResult preview =
            control.PreviewEngineConfigControlDocument(
                CoreConfig::SerializeEngineConfig(candidate),
                "physics-source-parity");
        ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));

        const Runtime::RuntimeEngineConfigApplyResult applied =
            control.ApplyEngineConfigHotSubset(preview, source);
        ASSERT_TRUE(applied.Succeeded());
        EXPECT_EQ(applied.Source, source);
        EXPECT_TRUE(applied.SectionChanged(
            Runtime::kPhysicsModuleConfigSectionName));

        const auto active = Runtime::GetPhysicsModuleConfig(
            control.GetEngineConfigControlState().ActiveConfig);
        ASSERT_TRUE(active.has_value());
        EXPECT_TRUE(active->Enabled);
        EXPECT_FLOAT_EQ(active->FixedDeltaSeconds, 0.005f);
        EXPECT_FLOAT_EQ(active->MaxAccumulatedSeconds, 0.125f);
        EXPECT_EQ(active->MaxStepsPerFrame, 17u);
        EXPECT_FLOAT_EQ(active->Gravity.x, requested.Gravity.x);
        EXPECT_FLOAT_EQ(active->Gravity.y, requested.Gravity.y);
        EXPECT_FLOAT_EQ(active->Gravity.z, requested.Gravity.z);

        const std::string serialized = CoreConfig::SerializeEngineConfig(
            control.GetEngineConfigControlState().ActiveConfig);
        if (!referenceSerialized.has_value())
            referenceSerialized = serialized;
        else
            EXPECT_EQ(serialized, *referenceSerialized);
    }
}

TEST(SandboxConfigSections,
     PointCloudConsolidationSourcesProduceIdenticalValidatedState)
{
    constexpr std::array sources{
        Runtime::RuntimeConfigControlSource::Editor,
        Runtime::RuntimeConfigControlSource::AgentCli,
        Runtime::RuntimeConfigControlSource::Programmatic,
    };
    Runtime::PointCloudConsolidationConfig requested{};
    requested.Strategy = Runtime::PointCloudConsolidationStrategy::Clop;
    requested.SupportRadiusMode = Runtime::
        PointCloudConsolidationSupportRadiusMode::Manual;
    requested.SupportRadius = 0.375;
    requested.MaxSupportNeighbors = 2'048u;
    requested.MaxPredictedContributions = 88'000'000u;
    requested.TargetPointCount = 63u;
    requested.Seed = 0x13579bdfu;
    requested.ClopMixtureComponentCount = 11u;

    std::optional<std::string> referenceSerialized{};
    for (const Runtime::RuntimeConfigControlSource source : sources)
    {
        ConfigControlHarness harness{};
        Runtime::EngineConfigControl& control = harness.Control();
        CoreConfig::EngineConfig candidate =
            control.GetEngineConfigControlState().ActiveConfig;
        Runtime::SetPointCloudConsolidationConfig(candidate, requested);
        const CoreConfig::EngineConfigLoadResult preview =
            control.PreviewEngineConfigControlDocument(
                CoreConfig::SerializeEngineConfig(candidate),
                "point-cloud-consolidation-source-parity");
        ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));

        const Runtime::RuntimeEngineConfigApplyResult applied =
            control.ApplyEngineConfigHotSubset(preview, source);
        ASSERT_TRUE(applied.Succeeded());
        EXPECT_EQ(applied.Source, source);
        EXPECT_TRUE(applied.SectionChanged(
            Runtime::kPointCloudConsolidationConfigSectionName));

        const auto active = Runtime::GetPointCloudConsolidationConfig(
            control.GetEngineConfigControlState().ActiveConfig);
        ASSERT_TRUE(active.has_value());
        EXPECT_EQ(active->Strategy, requested.Strategy);
        EXPECT_EQ(active->SupportRadiusMode, requested.SupportRadiusMode);
        EXPECT_DOUBLE_EQ(active->SupportRadius, requested.SupportRadius);
        EXPECT_EQ(active->MaxSupportNeighbors,
                  requested.MaxSupportNeighbors);
        EXPECT_EQ(active->MaxPredictedContributions,
                  requested.MaxPredictedContributions);
        EXPECT_EQ(active->TargetPointCount, requested.TargetPointCount);
        EXPECT_EQ(active->Seed, requested.Seed);
        EXPECT_EQ(
            active->ClopMixtureComponentCount,
            requested.ClopMixtureComponentCount);

        const std::string serialized = CoreConfig::SerializeEngineConfig(
            control.GetEngineConfigControlState().ActiveConfig);
        if (!referenceSerialized.has_value())
            referenceSerialized = serialized;
        else
            EXPECT_EQ(serialized, *referenceSerialized);
    }
}

TEST(SandboxConfigSections,
     CurvatureSegmentationSourcesProduceIdenticalValidatedState)
{
    constexpr std::array sources{
        Runtime::RuntimeConfigControlSource::Editor,
        Runtime::RuntimeConfigControlSource::AgentCli,
        Runtime::RuntimeConfigControlSource::Programmatic,
    };
    Runtime::CurvatureSegmentationConfig requested{};
    requested.Method =
        Runtime::CurvatureSegmentationMethod::FeatureAlignedPatches;
    requested.SelectionMode =
        Runtime::CurvatureSegmentationSelectionMode::Automatic;
    requested.FixedComponentCount = 5u;
    requested.AutomaticMinComponents = 2u;
    requested.AutomaticMaxComponents = 9u;
    requested.AutomaticFitTolerance = 0.18;
    requested.AutomaticComplexityWeight = 1.75;
    requested.MaxEmIterations = 77u;
    requested.EmRelativeTolerance = 2.5e-6;
    requested.CovarianceFloor = 4.0e-5;
    requested.Seed = 0x13579bdfu;
    requested.SpatialWeight = 1.5;
    requested.FeatureSensitivity = 8.0;
    requested.MaxSpatialIterations = 19u;
    requested.MinimumRegionFaces = 3u;
    requested.FeatureBaseRadiusRatio = 0.035;
    requested.HardDihedralThresholdDegrees = 52.0;
    requested.PatchComplexityCost = 0.625;
    ASSERT_TRUE(Runtime::IsValidCurvatureSegmentationConfig(requested));

    for (const auto method : {Runtime::CurvatureSegmentationMethod::CurvatureGmm,
                              Runtime::CurvatureSegmentationMethod::FeatureAlignedPatches,
                              Runtime::CurvatureSegmentationMethod::FeatureBoundaryCurves})
    {
        requested.Method = method;
        std::optional<std::string> referenceSerialized{};
        for (const Runtime::RuntimeConfigControlSource source : sources)
        {
            ConfigControlHarness harness{};
            Runtime::EngineConfigControl& control = harness.Control();
            CoreConfig::EngineConfig candidate =
                control.GetEngineConfigControlState().ActiveConfig;
            Runtime::SetCurvatureSegmentationConfig(candidate, requested);
            const CoreConfig::EngineConfigLoadResult preview =
                control.PreviewEngineConfigControlDocument(
                    CoreConfig::SerializeEngineConfig(candidate),
                    "curvature-segmentation-source-parity");
            ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));

            const Runtime::RuntimeEngineConfigApplyResult applied =
                control.ApplyEngineConfigHotSubset(preview, source);
            ASSERT_TRUE(applied.Succeeded());
            EXPECT_EQ(applied.Source, source);
            EXPECT_TRUE(applied.SectionChanged(
                Runtime::kCurvatureSegmentationConfigSectionName));

            const auto active = Runtime::GetCurvatureSegmentationConfig(
                control.GetEngineConfigControlState().ActiveConfig);
            ASSERT_TRUE(active.has_value());
            EXPECT_EQ(active->Method, requested.Method);
            EXPECT_EQ(active->SelectionMode, requested.SelectionMode);
            EXPECT_EQ(active->FixedComponentCount,
                      requested.FixedComponentCount);
            EXPECT_EQ(active->AutomaticMinComponents,
                      requested.AutomaticMinComponents);
            EXPECT_EQ(active->AutomaticMaxComponents,
                      requested.AutomaticMaxComponents);
            EXPECT_DOUBLE_EQ(active->AutomaticFitTolerance,
                             requested.AutomaticFitTolerance);
            EXPECT_DOUBLE_EQ(active->AutomaticComplexityWeight,
                             requested.AutomaticComplexityWeight);
            EXPECT_EQ(active->MaxEmIterations, requested.MaxEmIterations);
            EXPECT_DOUBLE_EQ(active->EmRelativeTolerance,
                             requested.EmRelativeTolerance);
            EXPECT_DOUBLE_EQ(active->CovarianceFloor,
                             requested.CovarianceFloor);
            EXPECT_EQ(active->Seed, requested.Seed);
            EXPECT_DOUBLE_EQ(active->SpatialWeight, requested.SpatialWeight);
            EXPECT_DOUBLE_EQ(active->FeatureSensitivity,
                             requested.FeatureSensitivity);
            EXPECT_EQ(active->MaxSpatialIterations,
                      requested.MaxSpatialIterations);
            EXPECT_EQ(active->MinimumRegionFaces,
                      requested.MinimumRegionFaces);
            EXPECT_DOUBLE_EQ(active->FeatureBaseRadiusRatio,
                             requested.FeatureBaseRadiusRatio);
            EXPECT_DOUBLE_EQ(active->HardDihedralThresholdDegrees,
                             requested.HardDihedralThresholdDegrees);
            EXPECT_DOUBLE_EQ(active->PatchComplexityCost,
                             requested.PatchComplexityCost);

            const std::string serialized = CoreConfig::SerializeEngineConfig(
                control.GetEngineConfigControlState().ActiveConfig);
            if (!referenceSerialized.has_value())
                referenceSerialized = serialized;
            else
                EXPECT_EQ(serialized, *referenceSerialized);
        }
    }

}

TEST(SandboxConfigSections,
     CurvatureSegmentationDefaultsAndInvalidRangesFailClosed)
{
    ConfigControlHarness harness{};
    const auto defaults = Runtime::GetCurvatureSegmentationConfig(
        harness.Control().GetEngineConfigControlState().ActiveConfig);
    ASSERT_TRUE(defaults.has_value());
    EXPECT_TRUE(Runtime::IsValidCurvatureSegmentationConfig(*defaults));
    EXPECT_EQ(defaults->Method,
              Runtime::CurvatureSegmentationMethod::CurvatureGmm);
    EXPECT_EQ(defaults->SelectionMode,
              Runtime::CurvatureSegmentationSelectionMode::Automatic);
    EXPECT_EQ(defaults->FixedComponentCount, 6u);
    EXPECT_DOUBLE_EQ(defaults->FeatureBaseRadiusRatio, 0.02);
    EXPECT_DOUBLE_EQ(defaults->HardDihedralThresholdDegrees, 45.0);
    EXPECT_DOUBLE_EQ(defaults->PatchComplexityCost, 0.5);

    Runtime::CurvatureSegmentationConfig invalid = *defaults;
    invalid.AutomaticMinComponents = 8u;
    invalid.AutomaticMaxComponents = 2u;
    EXPECT_FALSE(Runtime::IsValidCurvatureSegmentationConfig(invalid));

    invalid = *defaults;
    invalid.PatchComplexityCost = 1.0e13;
    EXPECT_FALSE(Runtime::IsValidCurvatureSegmentationConfig(invalid));

    invalid.AutomaticMinComponents = 8u;
    invalid.AutomaticMaxComponents = 2u;

    CoreConfig::EngineConfig candidate =
        harness.Control().GetEngineConfigControlState().ActiveConfig;
    Runtime::SetCurvatureSegmentationConfig(candidate, invalid);
    const CoreConfig::EngineConfigLoadResult preview =
        harness.Control().PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "curvature-segmentation-invalid-range");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));
    EXPECT_EQ(preview.State,
              CoreConfig::EngineConfigState::FallbackApplied);
    EXPECT_FALSE(preview.Diagnostics.empty());

    const Runtime::RuntimeEngineConfigApplyResult applied =
        harness.Control().ApplyEngineConfigHotSubset(
            preview,
            Runtime::RuntimeConfigControlSource::Editor);
    ASSERT_TRUE(applied.Succeeded());
    const auto active = Runtime::GetCurvatureSegmentationConfig(
        harness.Control().GetEngineConfigControlState().ActiveConfig);
    ASSERT_TRUE(active.has_value());
    EXPECT_TRUE(Runtime::IsValidCurvatureSegmentationConfig(*active));
    EXPECT_EQ(active->AutomaticMinComponents,
              defaults->AutomaticMinComponents);
    EXPECT_EQ(active->AutomaticMaxComponents,
              defaults->AutomaticMaxComponents);
}

TEST(SandboxConfigSections, CurvatureBindingsRoundTripAndRejectAliasing)
{
    ConfigControlHarness harness;
    auto config = harness.Control().GetEngineConfigControlState().ActiveConfig;
    auto curvature = Runtime::GetMeshCurvatureConfig(config);
    ASSERT_TRUE(curvature);
    curvature->StableEntityId = 17;
    curvature->Positions.Name = "v:rest";
    curvature->Mean.Name = "v:mean_custom";
    curvature->Direction2.Name = "v:direction_custom";
    Runtime::SetMeshCurvatureConfig(config, *curvature);
    const auto preview = harness.Control().PreviewEngineConfigControlDocument(CoreConfig::SerializeEngineConfig(config));
    ASSERT_TRUE(harness.Control().ApplyEngineConfigHotSubset(preview).Succeeded());
    const auto decoded = Runtime::GetMeshCurvatureConfig(harness.Control().GetEngineConfigControlState().ActiveConfig);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->Positions, curvature->Positions);
    EXPECT_EQ(decoded->Mean, curvature->Mean);
    EXPECT_EQ(decoded->Direction2, curvature->Direction2);
    curvature->Direction2 = curvature->Positions;
    EXPECT_FALSE(Runtime::ValidateMeshCurvatureConfigSection(
        Runtime::SerializeMeshCurvatureConfig(*curvature), {}, "curvature").Usable());
    auto segmentation = *Runtime::GetCurvatureSegmentationConfig(config);
    segmentation.Regions.Name = "f:region_custom";
    Runtime::SetCurvatureSegmentationConfig(config, segmentation);
    ASSERT_TRUE(Runtime::GetCurvatureSegmentationConfig(config));
    EXPECT_EQ(Runtime::GetCurvatureSegmentationConfig(config)->Regions, segmentation.Regions);
    segmentation.Regions = segmentation.Components;
    EXPECT_FALSE(Runtime::ValidateCurvatureSegmentationConfigSection(
        Runtime::SerializeCurvatureSegmentationConfig(segmentation), {}, "segmentation").Usable());
    Runtime::ClusteringConfig clustering;
    clustering.Properties = Runtime::MakeKMeansPropertyRefs(Runtime::GeometryElementDomain::MeshFace);
    clustering.Properties->InputPositions.Name = "f:centroid";
    clustering.Properties->OutputLabels.Name = "f:custom_clusters";
    Runtime::SetClusteringConfig(config, clustering);
    const auto decodedClustering = Runtime::GetClusteringConfig(config);
    ASSERT_TRUE(decodedClustering);
    ASSERT_TRUE(decodedClustering->Properties);
    EXPECT_EQ(decodedClustering->Properties->InputPositions, clustering.Properties->InputPositions);
    const auto request = Runtime::MakeConfiguredKMeansRequest(17, *decodedClustering);
    EXPECT_EQ(request.Properties.OutputLabels, clustering.Properties->OutputLabels);    clustering.Properties->OutputScalarLabels.reset();
    Runtime::SetClusteringConfig(config, clustering);
    const auto noScalarLabels = Runtime::GetClusteringConfig(config);
    ASSERT_TRUE(noScalarLabels);
    ASSERT_TRUE(noScalarLabels->Properties);
    EXPECT_FALSE(noScalarLabels->Properties->OutputScalarLabels);

}

TEST(SandboxConfigSections,
     AllFeatureFamilySectionsCoexistAndDecodeIndependently)
{
    CoreConfig::EngineConfig config{};

    Runtime::ClusteringConfig clustering{};
    clustering.Parameters.ClusterCount = 9u;
    Runtime::SetClusteringConfig(config, clustering);

    Runtime::CurvatureSegmentationConfig curvatureSegmentation{};
    curvatureSegmentation.SelectionMode =
        Runtime::CurvatureSegmentationSelectionMode::FixedCount;
    curvatureSegmentation.FixedComponentCount = 11u;
    Runtime::SetCurvatureSegmentationConfig(config, curvatureSegmentation);

    Runtime::ProgressivePoissonPlaygroundConfig progressivePoisson{};
    progressivePoisson.Channel =
        Runtime::ProgressivePoissonPlaygroundChannel::Rank;
    Runtime::SetProgressivePoissonPlaygroundConfig(config, progressivePoisson);

    Runtime::ParameterizationConfig parameterization{};
    parameterization.View.BackgroundMode =
        Runtime::ParameterizationUvBackgroundMode::TexelDensity;
    Runtime::SetParameterizationConfig(config, parameterization);

    Runtime::PointCloudConsolidationConfig pointCloudConsolidation{};
    pointCloudConsolidation.Strategy =
        Runtime::PointCloudConsolidationStrategy::Clop;
    Runtime::SetPointCloudConsolidationConfig(config, pointCloudConsolidation);

    ASSERT_EQ(config.AppSections.size(), 5u);
    EXPECT_EQ(config.AppSections[0].Name,
              Runtime::kClusteringConfigSectionName);
    EXPECT_EQ(config.AppSections[1].Name,
              Runtime::kCurvatureSegmentationConfigSectionName);
    EXPECT_EQ(config.AppSections[2].Name,
              Runtime::kParameterizationConfigSectionName);
    EXPECT_EQ(config.AppSections[3].Name,
              Runtime::kPointCloudConsolidationConfigSectionName);
    EXPECT_EQ(config.AppSections[4].Name,
              Runtime::kProgressivePoissonConfigSectionName);

    const auto decodedClustering = Runtime::GetClusteringConfig(config);
    ASSERT_TRUE(decodedClustering.has_value());
    EXPECT_EQ(decodedClustering->Parameters.ClusterCount, 9u);

    const auto decodedCurvatureSegmentation =
        Runtime::GetCurvatureSegmentationConfig(config);
    ASSERT_TRUE(decodedCurvatureSegmentation.has_value());
    EXPECT_EQ(decodedCurvatureSegmentation->FixedComponentCount, 11u);

    const auto decodedProgressivePoisson =
        Runtime::GetProgressivePoissonPlaygroundConfig(config);
    ASSERT_TRUE(decodedProgressivePoisson.has_value());
    EXPECT_EQ(decodedProgressivePoisson->Channel,
              Runtime::ProgressivePoissonPlaygroundChannel::Rank);

    const auto decodedParameterization =
        Runtime::GetParameterizationConfig(config);
    ASSERT_TRUE(decodedParameterization.has_value());
    EXPECT_EQ(decodedParameterization->View.BackgroundMode,
              Runtime::ParameterizationUvBackgroundMode::TexelDensity);

    const auto decodedPointCloudConsolidation =
        Runtime::GetPointCloudConsolidationConfig(config);
    ASSERT_TRUE(decodedPointCloudConsolidation.has_value());
    EXPECT_EQ(decodedPointCloudConsolidation->Strategy,
              Runtime::PointCloudConsolidationStrategy::Clop);

    // Dropping one family's section must not disturb the other four decoders.
    ASSERT_EQ(config.AppSections.front().Name,
              Runtime::kClusteringConfigSectionName);
    config.AppSections.erase(config.AppSections.begin());
    EXPECT_FALSE(Runtime::GetClusteringConfig(config).has_value());
    EXPECT_TRUE(Runtime::GetCurvatureSegmentationConfig(config).has_value());
    EXPECT_TRUE(
        Runtime::GetProgressivePoissonPlaygroundConfig(config).has_value());
    EXPECT_TRUE(Runtime::GetParameterizationConfig(config).has_value());
    EXPECT_TRUE(
        Runtime::GetPointCloudConsolidationConfig(config).has_value());
}

TEST(SandboxConfigSections,
     FeatureSectionRegistrationsCarryFreshSchemaIdentityDefaults)
{
    const std::array<CoreConfig::EngineConfigSectionRegistration, 5u>
        registrations{
            Runtime::MakeClusteringConfigSectionRegistration(),
            Runtime::MakeCurvatureSegmentationConfigSectionRegistration(),
            Runtime::MakeProgressivePoissonConfigSectionRegistration(),
            Runtime::MakeParameterizationConfigSectionRegistration(),
            Runtime::MakePointCloudConsolidationConfigSectionRegistration(),
        };
    const std::array<std::string_view, 5u> expectedNames{
        Runtime::kClusteringConfigSectionName,
        Runtime::kCurvatureSegmentationConfigSectionName,
        Runtime::kProgressivePoissonConfigSectionName,
        Runtime::kParameterizationConfigSectionName,
        Runtime::kPointCloudConsolidationConfigSectionName,
    };
    const std::array<std::string_view, 5u> expectedSchemaIds{
        Runtime::kClusteringConfigSectionSchemaId,
        Runtime::kCurvatureSegmentationConfigSectionSchemaId,
        Runtime::kProgressivePoissonConfigSectionSchemaId,
        Runtime::kParameterizationConfigSectionSchemaId,
        Runtime::kPointCloudConsolidationConfigSectionSchemaId,
    };
    const std::array<std::uint32_t, 5u> expectedSchemaVersions{
        Runtime::kClusteringConfigSectionSchemaVersion,
        Runtime::kCurvatureSegmentationConfigSectionSchemaVersion,
        Runtime::kProgressivePoissonConfigSectionSchemaVersion,
        Runtime::kParameterizationConfigSectionSchemaVersion,
        Runtime::kPointCloudConsolidationConfigSectionSchemaVersion,
    };
    const std::array<std::string, 5u> expectedPayloads{
        Runtime::SerializeClusteringConfig(Runtime::ClusteringConfig{}),
        Runtime::SerializeCurvatureSegmentationConfig(
            Runtime::CurvatureSegmentationConfig{}),
        Runtime::SerializeProgressivePoissonPlaygroundConfig(
            Runtime::ProgressivePoissonPlaygroundConfig{}),
        Runtime::SerializeParameterizationConfig(
            Runtime::ParameterizationConfig{}),
        Runtime::SerializePointCloudConsolidationConfig(
            Runtime::PointCloudConsolidationConfig{}),
    };

    CoreConfig::EngineConfig config{};
    for (std::size_t index = 0; index < registrations.size(); ++index)
    {
        const CoreConfig::EngineConfigSectionRegistration& registration =
            registrations[index];
        EXPECT_EQ(registration.DefaultSection.Name, expectedNames[index]);
        EXPECT_EQ(registration.DefaultSection.SchemaId,
                  expectedSchemaIds[index]);
        EXPECT_EQ(registration.DefaultSection.SchemaVersion,
                  expectedSchemaVersions[index]);
        EXPECT_EQ(registration.DefaultSection.PayloadJson,
                  expectedPayloads[index]);
        EXPECT_TRUE(static_cast<bool>(registration.Validate));
        EXPECT_FALSE(static_cast<bool>(registration.OnChanged));
        CoreConfig::UpsertEngineConfigSection(
            config.AppSections,
            registration.DefaultSection);
    }

    ASSERT_EQ(config.AppSections.size(), 5u);
    const auto defaultClustering = Runtime::GetClusteringConfig(config);
    ASSERT_TRUE(defaultClustering.has_value());
    EXPECT_EQ(Runtime::SerializeClusteringConfig(*defaultClustering),
              expectedPayloads[0]);
    const auto defaultCurvatureSegmentation =
        Runtime::GetCurvatureSegmentationConfig(config);
    ASSERT_TRUE(defaultCurvatureSegmentation.has_value());
    EXPECT_EQ(Runtime::SerializeCurvatureSegmentationConfig(
                  *defaultCurvatureSegmentation),
              expectedPayloads[1]);
    const auto defaultProgressivePoisson =
        Runtime::GetProgressivePoissonPlaygroundConfig(config);
    ASSERT_TRUE(defaultProgressivePoisson.has_value());
    EXPECT_EQ(Runtime::SerializeProgressivePoissonPlaygroundConfig(
                  *defaultProgressivePoisson),
              expectedPayloads[2]);
    const auto defaultParameterization =
        Runtime::GetParameterizationConfig(config);
    ASSERT_TRUE(defaultParameterization.has_value());
    EXPECT_EQ(
        Runtime::SerializeParameterizationConfig(*defaultParameterization),
        expectedPayloads[3]);
    const auto defaultPointCloudConsolidation =
        Runtime::GetPointCloudConsolidationConfig(config);
    ASSERT_TRUE(defaultPointCloudConsolidation.has_value());
    EXPECT_EQ(Runtime::SerializePointCloudConsolidationConfig(
                  *defaultPointCloudConsolidation),
              expectedPayloads[4]);
}

TEST(SandboxConfigSections,
     FeatureSectionGetAcceptsOnlyValidMatchingSchemaSections)
{
    CoreConfig::EngineConfig config{};
    Runtime::ClusteringConfig clustering{};
    clustering.Parameters.ClusterCount = 12u;
    Runtime::SetClusteringConfig(config, clustering);
    ASSERT_TRUE(Runtime::GetClusteringConfig(config).has_value());

    CoreConfig::EngineConfigSection* const section =
        CoreConfig::FindEngineConfigSection(
            config.AppSections,
            Runtime::kClusteringConfigSectionName);
    ASSERT_NE(section, nullptr);

    section->SchemaId = "intrinsic.runtime.sandbox.clustering.foreign";
    EXPECT_FALSE(Runtime::GetClusteringConfig(config).has_value());
    section->SchemaId = std::string{Runtime::kClusteringConfigSectionSchemaId};

    section->SchemaVersion =
        Runtime::kClusteringConfigSectionSchemaVersion + 1u;
    EXPECT_FALSE(Runtime::GetClusteringConfig(config).has_value());
    section->SchemaVersion = Runtime::kClusteringConfigSectionSchemaVersion;
    EXPECT_TRUE(Runtime::GetClusteringConfig(config).has_value());

    // A usable fallback is still not a decodable section for Get.
    constexpr std::string_view unknownFieldPayload =
        R"({"cluster_count":12,"totally_unknown_field":true})";
    section->PayloadJson = std::string{unknownFieldPayload};
    const CoreConfig::EngineConfigSectionValidationResult clusteringFallback =
        Runtime::ValidateClusteringConfigSection(
            unknownFieldPayload,
            Runtime::SerializeClusteringConfig(Runtime::ClusteringConfig{}),
            Runtime::kClusteringConfigSectionName);
    EXPECT_EQ(clusteringFallback.State,
              CoreConfig::EngineConfigState::FallbackApplied);
    EXPECT_TRUE(clusteringFallback.Usable());
    EXPECT_FALSE(Runtime::GetClusteringConfig(config).has_value());

    config.AppSections.clear();
    EXPECT_FALSE(Runtime::GetClusteringConfig(config).has_value());
}

TEST(SandboxConfigSections,
     FeatureSectionSetStoresRawPayloadAndPreservesUnrelatedSections)
{
    CoreConfig::EngineConfig config{};
    Runtime::SetParameterizationConfig(
        config,
        Runtime::ParameterizationConfig{});
    Runtime::SetProgressivePoissonPlaygroundConfig(
        config,
        Runtime::ProgressivePoissonPlaygroundConfig{});

    const auto unrelatedSections = config.AppSections;

    // Set never validates: the raw serializer output is stored verbatim even
    // when the payload cannot survive the Get validation gate.
    Runtime::ClusteringConfig unusable{};
    unusable.Parameters.ClusterCount = 0u;
    Runtime::SetClusteringConfig(config, unusable);
    ASSERT_EQ(config.AppSections.size(), 3u);
    const CoreConfig::EngineConfigSection* stored =
        CoreConfig::FindEngineConfigSection(
            config.AppSections,
            Runtime::kClusteringConfigSectionName);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->PayloadJson,
              Runtime::SerializeClusteringConfig(unusable));
    EXPECT_EQ(stored->SchemaId, Runtime::kClusteringConfigSectionSchemaId);
    EXPECT_EQ(stored->SchemaVersion,
              Runtime::kClusteringConfigSectionSchemaVersion);
    EXPECT_FALSE(Runtime::GetClusteringConfig(config).has_value());

    Runtime::ClusteringConfig repaired{};
    repaired.Parameters.ClusterCount = 6u;
    repaired.Parameters.MaxIterations = 44u;
    Runtime::SetClusteringConfig(config, repaired);

    ASSERT_EQ(config.AppSections.size(), 3u);
    EXPECT_TRUE(std::is_sorted(
        config.AppSections.begin(),
        config.AppSections.end(),
        [](const CoreConfig::EngineConfigSection& lhs,
           const CoreConfig::EngineConfigSection& rhs)
        {
            return lhs.Name < rhs.Name;
        }));
    stored = CoreConfig::FindEngineConfigSection(
        config.AppSections,
        Runtime::kClusteringConfigSectionName);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->PayloadJson,
              Runtime::SerializeClusteringConfig(repaired));
    auto preserved = config.AppSections;
    std::erase_if(preserved, [](const auto& section)
    {
        return section.Name == Runtime::kClusteringConfigSectionName;
    });
    EXPECT_EQ(preserved, unrelatedSections);

    const auto decoded = Runtime::GetClusteringConfig(config);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->Parameters.ClusterCount, 6u);
    EXPECT_EQ(decoded->Parameters.MaxIterations, 44u);
}

TEST(SandboxConfigSections, PointPropertySerializersPreserveTokensAndNameBytes)
{
    using D = Runtime::GeometryElementDomain;
    using K = Geometry::PropertyValueKind;
    using Writer = std::string (*)(const Runtime::GeometryPropertyRef&);
    using Validator = CoreConfig::EngineConfigSectionValidationResult (*)(
        std::string_view, std::string_view, std::string_view);
    struct Family
    {
        const char* Name;
        Writer Write;
        Validator Validate;
    };
    const std::array<Family, 7> writers{{
        {"BilateralFilter", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::BilateralFilterConfig config;
            config.Positions = ref;
            return Runtime::SerializeBilateralFilterConfig(config);
        }, Runtime::ValidateBilateralFilterConfigSection},
        {"KernelDensity", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::KernelDensityConfig config;
            config.Positions = ref;
            return Runtime::SerializeKernelDensityConfig(config);
        }, Runtime::ValidateKernelDensityConfigSection},
        {"PointSpacing", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::PointSpacingConfig config;
            config.Positions = ref;
            return Runtime::SerializePointSpacingConfig(config);
        }, Runtime::ValidatePointSpacingConfigSection},
        {"OutlierAnalysis", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::OutlierAnalysisConfig config;
            config.Positions = ref;
            return Runtime::SerializeOutlierAnalysisConfig(config);
        }, Runtime::ValidateOutlierAnalysisConfigSection},
        {"KeypointAnalysis", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::KeypointAnalysisConfig config;
            config.Positions = ref;
            return Runtime::SerializeKeypointAnalysisConfig(config);
        }, Runtime::ValidateKeypointAnalysisConfigSection},
        {"DescriptorAnalysis", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::DescriptorAnalysisConfig config;
            config.Positions = ref;
            return Runtime::SerializeDescriptorAnalysisConfig(config);
        }, Runtime::ValidateDescriptorAnalysisConfigSection},
        {"DensityWeight", +[](const Runtime::GeometryPropertyRef& ref) {
            Runtime::DensityWeightConfig config;
            config.Positions = ref;
            return Runtime::SerializeDensityWeightConfig(config);
        }, Runtime::ValidateDensityWeightConfigSection},
    }};
    struct Case
    {
        D Domain;
        K Kind;
        std::string Name;
        std::string_view Expected;
    };
    const std::array cases{
        Case{D::Unknown, K::Vec3, "samples", R"({"domain":"Unknown","kind":"vec3","name":"samples"})"},
        Case{D::MeshFace, K::Float, "f:weight", R"({"domain":"MeshFace","kind":"float","name":"f:weight"})"},
        Case{D::PointCloudPoint, K::UInt32, "labels", R"({"domain":"PointCloudPoint","kind":"uint32","name":"labels"})"},
        Case{D::MeshVertex, K::Vec4, "v:color", R"({"domain":"MeshVertex","kind":"invalid","name":"v:color"})"},
        Case{D(255), K(255), "", R"({"domain":"invalid","kind":"invalid","name":""})"},
        Case{D::MeshVertex, K::Vec3, std::string{"v:\0\"\\\n", 6}, R"({"domain":"MeshVertex","kind":"vec3","name":"v:\u0000\"\\\n"})"},
    };
    for (const auto& [family, write, validate] : writers)
    {
        SCOPED_TRACE(family);
        for (const auto& test : cases)
        {
            SCOPED_TRACE(test.Expected);
            const auto document = write({test.Domain, test.Name, test.Kind});
            EXPECT_NE(document.find("\"positions\":" + std::string(test.Expected)), std::string::npos)
                << document;
            if (test.Kind != K::Vec3)
                EXPECT_FALSE(validate(document, {}, family).Usable());
        }
    }
}
