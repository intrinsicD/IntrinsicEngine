#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.SandboxConfigSections;
import Extrinsic.Sandbox.ConfigSections;

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
}

TEST(SandboxConfigSections, BootAndLiveApplyUseTheAppOwnedRegistryThroughNullRun)
{
    std::uint32_t clusteringChanges = 0u;
    std::uint32_t progressivePoissonChanges = 0u;
    std::uint32_t parameterizationChanges = 0u;
    auto configControl = std::make_unique<Runtime::EngineConfigControl>(
        Sandbox::CreateSandboxConfigSectionRegistry(
            Sandbox::SandboxConfigSectionCallbacks{
                .Clustering =
                    [&](const auto&, const auto&)
                    {
                        ++clusteringChanges;
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

    const ScopedConfigFile file{CoreConfig::SerializeEngineConfig(fileConfig)};
    const std::string filePath = file.Path().string();
    const std::array<std::string_view, 3u> args{
        "IntrinsicSandboxEditorIntegrationTests",
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
    EXPECT_EQ(clusteringChanges, 0u);
    EXPECT_EQ(progressivePoissonChanges, 0u);
    EXPECT_EQ(parameterizationChanges, 0u);

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

    auto liveParameterization =
        Runtime::GetParameterizationConfig(candidate);
    ASSERT_TRUE(liveParameterization.has_value());
    liveParameterization->View.ShowDistortionHeatmap = true;
    Runtime::SetParameterizationConfig(candidate, *liveParameterization);

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
        Runtime::kParameterizationConfigSectionName));
    EXPECT_FALSE(apply.SectionChanged(
        Runtime::kProgressivePoissonConfigSectionName));
    EXPECT_EQ(clusteringChanges, 1u);
    EXPECT_EQ(progressivePoissonChanges, 0u);
    EXPECT_EQ(parameterizationChanges, 1u);

    const auto activeParameterization =
        Runtime::GetParameterizationConfig(engine.GetEngineConfig());
    ASSERT_TRUE(activeParameterization.has_value());
    EXPECT_TRUE(activeParameterization->View.ShowDistortionHeatmap);

    const auto activeClustering =
        Runtime::GetClusteringConfig(engine.GetEngineConfig());
    ASSERT_TRUE(activeClustering.has_value());
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
