module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.SceneSerialization;

import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.Light;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.ShadowCaster;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Geometry.Graph.Fwd;
import Geometry.Properties;
import Extrinsic.Runtime.GeometryPresentation;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace ECSC = Extrinsic::ECS::Components;
        namespace GS = Extrinsic::ECS::Components::GeometrySources;
        namespace Sel = Extrinsic::ECS::Components::Selection;
        namespace G = Extrinsic::Graphics::Components;
        namespace PN = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

        using json = nlohmann::json;

        // ------------------------------------------------------------------
        // RUNTIME-192 — legacy property value-kind wire format.
        //
        // These strings are a PERSISTED scene-document format. They predate the
        // canonical `Geometry::PropertyValueKind` vocabulary and deliberately do
        // NOT match its debug names: the wire says "ScalarFloat"/"ScalarDouble"
        // where the canonical names are "Float"/"Double". Changing them would
        // silently fail to load every existing scene document, so the mapping
        // lives here — owned by the serializer — rather than being derived from
        // `DebugNameForGeometryPropertyValueKind`.
        //
        // The filter form additionally persists "Any" for an unconstrained
        // expectation (`std::nullopt`).
        // ------------------------------------------------------------------
        constexpr std::pair<Geometry::PropertyValueKind, std::string_view>
            kPropertyValueKindWire[]{
                {Geometry::PropertyValueKind::Unknown, "Unknown"},
                {Geometry::PropertyValueKind::Float, "ScalarFloat"},
                {Geometry::PropertyValueKind::Double, "ScalarDouble"},
                {Geometry::PropertyValueKind::UInt32, "UInt32"},
                {Geometry::PropertyValueKind::Vec2, "Vec2"},
                {Geometry::PropertyValueKind::Vec3, "Vec3"},
                {Geometry::PropertyValueKind::Vec4, "Vec4"},
            };

        constexpr std::string_view kPropertyValueKindAnyWire = "Any";

        // Legacy property-domain wire strings. Like the value kinds above these
        // are persisted and do NOT match the canonical `GeometryElementDomain`
        // names: the wire says "GraphVertex"/"Point" where the canonical names
        // are GraphNode/PointCloudPoint. "MeshSurface" was a whole-surface job
        // target that every property path already treated as unsupported, so it
        // is accepted on read and mapped to `Unknown` — the same behavior — and
        // is never written back.
        constexpr std::pair<GeometryElementDomain, std::string_view>
            kPropertyDomainWire[]{
                {GeometryElementDomain::Unknown, "Unknown"},
                {GeometryElementDomain::MeshVertex, "MeshVertex"},
                {GeometryElementDomain::MeshEdge, "MeshEdge"},
                {GeometryElementDomain::MeshHalfedge, "MeshHalfedge"},
                {GeometryElementDomain::MeshFace, "MeshFace"},
                {GeometryElementDomain::GraphNode, "GraphVertex"},
                {GeometryElementDomain::GraphHalfedge, "GraphHalfedge"},
                {GeometryElementDomain::GraphEdge, "GraphEdge"},
                {GeometryElementDomain::PointCloudPoint, "Point"},
            };

        constexpr std::string_view kPropertyDomainLegacySurfaceWire = "MeshSurface";

        [[nodiscard]] std::string_view PropertyDomainToWire(
            const GeometryElementDomain value) noexcept
        {
            for (const auto& [domain, text] : kPropertyDomainWire)
            {
                if (domain == value)
                    return text;
            }
            return "Unknown";
        }

        [[nodiscard]] bool TryPropertyDomainFromWire(
            const std::string_view text,
            GeometryElementDomain& out) noexcept
        {
            if (text == kPropertyDomainLegacySurfaceWire)
            {
                out = GeometryElementDomain::Unknown;
                return true;
            }
            for (const auto& [domain, candidate] : kPropertyDomainWire)
            {
                if (candidate == text)
                {
                    out = domain;
                    return true;
                }
            }
            return false;
        }


        [[nodiscard]] std::string_view PropertyValueKindToWire(
            const Geometry::PropertyValueKind value) noexcept
        {
            for (const auto& [kind, text] : kPropertyValueKindWire)
            {
                if (kind == value)
                    return text;
            }
            return "Unknown";
        }

        [[nodiscard]] bool TryPropertyValueKindFromWire(
            const std::string_view text,
            Geometry::PropertyValueKind& out) noexcept
        {
            for (const auto& [kind, candidate] : kPropertyValueKindWire)
            {
                if (candidate == text)
                {
                    out = kind;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool TryPropertyValueKindFilterFromWire(
            const std::string_view text,
            GeometryPropertyValueKindFilter& out) noexcept
        {
            if (text == kPropertyValueKindAnyWire)
            {
                out = std::nullopt;
                return true;
            }
            Geometry::PropertyValueKind kind{};
            if (!TryPropertyValueKindFromWire(text, kind))
                return false;
            out = kind;
            return true;
        }

        // Version 2 makes graph halfedge connectivity mandatory. Version 1
        // graph documents contained only nodes and edges and cannot satisfy
        // the unified source contract without synthesizing topology.
        constexpr std::uint32_t kSceneDocumentVersion = 2u;
        constexpr std::uint32_t kInvalidSerializedId = 0xFFFFFFFFu;

        [[nodiscard]] std::uint32_t EntitySortKey(const ECS::EntityHandle entity) noexcept
        {
            return static_cast<std::uint32_t>(entity);
        }

        [[nodiscard]] bool IsArrayOfSize(const json& value,
                                         const std::size_t expected) noexcept
        {
            return value.is_array() && value.size() == expected;
        }

        [[nodiscard]] json Vec2ToJson(const glm::vec2& value)
        {
            return json::array({value.x, value.y});
        }

        [[nodiscard]] json Vec3ToJson(const glm::vec3& value)
        {
            return json::array({value.x, value.y, value.z});
        }

        [[nodiscard]] json Vec4ToJson(const glm::vec4& value)
        {
            return json::array({value.x, value.y, value.z, value.w});
        }

        [[nodiscard]] json QuatToJson(const glm::quat& value)
        {
            return json::array({value.w, value.x, value.y, value.z});
        }

        [[nodiscard]] json AssetIdToJson(const Assets::AssetId asset)
        {
            if (!asset.IsValid())
                return nullptr;
            return json{
                {"index", asset.Index},
                {"generation", asset.Generation},
            };
        }

        [[nodiscard]] bool TryReadAssetId(const json& value, Assets::AssetId& out) noexcept
        {
            out = {};
            if (value.is_null())
                return true;
            if (!value.is_object() ||
                !value.contains("index") || !value["index"].is_number_unsigned() ||
                !value.contains("generation") || !value["generation"].is_number_unsigned())
            {
                return false;
            }
            const std::uint64_t index = value["index"].get<std::uint64_t>();
            const std::uint64_t generation = value["generation"].get<std::uint64_t>();
            if (index > UINT32_MAX || generation > UINT32_MAX)
                return false;
            out = Assets::AssetId{static_cast<std::uint32_t>(index),
                                  static_cast<std::uint32_t>(generation)};
            return true;
        }

        [[nodiscard]] bool TryReadVec2(const json& value, glm::vec2& out) noexcept
        {
            if (!IsArrayOfSize(value, 2u))
                return false;
            for (const json& element : value)
            {
                if (!element.is_number())
                    return false;
            }
            out = glm::vec2{
                value[0].get<float>(),
                value[1].get<float>(),
            };
            return true;
        }

        [[nodiscard]] bool TryReadVec3(const json& value, glm::vec3& out) noexcept
        {
            if (!IsArrayOfSize(value, 3u))
                return false;
            for (const json& element : value)
            {
                if (!element.is_number())
                    return false;
            }
            out = glm::vec3{
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>(),
            };
            return true;
        }

        [[nodiscard]] bool TryReadVec4(const json& value, glm::vec4& out) noexcept
        {
            if (!IsArrayOfSize(value, 4u))
                return false;
            for (const json& element : value)
            {
                if (!element.is_number())
                    return false;
            }
            out = glm::vec4{
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>(),
                value[3].get<float>(),
            };
            return true;
        }

        [[nodiscard]] json GeometryPresentationDefaultValueToJson(
            const GeometryPresentationDefaultValue& value)
        {
            return json{
                {"kind", std::string(PropertyValueKindToWire(value.Kind))},
                {"vector", Vec4ToJson(value.Vector)},
                {"scalar", value.Scalar},
                {"uint", value.UInt},
            };
        }

        [[nodiscard]] bool TryReadGeometryPresentationDefaultValue(
            const json& value,
            GeometryPresentationDefaultValue& out)
        {
            if (!value.is_object())
                return false;
            if (value.contains("kind"))
            {
                if (!value["kind"].is_string() ||
                    !TryPropertyValueKindFromWire(value["kind"].get<std::string>(),
                                                  out.Kind))
                {
                    return false;
                }
            }
            if (value.contains("vector") && !TryReadVec4(value["vector"], out.Vector))
                return false;
            if (value.contains("scalar"))
            {
                if (!value["scalar"].is_number())
                    return false;
                out.Scalar = value["scalar"].get<double>();
            }
            if (value.contains("uint"))
            {
                if (!value["uint"].is_number_unsigned())
                    return false;
                const std::uint64_t wide = value["uint"].get<std::uint64_t>();
                if (wide > UINT32_MAX)
                    return false;
                out.UInt = static_cast<std::uint32_t>(wide);
            }
            return true;
        }

        [[nodiscard]] bool TryReadQuat(const json& value, glm::quat& out) noexcept
        {
            if (!IsArrayOfSize(value, 4u))
                return false;
            for (const json& element : value)
            {
                if (!element.is_number())
                    return false;
            }
            out = glm::quat{
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>(),
                value[3].get<float>(),
            };
            return true;
        }

        [[nodiscard]] json Vec2ArrayToJson(const std::vector<glm::vec2>& values)
        {
            json out = json::array();
            for (const glm::vec2& value : values)
                out.push_back(Vec2ToJson(value));
            return out;
        }

        [[nodiscard]] json Vec3ArrayToJson(const std::vector<glm::vec3>& values)
        {
            json out = json::array();
            for (const glm::vec3& value : values)
                out.push_back(Vec3ToJson(value));
            return out;
        }

        [[nodiscard]] json UIntArrayToJson(const std::vector<std::uint32_t>& values)
        {
            json out = json::array();
            for (const std::uint32_t value : values)
                out.push_back(value);
            return out;
        }

        [[nodiscard]] bool TryReadVec2Array(const json& value,
                                            std::vector<glm::vec2>& out)
        {
            if (!value.is_array())
                return false;
            std::vector<glm::vec2> decoded;
            decoded.reserve(value.size());
            for (const json& element : value)
            {
                glm::vec2 vec{0.0f};
                if (!TryReadVec2(element, vec))
                    return false;
                decoded.push_back(vec);
            }
            out = std::move(decoded);
            return true;
        }

        [[nodiscard]] bool TryReadVec3Array(const json& value,
                                            std::vector<glm::vec3>& out)
        {
            if (!value.is_array())
                return false;
            std::vector<glm::vec3> decoded;
            decoded.reserve(value.size());
            for (const json& element : value)
            {
                glm::vec3 vec{0.0f};
                if (!TryReadVec3(element, vec))
                    return false;
                decoded.push_back(vec);
            }
            out = std::move(decoded);
            return true;
        }

        [[nodiscard]] bool TryReadUIntArray(const json& value,
                                            std::vector<std::uint32_t>& out)
        {
            if (!value.is_array())
                return false;
            std::vector<std::uint32_t> decoded;
            decoded.reserve(value.size());
            for (const json& element : value)
            {
                if (!element.is_number_unsigned())
                    return false;
                const std::uint64_t wide = element.get<std::uint64_t>();
                if (wide > UINT32_MAX)
                    return false;
                decoded.push_back(static_cast<std::uint32_t>(wide));
            }
            out = std::move(decoded);
            return true;
        }

        [[nodiscard]] bool ReadOptionalDeleted(const json& object,
                                               std::size_t& out) noexcept
        {
            out = 0u;
            if (!object.contains("deleted"))
                return true;
            const json& deleted = object["deleted"];
            if (!deleted.is_number_unsigned())
                return false;
            const std::uint64_t wide = deleted.get<std::uint64_t>();
            if (wide > static_cast<std::uint64_t>(SIZE_MAX))
                return false;
            out = static_cast<std::size_t>(wide);
            return true;
        }

        [[nodiscard]] const std::vector<glm::vec2>* FindVec2Property(
            const Geometry::PropertySet& properties,
            const std::string_view name)
        {
            const Geometry::ConstProperty<glm::vec2> property =
                properties.Get<glm::vec2>(name);
            return property.IsValid() ? &property.Vector() : nullptr;
        }

        [[nodiscard]] const std::vector<glm::vec3>* FindVec3Property(
            const Geometry::PropertySet& properties,
            const std::string_view name)
        {
            const Geometry::ConstProperty<glm::vec3> property =
                properties.Get<glm::vec3>(name);
            return property.IsValid() ? &property.Vector() : nullptr;
        }

        [[nodiscard]] const std::vector<std::uint32_t>* FindUIntProperty(
            const Geometry::PropertySet& properties,
            const std::string_view name)
        {
            const Geometry::ConstProperty<std::uint32_t> property =
                properties.Get<std::uint32_t>(name);
            return property.IsValid() ? &property.Vector() : nullptr;
        }

        [[nodiscard]] bool AddVec2Property(json& object,
                                           const char* key,
                                           const Geometry::PropertySet& properties,
                                           const std::string_view propertyName,
                                           const bool required)
        {
            const std::vector<glm::vec2>* values = FindVec2Property(properties, propertyName);
            if (values == nullptr)
                return !required;
            object[key] = Vec2ArrayToJson(*values);
            return true;
        }

        [[nodiscard]] bool AddVec3Property(json& object,
                                           const char* key,
                                           const Geometry::PropertySet& properties,
                                           const std::string_view propertyName,
                                           const bool required)
        {
            const std::vector<glm::vec3>* values = FindVec3Property(properties, propertyName);
            if (values == nullptr)
                return !required;
            object[key] = Vec3ArrayToJson(*values);
            return true;
        }

        [[nodiscard]] bool AddUIntProperty(json& object,
                                           const char* key,
                                           const Geometry::PropertySet& properties,
                                           const std::string_view propertyName,
                                           const bool required)
        {
            const std::vector<std::uint32_t>* values = FindUIntProperty(properties, propertyName);
            if (values == nullptr)
                return !required;
            object[key] = UIntArrayToJson(*values);
            return true;
        }

        void WriteVec2Property(Geometry::PropertySet& properties,
                               const std::string_view name,
                               std::vector<glm::vec2> values)
        {
            properties.Resize(values.size());
            Geometry::Property<glm::vec2> property =
                properties.GetOrAdd<glm::vec2>(std::string{name}, glm::vec2{0.0f});
            property.Vector() = std::move(values);
        }

        void WriteVec3Property(Geometry::PropertySet& properties,
                               const std::string_view name,
                               std::vector<glm::vec3> values)
        {
            properties.Resize(values.size());
            Geometry::Property<glm::vec3> property =
                properties.GetOrAdd<glm::vec3>(std::string{name}, glm::vec3{0.0f});
            property.Vector() = std::move(values);
        }

        void WriteUIntProperty(Geometry::PropertySet& properties,
                               const std::string_view name,
                               std::vector<std::uint32_t> values)
        {
            properties.Resize(values.size());
            Geometry::Property<std::uint32_t> property =
                properties.GetOrAdd<std::uint32_t>(std::string{name}, 0u);
            property.Vector() = std::move(values);
        }

        [[nodiscard]] const char* DomainToString(const GS::Domain domain) noexcept
        {
            switch (domain)
            {
            case GS::Domain::Mesh: return "Mesh";
            case GS::Domain::Graph: return "Graph";
            case GS::Domain::PointCloud: return "PointCloud";
            case GS::Domain::None: return "None";
            case GS::Domain::Unknown: return "Unknown";
            }
            return "Unknown";
        }

        [[nodiscard]] bool TryDomainFromString(const std::string_view value,
                                               GS::Domain& out) noexcept
        {
            if (value == "Mesh")
                out = GS::Domain::Mesh;
            else if (value == "Graph")
                out = GS::Domain::Graph;
            else if (value == "PointCloud")
                out = GS::Domain::PointCloud;
            else if (value == "None")
                out = GS::Domain::None;
            else
                return false;
            return true;
        }

        [[nodiscard]] const char* SurfaceDomainToString(
            const G::RenderSurface::SourceDomain domain) noexcept
        {
            return domain == G::RenderSurface::SourceDomain::Face
                ? "Face"
                : "Vertex";
        }

        [[nodiscard]] bool TrySurfaceDomainFromString(
            const std::string_view value,
            G::RenderSurface::SourceDomain& out) noexcept
        {
            if (value == "Face")
                out = G::RenderSurface::SourceDomain::Face;
            else if (value == "Vertex")
                out = G::RenderSurface::SourceDomain::Vertex;
            else
                return false;
            return true;
        }

        [[nodiscard]] const char* EdgeDomainToString(
            const G::RenderEdges::SourceDomain domain) noexcept
        {
            return domain == G::RenderEdges::SourceDomain::Edge
                ? "Edge"
                : "Vertex";
        }

        [[nodiscard]] bool TryEdgeDomainFromString(
            const std::string_view value,
            G::RenderEdges::SourceDomain& out) noexcept
        {
            if (value == "Edge")
                out = G::RenderEdges::SourceDomain::Edge;
            else if (value == "Vertex")
                out = G::RenderEdges::SourceDomain::Vertex;
            else
                return false;
            return true;
        }

        [[nodiscard]] const char* PointTypeToString(
            const G::RenderPoints::RenderType type) noexcept
        {
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat: return "Flat";
            case G::RenderPoints::RenderType::Sphere: return "Sphere";
            case G::RenderPoints::RenderType::Surfel: return "Surfel";
            }
            return "Sphere";
        }

        [[nodiscard]] bool TryPointTypeFromString(const std::string_view value,
                                                  G::RenderPoints::RenderType& out) noexcept
        {
            if (value == "Flat")
                out = G::RenderPoints::RenderType::Flat;
            else if (value == "Sphere")
                out = G::RenderPoints::RenderType::Sphere;
            else if (value == "Surfel")
                out = G::RenderPoints::RenderType::Surfel;
            else
                return false;
            return true;
        }

        [[nodiscard]] const char* VisualizationColorSourceToString(
            const G::VisualizationConfig::ColorSource source) noexcept
        {
            switch (source)
            {
            case G::VisualizationConfig::ColorSource::Material: return "Material";
            case G::VisualizationConfig::ColorSource::UniformColor: return "UniformColor";
            case G::VisualizationConfig::ColorSource::ScalarField: return "ScalarField";
            case G::VisualizationConfig::ColorSource::PerVertexBuffer: return "PerVertexBuffer";
            case G::VisualizationConfig::ColorSource::PerEdgeBuffer: return "PerEdgeBuffer";
            case G::VisualizationConfig::ColorSource::PerFaceBuffer: return "PerFaceBuffer";
            }
            return "Material";
        }

        [[nodiscard]] bool TryVisualizationColorSourceFromString(
            const std::string_view value,
            G::VisualizationConfig::ColorSource& out) noexcept
        {
            if (value == "Material")
                out = G::VisualizationConfig::ColorSource::Material;
            else if (value == "UniformColor")
                out = G::VisualizationConfig::ColorSource::UniformColor;
            else if (value == "ScalarField")
                out = G::VisualizationConfig::ColorSource::ScalarField;
            else if (value == "PerVertexBuffer")
                out = G::VisualizationConfig::ColorSource::PerVertexBuffer;
            else if (value == "PerEdgeBuffer")
                out = G::VisualizationConfig::ColorSource::PerEdgeBuffer;
            else if (value == "PerFaceBuffer")
                out = G::VisualizationConfig::ColorSource::PerFaceBuffer;
            else
                return false;
            return true;
        }

        [[nodiscard]] const char* VisualizationDomainToString(
            const G::VisualizationConfig::Domain domain) noexcept
        {
            switch (domain)
            {
            case G::VisualizationConfig::Domain::Vertex: return "Vertex";
            case G::VisualizationConfig::Domain::Edge: return "Edge";
            case G::VisualizationConfig::Domain::Face: return "Face";
            }
            return "Vertex";
        }

        [[nodiscard]] bool TryVisualizationDomainFromString(
            const std::string_view value,
            G::VisualizationConfig::Domain& out) noexcept
        {
            if (value == "Vertex")
                out = G::VisualizationConfig::Domain::Vertex;
            else if (value == "Edge")
                out = G::VisualizationConfig::Domain::Edge;
            else if (value == "Face")
                out = G::VisualizationConfig::Domain::Face;
            else
                return false;
            return true;
        }

        [[nodiscard]] const char* ColormapToString(
            const Graphics::Colormap::Type map) noexcept
        {
            switch (map)
            {
            case Graphics::Colormap::Type::Viridis: return "Viridis";
            case Graphics::Colormap::Type::Inferno: return "Inferno";
            case Graphics::Colormap::Type::Plasma: return "Plasma";
            case Graphics::Colormap::Type::Jet: return "Jet";
            case Graphics::Colormap::Type::Coolwarm: return "Coolwarm";
            case Graphics::Colormap::Type::Heat: return "Heat";
            case Graphics::Colormap::Type::Count: break;
            }
            return "Viridis";
        }

        [[nodiscard]] bool TryColormapFromString(
            const std::string_view value,
            Graphics::Colormap::Type& out) noexcept
        {
            if (value == "Viridis")
                out = Graphics::Colormap::Type::Viridis;
            else if (value == "Inferno")
                out = Graphics::Colormap::Type::Inferno;
            else if (value == "Plasma")
                out = Graphics::Colormap::Type::Plasma;
            else if (value == "Jet")
                out = Graphics::Colormap::Type::Jet;
            else if (value == "Coolwarm")
                out = Graphics::Colormap::Type::Coolwarm;
            else if (value == "Heat")
                out = Graphics::Colormap::Type::Heat;
            else
                return false;
            return true;
        }

        [[nodiscard]] json SizeOrWidthSourceToJson(
            const std::variant<float, std::string>& value)
        {
            if (const float* uniform = std::get_if<float>(&value))
            {
                return json{
                    {"kind", "uniform"},
                    {"value", *uniform},
                };
            }
            return json{
                {"kind", "property"},
                {"name", *std::get_if<std::string>(&value)},
            };
        }

        [[nodiscard]] bool TrySourceFromJson(
            const json& value,
            std::variant<float, std::string>& out)
        {
            if (!value.is_object() || !value.contains("kind") || !value["kind"].is_string())
                return false;
            const std::string kind = value["kind"].get<std::string>();
            if (kind == "uniform")
            {
                if (!value.contains("value") || !value["value"].is_number())
                    return false;
                out = value["value"].get<float>();
                return true;
            }
            if (kind == "property")
            {
                if (!value.contains("name") || !value["name"].is_string())
                    return false;
                out = value["name"].get<std::string>();
                return true;
            }
            return false;
        }

        [[nodiscard]] json VisualizationConfigToJson(
            const G::VisualizationConfig& config)
        {
            return json{
                {"source", VisualizationColorSourceToString(config.Source)},
                {"color", Vec4ToJson(config.Color)},
                {"scalarFieldName", config.ScalarFieldName},
                {"scalarDomain", VisualizationDomainToString(config.ScalarDomain)},
                {"colorBufferName", config.ColorBufferName},
                {"scalar", json{
                    {"map", ColormapToString(config.Scalar.Map)},
                    {"autoRange", config.Scalar.AutoRange},
                    {"rangeMin", config.Scalar.RangeMin},
                    {"rangeMax", config.Scalar.RangeMax},
                    {"binCount", config.Scalar.BinCount},
                    {"isolines", json{
                        {"num", config.Scalar.Isolines.Num},
                        {"color", Vec4ToJson(config.Scalar.Isolines.Color)},
                        {"width", config.Scalar.Isolines.Width},
                        {"values", json(std::vector<float>(
                            config.Scalar.Isolines.Values.begin(),
                            config.Scalar.Isolines.Values.begin() +
                                std::min<std::size_t>(
                                    config.Scalar.Isolines.ValueCount,
                                    config.Scalar.Isolines.Values.size())))},
                    }},
                }},
            };
        }

        [[nodiscard]] bool ReadVisualizationConfigFromJson(
            const json& value,
            G::VisualizationConfig& config)
        {
            if (!value.is_object() ||
                !value.contains("source") ||
                !value["source"].is_string())
            {
                return false;
            }

            if (!TryVisualizationColorSourceFromString(value["source"].get<std::string>(),
                                                       config.Source))
            {
                return false;
            }

            if (value.contains("color") && !TryReadVec4(value["color"], config.Color))
                return false;

            if (value.contains("scalarFieldName"))
            {
                if (!value["scalarFieldName"].is_string())
                    return false;
                config.ScalarFieldName = value["scalarFieldName"].get<std::string>();
            }

            if (value.contains("scalarDomain"))
            {
                if (!value["scalarDomain"].is_string() ||
                    !TryVisualizationDomainFromString(value["scalarDomain"].get<std::string>(),
                                                      config.ScalarDomain))
                {
                    return false;
                }
            }

            if (value.contains("colorBufferName"))
            {
                if (!value["colorBufferName"].is_string())
                    return false;
                config.ColorBufferName = value["colorBufferName"].get<std::string>();
            }

            if (value.contains("scalar"))
            {
                const json& scalar = value["scalar"];
                if (!scalar.is_object())
                    return false;

                if (scalar.contains("map"))
                {
                    if (!scalar["map"].is_string() ||
                        !TryColormapFromString(scalar["map"].get<std::string>(),
                                               config.Scalar.Map))
                    {
                        return false;
                    }
                }

                if (scalar.contains("autoRange"))
                {
                    if (!scalar["autoRange"].is_boolean())
                        return false;
                    config.Scalar.AutoRange = scalar["autoRange"].get<bool>();
                }

                if (scalar.contains("rangeMin"))
                {
                    if (!scalar["rangeMin"].is_number())
                        return false;
                    config.Scalar.RangeMin = scalar["rangeMin"].get<float>();
                }

                if (scalar.contains("rangeMax"))
                {
                    if (!scalar["rangeMax"].is_number())
                        return false;
                    config.Scalar.RangeMax = scalar["rangeMax"].get<float>();
                }

                if (scalar.contains("binCount"))
                {
                    if (!scalar["binCount"].is_number_unsigned())
                        return false;
                    const std::uint64_t bins = scalar["binCount"].get<std::uint64_t>();
                    if (bins > UINT32_MAX)
                        return false;
                    config.Scalar.BinCount = static_cast<std::uint32_t>(bins);
                }

                if (scalar.contains("isolines"))
                {
                    const json& isolines = scalar["isolines"];
                    if (!isolines.is_object())
                        return false;

                    if (isolines.contains("num"))
                    {
                        if (!isolines["num"].is_number_unsigned())
                            return false;
                        const std::uint64_t num = isolines["num"].get<std::uint64_t>();
                        if (num > UINT32_MAX)
                            return false;
                        config.Scalar.Isolines.Num = static_cast<std::uint32_t>(num);
                    }

                    if (isolines.contains("color") &&
                        !TryReadVec4(isolines["color"], config.Scalar.Isolines.Color))
                    {
                        return false;
                    }

                    if (isolines.contains("width"))
                    {
                        if (!isolines["width"].is_number())
                            return false;
                        config.Scalar.Isolines.Width = isolines["width"].get<float>();
                    }

                    // UI-032 — bounded explicit highlight isovalues.
                    if (isolines.contains("values"))
                    {
                        const json& values = isolines["values"];
                        if (!values.is_array() ||
                            values.size() >
                                G::ScalarFieldConfig::kMaxIsolineValues)
                        {
                            return false;
                        }
                        std::uint32_t count = 0u;
                        for (const json& entry : values)
                        {
                            if (!entry.is_number())
                                return false;
                            config.Scalar.Isolines.Values[count] =
                                entry.get<float>();
                            ++count;
                        }
                        config.Scalar.Isolines.ValueCount = count;
                    }
                }
            }

            return true;
        }

        [[nodiscard]] bool ApplyVisualizationConfigFromJson(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const json& value)
        {
            G::VisualizationConfig config{};
            if (!ReadVisualizationConfigFromJson(value, config))
                return false;

            raw.emplace_or_replace<G::VisualizationConfig>(entity, std::move(config));
            return true;
        }

        [[nodiscard]] json VisualizationLaneOverridesToJson(
            const G::VisualizationLaneOverrides& overrides)
        {
            json lanes = json::object();
            if (overrides.Surface.has_value())
                lanes["surface"] = VisualizationConfigToJson(*overrides.Surface);
            if (overrides.Edges.has_value())
                lanes["edges"] = VisualizationConfigToJson(*overrides.Edges);
            if (overrides.Points.has_value())
                lanes["points"] = VisualizationConfigToJson(*overrides.Points);
            return lanes;
        }

        [[nodiscard]] bool ApplyVisualizationLaneOverridesFromJson(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const json& value)
        {
            if (!value.is_object())
                return false;

            G::VisualizationLaneOverrides overrides{};
            const auto readLane =
                [&value](const char* key,
                         std::optional<G::VisualizationConfig>& out)
                {
                    if (!value.contains(key))
                        return true;
                    G::VisualizationConfig config{};
                    if (!ReadVisualizationConfigFromJson(value[key], config))
                        return false;
                    out = std::move(config);
                    return true;
                };

            if (!readLane("surface", overrides.Surface) ||
                !readLane("edges", overrides.Edges) ||
                !readLane("points", overrides.Points))
            {
                return false;
            }

            if (!overrides.Empty())
                raw.emplace_or_replace<G::VisualizationLaneOverrides>(
                    entity,
                    std::move(overrides));
            return true;
        }

        [[nodiscard]] json RenderHintsToJson(const entt::registry& raw,
                                             const ECS::EntityHandle entity,
                                             SceneSerializationStats& stats)
        {
            json render = json::object();
            bool hasAny = false;

            if (const auto* surface = raw.try_get<G::RenderSurface>(entity))
            {
                render["surface"] = json{
                    {"domain", SurfaceDomainToString(surface->Domain)},
                };
                hasAny = true;
            }

            if (const auto* edges = raw.try_get<G::RenderEdges>(entity))
            {
                render["edges"] = json{
                    {"domain", EdgeDomainToString(edges->Domain)},
                    {"width", SizeOrWidthSourceToJson(edges->WidthSource)},
                };
                hasAny = true;
            }

            if (const auto* points = raw.try_get<G::RenderPoints>(entity))
            {
                render["points"] = json{
                    {"type", PointTypeToString(points->Type)},
                    {"size", SizeOrWidthSourceToJson(points->SizeSource)},
                };
                hasAny = true;
            }

            if (const auto* visualization = raw.try_get<G::VisualizationConfig>(entity))
            {
                render["visualization"] = VisualizationConfigToJson(*visualization);
                hasAny = true;
            }
            if (const auto* overrides =
                    raw.try_get<G::VisualizationLaneOverrides>(entity);
                overrides != nullptr && !overrides->Empty())
            {
                render["visualizationLanes"] =
                    VisualizationLaneOverridesToJson(*overrides);
                hasAny = true;
            }

            if (hasAny)
                ++stats.RenderHintEntities;
            return render;
        }

        [[nodiscard]] bool ApplyRenderHintsFromJson(entt::registry& raw,
                                                    const ECS::EntityHandle entity,
                                                    const json& render,
                                                    SceneSerializationStats& stats)
        {
            if (!render.is_object())
                return false;

            bool hasAny = false;
            if (render.contains("surface"))
            {
                const json& surfaceJson = render["surface"];
                if (!surfaceJson.is_object() ||
                    !surfaceJson.contains("domain") ||
                    !surfaceJson["domain"].is_string())
                {
                    return false;
                }

                G::RenderSurface surface{};
                if (!TrySurfaceDomainFromString(surfaceJson["domain"].get<std::string>(),
                                                surface.Domain))
                {
                    return false;
                }
                raw.emplace_or_replace<G::RenderSurface>(entity, surface);
                hasAny = true;
            }

            if (render.contains("edges") || render.contains("lines"))
            {
                const json& edgesJson =
                    render.contains("edges") ? render["edges"] : render["lines"];
                if (!edgesJson.is_object() ||
                    !edgesJson.contains("domain") ||
                    !edgesJson["domain"].is_string() ||
                    !edgesJson.contains("width"))
                {
                    return false;
                }

                G::RenderEdges edges{};
                if (!TryEdgeDomainFromString(edgesJson["domain"].get<std::string>(),
                                             edges.Domain) ||
                    !TrySourceFromJson(edgesJson["width"], edges.WidthSource))
                {
                    return false;
                }
                raw.emplace_or_replace<G::RenderEdges>(entity, std::move(edges));
                hasAny = true;
            }

            if (render.contains("points"))
            {
                const json& pointsJson = render["points"];
                if (!pointsJson.is_object() ||
                    !pointsJson.contains("type") ||
                    !pointsJson["type"].is_string() ||
                    !pointsJson.contains("size"))
                {
                    return false;
                }

                G::RenderPoints points{};
                if (!TryPointTypeFromString(pointsJson["type"].get<std::string>(),
                                            points.Type) ||
                    !TrySourceFromJson(pointsJson["size"], points.SizeSource))
                {
                    return false;
                }
                raw.emplace_or_replace<G::RenderPoints>(entity, std::move(points));
                hasAny = true;
            }

            if (render.contains("visualization"))
            {
                if (!ApplyVisualizationConfigFromJson(raw,
                                                      entity,
                                                      render["visualization"]))
                {
                    return false;
                }
                hasAny = true;
            }
            if (render.contains("visualizationLanes"))
            {
                if (!ApplyVisualizationLaneOverridesFromJson(
                        raw,
                        entity,
                        render["visualizationLanes"]))
                {
                    return false;
                }
                hasAny = true;
            }

            if (hasAny)
                ++stats.RenderHintEntities;
            return true;
        }

        [[nodiscard]] json GeometryPropertyRefToJson(
            const GeometryPropertyRef& property)
        {
            return json{
                {"domain", std::string(PropertyDomainToWire(property.Domain))},
                {"name", property.Name},
                {"valueKind", std::string(PropertyValueKindToWire(property.ValueKind))},
            };
        }

        [[nodiscard]] bool TryReadGeometryPropertyRef(
            const json& value,
            GeometryPropertyRef& out)
        {
            if (!value.is_object() ||
                !value.contains("domain") || !value["domain"].is_string())
            {
                return false;
            }
            if (!TryPropertyDomainFromWire(value["domain"].get<std::string>(),
                                           out.Domain))
            {
                return false;
            }

            const json* name = nullptr;
            if (value.contains("name"))
                name = &value["name"];
            else if (value.contains("propertyName"))
                name = &value["propertyName"];
            if (name == nullptr || !name->is_string())
                return false;
            out.Name = name->get<std::string>();

            out.ValueKind = Geometry::PropertyValueKind::Unknown;
            if (value.contains("valueKind"))
            {
                if (!value["valueKind"].is_string() ||
                    !TryPropertyValueKindFromWire(
                        value["valueKind"].get<std::string>(),
                        out.ValueKind))
                {
                    return false;
                }
            }
            else if (value.contains("expectedValueKind"))
            {
                GeometryPropertyValueKindFilter legacyKind{};
                if (!value["expectedValueKind"].is_string() ||
                    !TryPropertyValueKindFilterFromWire(
                        value["expectedValueKind"].get<std::string>(),
                        legacyKind))
                {
                    return false;
                }
                out.ValueKind = legacyKind.value_or(
                    Geometry::PropertyValueKind::Unknown);
            }
            return true;
        }

        [[nodiscard]] json GeometryPresentationSlotToJson(
            const GeometryPresentationSlotRecipe& slot)
        {
            return json{
                {"semantic", std::string(ToString(slot.Semantic))},
                {"sourceKind", std::string(ToString(slot.SourceKind))},
                {"enabled", slot.Enabled},
                {"uniformDefault", GeometryPresentationDefaultValueToJson(slot.UniformDefault)},
                {"property", GeometryPropertyRefToJson(slot.Property)},
                {"textureAsset", AssetIdToJson(slot.TextureAsset)},
                {"generatedOutputName", slot.GeneratedOutputName},
                {"textureColormap", ColormapToString(slot.TextureColormap)},
                {"normalSpace", std::string(ToString(slot.NormalSpace))},
                {"generatedPolicy", std::string(ToString(slot.GeneratedPolicy))},
            };
        }

        [[nodiscard]] bool TryReadGeometryPresentationSlot(
            const json& value,
            GeometryPresentationSlotRecipe& out)
        {
            if (!value.is_object() ||
                !value.contains("semantic") || !value["semantic"].is_string() ||
                !value.contains("sourceKind") || !value["sourceKind"].is_string())
            {
                return false;
            }
            if (!TryParseGeometryPresentationSlotSemantic(
                    value["semantic"].get<std::string>(), out.Semantic) ||
                !TryParseGeometryPresentationSourceKind(
                    value["sourceKind"].get<std::string>(), out.SourceKind))
            {
                return false;
            }
            out.GeneratedPolicy = DefaultGeometryGeneratedOutputPolicyFor(out.SourceKind);

            if (value.contains("enabled"))
            {
                if (!value["enabled"].is_boolean())
                    return false;
                out.Enabled = value["enabled"].get<bool>();
            }
            if (value.contains("uniformDefault") &&
                !TryReadGeometryPresentationDefaultValue(value["uniformDefault"], out.UniformDefault))
            {
                return false;
            }
            if (value.contains("property") &&
                !TryReadGeometryPropertyRef(value["property"], out.Property))
            {
                return false;
            }
            if (value.contains("textureAsset") &&
                !TryReadAssetId(value["textureAsset"], out.TextureAsset))
            {
                return false;
            }
            else if (out.SourceKind ==
                         GeometryPresentationSourceKind::AuthoredTextureAsset &&
                     value.contains("authoredTexture") &&
                     !TryReadAssetId(value["authoredTexture"], out.TextureAsset))
            {
                return false;
            }
            else if (out.SourceKind ==
                         GeometryPresentationSourceKind::GeneratedTextureAsset &&
                     value.contains("generatedTexture") &&
                     !TryReadAssetId(value["generatedTexture"], out.TextureAsset))
            {
                return false;
            }
            if (value.contains("generatedOutputName"))
            {
                if (!value["generatedOutputName"].is_string())
                    return false;
                out.GeneratedOutputName =
                    value["generatedOutputName"].get<std::string>();
            }
            if (value.contains("textureColormap"))
            {
                if (!value["textureColormap"].is_string() ||
                    !TryColormapFromString(
                        value["textureColormap"].get<std::string>(),
                        out.TextureColormap))
                {
                    return false;
                }
            }
            if (value.contains("normalSpace"))
            {
                if (!value["normalSpace"].is_string() ||
                    !TryParseGeometryPresentationNormalSpace(
                        value["normalSpace"].get<std::string>(),
                        out.NormalSpace))
                {
                    return false;
                }
            }
            if (value.contains("generatedPolicy"))
            {
                if (!value["generatedPolicy"].is_string() ||
                    !TryParseGeometryGeneratedOutputPolicy(
                        value["generatedPolicy"].get<std::string>(),
                        out.GeneratedPolicy))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] json GeometryPresentationBindingToJson(
            const GeometryPresentationBindingRecipe& presentation)
        {
            json slots = json::array();
            for (const GeometryPresentationSlotRecipe& slot : presentation.Slots)
                slots.push_back(GeometryPresentationSlotToJson(slot));
            return json{
                {"key", presentation.Key},
                {"kind", std::string(ToString(presentation.Kind))},
                {"slots", std::move(slots)},
            };
        }

        [[nodiscard]] bool TryReadGeometryPresentationBinding(
            const json& value,
            GeometryPresentationBindingRecipe& out)
        {
            if (!value.is_object() ||
                !value.contains("key") || !value["key"].is_string() ||
                !value.contains("kind") || !value["kind"].is_string() ||
                !value.contains("slots") || !value["slots"].is_array())
            {
                return false;
            }
            out.Key = value["key"].get<std::string>();
            if (!TryParseGeometryPresentationKind(
                    value["kind"].get<std::string>(), out.Kind))
            {
                return false;
            }
            out.Slots.clear();
            out.Slots.reserve(value["slots"].size());
            for (const json& slotJson : value["slots"])
            {
                GeometryPresentationSlotRecipe slot{};
                if (!TryReadGeometryPresentationSlot(slotJson, slot))
                    return false;
                out.Slots.push_back(std::move(slot));
            }
            return true;
        }

        [[nodiscard]] json GeometryPresentationRecipeToJson(
            const GeometryPresentationRecipe& recipe)
        {
            json lanes = json::array();
            for (const GeometryPresentationLaneRecipe& lane : recipe.Lanes)
            {
                lanes.push_back(json{
                    {"lane", std::string(ToString(lane.Lane))},
                    {"presentationKey", lane.PresentationKey},
                });
            }

            json presentations = json::array();
            for (const GeometryPresentationBindingRecipe& presentation : recipe.Presentations)
                presentations.push_back(GeometryPresentationBindingToJson(presentation));

            return json{
                {"shape", std::string(ToString(recipe.Shape))},
                {"lanes", std::move(lanes)},
                {"presentations", std::move(presentations)},
            };
        }

        [[nodiscard]] bool TryApplyGeometryPresentationFromJson(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const json& value,
            SceneSerializationStats& stats)
        {
            if (!value.is_object() ||
                !value.contains("shape") || !value["shape"].is_string() ||
                !value.contains("lanes") || !value["lanes"].is_array() ||
                !value.contains("presentations") || !value["presentations"].is_array())
            {
                return false;
            }

            GeometryPresentationRecipe recipe{};
            if (!TryParseGeometryPresentationShape(
                    value["shape"].get<std::string>(), recipe.Shape))
            {
                return false;
            }

            recipe.Lanes.reserve(value["lanes"].size());
            for (const json& laneJson : value["lanes"])
            {
                if (!laneJson.is_object() ||
                    !laneJson.contains("lane") || !laneJson["lane"].is_string() ||
                    !laneJson.contains("presentationKey") || !laneJson["presentationKey"].is_string())
                {
                    return false;
                }
                GeometryPresentationLaneRecipe lane{};
                if (!TryParseGeometryRenderLane(
                        laneJson["lane"].get<std::string>(), lane.Lane))
                {
                    return false;
                }
                lane.PresentationKey = laneJson["presentationKey"].get<std::string>();
                recipe.Lanes.push_back(std::move(lane));
            }

            recipe.Presentations.reserve(value["presentations"].size());
            for (const json& presentationJson : value["presentations"])
            {
                GeometryPresentationBindingRecipe presentation{};
                if (!TryReadGeometryPresentationBinding(
                        presentationJson, presentation))
                    return false;
                recipe.Presentations.push_back(std::move(presentation));
            }

            raw.emplace_or_replace<GeometryPresentationRecipe>(
                entity, std::move(recipe));
            raw.emplace_or_replace<GeometryPresentationRuntimeState>(entity);
            ++stats.GeometryPresentationEntities;
            return true;
        }

        [[nodiscard]] bool AddVertices(json& geometry,
                                       const GS::Vertices& vertices,
                                       const bool requirePositions)
        {
            json out = json::object();
            out["deleted"] = vertices.NumDeleted;
            if (!AddVec3Property(out, "positions", vertices.Properties,
                                 PN::kPosition, requirePositions))
            {
                return false;
            }
            if (!AddVec3Property(out, "normals", vertices.Properties,
                                 PN::kNormal, false))
            {
                return false;
            }
            if (!AddVec2Property(out, "texcoords", vertices.Properties,
                                 "v:texcoord", false))
            {
                return false;
            }
            geometry["vertices"] = std::move(out);
            return true;
        }

        [[nodiscard]] bool AddNodes(json& geometry, const GS::Vertices& nodes)
        {
            json out = json::object();
            out["deleted"] = nodes.NumDeleted;
            if (!AddVec3Property(out, "positions", nodes.Properties,
                                 PN::kPosition, true))
            {
                return false;
            }
            if (!AddVec3Property(out, "normals", nodes.Properties,
                                 PN::kNormal, false))
            {
                return false;
            }
            geometry["nodes"] = std::move(out);
            return true;
        }

        [[nodiscard]] bool AddEdges(json& geometry, const GS::Edges& edges)
        {
            json out = json::object();
            out["deleted"] = edges.NumDeleted;
            if (!AddUIntProperty(out, "v0", edges.Properties, PN::kEdgeV0, true) ||
                !AddUIntProperty(out, "v1", edges.Properties, PN::kEdgeV1, true))
            {
                return false;
            }
            geometry["edges"] = std::move(out);
            return true;
        }

        [[nodiscard]] bool AddHalfedges(json& geometry,
                                        const GS::Halfedges& halfedges)
        {
            json out = json::object();
            if (!AddUIntProperty(out, "toVertex", halfedges.Properties,
                                 PN::kHalfedgeToVertex, true) ||
                !AddUIntProperty(out, "next", halfedges.Properties,
                                 PN::kHalfedgeNext, true) ||
                !AddUIntProperty(out, "face", halfedges.Properties,
                                 PN::kHalfedgeFace, true))
            {
                return false;
            }
            geometry["halfedges"] = std::move(out);
            return true;
        }

        [[nodiscard]] bool AddGraphHalfedges(
            json& geometry,
            const GS::Halfedges& halfedges)
        {
            const auto connectivity =
                halfedges.Properties.Get<
                    Geometry::Graph::HalfedgeConnectivity>(
                    PN::kHalfedgeConnectivity);
            if (!connectivity ||
                connectivity.Vector().size() != halfedges.Properties.Size())
            {
                return false;
            }

            std::vector<std::uint32_t> toVertex{};
            std::vector<std::uint32_t> next{};
            std::vector<std::uint32_t> prev{};
            toVertex.reserve(connectivity.Vector().size());
            next.reserve(connectivity.Vector().size());
            prev.reserve(connectivity.Vector().size());
            for (const Geometry::Graph::HalfedgeConnectivity& item :
                 connectivity.Vector())
            {
                toVertex.push_back(
                    static_cast<std::uint32_t>(item.Vertex.Index));
                next.push_back(static_cast<std::uint32_t>(item.Next.Index));
                prev.push_back(static_cast<std::uint32_t>(item.Prev.Index));
            }

            json out = json::object();
            out["toVertex"] = UIntArrayToJson(toVertex);
            out["next"] = UIntArrayToJson(next);
            out["prev"] = UIntArrayToJson(prev);
            geometry["halfedges"] = std::move(out);
            return true;
        }

        [[nodiscard]] bool ValidateGraphSources(
            const GS::Vertices& vertices,
            const GS::Halfedges& halfedges,
            const GS::Edges& edges) noexcept
        {
            // Serialized graphs are compact. Deleted-row counts without the
            // corresponding per-row deletion properties would be ambiguous.
            if (vertices.NumDeleted != 0u || edges.NumDeleted != 0u)
                return false;

            const std::size_t vertexCount = vertices.Properties.Size();
            const std::size_t halfedgeCount = halfedges.Properties.Size();
            const std::size_t edgeCount = edges.Properties.Size();
            if ((halfedgeCount % 2u) != 0u ||
                (halfedgeCount / 2u) != edgeCount)
            {
                return false;
            }

            const auto edgeV0 =
                edges.Properties.Get<std::uint32_t>(PN::kEdgeV0);
            const auto edgeV1 =
                edges.Properties.Get<std::uint32_t>(PN::kEdgeV1);
            const auto connectivity =
                halfedges.Properties.Get<
                    Geometry::Graph::HalfedgeConnectivity>(
                    PN::kHalfedgeConnectivity);
            if (!edgeV0 || !edgeV1 || !connectivity ||
                edgeV0.Vector().size() != edgeCount ||
                edgeV1.Vector().size() != edgeCount ||
                connectivity.Vector().size() != halfedgeCount)
            {
                return false;
            }

            for (std::size_t edgeIndex = 0u;
                 edgeIndex < edgeCount;
                 ++edgeIndex)
            {
                const std::uint32_t v0 = edgeV0.Vector()[edgeIndex];
                const std::uint32_t v1 = edgeV1.Vector()[edgeIndex];
                if (v0 >= vertexCount || v1 >= vertexCount || v0 == v1)
                    return false;

                const std::size_t h0 = edgeIndex * 2u;
                const std::size_t h1 = h0 + 1u;
                if (connectivity.Vector()[h0].Vertex.Index != v1 ||
                    connectivity.Vector()[h1].Vertex.Index != v0)
                {
                    return false;
                }
            }

            for (std::size_t index = 0u;
                 index < halfedgeCount;
                 ++index)
            {
                const auto& item = connectivity.Vector()[index];
                if (item.Vertex.Index >= vertexCount ||
                    item.Next.Index >= halfedgeCount ||
                    item.Prev.Index >= halfedgeCount)
                {
                    return false;
                }

                const std::size_t next = item.Next.Index;
                const std::size_t prev = item.Prev.Index;
                if (connectivity.Vector()[next].Prev.Index != index ||
                    connectivity.Vector()[prev].Next.Index != index)
                {
                    return false;
                }

                // Next(h) must be outgoing from ToVertex(h); the opposite of
                // an outgoing halfedge stores that source vertex.
                const std::size_t nextOpposite = next ^ 1u;
                if (nextOpposite >= halfedgeCount ||
                    connectivity.Vector()[nextOpposite].Vertex != item.Vertex)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool AddFaces(json& geometry, const GS::Faces& faces)
        {
            json out = json::object();
            out["deleted"] = faces.NumDeleted;
            if (!AddUIntProperty(out, "halfedge", faces.Properties,
                                 PN::kFaceHalfedge, true))
            {
                return false;
            }
            geometry["faces"] = std::move(out);
            return true;
        }

        [[nodiscard]] bool AddGeometry(json& entityJson,
                                       const entt::registry& raw,
                                       const ECS::EntityHandle entity,
                                       SceneSerializationStats& stats)
        {
            const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
            if (view.ActiveDomain == GS::Domain::None)
                return true;
            if (!view.Valid())
                return false;

            json geometry = json::object();
            geometry["domain"] = DomainToString(view.ActiveDomain);

            switch (view.ActiveDomain)
            {
            case GS::Domain::Mesh:
                if (view.VertexSource == nullptr || view.EdgeSource == nullptr ||
                    view.HalfedgeSource == nullptr || view.FaceSource == nullptr)
                {
                    return false;
                }
                if (!AddVertices(geometry, *view.VertexSource, true) ||
                    !AddEdges(geometry, *view.EdgeSource) ||
                    !AddHalfedges(geometry, *view.HalfedgeSource) ||
                    !AddFaces(geometry, *view.FaceSource))
                {
                    return false;
                }
                ++stats.MeshEntities;
                break;
            case GS::Domain::Graph:
                if (view.VertexSource == nullptr ||
                    view.HalfedgeSource == nullptr ||
                    view.EdgeSource == nullptr)
                    return false;
                if (!ValidateGraphSources(*view.VertexSource,
                                          *view.HalfedgeSource,
                                          *view.EdgeSource) ||
                    !AddNodes(geometry, *view.VertexSource) ||
                    !AddGraphHalfedges(geometry, *view.HalfedgeSource) ||
                    !AddEdges(geometry, *view.EdgeSource))
                {
                    return false;
                }
                ++stats.GraphEntities;
                break;
            case GS::Domain::PointCloud:
                if (view.VertexSource == nullptr)
                    return false;
                if (!AddVertices(geometry, *view.VertexSource, true))
                    return false;
                ++stats.PointCloudEntities;
                break;
            case GS::Domain::None:
            case GS::Domain::Unknown:
                return false;
            }

            entityJson["geometrySources"] = std::move(geometry);
            return true;
        }

        [[nodiscard]] bool ApplyVertices(entt::registry& raw,
                                         const ECS::EntityHandle entity,
                                         const json& value,
                                         const bool requirePositions,
                                         GS::Vertices*& out)
        {
            if (!value.is_object())
                return false;

            std::size_t deleted = 0u;
            if (!ReadOptionalDeleted(value, deleted))
                return false;

            std::vector<glm::vec3> positions;
            if (value.contains("positions"))
            {
                if (!TryReadVec3Array(value["positions"], positions))
                    return false;
            }
            else if (requirePositions)
            {
                return false;
            }

            GS::Vertices vertices{};
            if (!positions.empty())
                WriteVec3Property(vertices.Properties, PN::kPosition, std::move(positions));

            if (value.contains("normals"))
            {
                std::vector<glm::vec3> normals;
                if (!TryReadVec3Array(value["normals"], normals) ||
                    normals.size() != vertices.Properties.Size())
                {
                    return false;
                }
                WriteVec3Property(vertices.Properties, PN::kNormal, std::move(normals));
            }
            if (value.contains("texcoords"))
            {
                std::vector<glm::vec2> texcoords;
                if (!TryReadVec2Array(value["texcoords"], texcoords) ||
                    texcoords.size() != vertices.Properties.Size())
                {
                    return false;
                }
                WriteVec2Property(vertices.Properties, "v:texcoord", std::move(texcoords));
            }
            vertices.NumDeleted = deleted;
            out = &raw.emplace_or_replace<GS::Vertices>(entity, std::move(vertices));
            return true;
        }

        [[nodiscard]] bool ApplyGraphVertices(entt::registry& raw,
                                              const ECS::EntityHandle entity,
                                              const json& value)
        {
            if (!value.is_object() || !value.contains("positions"))
                return false;

            std::size_t deleted = 0u;
            std::vector<glm::vec3> positions;
            if (!ReadOptionalDeleted(value, deleted) ||
                !TryReadVec3Array(value["positions"], positions))
            {
                return false;
            }

            GS::Vertices nodes{};
            WriteVec3Property(nodes.Properties, PN::kPosition, std::move(positions));
            if (value.contains("normals"))
            {
                std::vector<glm::vec3> normals;
                if (!TryReadVec3Array(value["normals"], normals) ||
                    normals.size() != nodes.Properties.Size())
                {
                    return false;
                }
                WriteVec3Property(nodes.Properties, PN::kNormal, std::move(normals));
            }
            nodes.NumDeleted = deleted;
            raw.emplace_or_replace<GS::Vertices>(entity, std::move(nodes));
            return true;
        }

        [[nodiscard]] bool ApplyEdges(entt::registry& raw,
                                      const ECS::EntityHandle entity,
                                      const json& value)
        {
            if (!value.is_object() ||
                !value.contains("v0") ||
                !value.contains("v1"))
            {
                return false;
            }

            std::size_t deleted = 0u;
            std::vector<std::uint32_t> v0;
            std::vector<std::uint32_t> v1;
            if (!ReadOptionalDeleted(value, deleted) ||
                !TryReadUIntArray(value["v0"], v0) ||
                !TryReadUIntArray(value["v1"], v1) ||
                v0.size() != v1.size())
            {
                return false;
            }

            GS::Edges edges{};
            WriteUIntProperty(edges.Properties, PN::kEdgeV0, std::move(v0));
            WriteUIntProperty(edges.Properties, PN::kEdgeV1, std::move(v1));
            edges.NumDeleted = deleted;
            raw.emplace_or_replace<GS::Edges>(entity, std::move(edges));
            return true;
        }

        [[nodiscard]] bool ApplyHalfedges(entt::registry& raw,
                                          const ECS::EntityHandle entity,
                                          const json& value)
        {
            if (!value.is_object() ||
                !value.contains("toVertex") ||
                !value.contains("next") ||
                !value.contains("face"))
            {
                return false;
            }

            std::vector<std::uint32_t> toVertex;
            std::vector<std::uint32_t> next;
            std::vector<std::uint32_t> face;
            if (!TryReadUIntArray(value["toVertex"], toVertex) ||
                !TryReadUIntArray(value["next"], next) ||
                !TryReadUIntArray(value["face"], face) ||
                toVertex.size() != next.size() ||
                toVertex.size() != face.size())
            {
                return false;
            }

            GS::Halfedges halfedges{};
            WriteUIntProperty(halfedges.Properties, PN::kHalfedgeToVertex, std::move(toVertex));
            WriteUIntProperty(halfedges.Properties, PN::kHalfedgeNext, std::move(next));
            WriteUIntProperty(halfedges.Properties, PN::kHalfedgeFace, std::move(face));
            raw.emplace_or_replace<GS::Halfedges>(entity, std::move(halfedges));
            return true;
        }

        [[nodiscard]] bool ApplyGraphHalfedges(
            entt::registry& raw,
            const ECS::EntityHandle entity,
            const json& value)
        {
            if (!value.is_object() ||
                !value.contains("toVertex") ||
                !value.contains("next") ||
                !value.contains("prev"))
            {
                return false;
            }

            std::vector<std::uint32_t> toVertex{};
            std::vector<std::uint32_t> next{};
            std::vector<std::uint32_t> prev{};
            if (!TryReadUIntArray(value["toVertex"], toVertex) ||
                !TryReadUIntArray(value["next"], next) ||
                !TryReadUIntArray(value["prev"], prev) ||
                toVertex.size() != next.size() ||
                toVertex.size() != prev.size() ||
                (toVertex.size() % 2u) != 0u)
            {
                return false;
            }

            GS::Halfedges halfedges{};
            halfedges.Properties.Resize(toVertex.size());
            auto connectivity =
                halfedges.Properties.GetOrAdd<
                    Geometry::Graph::HalfedgeConnectivity>(
                    std::string{PN::kHalfedgeConnectivity},
                    {});
            for (std::size_t i = 0u; i < toVertex.size(); ++i)
            {
                connectivity.Vector()[i] =
                    Geometry::Graph::HalfedgeConnectivity{
                        .Vertex = Geometry::VertexHandle{
                            static_cast<Geometry::PropertyIndex>(toVertex[i])},
                        .Next = Geometry::HalfedgeHandle{
                            static_cast<Geometry::PropertyIndex>(next[i])},
                        .Prev = Geometry::HalfedgeHandle{
                            static_cast<Geometry::PropertyIndex>(prev[i])},
                    };
            }
            raw.emplace_or_replace<GS::Halfedges>(entity, std::move(halfedges));
            return true;
        }

        [[nodiscard]] bool ApplyFaces(entt::registry& raw,
                                      const ECS::EntityHandle entity,
                                      const json& value)
        {
            if (!value.is_object() || !value.contains("halfedge"))
                return false;

            std::size_t deleted = 0u;
            std::vector<std::uint32_t> halfedge;
            if (!ReadOptionalDeleted(value, deleted) ||
                !TryReadUIntArray(value["halfedge"], halfedge))
            {
                return false;
            }

            GS::Faces faces{};
            WriteUIntProperty(faces.Properties, PN::kFaceHalfedge, std::move(halfedge));
            faces.NumDeleted = deleted;
            raw.emplace_or_replace<GS::Faces>(entity, std::move(faces));
            return true;
        }

        [[nodiscard]] bool ApplyGeometry(entt::registry& raw,
                                         const ECS::EntityHandle entity,
                                         const json& geometry,
                                         SceneSerializationStats& stats)
        {
            if (!geometry.is_object() ||
                !geometry.contains("domain") ||
                !geometry["domain"].is_string())
            {
                return false;
            }

            GS::Domain domain{GS::Domain::None};
            if (!TryDomainFromString(geometry["domain"].get<std::string>(), domain))
                return false;

            switch (domain)
            {
            case GS::Domain::Mesh:
            {
                if (!geometry.contains("vertices") ||
                    !geometry.contains("edges") ||
                    !geometry.contains("halfedges") ||
                    !geometry.contains("faces"))
                {
                    return false;
                }
                GS::Vertices* vertices = nullptr;
                if (!ApplyVertices(raw, entity, geometry["vertices"], true, vertices) ||
                    !ApplyEdges(raw, entity, geometry["edges"]) ||
                    !ApplyHalfedges(raw, entity, geometry["halfedges"]) ||
                    !ApplyFaces(raw, entity, geometry["faces"]))
                {
                    return false;
                }
                raw.emplace_or_replace<GS::HasMeshTopology>(entity);
                ++stats.MeshEntities;
                break;
            }
            case GS::Domain::Graph:
                if (!geometry.contains("nodes") ||
                    !geometry.contains("halfedges") ||
                    !geometry.contains("edges"))
                    return false;
                if (!ApplyGraphVertices(raw, entity, geometry["nodes"]) ||
                    !ApplyGraphHalfedges(raw, entity, geometry["halfedges"]) ||
                    !ApplyEdges(raw, entity, geometry["edges"]))
                {
                    return false;
                }
                if (!ValidateGraphSources(
                        raw.get<GS::Vertices>(entity),
                        raw.get<GS::Halfedges>(entity),
                        raw.get<GS::Edges>(entity)))
                {
                    return false;
                }
                raw.emplace_or_replace<GS::HasGraphTopology>(entity);
                ++stats.GraphEntities;
                break;
            case GS::Domain::PointCloud:
            {
                if (!geometry.contains("vertices"))
                    return false;
                GS::Vertices* vertices = nullptr;
                if (!ApplyVertices(raw, entity, geometry["vertices"], true, vertices))
                    return false;
                ++stats.PointCloudEntities;
                break;
            }
            case GS::Domain::None:
                break;
            case GS::Domain::Unknown:
                return false;
            }
            return true;
        }

        [[nodiscard]] bool TryReadEntityId(const json& value,
                                           std::uint32_t& out) noexcept
        {
            if (!value.is_number_unsigned())
                return false;
            const std::uint64_t wide = value.get<std::uint64_t>();
            if (wide > UINT32_MAX)
                return false;
            out = static_cast<std::uint32_t>(wide);
            return true;
        }

        [[nodiscard]] std::string EntityNameFromJson(const json& entityJson)
        {
            if (entityJson.contains("name") && entityJson["name"].is_string())
                return entityJson["name"].get<std::string>();
            return "Entity";
        }

        [[nodiscard]] bool ApplyStableIdFromJson(entt::registry& raw,
                                                 const ECS::EntityHandle entity,
                                                 const json& value)
        {
            if (!value.is_object() ||
                !value.contains("high") || !value["high"].is_number_unsigned() ||
                !value.contains("low") || !value["low"].is_number_unsigned())
            {
                return false;
            }
            raw.emplace_or_replace<ECSC::StableId>(
                entity,
                ECSC::StableId{
                    .High = value["high"].get<std::uint64_t>(),
                    .Low = value["low"].get<std::uint64_t>(),
                });
            return true;
        }

        [[nodiscard]] bool ApplyTransformFromJson(entt::registry& raw,
                                                  const ECS::EntityHandle entity,
                                                  const json& value,
                                                  SceneSerializationStats& stats)
        {
            if (!value.is_object() ||
                !value.contains("position") ||
                !value.contains("rotation") ||
                !value.contains("scale"))
            {
                return false;
            }

            ECSC::Transform::Component transform{};
            if (!TryReadVec3(value["position"], transform.Position) ||
                !TryReadQuat(value["rotation"], transform.Rotation) ||
                !TryReadVec3(value["scale"], transform.Scale))
            {
                return false;
            }

            raw.emplace_or_replace<ECSC::Transform::Component>(entity, transform);
            raw.emplace_or_replace<ECSC::Transform::WorldMatrix>(entity);
            raw.emplace_or_replace<ECSC::Transform::IsDirtyTag>(entity);
            ++stats.TransformEntities;
            return true;
        }

        void CountUnsupportedPersistenceDiagnostics(const entt::registry& raw,
                                                    const ECS::EntityHandle entity,
                                                    SceneSerializationStats& stats)
        {
            bool unsupported = false;

            if (raw.any_of<ECSC::Lights::LightTag,
                           ECSC::Lights::DirectionalLight,
                           ECSC::Lights::PointLight,
                           ECSC::Lights::SpotLight,
                           ECSC::Lights::AmbientLight>(entity))
            {
                ++stats.UnsupportedLightEntities;
                unsupported = true;
            }

            if (raw.any_of<ECSC::Shadows::CasterTag>(entity))
            {
                ++stats.UnsupportedShadowEntities;
                unsupported = true;
            }

            if (raw.any_of<ECSC::Collider::Component,
                           ECSC::RigidBody::Component>(entity))
            {
                ++stats.UnsupportedPhysicsEntities;
                unsupported = true;
            }

            if (raw.any_of<ECSC::AssetInstance::Source>(entity))
            {
                ++stats.UnsupportedAssetInstanceEntities;
                unsupported = true;
            }

            if (unsupported)
                ++stats.UnsupportedPersistenceEntities;
        }

        [[nodiscard]] std::vector<ECS::EntityHandle> SortedEntities(
            const entt::registry& raw)
        {
            std::vector<ECS::EntityHandle> entities;
            entities.reserve(raw.storage<entt::entity>()->size());
            for (const entt::entity entity : raw.view<entt::entity>())
                entities.push_back(entity);
            std::sort(entities.begin(), entities.end(),
                      [](const ECS::EntityHandle lhs, const ECS::EntityHandle rhs)
                      {
                          return EntitySortKey(lhs) < EntitySortKey(rhs);
                      });
            return entities;
        }

        [[nodiscard]] Core::Expected<SceneDeserializationResult> DeserializeSceneRoot(
            ECS::Scene::Registry& scene,
            const json& root)
        {
            if (!root.is_object() ||
                !root.contains("version") ||
                !root["version"].is_number_unsigned() ||
                root["version"].get<std::uint64_t>() != kSceneDocumentVersion ||
                !root.contains("entities") ||
                !root["entities"].is_array())
            {
                return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
            }

            struct PendingParent
            {
                ECS::EntityHandle Child{ECS::InvalidEntityHandle};
                std::uint32_t ParentId{kInvalidSerializedId};
            };

            ECS::Scene::Registry loadedScene;
            entt::registry& raw = loadedScene.Raw();
            std::unordered_map<std::uint32_t, ECS::EntityHandle> idToEntity;
            idToEntity.reserve(root["entities"].size());
            std::vector<PendingParent> pendingParents;
            pendingParents.reserve(root["entities"].size());

            SceneDeserializationResult result{};
            for (const json& entityJson : root["entities"])
            {
                if (!entityJson.is_object() || !entityJson.contains("id"))
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);

                std::uint32_t id = 0u;
                if (!TryReadEntityId(entityJson["id"], id) || idToEntity.contains(id))
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);

                const ECS::EntityHandle entity =
                    ECS::Scene::CreateDefault(loadedScene, EntityNameFromJson(entityJson));
                idToEntity.emplace(id, entity);
                ++result.Stats.Entities;

                if (entityJson.contains("stableId") &&
                    !ApplyStableIdFromJson(raw, entity, entityJson["stableId"]))
                {
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                }

                if (entityJson.contains("selectable"))
                {
                    if (!entityJson["selectable"].is_boolean())
                        return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                    if (entityJson["selectable"].get<bool>())
                    {
                        raw.emplace_or_replace<Sel::SelectableTag>(entity);
                        ++result.Stats.SelectableEntities;
                    }
                }

                if (entityJson.contains("transform"))
                {
                    if (!ApplyTransformFromJson(raw, entity, entityJson["transform"], result.Stats))
                        return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                }

                if (entityJson.contains("render") &&
                    !ApplyRenderHintsFromJson(raw, entity, entityJson["render"], result.Stats))
                {
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                }

                if (entityJson.contains("geometrySources") &&
                    !ApplyGeometry(raw, entity, entityJson["geometrySources"], result.Stats))
                {
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                }

                const json* geometryPresentation = nullptr;
                if (entityJson.contains("geometryPresentation"))
                {
                    geometryPresentation = &entityJson["geometryPresentation"];
                }
                else if (entityJson.contains("progressiveRenderData"))
                {
                    // Read-only compatibility for scene documents emitted by
                    // the retired combined desired/runtime component.
                    geometryPresentation = &entityJson["progressiveRenderData"];
                }
                if (geometryPresentation != nullptr &&
                    !TryApplyGeometryPresentationFromJson(
                        raw, entity, *geometryPresentation, result.Stats))
                {
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                }

                if (entityJson.contains("parentId"))
                {
                    std::uint32_t parentId = 0u;
                    if (!TryReadEntityId(entityJson["parentId"], parentId))
                        return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                    pendingParents.push_back(PendingParent{
                        .Child = entity,
                        .ParentId = parentId,
                    });
                }
            }

            for (const PendingParent& pending : pendingParents)
            {
                const auto found = idToEntity.find(pending.ParentId);
                if (found == idToEntity.end())
                    return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);
                ECS::Hierarchy::Attach(raw, pending.Child, found->second);
                ++result.Stats.HierarchyLinks;
            }

            scene.Clear();
            scene.Raw() = std::move(loadedScene.Raw());
            return result;
        }
    }

    Core::Expected<std::string> SerializeSceneDocument(const ECS::Scene::Registry& scene)
    {
        const entt::registry& raw = scene.Raw();
        const std::vector<ECS::EntityHandle> entities = SortedEntities(raw);
        std::unordered_map<ECS::EntityHandle, std::uint32_t> entityToId;
        entityToId.reserve(entities.size());
        for (std::uint32_t index = 0u; index < entities.size(); ++index)
            entityToId.emplace(entities[index], index);

        SceneSerializationStats stats{};
        json root = json::object();
        root["version"] = kSceneDocumentVersion;
        root["entities"] = json::array();

        for (std::uint32_t index = 0u; index < entities.size(); ++index)
        {
            const ECS::EntityHandle entity = entities[index];
            json entityJson = json::object();
            entityJson["id"] = index;

            std::string name = "Entity " + std::to_string(EntitySortKey(entity));
            if (const auto* meta = raw.try_get<ECSC::MetaData>(entity);
                meta != nullptr && !meta->EntityName.empty())
            {
                name = meta->EntityName;
            }
            entityJson["name"] = std::move(name);

            if (const auto* stableId = raw.try_get<ECSC::StableId>(entity);
                stableId != nullptr && ECSC::IsValid(*stableId))
            {
                entityJson["stableId"] = json{
                    {"high", stableId->High},
                    {"low", stableId->Low},
                };
            }

            const bool selectable = raw.all_of<Sel::SelectableTag>(entity);
            entityJson["selectable"] = selectable;
            if (selectable)
                ++stats.SelectableEntities;

            if (const auto* transform = raw.try_get<ECSC::Transform::Component>(entity))
            {
                entityJson["transform"] = json{
                    {"position", Vec3ToJson(transform->Position)},
                    {"rotation", QuatToJson(transform->Rotation)},
                    {"scale", Vec3ToJson(transform->Scale)},
                };
                ++stats.TransformEntities;
            }

            if (const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(entity);
                hierarchy != nullptr && hierarchy->Parent != ECS::InvalidEntityHandle)
            {
                const auto parent = entityToId.find(hierarchy->Parent);
                if (parent != entityToId.end())
                {
                    entityJson["parentId"] = parent->second;
                    ++stats.HierarchyLinks;
                }
            }

            CountUnsupportedPersistenceDiagnostics(raw, entity, stats);

            const json render = RenderHintsToJson(raw, entity, stats);
            if (!render.empty())
                entityJson["render"] = render;

            if (!AddGeometry(entityJson, raw, entity, stats))
                return Core::Err<std::string>(Core::ErrorCode::InvalidFormat);

            if (const auto* presentation =
                    raw.try_get<GeometryPresentationRecipe>(entity))
            {
                entityJson["geometryPresentation"] =
                    GeometryPresentationRecipeToJson(*presentation);
                ++stats.GeometryPresentationEntities;
            }

            root["entities"].push_back(std::move(entityJson));
            ++stats.Entities;
        }

        root["stats"] = json{
            {"entities", stats.Entities},
            {"selectableEntities", stats.SelectableEntities},
            {"transformEntities", stats.TransformEntities},
            {"hierarchyLinks", stats.HierarchyLinks},
            {"meshEntities", stats.MeshEntities},
            {"graphEntities", stats.GraphEntities},
            {"pointCloudEntities", stats.PointCloudEntities},
            {"renderHintEntities", stats.RenderHintEntities},
            {"geometryPresentationEntities", stats.GeometryPresentationEntities},
            {"unsupportedPersistenceEntities", stats.UnsupportedPersistenceEntities},
            {"unsupportedLightEntities", stats.UnsupportedLightEntities},
            {"unsupportedShadowEntities", stats.UnsupportedShadowEntities},
            {"unsupportedPhysicsEntities", stats.UnsupportedPhysicsEntities},
            {"unsupportedAssetInstanceEntities", stats.UnsupportedAssetInstanceEntities},
        };
        return root.dump(2);
    }

    Core::Expected<SceneSerializationResult> SaveSceneDocument(
        const ECS::Scene::Registry& scene,
        const std::string_view path,
        Core::IO::IIOBackend& backend)
    {
        if (path.empty())
            return Core::Err<SceneSerializationResult>(Core::ErrorCode::InvalidPath);

        auto document = SerializeSceneDocument(scene);
        if (!document.has_value())
            return Core::Err<SceneSerializationResult>(document.error());

        const auto* bytes = reinterpret_cast<const std::byte*>(document->data());
        const std::span<const std::byte> data(bytes, document->size());
        const Core::Result written = backend.Write(
            Core::IO::IORequest{.Path = std::string(path)},
            data);
        if (!written.has_value())
            return Core::Err<SceneSerializationResult>(written.error());

        const json root = json::parse(*document, nullptr, false);
        SceneSerializationStats stats{};
        if (root.is_object() && root.contains("stats") && root["stats"].is_object())
        {
            const json& statsJson = root["stats"];
            stats.Entities = statsJson.value("entities", 0u);
            stats.SelectableEntities = statsJson.value("selectableEntities", 0u);
            stats.TransformEntities = statsJson.value("transformEntities", 0u);
            stats.HierarchyLinks = statsJson.value("hierarchyLinks", 0u);
            stats.MeshEntities = statsJson.value("meshEntities", 0u);
            stats.GraphEntities = statsJson.value("graphEntities", 0u);
            stats.PointCloudEntities = statsJson.value("pointCloudEntities", 0u);
            stats.RenderHintEntities = statsJson.value("renderHintEntities", 0u);
            stats.GeometryPresentationEntities = statsJson.value(
                "geometryPresentationEntities",
                statsJson.value("progressiveRenderDataEntities", 0u));
            stats.UnsupportedPersistenceEntities =
                statsJson.value("unsupportedPersistenceEntities", 0u);
            stats.UnsupportedLightEntities =
                statsJson.value("unsupportedLightEntities", 0u);
            stats.UnsupportedShadowEntities =
                statsJson.value("unsupportedShadowEntities", 0u);
            stats.UnsupportedPhysicsEntities =
                statsJson.value("unsupportedPhysicsEntities", 0u);
            stats.UnsupportedAssetInstanceEntities =
                statsJson.value("unsupportedAssetInstanceEntities", 0u);
        }
        return SceneSerializationResult{.Stats = stats};
    }

    Core::Expected<SceneDeserializationResult> DeserializeSceneDocument(
        ECS::Scene::Registry& scene,
        const std::string_view document)
    {
        if (document.empty())
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);

        const json root = json::parse(document.begin(), document.end(), nullptr, false);
        if (root.is_discarded())
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidFormat);

        return DeserializeSceneRoot(scene, root);
    }

    Core::Expected<SceneDeserializationResult> LoadSceneDocument(
        ECS::Scene::Registry& scene,
        const std::string_view path,
        Core::IO::IIOBackend& backend)
    {
        if (path.empty())
            return Core::Err<SceneDeserializationResult>(Core::ErrorCode::InvalidPath);

        auto read = backend.Read(Core::IO::IORequest{.Path = std::string(path)});
        if (!read.has_value())
            return Core::Err<SceneDeserializationResult>(read.error());

        const char* chars = reinterpret_cast<const char*>(read->Data.data());
        const std::string_view document(chars, read->Data.size());
        return DeserializeSceneDocument(scene, document);
    }
}
