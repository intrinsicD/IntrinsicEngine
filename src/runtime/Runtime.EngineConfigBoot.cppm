module;

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.EngineConfigBoot;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Render;

namespace Extrinsic::Runtime
{
    export using RuntimeEngineConfig = Core::Config::EngineConfig;

    export [[nodiscard]] Core::Config::EngineConfig CreateReferenceEngineConfig();
    export [[nodiscard]] Core::Config::EngineConfig CreateReferenceEngineConfig(
        const Core::Config::EngineConfigSectionRegistry& sectionRegistry);

    export enum class EngineConfigBootSource : std::uint8_t
    {
        ReferenceDefaults = 0,
        DefaultPath,
        Environment,
        CommandLine,
    };

    export struct EngineConfigBootOptions
    {
        std::string DefaultConfigPath{"config/engine.json"};
        std::string EnvironmentVariable{"INTRINSIC_ENGINE_CONFIG"};
        std::string CliFlag{"--engine-config"};
    };

    export struct EngineConfigBootResult
    {
        Core::Config::EngineConfig Config{};
        EngineConfigBootSource Source{EngineConfigBootSource::ReferenceDefaults};
        std::string SourcePath{};
        Core::Config::EngineConfigLoadResult LoadResult{};
        bool LoadedFile{false};
        bool UsedReferenceFallback{true};
    };

    export [[nodiscard]] EngineConfigBootResult ResolveEngineConfigForBoot(
        std::span<const std::string_view> args,
        const EngineConfigBootOptions& options = {});

    export [[nodiscard]] EngineConfigBootResult ResolveEngineConfigForBoot(
        std::span<const std::string_view> args,
        const Core::Config::EngineConfigSectionRegistry& sectionRegistry,
        const EngineConfigBootOptions& options = {});
}
