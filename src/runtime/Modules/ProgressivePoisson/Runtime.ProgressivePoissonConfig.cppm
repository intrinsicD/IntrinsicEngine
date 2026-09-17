// Persisted progressive Poisson playground controls and typed point bindings.
module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ProgressivePoissonConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;

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
        Rank = 1,
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
        bool AutoRunOnEdit{true};
        double DebounceSeconds{0.25};
        GeometryPropertyRef Positions{GeometryElementDomain::Unknown, "v:position", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef Level{GeometryElementDomain::Unknown, "v:poisson_level", Geometry::PropertyValueKind::Float};
        GeometryPropertyRef Rank{GeometryElementDomain::Unknown, "v:poisson_rank", Geometry::PropertyValueKind::Float};
        GeometryPropertyRef SplatRadius{GeometryElementDomain::Unknown, "v:poisson_splat_radius", Geometry::PropertyValueKind::Float};
        GeometryPropertyRef PrefixVisible{GeometryElementDomain::Unknown, "v:poisson_prefix_visible", Geometry::PropertyValueKind::Float};

    };

    // Defined by the shared config codec translation unit, which compiles the
    // JSON dependency once for every feature family.
    extern "C++"
    {
        [[nodiscard]] bool IsValidProgressivePoissonPropertyBindings(
            const ProgressivePoissonPlaygroundConfig& config) noexcept;

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
}
