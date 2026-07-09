module;

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

module Extrinsic.Runtime.EngineConfigBoot;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Render;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::optional<std::string> FindCommandLineEngineConfigPath(
            const std::span<const std::string_view> args,
            const std::string_view flag)
        {
            if (flag.empty())
            {
                return std::nullopt;
            }

            const std::string equalsPrefix = std::string{flag} + "=";
            for (std::size_t index = 1; index < args.size(); ++index)
            {
                const std::string_view arg = args[index];
                if (arg == flag)
                {
                    if (index + 1 < args.size() && !args[index + 1].empty())
                    {
                        return std::string{args[index + 1]};
                    }
                    return std::nullopt;
                }
                if (arg.starts_with(equalsPrefix))
                {
                    const std::string_view value = arg.substr(equalsPrefix.size());
                    if (!value.empty())
                    {
                        return std::string{value};
                    }
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> FindEnvironmentEngineConfigPath(
            const std::string_view variableName)
        {
            if (variableName.empty())
            {
                return std::nullopt;
            }
            const char* value = std::getenv(std::string{variableName}.c_str());
            if (value == nullptr || value[0] == '\0')
            {
                return std::nullopt;
            }
            return std::string{value};
        }

        [[nodiscard]] bool ExistingFilePath(const std::string_view path)
        {
            if (path.empty())
            {
                return false;
            }
            std::error_code ec{};
            return std::filesystem::is_regular_file(std::filesystem::path{path}, ec) && !ec;
        }
    }

    Core::Config::EngineConfig CreateReferenceEngineConfig()
    {
        Core::Config::EngineConfig config{};
        config.Window.Title = "Modular Vulkan Engine";
        config.Window.Width = 1600;
        config.Window.Height = 900;
        config.Render.Backend = Core::Config::GraphicsBackend::Vulkan;
        config.Render.EnablePromotedVulkanDevice = true;
        config.Render.EnableValidation = true;
        config.Render.EnableVSync = true;
        config.Render.FramesInFlight = 2;
        config.ReferenceScene.Enabled = true;
        config.ReferenceScene.Selector = Core::Config::ReferenceSceneSelector::Triangle;
        return config;
    }

    EngineConfigBootResult ResolveEngineConfigForBoot(
        const std::span<const std::string_view> args,
        const EngineConfigBootOptions& options)
    {
        EngineConfigBootResult result{};
        result.Config = CreateReferenceEngineConfig();
        result.LoadResult.Preview.Config = result.Config;
        result.LoadResult.SourceId = "<reference>";

        std::optional<std::string> path =
            FindCommandLineEngineConfigPath(args, options.CliFlag);
        EngineConfigBootSource source = EngineConfigBootSource::CommandLine;

        if (!path.has_value())
        {
            path = FindEnvironmentEngineConfigPath(options.EnvironmentVariable);
            source = EngineConfigBootSource::Environment;
        }

        if (!path.has_value() && ExistingFilePath(options.DefaultConfigPath))
        {
            path = options.DefaultConfigPath;
            source = EngineConfigBootSource::DefaultPath;
        }

        if (!path.has_value())
        {
            return result;
        }

        result.Source = source;
        result.SourcePath = *path;
        result.LoadResult = Core::Config::LoadEngineConfigFile(
            *path,
            result.Config,
            Core::Config::EngineConfigParseOptions{.SourceId = *path});
        result.LoadedFile = Core::Config::IsConfigUsable(result.LoadResult);
        result.UsedReferenceFallback =
            result.LoadResult.State != Core::Config::EngineConfigState::Valid;

        if (result.LoadedFile)
        {
            result.Config = result.LoadResult.Preview.Config;
        }
        return result;
    }
}
