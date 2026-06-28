#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.Engine;

using namespace Extrinsic;

namespace
{
    [[nodiscard]] std::filesystem::path TempConfigPath()
    {
        return std::filesystem::temp_directory_path()
            / "intrinsic_runtime_engine_config_boot.json";
    }

    void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(file.is_open());
        file << text;
    }
}

TEST(RuntimeEngineConfigBoot, CommandLineConfigOverridesReferenceDefaults)
{
    Core::Config::EngineConfig config = Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "CLI Config";
    config.Window.Width = 1024;
    config.Window.Height = 768;
    config.Window.Backend = Core::Config::WindowBackend::Null;
    config.Render.EnablePromotedVulkanDevice = false;
    config.ReferenceScene.Enabled = false;

    const std::filesystem::path path = TempConfigPath();
    const std::string pathText = path.string();
    WriteTextFile(path, Core::Config::SerializeEngineConfig(config));

    std::vector<std::string_view> args{"sandbox", "--engine-config", pathText};
    Runtime::EngineConfigBootOptions options{};
    options.DefaultConfigPath.clear();
    options.EnvironmentVariable.clear();

    const Runtime::EngineConfigBootResult boot =
        Runtime::ResolveEngineConfigForBoot(args, options);
    std::filesystem::remove(path);

    EXPECT_EQ(boot.Source, Runtime::EngineConfigBootSource::CommandLine);
    EXPECT_EQ(boot.SourcePath, pathText);
    EXPECT_TRUE(boot.LoadedFile);
    EXPECT_FALSE(boot.UsedReferenceFallback);
    EXPECT_EQ(boot.LoadResult.State, Core::Config::EngineConfigState::Valid);
    EXPECT_EQ(boot.Config.Window.Title, "CLI Config");
    EXPECT_EQ(boot.Config.Window.Width, 1024);
    EXPECT_EQ(boot.Config.Window.Height, 768);
    EXPECT_EQ(boot.Config.Window.Backend, Core::Config::WindowBackend::Null);
    EXPECT_FALSE(boot.Config.Render.EnablePromotedVulkanDevice);
    EXPECT_FALSE(boot.Config.ReferenceScene.Enabled);
}

TEST(RuntimeEngineConfigBoot, MissingExplicitPathFallsBackToReferenceConfig)
{
    const std::string missingPath =
        (std::filesystem::temp_directory_path()
         / "intrinsic_runtime_engine_config_missing.json").string();
    std::filesystem::remove(missingPath);

    std::vector<std::string_view> args{"sandbox", "--engine-config", missingPath};
    Runtime::EngineConfigBootOptions options{};
    options.DefaultConfigPath.clear();
    options.EnvironmentVariable.clear();

    const Runtime::EngineConfigBootResult boot =
        Runtime::ResolveEngineConfigForBoot(args, options);

    EXPECT_EQ(boot.Source, Runtime::EngineConfigBootSource::CommandLine);
    EXPECT_EQ(boot.SourcePath, missingPath);
    EXPECT_FALSE(boot.LoadedFile);
    EXPECT_TRUE(boot.UsedReferenceFallback);
    EXPECT_EQ(boot.LoadResult.State, Core::Config::EngineConfigState::Invalid);
    EXPECT_TRUE(Core::Config::HasDiagnostic(
        boot.LoadResult,
        Core::Config::EngineConfigDiagnosticCode::LoadError));
    EXPECT_EQ(boot.Config.Window.Width, Runtime::CreateReferenceEngineConfig().Window.Width);
}
