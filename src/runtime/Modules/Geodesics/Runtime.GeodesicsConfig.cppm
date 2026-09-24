// Persisted virtual-source geodesics inputs shared by UI and config callers.
module;
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
export module Extrinsic.Runtime.GeodesicsConfig;
export import Extrinsic.Runtime.GeometryProperty.Types;
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
        // Optional scalar vertex property (bool, integer or floating point,
        // e.g. v:feature); vertices with a finite nonzero value are added to
        // SourceVertices. An empty name binds nothing.
        GeometryPropertyRef SourceVertexProperty{GeometryElementDomain::MeshVertex, "", Geometry::PropertyValueKind::Bool};
        std::uint32_t MaxHalfedgeExpansions{10000000};
        // Vertex-domain float3 binding; topology still comes from the mesh.
        GeometryPropertyRef PositionProperty{GeometryElementDomain::MeshVertex, "v:position", Geometry::PropertyValueKind::Vec3};
        GeometryPropertyRef DistanceProperty{GeometryElementDomain::MeshVertex, "v:geodesic_distance", Geometry::PropertyValueKind::Double};
        GeometryPropertyRef SourceMaskProperty{GeometryElementDomain::MeshVertex, "v:is_geodesic_source", Geometry::PropertyValueKind::Bool};
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
