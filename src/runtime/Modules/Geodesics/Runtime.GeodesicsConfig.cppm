// Persisted virtual-source geodesics inputs shared by UI and config callers.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
export module Extrinsic.Runtime.GeodesicsConfig;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kGeodesicsConfigSectionName = "sandbox.geodesics";
    inline constexpr std::string_view kGeodesicsConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.geodesics";
    struct GeodesicsConfig
    {
        std::vector<std::uint32_t> SourceVertices{};
        std::uint32_t MaxHalfedgeExpansions{10000000};
        // Vertex-domain float3 binding; topology still comes from the mesh.
        std::string PositionProperty{"v:position"};
    };
    [[nodiscard]] std::string SerializeGeodesicsConfig(const GeodesicsConfig& config);
    [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateGeodesicsConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    [[nodiscard]] std::optional<GeodesicsConfig> GetGeodesicsConfig(
        const Core::Config::EngineConfig& config);
    void SetGeodesicsConfig(Core::Config::EngineConfig& config, const GeodesicsConfig& value);
    [[nodiscard]] Core::Config::EngineConfigSectionRegistration
    MakeGeodesicsConfigSectionRegistration();
}
