module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ProgressivePoissonConfig;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kProgressivePoissonConfigSectionName =
        "sandbox.progressive_poisson";
    inline constexpr std::string_view kProgressivePoissonConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.progressive-poisson";
    inline constexpr std::uint32_t
        kProgressivePoissonConfigSectionSchemaVersion = 1u;

    enum class ProgressivePoissonPlaygroundChannel : std::uint32_t
    {
        Level = 0,
        Phase = 1,
        SplatRadius = 2,
        PrefixVisible = 3,
    };

    enum class ProgressivePoissonPlaygroundBackend : std::uint32_t
    {
        CpuReference = 0,
        VulkanCompute = 1,
    };

    struct ProgressivePoissonPlaygroundConfig
    {
        std::uint32_t Dimension{3u};
        std::uint32_t GridWidth{4u};
        std::uint32_t MaxLevels{16u};
        double HashLoadFactor{0.25};
        double RadiusAlpha{-1.0};
        bool RandomizeGridOrigin{true};
        std::uint32_t GridOriginSeed{1337u};
        bool ShuffleWithinLevels{true};
        std::uint32_t ShuffleSeed{0x51ed270bu};
        std::uint32_t PrefixCount{0u};
        ProgressivePoissonPlaygroundChannel Channel{
            ProgressivePoissonPlaygroundChannel::Level};
        ProgressivePoissonPlaygroundBackend Backend{
            ProgressivePoissonPlaygroundBackend::CpuReference};
        std::uint32_t MeshSurfaceSampleCount{4096u};
        std::uint32_t MeshSurfaceSampleSeed{1337u};
        double MeshSurfaceMinTriangleArea{1.0e-14};
        bool MeshSurfaceInterpolateNormals{true};
        bool AutoRunOnEdit{true};
        double DebounceSeconds{0.25};
    };

    [[nodiscard]] std::string SerializeProgressivePoissonPlaygroundConfig(
        const ProgressivePoissonPlaygroundConfig& config);

    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult
    ValidateProgressivePoissonConfigSection(
        std::string_view documentPayloadJson,
        std::string_view referencePayloadJson,
        std::string_view diagnosticSubject);

    [[nodiscard]] std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfig(
        const Core::Config::EngineConfig& config);

    void SetProgressivePoissonPlaygroundConfig(
        Core::Config::EngineConfig& config,
        const ProgressivePoissonPlaygroundConfig& value);

    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged = {});
}
