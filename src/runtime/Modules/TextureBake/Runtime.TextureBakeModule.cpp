module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.TextureBakeModule;

import Extrinsic.Asset.Service;
import Extrinsic.Core.Error;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.PropertyTextureBake;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;
import Geometry.UvAtlas.Validation;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;

        constexpr float kAtlasEpsilon = 1.0e-4f;
        constexpr std::size_t kMaxActivePropertyTextureBakes = 256u;
        constexpr std::size_t kMaxRetainedPropertyBakeSourceBytes =
            64u * 1024u * 1024u;

        [[nodiscard]] PropertyTextureBakeResult UnavailableBakeResult()
        {
            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::NonOperationalBackend,
                .Diagnostic =
                    "texture-bake GPU service is non-operational; no CPU fallback is available",
            };
        }

        [[nodiscard]] ECS::EntityHandle ResolveEntity(
            const ECS::Scene::Registry& scene,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            return entity != ECS::InvalidEntityHandle &&
                           scene.Raw().valid(entity)
                ? entity
                : ECS::InvalidEntityHandle;
        }

        [[nodiscard]] bool Finite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool Finite(const glm::vec4 value) noexcept
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z) &&
                   std::isfinite(value.w);
        }



        [[nodiscard]] std::uint64_t EdgeKey(
            std::uint32_t a,
            std::uint32_t b) noexcept
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<std::uint64_t>(a) << 32u) |
                   static_cast<std::uint64_t>(b);
        }

        void AdvanceGeneration(std::uint64_t& generation) noexcept
        {
            ++generation;
            if (generation == 0u)
                generation = 1u;
        }

        [[nodiscard]] bool IsScalar(
            const Geometry::PropertyValueKind kind) noexcept
        {
            return GeometryPropertyComponentCount(kind) == 1u;
        }

        [[nodiscard]] PropertyTextureBakeStatus StatusForResolution(
            const GeometryPropertyResolutionStatus status) noexcept
        {
            switch (status)
            {
            case GeometryPropertyResolutionStatus::Resolved:
                return PropertyTextureBakeStatus::Success;
            case GeometryPropertyResolutionStatus::MissingName:
            case GeometryPropertyResolutionStatus::MissingProperty:
                return PropertyTextureBakeStatus::MissingProperty;
            case GeometryPropertyResolutionStatus::ValueKindMismatch:
                return PropertyTextureBakeStatus::UnsupportedPropertyType;
            case GeometryPropertyResolutionStatus::ElementCountMismatch:
                return PropertyTextureBakeStatus::MismatchedPropertyCount;
            case GeometryPropertyResolutionStatus::UnsupportedDomain:
                return PropertyTextureBakeStatus::UnsupportedSourceDomain;
            case GeometryPropertyResolutionStatus::NonFiniteValues:
                return PropertyTextureBakeStatus::NonFinitePropertyValue;
            }
            return PropertyTextureBakeStatus::UnsupportedPropertyType;
        }

        struct GeneratedPropertyTextureMetadata
        {
            std::uint32_t SchemaVersion{1u};
            std::uint32_t StableEntityId{0u};
            std::string OutputName{};
            GeometryElementDomain SourceDomain{
                GeometryElementDomain::Unknown};
            std::string SourcePropertyName{};
            std::string TexcoordPropertyName{};
            Geometry::PropertyValueKind ValueKind{
                Geometry::PropertyValueKind::Unknown};
            PropertyTextureBakeStorage Storage{
                PropertyTextureBakeStorage::Auto};
            PropertyTextureBakeEncoding Encoder{
                PropertyTextureBakeEncoding::Auto};
            Graphics::Colormap::Type EncodingColormap{
                Graphics::Colormap::Type::Viridis};
            float RangeMin{0.0f};
            float RangeMax{1.0f};
            std::uint32_t Width{0u};
            std::uint32_t Height{0u};
            std::uint64_t SourceGeneration{0u};
            std::uint64_t Serial{0u};
        };

        struct PreparedPropertyBake
        {
            PropertyTextureBakeStatus Status{
                PropertyTextureBakeStatus::Success};
            Geometry::PropertyValueKind ValueKind{
                Geometry::PropertyValueKind::Unknown};
            PropertyTextureBakeStorage Storage{
                PropertyTextureBakeStorage::RawFloat};
            PropertyTextureBakeEncoding Encoder{
                PropertyTextureBakeEncoding::Auto};
            Graphics::Colormap::Type EncodingColormap{
                Graphics::Colormap::Type::Viridis};
            Graphics::PropertyTextureBakeDomain Domain{
                Graphics::PropertyTextureBakeDomain::Vertex};
            Graphics::PropertyTextureBakeValueKind GpuValueKind{
                Graphics::PropertyTextureBakeValueKind::Scalar};
            Graphics::PropertyTextureBakeEncoding GpuEncoding{
                Graphics::PropertyTextureBakeEncoding::Raw};
            RHI::Format Format{RHI::Format::Undefined};
            // Bake-owned indexed raster input: one texcoord slot per distinct
            // (mesh vertex, resolved UV), values per slot/triangle/corner.
            std::vector<glm::vec2> Texcoords{};
            std::vector<glm::vec4> Values{};
            std::vector<std::uint32_t> SurfaceIndices{};
            Graphics::PropertyTextureBakeCharts Charts{};
            std::vector<Graphics::PropertyTextureBakeGutterEdge> GutterEdges{};
            Graphics::PropertyTextureBakeCoverageReport Coverage{};
            PropertyTextureBakeSourceIdentity Identity{};
            // Adopted output extent: the requested one, or the adaptive
            // extent at which every chart resolves.
            std::uint32_t Width{0u};
            std::uint32_t Height{0u};
            std::size_t ExpectedElementCount{0u};
            std::uint64_t SourceGeneration{0u};
            float RangeMin{0.0f};
            float RangeMax{1.0f};
            std::string OutputName{};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == PropertyTextureBakeStatus::Success;
            }
        };

        [[nodiscard]] PreparedPropertyBake PrepareFailure(
            const PropertyTextureBakeStatus status,
            std::string diagnostic)
        {
            return PreparedPropertyBake{
                .Status = status,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] bool CopyPropertyValues(
            const Geometry::ConstPropertySet& properties,
            const std::string& name,
            const Geometry::PropertyValueKind kind,
            const bool labels,
            std::vector<glm::vec4>& values)
        {
            values.clear();
            const auto append = [&values](const auto& source, auto convert)
            {
                values.reserve(source.size());
                for (const auto& value : source)
                    values.push_back(convert(value));
            };

            const auto scalar = [&]<class T>() {
                const auto property = properties.Get<T>(name);
                if (!property) return false;
                for (const T value : property.Vector())
                {
                    const long double precise = value;
                    if (!std::isfinite(precise)) return false;
                    float converted{};
                    if (labels)
                    {
                        if (precise < 0 || precise > std::numeric_limits<std::uint32_t>::max() ||
                            std::trunc(precise) != precise) return false;
                        converted = std::bit_cast<float>(static_cast<std::uint32_t>(precise));
                    }
                    else
                    {
                        if (precise < -std::numeric_limits<float>::max() ||
                            precise > std::numeric_limits<float>::max()) return false;
                        converted = static_cast<float>(value);
                        if (!std::isfinite(converted)) return false;
                        if constexpr (std::is_integral_v<T>)
                            if (static_cast<long double>(converted) != precise) return false;
                    }
                    values.emplace_back(converted, 0.f, 0.f, 1.f);
                }
                return true;
            };
            switch (kind)
            {
            case Geometry::PropertyValueKind::Bool: return scalar.template operator()<bool>();
            case Geometry::PropertyValueKind::Int32: return scalar.template operator()<std::int32_t>();
            case Geometry::PropertyValueKind::UInt32: return scalar.template operator()<std::uint32_t>();
            case Geometry::PropertyValueKind::UInt64: return scalar.template operator()<std::uint64_t>();
            case Geometry::PropertyValueKind::Float: return scalar.template operator()<float>();
            case Geometry::PropertyValueKind::Double: return scalar.template operator()<double>();
            case Geometry::PropertyValueKind::Vec2:
                if (const auto property = properties.Get<glm::vec2>(name))
                {
                    append(property.Vector(), [](const glm::vec2 value)
                    {
                        return glm::vec4{value, 0.0f, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Vec3:
                if (const auto property = properties.Get<glm::vec3>(name))
                {
                    append(property.Vector(), [](const glm::vec3 value)
                    {
                        return glm::vec4{value, 1.0f};
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Vec4:
                if (const auto property = properties.Get<glm::vec4>(name))
                {
                    append(property.Vector(), [](const glm::vec4 value)
                    {
                        return value;
                    });
                    return true;
                }
                break;
            case Geometry::PropertyValueKind::Unknown:
                break;
            }
            return false;
        }

        constexpr std::string_view kCornerTexcoordProperty = "h:texcoord";
        constexpr std::string_view kVertexTexcoordProperty = "v:texcoord";

        // Word-wise FNV-1a with a down-mix, over the exact bytes consumed.
        // Identical content on the supported little-endian targets yields the
        // same value across runs, so persisted fingerprints stay comparable.
        class ContentFingerprint
        {
        public:
            void Word(const std::uint64_t value) noexcept
            {
                m_State ^= value;
                m_State *= 1099511628211ull;
                m_State ^= m_State >> 32u;
            }

            void Bytes(const void* const data, const std::size_t size) noexcept
            {
                Word(static_cast<std::uint64_t>(size));
                const auto* bytes = static_cast<const unsigned char*>(data);
                std::size_t offset = 0u;
                for (; offset + sizeof(std::uint64_t) <= size;
                     offset += sizeof(std::uint64_t))
                {
                    std::uint64_t chunk{};
                    std::memcpy(&chunk, bytes + offset, sizeof(chunk));
                    Word(chunk);
                }
                if (offset < size)
                {
                    std::uint64_t tail{0u};
                    std::memcpy(&tail, bytes + offset, size - offset);
                    Word(tail);
                }
            }

            void Text(const std::string_view text) noexcept
            {
                Bytes(text.data(), text.size());
            }

            template <class T>
            void Values(const std::span<const T> values) noexcept
            {
                Bytes(values.data(), values.size_bytes());
            }

            [[nodiscard]] std::uint64_t Value() const noexcept
            {
                return m_State == 0u ? 1u : m_State;
            }

        private:
            std::uint64_t m_State{14695981039346656037ull};
        };

        [[nodiscard]] std::optional<std::uint64_t> FingerprintProperty(
            const Geometry::ConstPropertySet& properties,
            const GeometryElementDomain domain,
            const std::string_view name,
            const Geometry::PropertyValueKind kind)
        {
            ContentFingerprint fingerprint{};
            fingerprint.Word(static_cast<std::uint64_t>(domain));
            fingerprint.Word(static_cast<std::uint64_t>(kind));
            fingerprint.Text(name);
            const auto typed = [&]<class T>() -> bool
            {
                const auto property = properties.Get<T>(name);
                if (!property)
                    return false;
                if constexpr (std::is_same_v<T, bool>)
                {
                    fingerprint.Word(property.Vector().size());
                    for (const bool value : property.Vector())
                        fingerprint.Word(value ? 1u : 0u);
                }
                else
                {
                    fingerprint.Values(std::span<const T>{property.Vector()});
                }
                return true;
            };
            bool found = false;
            switch (kind)
            {
            case Geometry::PropertyValueKind::Bool: found = typed.template operator()<bool>(); break;
            case Geometry::PropertyValueKind::Int32: found = typed.template operator()<std::int32_t>(); break;
            case Geometry::PropertyValueKind::UInt32: found = typed.template operator()<std::uint32_t>(); break;
            case Geometry::PropertyValueKind::UInt64: found = typed.template operator()<std::uint64_t>(); break;
            case Geometry::PropertyValueKind::Float: found = typed.template operator()<float>(); break;
            case Geometry::PropertyValueKind::Double: found = typed.template operator()<double>(); break;
            case Geometry::PropertyValueKind::Vec2: found = typed.template operator()<glm::vec2>(); break;
            case Geometry::PropertyValueKind::Vec3: found = typed.template operator()<glm::vec3>(); break;
            case Geometry::PropertyValueKind::Vec4: found = typed.template operator()<glm::vec4>(); break;
            case Geometry::PropertyValueKind::Unknown: break;
            }
            return found ? std::optional<std::uint64_t>{fingerprint.Value()}
                         : std::nullopt;
        }

        struct TexcoordBinding
        {
            PropertyTextureBakeStatus Status{
                PropertyTextureBakeStatus::Success};
            std::string Diagnostic{};
            GeometryPropertyRef Resolved{};
            std::span<const glm::vec2> Values{};
            bool Corner{false};
        };

        // The canonical atlas is the renderer's corner-over-vertex authority.
        // A named reference is exact: it never falls back to `h:texcoord`.
        [[nodiscard]] TexcoordBinding ResolveTexcoordBinding(
            const GS::ConstSourceView& view,
            const GeometryPropertyRef& requested)
        {
            const auto bind = [&view](
                const GeometryElementDomain domain,
                const std::string_view name) -> std::optional<TexcoordBinding>
            {
                const bool corner = domain == GeometryElementDomain::MeshHalfedge;
                const Geometry::PropertySet& set = corner
                    ? view.HalfedgeSource->Properties
                    : view.VertexSource->Properties;
                const auto property =
                    Geometry::ConstPropertySet{set}.Get<glm::vec2>(name);
                if (!property || property.Vector().size() != set.Size())
                    return std::nullopt;
                return TexcoordBinding{
                    .Resolved = GeometryPropertyRef{
                        .Domain = domain,
                        .Name = std::string{name},
                        .ValueKind = Geometry::PropertyValueKind::Vec2,
                    },
                    .Values = std::span<const glm::vec2>{property.Vector()},
                    .Corner = corner,
                };
            };

            if (!requested.HasName())
            {
                if (auto corner = bind(
                        GeometryElementDomain::MeshHalfedge,
                        kCornerTexcoordProperty))
                {
                    return std::move(*corner);
                }
                if (auto vertex = bind(
                        GeometryElementDomain::MeshVertex,
                        kVertexTexcoordProperty))
                {
                    return std::move(*vertex);
                }
                return TexcoordBinding{
                    .Status = PropertyTextureBakeStatus::MissingTexcoords,
                    .Diagnostic =
                        "texture bake requires a complete corner h:texcoord "
                        "or vertex v:texcoord atlas",
                };
            }
            if ((requested.Domain != GeometryElementDomain::MeshHalfedge &&
                 requested.Domain != GeometryElementDomain::MeshVertex) ||
                (requested.ValueKind != Geometry::PropertyValueKind::Unknown &&
                 requested.ValueKind != Geometry::PropertyValueKind::Vec2))
            {
                return TexcoordBinding{
                    .Status = PropertyTextureBakeStatus::MissingTexcoords,
                    .Diagnostic =
                        "texture bake texcoords must reference a mesh-corner "
                        "or mesh-vertex vec2 property",
                };
            }
            if (auto exact = bind(requested.Domain, requested.Name))
                return std::move(*exact);
            return TexcoordBinding{
                .Status = PropertyTextureBakeStatus::MissingTexcoords,
                .Diagnostic = "texture bake atlas property '" +
                              requested.Name +
                              "' is missing, not vec2, or not one value per " +
                              (requested.Domain ==
                                       GeometryElementDomain::MeshHalfedge
                                   ? "corner"
                                   : "vertex"),
            };
        }

        // Triangulated surface plus the UV each triangle corner carries under
        // the resolved binding. Shared by preparation and freshness capture so
        // both hash exactly what the raster consumes.
        struct BakeGeometry
        {
            PropertyTextureBakeStatus Status{
                PropertyTextureBakeStatus::Success};
            std::string Diagnostic{};
            std::vector<std::uint32_t> SurfaceIndices{};
            std::vector<std::uint32_t> TriangleToFace{};
            std::vector<std::uint32_t> CornerHalfedges{};
            TexcoordBinding Texcoords{};
            std::vector<glm::vec2> CornerUv{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == PropertyTextureBakeStatus::Success;
            }
        };

        [[nodiscard]] BakeGeometry ResolveBakeGeometry(
            const GS::ConstSourceView& view,
            const GeometryPropertyRef& requestedTexcoords)
        {
            BakeGeometry geometry{};
            const auto fail = [&geometry](
                const PropertyTextureBakeStatus status,
                std::string diagnostic)
            {
                geometry.Status = status;
                geometry.Diagnostic = std::move(diagnostic);
                return std::move(geometry);
            };
            if (view.ActiveDomain != GS::Domain::Mesh ||
                view.VertexSource == nullptr ||
                view.EdgeSource == nullptr ||
                view.HalfedgeSource == nullptr ||
                view.FaceSource == nullptr)
            {
                return fail(
                    PropertyTextureBakeStatus::NonMeshSource,
                    "texture bake requires complete mesh topology");
            }
            const MeshSurfaceTopologyStatus topology =
                BuildMeshSurfaceTriangleCornerTopology(
                    view,
                    geometry.SurfaceIndices,
                    geometry.TriangleToFace,
                    geometry.CornerHalfedges);
            if (topology != MeshSurfaceTopologyStatus::Success ||
                geometry.SurfaceIndices.empty() ||
                (geometry.SurfaceIndices.size() % 3u) != 0u)
            {
                return fail(
                    PropertyTextureBakeStatus::BakeFailed,
                    DebugNameForMeshSurfaceTopologyStatus(topology));
            }
            geometry.Texcoords =
                ResolveTexcoordBinding(view, requestedTexcoords);
            if (geometry.Texcoords.Status !=
                PropertyTextureBakeStatus::Success)
            {
                return fail(
                    geometry.Texcoords.Status,
                    geometry.Texcoords.Diagnostic);
            }
            geometry.CornerUv.resize(geometry.SurfaceIndices.size());
            for (std::size_t index = 0u;
                 index < geometry.SurfaceIndices.size();
                 ++index)
            {
                const std::uint32_t element = geometry.Texcoords.Corner
                    ? geometry.CornerHalfedges[index]
                    : geometry.SurfaceIndices[index];
                if (element >= geometry.Texcoords.Values.size())
                {
                    return fail(
                        PropertyTextureBakeStatus::BakeFailed,
                        "texture bake topology references an invalid atlas element");
                }
                geometry.CornerUv[index] = geometry.Texcoords.Values[element];
            }
            return geometry;
        }

        [[nodiscard]] std::uint64_t FingerprintUv(
            const BakeGeometry& geometry) noexcept
        {
            ContentFingerprint fingerprint{};
            fingerprint.Word(static_cast<std::uint64_t>(
                geometry.Texcoords.Resolved.Domain));
            fingerprint.Text(geometry.Texcoords.Resolved.Name);
            fingerprint.Values(std::span<const glm::vec2>{geometry.CornerUv});
            return fingerprint.Value();
        }

        [[nodiscard]] std::uint64_t FingerprintTopology(
            const BakeGeometry& geometry) noexcept
        {
            ContentFingerprint fingerprint{};
            fingerprint.Values(
                std::span<const std::uint32_t>{geometry.SurfaceIndices});
            fingerprint.Values(
                std::span<const std::uint32_t>{geometry.TriangleToFace});
            return fingerprint.Value();
        }

        [[nodiscard]] std::uint64_t FingerprintPositions(
            const GS::ConstSourceView& view) noexcept
        {
            ContentFingerprint fingerprint{};
            const auto positions =
                Geometry::ConstPropertySet{view.VertexSource->Properties}
                    .Get<glm::vec3>(GS::PropertyNames::kPosition);
            if (positions)
                fingerprint.Values(std::span<const glm::vec3>{positions.Vector()});
            else
                fingerprint.Text("absent");
            return fingerprint.Value();
        }

        struct CapturedSourceIdentity
        {
            PropertyTextureBakeStatus Status{
                PropertyTextureBakeStatus::Success};
            std::string Diagnostic{};
            PropertyTextureBakeSourceIdentity Identity{};
        };

        [[nodiscard]] std::optional<std::uint64_t> FingerprintSourceProperty(
            const GS::ConstSourceView& view,
            const GeometryPropertyRef& source)
        {
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, source.Domain);
            if (properties == nullptr)
                return std::nullopt;
            return FingerprintProperty(
                Geometry::ConstPropertySet{*properties},
                source.Domain,
                source.Name,
                DetectGeometryPropertyValueKind(*properties, source.Name));
        }

        [[nodiscard]] CapturedSourceIdentity CaptureSourceIdentity(
            const GS::ConstSourceView& view,
            const GeometryPropertyRef& source,
            const GeometryPropertyRef& requestedTexcoords)
        {
            const BakeGeometry geometry =
                ResolveBakeGeometry(view, requestedTexcoords);
            if (!geometry.Succeeded())
            {
                return CapturedSourceIdentity{
                    .Status = geometry.Status,
                    .Diagnostic = geometry.Diagnostic,
                };
            }
            const std::optional<std::uint64_t> property =
                FingerprintSourceProperty(view, source);
            if (!property.has_value())
            {
                return CapturedSourceIdentity{
                    .Status = PropertyTextureBakeStatus::MissingProperty,
                    .Diagnostic = "texture bake source property is missing",
                };
            }
            return CapturedSourceIdentity{
                .Identity = PropertyTextureBakeSourceIdentity{
                    .ResolvedTexcoords = geometry.Texcoords.Resolved,
                    .UvFingerprint = FingerprintUv(geometry),
                    .PositionFingerprint = FingerprintPositions(view),
                    .TopologyFingerprint = FingerprintTopology(geometry),
                    .PropertyFingerprint = *property,
                },
            };
        }

        [[nodiscard]] PropertyTextureBakeRevisionLookup MakeRevisionLookup(
            const GS::ConstSourceView& view)
        {
            return [availability = BuildGeometryAvailability(view)](
                       const GeometryElementDomain domain,
                       const std::string_view name)
                -> std::optional<std::uint64_t>
            {
                const Geometry::PropertySet* properties =
                    ResolveGeometryPropertySet(availability, domain);
                if (properties == nullptr)
                    return std::nullopt;
                const std::optional<Geometry::PropertyRevision> revision =
                    properties->FindPropertyRevision(name);
                return revision.has_value()
                    ? std::optional<std::uint64_t>{*revision}
                    : std::nullopt;
            };
        }
    }

    PropertyTextureBakeFreshness ComparePropertyTextureBakeSourceIdentity(
        const PropertyTextureBakeSourceIdentity& baked,
        const PropertyTextureBakeSourceIdentity& current) noexcept
    {
        if (baked.UvFingerprint == 0u ||
            baked.PositionFingerprint == 0u ||
            baked.TopologyFingerprint == 0u ||
            baked.PropertyFingerprint == 0u)
        {
            return PropertyTextureBakeFreshness::Unknown;
        }
        if (baked.TopologyFingerprint != current.TopologyFingerprint)
            return PropertyTextureBakeFreshness::TopologyChanged;
        if (baked.ResolvedTexcoords != current.ResolvedTexcoords ||
            baked.UvFingerprint != current.UvFingerprint)
        {
            return PropertyTextureBakeFreshness::UvChanged;
        }
        if (baked.PositionFingerprint != current.PositionFingerprint)
            return PropertyTextureBakeFreshness::PositionsChanged;
        if (baked.PropertyFingerprint != current.PropertyFingerprint)
            return PropertyTextureBakeFreshness::PropertyChanged;
        return PropertyTextureBakeFreshness::Fresh;
    }

    const char* DebugNameForPropertyTextureBakeFreshness(
        const PropertyTextureBakeFreshness freshness) noexcept
    {
        switch (freshness)
        {
        case PropertyTextureBakeFreshness::Unknown:
            return "PropertyTextureBakeFreshness.Unknown";
        case PropertyTextureBakeFreshness::Fresh:
            return "PropertyTextureBakeFreshness.Fresh";
        case PropertyTextureBakeFreshness::TopologyChanged:
            return "PropertyTextureBakeFreshness.TopologyChanged";
        case PropertyTextureBakeFreshness::UvChanged:
            return "PropertyTextureBakeFreshness.UvChanged";
        case PropertyTextureBakeFreshness::PositionsChanged:
            return "PropertyTextureBakeFreshness.PositionsChanged";
        case PropertyTextureBakeFreshness::PropertyChanged:
            return "PropertyTextureBakeFreshness.PropertyChanged";
        case PropertyTextureBakeFreshness::SourceUnavailable:
            return "PropertyTextureBakeFreshness.SourceUnavailable";
        }
        return "PropertyTextureBakeFreshness.Unknown";
    }

    std::uint64_t ComputePropertyTextureBakeRevisionToken(
        const PropertyTextureBakeRecord& record,
        const PropertyTextureBakeRevisionLookup& lookup)
    {
        if (!lookup)
            return 0u;
        ContentFingerprint token{};
        const auto watch = [&token, &lookup](
            const GeometryElementDomain domain,
            const std::string_view name)
        {
            const std::optional<std::uint64_t> revision = lookup(domain, name);
            token.Word(static_cast<std::uint64_t>(domain));
            token.Text(name);
            token.Word(revision.has_value() ? 1u : 0u);
            token.Word(revision.value_or(0u));
        };
        if (record.Texcoords.HasName())
        {
            watch(record.Texcoords.Domain, record.Texcoords.Name);
        }
        else
        {
            watch(GeometryElementDomain::MeshHalfedge, kCornerTexcoordProperty);
            watch(GeometryElementDomain::MeshVertex, kVertexTexcoordProperty);
        }
        watch(GeometryElementDomain::MeshVertex, GS::PropertyNames::kPosition);
        watch(GeometryElementDomain::MeshHalfedge,
              GS::PropertyNames::kHalfedgeToVertex);
        watch(GeometryElementDomain::MeshHalfedge,
              GS::PropertyNames::kHalfedgeNext);
        watch(GeometryElementDomain::MeshHalfedge,
              GS::PropertyNames::kHalfedgeFace);
        watch(GeometryElementDomain::MeshFace,
              GS::PropertyNames::kFaceHalfedge);
        if (record.Source.Domain == GeometryElementDomain::MeshEdge)
        {
            watch(GeometryElementDomain::MeshEdge, GS::PropertyNames::kEdgeV0);
            watch(GeometryElementDomain::MeshEdge, GS::PropertyNames::kEdgeV1);
        }
        watch(record.Source.Domain, record.Source.Name);
        return token.Value();
    }

    bool IsPropertyTextureBakeRecordBindable(
        const PropertyTextureBakeRecord& record,
        const std::uint64_t currentRevisionToken) noexcept
    {
        return record.State == PropertyTextureBakeOutputState::Ready &&
               record.Freshness == PropertyTextureBakeFreshness::Fresh &&
               record.Texture.IsValid() &&
               record.ObservedRevisionToken != 0u &&
               record.ObservedRevisionToken == currentRevisionToken;
    }

    PropertyTextureBakeEncoding ResolveSurfaceAppearanceEncoding(
        const Graphics::Components::VisualizationConfig& config,
        const Geometry::PropertyValueKind kind) noexcept
    {
        using Config = Graphics::Components::VisualizationConfig;
        if (config.Source == Config::ColorSource::ScalarField)
            return PropertyTextureBakeEncoding::ScalarColormap;
        if (config.Interpretation == Config::ColorInterpretation::NormalDirection)
            return PropertyTextureBakeEncoding::Normal;
        return GeometryPropertyComponentCount(kind) == 1u
            ? PropertyTextureBakeEncoding::LabelPalette : PropertyTextureBakeEncoding::RgbaColor;
    }

    PropertyTextureBakeRepresentation ResolvePropertyTextureBakeRepresentation(
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage requestedStorage,
        const PropertyTextureBakeEncoding requestedEncoding) noexcept
    {
        PropertyTextureBakeRepresentation representation{
            .Storage = requestedStorage,
            .Encoding = requestedEncoding,
        };
        if (representation.Storage == PropertyTextureBakeStorage::Auto)
        {
            representation.Storage =
                (requestedEncoding == PropertyTextureBakeEncoding::LabelPalette)
                ? PropertyTextureBakeStorage::EncodedRgba
                : PropertyTextureBakeStorage::RawFloat;
        }
        if (representation.Encoding != PropertyTextureBakeEncoding::Auto)
            return representation;

        switch (valueKind)
        {
        case Geometry::PropertyValueKind::Bool:
        case Geometry::PropertyValueKind::Int32:
        case Geometry::PropertyValueKind::UInt64:
        case Geometry::PropertyValueKind::UInt32:
        case Geometry::PropertyValueKind::Float:
        case Geometry::PropertyValueKind::Double:
            representation.Encoding =
                PropertyTextureBakeEncoding::LinearScalar;
            break;
        case Geometry::PropertyValueKind::Vec2:
            representation.Encoding =
                PropertyTextureBakeEncoding::Vector2;
            break;
        case Geometry::PropertyValueKind::Vec3:
            representation.Encoding =
                PropertyTextureBakeEncoding::Vector3;
            break;
        case Geometry::PropertyValueKind::Vec4:
            representation.Encoding =
                PropertyTextureBakeEncoding::RgbaColor;
            break;
        case Geometry::PropertyValueKind::Unknown:
            break;
        }
        return representation;
    }

    bool IsPropertyTextureBakeRepresentationCompatible(
        const Geometry::PropertyValueKind valueKind,
        const PropertyTextureBakeStorage storage,
        const PropertyTextureBakeEncoding encoding) noexcept
    {
        if (storage == PropertyTextureBakeStorage::Auto ||
            encoding == PropertyTextureBakeEncoding::Auto)
        {
            return false;
        }

        if (GeometryPropertyComponentCount(valueKind) == 1u)
            return storage == PropertyTextureBakeStorage::RawFloat
                ? encoding == PropertyTextureBakeEncoding::LinearScalar
                : storage == PropertyTextureBakeStorage::EncodedRgba &&
                  (encoding == PropertyTextureBakeEncoding::LinearScalar ||
                   encoding == PropertyTextureBakeEncoding::ScalarColormap ||
                   encoding == PropertyTextureBakeEncoding::LabelPalette);
        if (storage != PropertyTextureBakeStorage::RawFloat && storage != PropertyTextureBakeStorage::EncodedRgba)
            return false;
        switch (valueKind)
        {
        case Geometry::PropertyValueKind::Vec2:
            return encoding == PropertyTextureBakeEncoding::Vector2 ||
                   (storage == PropertyTextureBakeStorage::EncodedRgba && encoding == PropertyTextureBakeEncoding::RgbaColor);
        case Geometry::PropertyValueKind::Vec3:
            return encoding == PropertyTextureBakeEncoding::Vector3 ||
                   (storage == PropertyTextureBakeStorage::EncodedRgba &&
                    (encoding == PropertyTextureBakeEncoding::RgbaColor || encoding == PropertyTextureBakeEncoding::Normal));
        case Geometry::PropertyValueKind::Vec4:
            return encoding == PropertyTextureBakeEncoding::RgbaColor;
        default: return false;
        }
    }

    const char* DebugNameForPropertyTextureBakeStatus(
        const PropertyTextureBakeStatus status) noexcept
    {
        switch (status)
        {
        case PropertyTextureBakeStatus::Success:
            return "PropertyTextureBake.Success";
        case PropertyTextureBakeStatus::Scheduled:
            return "PropertyTextureBake.Scheduled";
        case PropertyTextureBakeStatus::NonOperationalBackend:
            return "PropertyTextureBake.NonOperationalBackend";
        case PropertyTextureBakeStatus::MissingScene:
            return "PropertyTextureBake.MissingScene";
        case PropertyTextureBakeStatus::MissingAssetService:
            return "PropertyTextureBake.MissingAssetService";
        case PropertyTextureBakeStatus::StaleEntity:
            return "PropertyTextureBake.StaleEntity";
        case PropertyTextureBakeStatus::NonMeshSource:
            return "PropertyTextureBake.NonMeshSource";
        case PropertyTextureBakeStatus::InvalidResolution:
            return "PropertyTextureBake.InvalidResolution";
        case PropertyTextureBakeStatus::InvalidPadding:
            return "PropertyTextureBake.InvalidPadding";
        case PropertyTextureBakeStatus::InvalidRange:
            return "PropertyTextureBake.InvalidRange";
        case PropertyTextureBakeStatus::MissingProperty:
            return "PropertyTextureBake.MissingProperty";
        case PropertyTextureBakeStatus::UnsupportedPropertyType:
            return "PropertyTextureBake.UnsupportedPropertyType";
        case PropertyTextureBakeStatus::MismatchedPropertyCount:
            return "PropertyTextureBake.MismatchedPropertyCount";
        case PropertyTextureBakeStatus::UnsupportedSourceDomain:
            return "PropertyTextureBake.UnsupportedSourceDomain";
        case PropertyTextureBakeStatus::MissingTexcoords:
            return "PropertyTextureBake.MissingTexcoords";
        case PropertyTextureBakeStatus::NonFiniteTexcoord:
            return "PropertyTextureBake.NonFiniteTexcoord";
        case PropertyTextureBakeStatus::NonFinitePropertyValue:
            return "PropertyTextureBake.NonFinitePropertyValue";
        case PropertyTextureBakeStatus::DegenerateAllTriangles:
            return "PropertyTextureBake.DegenerateAllTriangles";
        case PropertyTextureBakeStatus::DegenerateUvTriangles:
            return "PropertyTextureBake.DegenerateUvTriangles";
        case PropertyTextureBakeStatus::OverlappingUvCharts:
            return "PropertyTextureBake.OverlappingUvCharts";
        case PropertyTextureBakeStatus::UnderresolvedAtlas:
            return "PropertyTextureBake.UnderresolvedAtlas";
        case PropertyTextureBakeStatus::ZeroCoverageBake:
            return "PropertyTextureBake.ZeroCoverageBake";
        case PropertyTextureBakeStatus::BakeFailed:
            return "PropertyTextureBake.BakeFailed";
        case PropertyTextureBakeStatus::AssetLoadFailed:
            return "PropertyTextureBake.AssetLoadFailed";
        case PropertyTextureBakeStatus::CommandFailed:
            return "PropertyTextureBake.CommandFailed";
        case PropertyTextureBakeStatus::JobSubmitFailed:
            return "PropertyTextureBake.JobSubmitFailed";
        case PropertyTextureBakeStatus::StaleCompletion:
            return "PropertyTextureBake.StaleCompletion";
        }
        return "PropertyTextureBake.Unknown";
    }

    struct TextureBakeService::Impl
    {
        struct BoundContext
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{DefaultWorldHandle};
            std::uint64_t BindingEpoch{0u};
            Assets::AssetService* AssetService{};
            EditorCommandHistory* CommandHistory{};
            JobService* Jobs{};
        };

        enum class WorkPhase : std::uint8_t
        {
            Queued,
            WaitingForReadyFrame,
            Failed,
        };

        // Per-bake GPU inputs and transient targets, retired only after the
        // frame that consumed them can no longer be in flight.
        struct BakeGpuResources
        {
            std::optional<RHI::BufferManager::BufferLease> PropertyBuffer{};
            std::optional<RHI::BufferManager::BufferLease> TexcoordBuffer{};
            std::optional<RHI::BufferManager::BufferLease> IndexBuffer{};
            std::optional<RHI::BufferManager::BufferLease> ChartBuffer{};
            std::optional<RHI::BufferManager::BufferLease> GutterBuffer{};
            std::optional<RHI::TextureManager::TextureLease> Depth{};

            [[nodiscard]] bool Empty() const noexcept
            {
                return !PropertyBuffer.has_value() &&
                       !TexcoordBuffer.has_value() &&
                       !IndexBuffer.has_value() &&
                       !ChartBuffer.has_value() &&
                       !GutterBuffer.has_value() &&
                       !Depth.has_value();
            }
        };

        struct Work
        {
            WorkPhase Phase{WorkPhase::Queued};
            WorldHandle World{};
            std::uint64_t BindingEpoch{0u};
            ECS::EntityHandle Entity{ECS::InvalidEntityHandle};
            std::uint32_t StableEntityId{0u};
            std::string OutputName{};
            Assets::AssetId Asset{};
            Assets::AssetId CoverageAsset{};
            std::uint64_t RecordGeneration{0u};
            PropertyTextureBakeRequest Request{};
            PreparedPropertyBake Prepared{};
            std::uint32_t Width{0u};
            std::uint32_t Height{0u};
            BakeGpuResources Resources{};
            std::uint64_t CacheGeneration{0u};
            std::uint64_t CoverageCacheGeneration{0u};
            std::uint64_t ReadyFrame{0u};
        };

        struct PipelineEntry
        {
            RHI::Format Format{RHI::Format::Undefined};
            std::optional<RHI::PipelineManager::PipelineLease> Raster{};
        };

        struct RetiredWorkResources
        {
            BakeGpuResources Resources{};
            std::uint64_t SafeFrame{0u};
        };

        BoundContext Context{};
        RHI::IDevice* Device{};
        Graphics::GpuAssetCache* GpuAssets{};
        Graphics::IRenderer* Renderer{};
        RenderExtractionCache* Extraction{};
        TextureBakeModuleStats* Stats{};
        std::vector<Work> WorkItems{};
        std::vector<RetiredWorkResources> RetiredResources{};
        std::vector<PipelineEntry> Pipelines{};
        std::uint64_t NextAssetSerial{1u};
        GpuQueueParticipantHandle Participant{};
        // Total retained source-snapshot bytes; one request above it can
        // never be admitted, however empty the queue is.
        std::size_t SourceSnapshotBudget{kMaxRetainedPropertyBakeSourceBytes};

        // The owning module composes the service through its always-constructed Impl.
        void Bind(
            ECS::Scene::Registry* const scene,
            const WorldHandle world,
            const std::uint64_t bindingEpoch,
            Assets::AssetService* const assets,
            EditorCommandHistory* const history,
            JobService* const jobs,
            RHI::IDevice* const device,
            Graphics::GpuAssetCache* const gpuAssets,
            Graphics::IRenderer* const renderer,
            RenderExtractionCache* const extraction,
            TextureBakeModuleStats* const stats) noexcept
        {
            Context = BoundContext{
                .Scene = scene,
                .World = world,
                .BindingEpoch = bindingEpoch,
                .AssetService = assets,
                .CommandHistory = history,
                .Jobs = jobs,
            };
            Device = device;
            GpuAssets = gpuAssets;
            Renderer = renderer;
            Extraction = extraction;
            Stats = stats;
        }

        void SetTarget(
            const WorldHandle world,
            const std::uint64_t bindingEpoch,
            ECS::Scene::Registry* const scene) noexcept
        {
            Context.World = world;
            Context.BindingEpoch = bindingEpoch;
            Context.Scene = scene;
        }

        void SetCommandHistory(
            EditorCommandHistory* const history) noexcept
        {
            Context.CommandHistory = history;
        }

        // A second registration would leak the first participant, so an
        // already-registered service reports an invalid handle instead.
        [[nodiscard]] GpuQueueParticipantHandle RegisterGpuQueueParticipant(
            JobService& jobs)
        {
            if (Participant.IsValid())
                return {};
            Participant = jobs.RegisterGpuQueueParticipant(
                MakeParticipantDesc());
            return Participant;
        }

        void Unbind() noexcept
        {
            ShutdownAfterDeviceIdle();
            Context = {};
            Device = nullptr;
            GpuAssets = nullptr;
            Renderer = nullptr;
            Extraction = nullptr;
            Stats = nullptr;
            Participant = {};
        }

        [[nodiscard]] bool Available() const noexcept
        {
            return Context.Scene != nullptr &&
                   Context.AssetService != nullptr &&
                   Device != nullptr &&
                   GpuAssets != nullptr &&
                   Renderer != nullptr &&
                   Extraction != nullptr &&
                   Participant.IsValid() &&
                   Device->IsOperational();
        }

        [[nodiscard]] std::uint64_t CurrentFrame() const noexcept
        {
            return Device != nullptr
                ? Device->GetGlobalFrameNumber()
                : 0u;
        }

        [[nodiscard]] std::uint64_t SafeReleaseFrame() const noexcept
        {
            if (Device == nullptr)
                return 0u;
            return CurrentFrame() +
                   std::max<std::uint32_t>(
                       Device->GetFramesInFlight(),
                       1u);
        }

        [[nodiscard]] static std::size_t SourceByteCount(
            const PreparedPropertyBake& prepared) noexcept
        {
            return prepared.Texcoords.size() * sizeof(glm::vec2) +
                   prepared.Values.size() * sizeof(glm::vec4) +
                   prepared.SurfaceIndices.size() *
                       sizeof(std::uint32_t) +
                   prepared.Charts.TriangleChart.size() *
                       sizeof(std::uint32_t) +
                   prepared.GutterEdges.size() *
                       sizeof(Graphics::PropertyTextureBakeGutterEdge);
        }

        [[nodiscard]] PropertyTextureBakeResult OversizedSnapshotResult(
            std::string outputName,
            const std::size_t bytes,
            const std::string_view measure) const
        {
            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::BakeFailed,
                .OutputName = std::move(outputName),
                .Diagnostic = "texture bake source snapshot " + std::string{measure} + " " +
                              std::to_string(bytes) + " bytes, more than the " +
                              std::to_string(SourceSnapshotBudget) +
                              "-byte bake budget; this mesh cannot be baked until it is simplified",
            };
        }

        // Queue pressure only: the caller has already rejected a candidate
        // that exceeds the whole budget on its own.
        [[nodiscard]] bool HasScheduleCapacity(
            const ECS::EntityHandle entity,
            const std::string_view outputName,
            const PreparedPropertyBake& prepared) const noexcept
        {
            const std::size_t candidateBytes =
                SourceByteCount(prepared);
            std::size_t retainedBytes = 0u;
            std::size_t retainedCount = 0u;
            for (const Work& work : WorkItems)
            {
                if (work.Entity == entity &&
                    work.OutputName == outputName)
                {
                    continue;
                }
                ++retainedCount;
                retainedBytes += SourceByteCount(work.Prepared);
            }
            return retainedCount <
                       kMaxActivePropertyTextureBakes &&
                   retainedBytes <=
                       SourceSnapshotBudget - candidateBytes;
        }

        void RetireWorkResources(
            Work& work,
            const std::uint64_t safeFrame)
        {
            if (work.Resources.Empty())
                return;
            RetiredResources.push_back(RetiredWorkResources{
                .Resources = std::exchange(work.Resources, BakeGpuResources{}),
                .SafeFrame = safeFrame,
            });
        }

        void RetireWorkResources(
            BakeGpuResources& resources,
            const std::uint64_t safeFrame)
        {
            if (resources.Empty())
                return;
            RetiredResources.push_back(RetiredWorkResources{
                .Resources = std::exchange(resources, BakeGpuResources{}),
                .SafeFrame = safeFrame,
            });
        }

        void DrainRetiredResources()
        {
            const std::uint64_t frame = CurrentFrame();
            std::erase_if(
                RetiredResources,
                [frame](const RetiredWorkResources& resources)
                {
                    return frame >= resources.SafeFrame;
                });
        }

        [[nodiscard]] PropertyTextureBakeRecord* FindRecord(
            ECS::EntityHandle entity,
            const std::string_view outputName) noexcept
        {
            if (Context.Scene == nullptr ||
                entity == ECS::InvalidEntityHandle)
            {
                return nullptr;
            }
            auto* catalog = Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity);
            if (catalog == nullptr)
                return nullptr;
            const auto found = std::ranges::find(
                catalog->Records,
                outputName,
                &PropertyTextureBakeRecord::OutputName);
            return found != catalog->Records.end()
                ? &*found
                : nullptr;
        }

        [[nodiscard]] const PropertyTextureBakeRecord* FindRecord(
            const ECS::EntityHandle entity,
            const std::string_view outputName) const noexcept
        {
            return const_cast<Impl*>(this)->FindRecord(entity, outputName);
        }

        [[nodiscard]] bool AssetOwnedByAnotherRecord(
            const Assets::AssetId asset,
            const ECS::EntityHandle entity,
            const std::string_view outputName) const noexcept
        {
            if (!asset.IsValid() || Context.Scene == nullptr)
                return false;

            auto view = Context.Scene->Raw().view<PropertyTextureBakeOutputs>();
            for (auto&& [owner, catalog] : view.each())
            {
                for (const PropertyTextureBakeRecord& record :
                     catalog.Records)
                {
                    if (owner == entity && record.OutputName == outputName)
                        continue;
                    if (record.Texture == asset ||
                        record.CoverageTexture == asset)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] bool SourceStillCurrent(
            const Work& work,
            std::string& diagnostic) const
        {
            if (Context.Scene == nullptr ||
                !Context.Scene->IsValid(work.Entity))
            {
                diagnostic = "property texture bake entity is stale";
                return false;
            }
            const CapturedSourceIdentity current = CaptureSourceIdentity(
                GS::BuildConstView(Context.Scene->Raw(), work.Entity),
                work.Request.Source,
                work.Request.Texcoords);
            if (current.Status != PropertyTextureBakeStatus::Success)
            {
                diagnostic = current.Diagnostic.empty()
                    ? "property texture bake source is no longer valid"
                    : current.Diagnostic;
                return false;
            }
            if (current.Identity != work.Prepared.Identity)
            {
                diagnostic = std::string{
                    "property texture bake source changed ("} +
                    DebugNameForPropertyTextureBakeFreshness(
                        ComparePropertyTextureBakeSourceIdentity(
                            work.Prepared.Identity,
                            current.Identity)) +
                    "); submit a rebake";
                return false;
            }
            return true;
        }

        [[nodiscard]] PreparedPropertyBake Prepare(
            const PropertyTextureBakeRequest& request) const
        {
            if (Context.Scene == nullptr)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingScene,
                    "texture bake has no active scene");
            }
            if (!request.World.IsValid() ||
                request.World != Context.World)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingScene,
                    "texture bake request does not target the active world");
            }

            if (request.Width == 0u || request.Height == 0u ||
                request.Width > kPropertyTextureBakeMaxExtent ||
                request.Height > kPropertyTextureBakeMaxExtent)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::InvalidResolution,
                    "texture bake extent " + std::to_string(request.Width) + "x" +
                        std::to_string(request.Height) + " must be within [1, " +
                        std::to_string(kPropertyTextureBakeMaxExtent) + "]");
            }
            if (request.MaxAdaptiveExtent != 0u &&
                (request.MaxAdaptiveExtent < std::max(request.Width, request.Height) ||
                 request.MaxAdaptiveExtent > kPropertyTextureBakeMaxExtent))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::InvalidResolution,
                    "texture bake adaptive extent bound must cover the requested extent and be at most " +
                        std::to_string(kPropertyTextureBakeMaxExtent));
            }
            if (request.PaddingTexels >
                Graphics::kPropertyTextureBakeMaxPaddingTexels)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::InvalidPadding,
                    "texture bake padding must be within [0, 32]");
            }
            if (!request.Source.HasName())
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingProperty,
                    "texture bake source property name must not be empty");
            }
            if (request.EncodingColormap >= Graphics::Colormap::Type::Count)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::CommandFailed,
                    "texture bake colormap is invalid");
            }

            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::StaleEntity,
                    "texture bake entity is stale");
            }

            const GS::ConstSourceView view =
                GS::BuildConstView(Context.Scene->Raw(), entity);
            BakeGeometry geometry =
                ResolveBakeGeometry(view, request.Texcoords);
            if (!geometry.Succeeded())
                return PrepareFailure(geometry.Status, geometry.Diagnostic);
            // Every snapshot retains at least three corner indices and one
            // chart id per triangle, so a mesh whose floor already exceeds
            // the budget is rejected before coverage is measured.
            const std::size_t snapshotFloor =
                (geometry.SurfaceIndices.size() / 3u) * 4u * sizeof(std::uint32_t);
            if (snapshotFloor > SourceSnapshotBudget)
            {
                PropertyTextureBakeResult oversized = OversizedSnapshotResult(
                    {}, snapshotFloor, "needs at least");
                return PrepareFailure(oversized.Status, std::move(oversized.Diagnostic));
            }

            // Only UVs that triangles actually carry are validated; unused
            // boundary-corner slots may hold anything.
            for (const glm::vec2 uv : geometry.CornerUv)
            {
                if (!Finite(uv))
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::NonFiniteTexcoord,
                        "texture bake atlas coordinates must be finite");
                }
                if (uv.x < -kAtlasEpsilon || uv.y < -kAtlasEpsilon ||
                    uv.x > 1.0f + kAtlasEpsilon ||
                    uv.y > 1.0f + kAtlasEpsilon)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::MissingTexcoords,
                        "texture bake coordinates are not a normalized atlas");
                }
            }
            for (std::size_t index = 0u;
                 index < geometry.CornerUv.size();
                 index += 3u)
            {
                if (Geometry::UvAtlas::ExactUvOrientation(
                        geometry.CornerUv[index], geometry.CornerUv[index + 1u],
                        geometry.CornerUv[index + 2u]) == 0)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::DegenerateUvTriangles,
                        "texture bake atlas contains a degenerate UV triangle");
                }
            }

            PreparedPropertyBake prepared{};
            prepared.OutputName = request.OutputName.empty()
                ? request.Source.Name
                : request.OutputName;
            if (prepared.OutputName.empty())
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::CommandFailed,
                    "texture bake output name must not be empty");
            }
            prepared.SourceGeneration =
                request.ExpectedSourceGeneration;

            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(view);
            prepared.ExpectedElementCount = ResolveGeometryElementCount(
                availability, request.Source.Domain);
            const GeometryPropertyResolution resolution =
                ResolveGeometryProperty(
                    availability,
                    request.Source,
                    prepared.ExpectedElementCount,
                    true,
                    request.ExpectedSourceGeneration);
            if (!resolution.Resolved())
            {
                return PrepareFailure(
                    StatusForResolution(resolution.Status),
                    std::string{ToString(resolution.Status)});
            }
            prepared.ValueKind = resolution.ResolvedValueKind;
            if (request.ExpectedPropertyGeneration != 0u &&
                request.ExpectedPropertyGeneration !=
                    resolution.ObservedSourceGeneration)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::StaleCompletion,
                    "texture bake property generation is stale");
            }

            const PropertyTextureBakeRepresentation representation =
                ResolvePropertyTextureBakeRepresentation(
                    prepared.ValueKind,
                    request.Storage,
                    request.Encoding);
            const bool labels = representation.Encoding ==
                                PropertyTextureBakeEncoding::LabelPalette;
            const Geometry::PropertySet* propertySet =
                ResolveGeometryPropertySet(
                    availability, request.Source.Domain);
            if (propertySet == nullptr ||
                !CopyPropertyValues(
                    Geometry::ConstPropertySet{*propertySet},
                    request.Source.Name,
                    prepared.ValueKind,
                    labels,
                    prepared.Values))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::NonFinitePropertyValue,
                    "texture bake source values are nonfinite or cannot be represented by the selected scalar or label encoding");
            }
            if (prepared.Values.size() != prepared.ExpectedElementCount)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MismatchedPropertyCount,
                    "texture bake source property count changed during preparation");
            }
            if (!labels &&
                !std::ranges::all_of(
                    prepared.Values,
                    [](const glm::vec4 value) noexcept
                    {
                        return Finite(value);
                    }))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::NonFinitePropertyValue,
                    "texture bake source property contains a non-finite value");
            }
            const std::optional<std::uint64_t> propertyFingerprint =
                FingerprintProperty(
                    Geometry::ConstPropertySet{*propertySet},
                    request.Source.Domain,
                    request.Source.Name,
                    prepared.ValueKind);
            if (!propertyFingerprint.has_value())
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::MissingProperty,
                    "texture bake source property is missing");
            }
            prepared.Identity = PropertyTextureBakeSourceIdentity{
                .ResolvedTexcoords = geometry.Texcoords.Resolved,
                .UvFingerprint = FingerprintUv(geometry),
                .PositionFingerprint = FingerprintPositions(view),
                .TopologyFingerprint = FingerprintTopology(geometry),
                .PropertyFingerprint = *propertyFingerprint,
            };

            // Domain expansion reads mesh-vertex corner ids, so it runs before
            // corner UVs rewrite the index buffer into texcoord slots.
            const std::vector<std::uint32_t>& meshCorners =
                geometry.SurfaceIndices;
            switch (request.Source.Domain)
            {
            case GeometryElementDomain::MeshVertex:
                prepared.Domain = Graphics::PropertyTextureBakeDomain::Vertex;
                break;
            case GeometryElementDomain::MeshFace:
            {
                prepared.Domain = Graphics::PropertyTextureBakeDomain::Face;
                std::vector<glm::vec4> expanded{};
                expanded.reserve(geometry.TriangleToFace.size());
                for (const std::uint32_t face : geometry.TriangleToFace)
                {
                    if (face >= prepared.Values.size())
                    {
                        return PrepareFailure(
                            PropertyTextureBakeStatus::MismatchedPropertyCount,
                            "texture bake face map exceeds the property buffer");
                    }
                    expanded.push_back(prepared.Values[face]);
                }
                prepared.Values = std::move(expanded);
                break;
            }
            case GeometryElementDomain::MeshEdge:
            {
                prepared.Domain =
                    Graphics::PropertyTextureBakeDomain::NearestEdge;
                const Geometry::ConstPropertySet edgeProperties{
                    view.EdgeSource->Properties};
                const auto edgeV0 = edgeProperties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV0);
                const auto edgeV1 = edgeProperties.Get<std::uint32_t>(
                    GS::PropertyNames::kEdgeV1);
                if (!edgeV0.IsValid() || !edgeV1.IsValid() ||
                    edgeV0.Vector().size() != prepared.Values.size() ||
                    edgeV1.Vector().size() != prepared.Values.size())
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::BakeFailed,
                        "nearest-edge bake requires canonical edge endpoints");
                }
                std::unordered_map<std::uint64_t, std::uint32_t> edgeRows{};
                edgeRows.reserve(prepared.Values.size());
                for (std::uint32_t edge = 0u;
                     edge < prepared.Values.size();
                     ++edge)
                {
                    edgeRows.emplace(
                        EdgeKey(
                            edgeV0.Vector()[edge],
                            edgeV1.Vector()[edge]),
                        edge);
                }
                std::vector<glm::vec4> expanded{};
                expanded.reserve(meshCorners.size());
                for (std::size_t index = 0u;
                     index < meshCorners.size();
                     index += 3u)
                {
                    const std::uint32_t a = meshCorners[index + 0u];
                    const std::uint32_t b = meshCorners[index + 1u];
                    const std::uint32_t c = meshCorners[index + 2u];
                    const std::uint64_t keys[3]{
                        EdgeKey(b, c),
                        EdgeKey(c, a),
                        EdgeKey(a, b),
                    };
                    for (const std::uint64_t key : keys)
                    {
                        const auto found = edgeRows.find(key);
                        if (found == edgeRows.end())
                        {
                            return PrepareFailure(
                                PropertyTextureBakeStatus::BakeFailed,
                                "nearest-edge bake could not resolve a triangle edge");
                        }
                        expanded.push_back(prepared.Values[found->second]);
                    }
                }
                prepared.Values = std::move(expanded);
                break;
            }
            case GeometryElementDomain::Unknown:
            case GeometryElementDomain::MeshHalfedge:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::GraphHalfedge:
            case GeometryElementDomain::GraphEdge:
            case GeometryElementDomain::PointCloudPoint:
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnsupportedSourceDomain,
                    "texture bake supports mesh vertex, edge, and face properties");
            }

            // The bake owns its index buffer, so any vertex or corner binding
            // rasterizes without depending on the renderer's shading split.
            // Corner UVs split each mesh vertex into one slot per distinct UV;
            // triangles across a UV seam then share no slot edge, which is
            // exactly the chart boundary used for gutters.
            prepared.SurfaceIndices = geometry.SurfaceIndices;
            if (geometry.Texcoords.Corner)
            {
                MeshCornerTexcoordSplit split{};
                if (!BuildMeshCornerTexcoordSplit(
                        geometry.Texcoords.Values,
                        geometry.CornerHalfedges,
                        {},
                        view.VertexSource->Properties.Size(),
                        prepared.SurfaceIndices,
                        split))
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::BakeFailed,
                        "texture bake could not split corner atlas coordinates");
                }
                if (prepared.Domain == Graphics::PropertyTextureBakeDomain::Vertex)
                {
                    std::vector<glm::vec4> splitValues{};
                    splitValues.reserve(split.SourceVertexForSlot.size());
                    for (const std::uint32_t sourceVertex : split.SourceVertexForSlot)
                    {
                        if (sourceVertex >= prepared.Values.size())
                        {
                            return PrepareFailure(
                                PropertyTextureBakeStatus::MismatchedPropertyCount,
                                "texture bake corner split exceeds the property buffer");
                        }
                        splitValues.push_back(prepared.Values[sourceVertex]);
                    }
                    prepared.Values = std::move(splitValues);
                }
                prepared.Texcoords = std::move(split.TexcoordForSlot);
            }
            else
            {
                prepared.Texcoords.assign(
                    geometry.Texcoords.Values.begin(),
                    geometry.Texcoords.Values.end());
            }

            prepared.Charts =
                Graphics::BuildPropertyTextureBakeCharts(prepared.SurfaceIndices);
            if (prepared.Charts.ChartCount >
                Graphics::kPropertyTextureBakeMaxCharts)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnderresolvedAtlas,
                    "texture bake atlas has more UV charts than coverage ids can encode");
            }
            if (request.PaddingTexels != 0u)
            {
                prepared.GutterEdges =
                    Graphics::BuildPropertyTextureBakeGutterEdges(
                        prepared.SurfaceIndices);
            }
            // Adaptive requests grow the extent only while a chart is missing
            // texel centres; overlap is an atlas defect at every extent.
            prepared.Width = request.Width;
            prepared.Height = request.Height;
            for (;;)
            {
                prepared.Coverage = Graphics::MeasurePropertyTextureBakeCoverage(
                    prepared.Texcoords,
                    prepared.SurfaceIndices,
                    prepared.Charts,
                    prepared.Width,
                    prepared.Height);
                if (prepared.Coverage.OverlapTexels != 0u)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::OverlappingUvCharts,
                        std::to_string(prepared.Coverage.OverlapTexels) +
                            " texel centres are covered by more than one UV "
                            "triangle; a property bake needs a non-overlapping atlas");
                }
                const bool resolved =
                    prepared.Coverage.CoveredTexels != 0u &&
                    prepared.Coverage.UnderresolvedChartCount == 0u;
                if (resolved ||
                    request.MaxAdaptiveExtent == 0u ||
                    prepared.Width > request.MaxAdaptiveExtent / 2u ||
                    prepared.Height > request.MaxAdaptiveExtent / 2u)
                {
                    break;
                }
                prepared.Width *= 2u;
                prepared.Height *= 2u;
            }
            const std::string extent =
                std::to_string(prepared.Width) + "x" +
                std::to_string(prepared.Height) +
                (request.MaxAdaptiveExtent != 0u
                     ? " (largest adaptive extent within " +
                           std::to_string(request.MaxAdaptiveExtent) + ")"
                     : std::string{});
            if (prepared.Coverage.CoveredTexels == 0u)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::ZeroCoverageBake,
                    "texture bake atlas covers no texel centre at " + extent);
            }
            if (prepared.Coverage.UnderresolvedChartCount != 0u)
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnderresolvedAtlas,
                    std::to_string(prepared.Coverage.UnderresolvedChartCount) +
                        " of " + std::to_string(prepared.Charts.ChartCount) +
                        " UV charts cover no texel centre at " + extent +
                        " (first chart " +
                        std::to_string(prepared.Coverage.FirstUnderresolvedChart) +
                        "); increase the bake extent or repack the atlas");
            }

            switch (prepared.ValueKind)
            {
            case Geometry::PropertyValueKind::Bool:
            case Geometry::PropertyValueKind::Int32:
            case Geometry::PropertyValueKind::UInt64:
            case Geometry::PropertyValueKind::UInt32:
            case Geometry::PropertyValueKind::Float:
            case Geometry::PropertyValueKind::Double:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Scalar;
                break;
            case Geometry::PropertyValueKind::Vec2:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Vector2;
                break;
            case Geometry::PropertyValueKind::Vec3:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Vector3;
                break;
            case Geometry::PropertyValueKind::Vec4:
                prepared.GpuValueKind =
                    Graphics::PropertyTextureBakeValueKind::Vector4;
                break;
            case Geometry::PropertyValueKind::Unknown:
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnsupportedPropertyType,
                    "texture bake property type is not GPU-rasterizable");
            }

            prepared.Storage = representation.Storage;
            prepared.Encoder = representation.Encoding;
            if (prepared.Encoder == PropertyTextureBakeEncoding::LabelPalette)
                prepared.GpuValueKind = Graphics::PropertyTextureBakeValueKind::Label;
            prepared.EncodingColormap = request.EncodingColormap;
            if (!IsPropertyTextureBakeRepresentationCompatible(
                    prepared.ValueKind,
                    prepared.Storage,
                    prepared.Encoder))
            {
                return PrepareFailure(
                    PropertyTextureBakeStatus::UnsupportedPropertyType,
                    "texture bake encoder is incompatible with the selected property type and storage");
            }
            if (prepared.Storage == PropertyTextureBakeStorage::RawFloat)
            {
                prepared.GpuEncoding =
                    Graphics::PropertyTextureBakeEncoding::Raw;
                switch (prepared.ValueKind)
                {
                case Geometry::PropertyValueKind::Bool:
                case Geometry::PropertyValueKind::Int32:
                case Geometry::PropertyValueKind::UInt64:
                case Geometry::PropertyValueKind::Float:
                case Geometry::PropertyValueKind::Double:
                case Geometry::PropertyValueKind::UInt32:
                    prepared.Format = RHI::Format::R32_FLOAT;
                    break;
                case Geometry::PropertyValueKind::Vec2:
                    prepared.Format = RHI::Format::RG32_FLOAT;
                    break;
                case Geometry::PropertyValueKind::Vec3:
                case Geometry::PropertyValueKind::Vec4:
                    prepared.Format = RHI::Format::RGBA32_FLOAT;
                    break;
                case Geometry::PropertyValueKind::Unknown:
                    break;
                }
            }
            else
            {
                prepared.Format = RHI::Format::RGBA8_UNORM;
                switch (prepared.Encoder)
                {
                case PropertyTextureBakeEncoding::Normal:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::Normal;
                    break;
                case PropertyTextureBakeEncoding::ScalarColormap:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::ScalarColormap;
                    break;
                case PropertyTextureBakeEncoding::LabelPalette:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::LabelPalette;
                    break;
                case PropertyTextureBakeEncoding::RgbaColor:
                case PropertyTextureBakeEncoding::Vector2:
                case PropertyTextureBakeEncoding::Vector3:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::RgbaColor;
                    break;
                case PropertyTextureBakeEncoding::LinearScalar:
                    prepared.GpuEncoding =
                        Graphics::PropertyTextureBakeEncoding::LinearScalar;
                    break;
                case PropertyTextureBakeEncoding::Auto:
                    break;
                }
            }

            prepared.RangeMin = request.RangeMin;
            prepared.RangeMax = request.RangeMax;
            if ((IsScalar(prepared.ValueKind) && prepared.Encoder != PropertyTextureBakeEncoding::LabelPalette) ||
                prepared.Storage == PropertyTextureBakeStorage::RawFloat)
            {
                if (request.RangePolicy ==
                    PropertyTextureBakeRangePolicy::AutoFinite)
                {
                    prepared.RangeMin = std::numeric_limits<float>::infinity();
                    prepared.RangeMax = -std::numeric_limits<float>::infinity();
                    const int components = prepared.ValueKind == Geometry::PropertyValueKind::Vec4 ? 4 :
                        prepared.ValueKind == Geometry::PropertyValueKind::Vec3 ? 3 :
                        prepared.ValueKind == Geometry::PropertyValueKind::Vec2 ? 2 : 1;
                    for (const glm::vec4 value : prepared.Values)
                        for (int component = 0; component < components; ++component)
                        {
                            prepared.RangeMin = std::min(prepared.RangeMin, value[component]);
                            prepared.RangeMax = std::max(prepared.RangeMax, value[component]);
                        }
                    if (prepared.RangeMin == prepared.RangeMax)
                    {
                        const float delta = std::max(
                            1.0e-6f,
                            std::abs(prepared.RangeMin) * 1.0e-6f);
                        prepared.RangeMin -= delta;
                        prepared.RangeMax += delta;
                    }
                }
                if (!std::isfinite(prepared.RangeMin) ||
                    !std::isfinite(prepared.RangeMax) ||
                    prepared.RangeMin >= prepared.RangeMax)
                {
                    return PrepareFailure(
                        PropertyTextureBakeStatus::InvalidRange,
                        "texture bake display range is invalid");
                }
            }
            else
            {
                prepared.RangeMin = 0.0f;
                prepared.RangeMax = 1.0f;
            }

            return prepared;
        }

        void FailGpuTextures(const Work& work)
        {
            if (GpuAssets == nullptr) return;
            if (work.CacheGeneration != 0u)
                (void)GpuAssets->FailGpuProducedTexture(work.Asset, work.CacheGeneration);
            if (work.CoverageCacheGeneration != 0u)
                (void)GpuAssets->FailGpuProducedTexture(work.CoverageAsset, work.CoverageCacheGeneration);
        }

        void CancelWork(
            const ECS::EntityHandle entity,
            const std::string_view outputName)
        {
            for (std::size_t index = 0u; index < WorkItems.size();)
            {
                Work& work = WorkItems[index];
                if (work.Entity != entity || work.OutputName != outputName)
                {
                    ++index;
                    continue;
                }
                FailGpuTextures(work);
                RetireWorkResources(
                    work,
                    work.ReadyFrame != 0u
                        ? work.ReadyFrame
                        : SafeReleaseFrame());
                WorkItems.erase(
                    WorkItems.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        [[nodiscard]] bool AssetAlive(const Assets::AssetId asset) const noexcept
        {
            return asset.IsValid() &&
                   Context.AssetService != nullptr &&
                   Context.AssetService->IsAlive(asset);
        }

        enum class GeneratedAssetUse : std::uint8_t
        {
            Create,
            Reload,
            // A previous reload or load has not completed yet.
            Busy,
            // The asset service gave up on it; it never becomes Ready again.
            Failed,
            Foreign,
        };

        // In-place reload keeps the generated AssetId stable. AssetService
        // accepts it only for a Ready asset of the same payload type, so this
        // predicts the reload outcome before any asset of a pair is touched.
        [[nodiscard]] GeneratedAssetUse ClassifyGeneratedAsset(
            const Assets::AssetId asset) const
        {
            if (!AssetAlive(asset))
                return GeneratedAssetUse::Create;
            const Core::Expected<Assets::AssetMeta> meta =
                Context.AssetService->GetMeta(asset);
            if (!meta.has_value() ||
                meta->typeId !=
                    Assets::AssetService::TypeIdOf<
                        GeneratedPropertyTextureMetadata>())
            {
                return GeneratedAssetUse::Foreign;
            }
            switch (meta->state)
            {
            case Assets::AssetState::Ready:
                return GeneratedAssetUse::Reload;
            case Assets::AssetState::Failed:
                return GeneratedAssetUse::Failed;
            default:
                return GeneratedAssetUse::Busy;
            }
        }

        [[nodiscard]] static PropertyTextureBakeResult BusyAssetsResult(
            std::string outputName)
        {
            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::JobSubmitFailed,
                .OutputName = std::move(outputName),
                .Diagnostic =
                    "generated property texture assets are still loading from the previous bake; "
                    "retry once they are ready (the previous bake is unchanged)",
            };
        }

        // A failed generated asset is not replaced in place: its id may
        // still be bound elsewhere, so the owner removes the output first.
        [[nodiscard]] static PropertyTextureBakeResult FailedAssetsResult(
            std::string outputName)
        {
            std::string diagnostic =
                "a generated property texture asset of '" + outputName +
                "' failed to load and is not reused; remove this bake output, then bake again";
            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::AssetLoadFailed,
                .OutputName = std::move(outputName),
                .Diagnostic = std::move(diagnostic),
            };
        }

        // Rejections that need no preparation are decided first, so a
        // producer retrying them every frame never re-measures coverage.
        [[nodiscard]] std::optional<PropertyTextureBakeResult>
            RejectBeforePreparation(
                const PropertyTextureBakeRequest& request) const
        {
            if (Context.Scene == nullptr ||
                request.World != Context.World)
            {
                return std::nullopt;
            }
            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return std::nullopt;
            const std::string_view outputName = request.OutputName.empty()
                ? std::string_view{request.Source.Name}
                : std::string_view{request.OutputName};
            const auto queued = std::ranges::count_if(
                WorkItems,
                [entity, outputName](const Work& work)
                {
                    return work.Entity != entity ||
                           work.OutputName != outputName;
                });
            if (static_cast<std::size_t>(queued) >=
                kMaxActivePropertyTextureBakes)
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::JobSubmitFailed,
                    .OutputName = std::string{outputName},
                    .Diagnostic =
                        "property texture bake queue reached its bounded bake count",
                };
            }
            const PropertyTextureBakeRecord* record =
                FindRecord(entity, outputName);
            if (record == nullptr)
                return std::nullopt;
            const GeneratedAssetUse value = ClassifyGeneratedAsset(record->Texture);
            const GeneratedAssetUse coverage =
                ClassifyGeneratedAsset(record->CoverageTexture);
            if (value == GeneratedAssetUse::Failed ||
                coverage == GeneratedAssetUse::Failed)
            {
                return FailedAssetsResult(std::string{outputName});
            }
            if (value == GeneratedAssetUse::Busy ||
                coverage == GeneratedAssetUse::Busy)
            {
                return BusyAssetsResult(std::string{outputName});
            }
            return std::nullopt;
        }

        [[nodiscard]] bool ReloadGeneratedAsset(
            const Assets::AssetId asset,
            const GeneratedPropertyTextureMetadata& metadata)
        {
            return Context.AssetService != nullptr &&
                   Context.AssetService->Reload<
                       GeneratedPropertyTextureMetadata>(
                       asset,
                       [metadata](std::string_view, Assets::AssetId)
                           -> Core::Expected<GeneratedPropertyTextureMetadata>
                       {
                           return metadata;
                       })
                       .has_value();
        }

        [[nodiscard]] Core::Expected<Assets::AssetId> CreateGeneratedAsset(
            const GeneratedPropertyTextureMetadata& metadata)
        {
            if (Context.AssetService == nullptr)
            {
                return Core::Err<Assets::AssetId>(
                    Core::ErrorCode::InvalidState);
            }
            const std::string path =
                "intrinsic-runtime-generated/property-texture/v1/entity-" +
                std::to_string(metadata.StableEntityId) + "-" +
                std::to_string(metadata.Serial) + ".metadata";
            return Context.AssetService->Load<GeneratedPropertyTextureMetadata>(
                path,
                [metadata](std::string_view, Assets::AssetId)
                    -> Core::Expected<GeneratedPropertyTextureMetadata>
                {
                    return metadata;
                });
        }

        [[nodiscard]] PropertyTextureBakeResult Schedule(
            const PropertyTextureBakeRequest& request)
        {
            if (std::optional<PropertyTextureBakeResult> rejected =
                    RejectBeforePreparation(request))
            {
                return std::move(*rejected);
            }
            PreparedPropertyBake prepared = Prepare(request);
            if (!prepared.Succeeded())
            {
                return PropertyTextureBakeResult{
                    .Status = prepared.Status,
                    .OutputName = std::move(prepared.OutputName),
                    .Diagnostic = std::move(prepared.Diagnostic),
                };
            }

            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                request.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::StaleEntity,
                    .Diagnostic = "texture bake entity is stale",
                };
            }
            if (const std::size_t bytes = SourceByteCount(prepared);
                bytes > SourceSnapshotBudget)
            {
                return OversizedSnapshotResult(
                    std::move(prepared.OutputName), bytes, "needs");
            }
            if (!HasScheduleCapacity(
                    entity,
                    prepared.OutputName,
                    prepared))
            {
                return PropertyTextureBakeResult{
                    .Status =
                        PropertyTextureBakeStatus::JobSubmitFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic =
                        "property texture bake queue reached its bounded source-snapshot capacity",
                };
            }

            auto& catalog = Context.Scene->Raw()
                .get_or_emplace<PropertyTextureBakeOutputs>(entity);
            auto found = std::ranges::find(
                catalog.Records,
                prepared.OutputName,
                &PropertyTextureBakeRecord::OutputName);
            const bool replacing = found != catalog.Records.end();
            // Appearance owns a stable output slot while its selected property changes.
            if (replacing && prepared.OutputName != kSurfaceAppearanceTextureOutput &&
                (found->Source.Domain != request.Source.Domain ||
                 found->Source.Name != request.Source.Name ||
                 found->Texcoords != request.Texcoords))
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::CommandFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic =
                        "output name already belongs to another property texture; rename it first",
                };
            }
            if (!replacing &&
                request.ExistingGeneratedTexture.IsValid() &&
                AssetOwnedByAnotherRecord(
                    request.ExistingGeneratedTexture,
                    entity,
                    prepared.OutputName))
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::CommandFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic =
                        "existing generated texture is owned by another bake record",
                };
            }
            std::uint64_t serial = NextAssetSerial++;
            if (serial == 0u)
                serial = NextAssetSerial++;
            const GeneratedPropertyTextureMetadata metadata{
                .StableEntityId = request.StableEntityId,
                .OutputName = prepared.OutputName,
                .SourceDomain = request.Source.Domain,
                .SourcePropertyName = request.Source.Name,
                .TexcoordPropertyName =
                    prepared.Identity.ResolvedTexcoords.Name,
                .ValueKind = prepared.ValueKind,
                .Storage = prepared.Storage,
                .Encoder = prepared.Encoder,
                .EncodingColormap = prepared.EncodingColormap,
                .RangeMin = prepared.RangeMin,
                .RangeMax = prepared.RangeMax,
                .Width = prepared.Width,
                .Height = prepared.Height,
                .SourceGeneration = prepared.SourceGeneration,
                .Serial = serial,
            };
            auto coverageMetadata = metadata;
            coverageMetadata.OutputName += ".coverage";
            coverageMetadata.Serial = NextAssetSerial++;
            if (coverageMetadata.Serial == 0u) coverageMetadata.Serial = NextAssetSerial++;
            coverageMetadata.ValueKind = Geometry::PropertyValueKind::Float;
            coverageMetadata.Storage = PropertyTextureBakeStorage::RawFloat;
            coverageMetadata.Encoder = PropertyTextureBakeEncoding::LinearScalar;

            // The value/coverage pair is admitted as a unit: live assets are
            // reloaded in place (stable ids) and missing ones are created.
            // Every precondition is checked and every creation completes
            // before either existing asset's metadata is replaced; created
            // assets are destroyed again if the pair cannot be completed.
            const auto assetFailure = [&prepared](std::string diagnostic)
            {
                return PropertyTextureBakeResult{
                    .Status = PropertyTextureBakeStatus::AssetLoadFailed,
                    .OutputName = prepared.OutputName,
                    .Diagnostic = std::move(diagnostic),
                };
            };
            const Assets::AssetId existingValue = replacing
                ? found->Texture
                : request.ExistingGeneratedTexture;
            const Assets::AssetId existingCoverage = replacing
                ? found->CoverageTexture
                : Assets::AssetId{};
            const GeneratedAssetUse valueUse =
                ClassifyGeneratedAsset(existingValue);
            const GeneratedAssetUse coverageUse =
                ClassifyGeneratedAsset(existingCoverage);
            if (valueUse == GeneratedAssetUse::Foreign ||
                coverageUse == GeneratedAssetUse::Foreign)
            {
                return assetFailure(
                    "existing generated texture is not a property texture asset");
            }
            if (valueUse == GeneratedAssetUse::Failed ||
                coverageUse == GeneratedAssetUse::Failed)
            {
                return FailedAssetsResult(prepared.OutputName);
            }
            if (valueUse == GeneratedAssetUse::Busy ||
                coverageUse == GeneratedAssetUse::Busy)
            {
                return BusyAssetsResult(prepared.OutputName);
            }
            const bool reuseValue = valueUse == GeneratedAssetUse::Reload;
            const bool reuseCoverage = coverageUse == GeneratedAssetUse::Reload;
            Assets::AssetId valueAsset = existingValue;
            Assets::AssetId coverageAsset = existingCoverage;
            Assets::AssetId createdValue{};
            if (!reuseValue)
            {
                auto created = CreateGeneratedAsset(metadata);
                if (!created.has_value())
                    return assetFailure("failed to create generated property texture asset");
                valueAsset = createdValue = *created;
            }
            if (!reuseCoverage)
            {
                auto created = CreateGeneratedAsset(coverageMetadata);
                if (!created.has_value())
                {
                    (void)DestroyAsset(createdValue);
                    return assetFailure("failed to create property texture coverage asset");
                }
                coverageAsset = *created;
            }
            const Assets::AssetId createdCoverage =
                reuseCoverage ? Assets::AssetId{} : coverageAsset;
            if (reuseValue && !ReloadGeneratedAsset(valueAsset, metadata))
            {
                (void)DestroyAsset(createdCoverage);
                return assetFailure(
                    "failed to reload generated property texture asset; the previous bake is unchanged");
            }
            if (reuseCoverage &&
                !ReloadGeneratedAsset(coverageAsset, coverageMetadata))
            {
                (void)DestroyAsset(createdValue);
                if (!reuseValue)
                {
                    return assetFailure(
                        "failed to reload property texture coverage asset; the previous bake is unchanged");
                }
                // Only an internal asset-service failure after a successful
                // precheck reaches this point. The value asset now carries
                // the new metadata, so the previous pair is not left bindable.
                CancelWork(entity, prepared.OutputName);
                found->State = PropertyTextureBakeOutputState::Failed;
                found->Diagnostic =
                    "coverage asset reload failed after the value asset was reloaded; rebake required";
                found->UvFingerprint = 0u;
                found->PositionFingerprint = 0u;
                found->TopologyFingerprint = 0u;
                found->PropertyFingerprint = 0u;
                found->Freshness = PropertyTextureBakeFreshness::Unknown;
                AdvanceGeneration(found->Generation);
                AdvanceGeneration(catalog.Generation);
                return assetFailure(found->Diagnostic);
            }
            CancelWork(entity, prepared.OutputName);

            PropertyTextureBakeRecord record{
                .OutputName = prepared.OutputName,
                .Source = GeometryPropertyRef{
                    .Domain = request.Source.Domain,
                    .Name = request.Source.Name,
                    .ValueKind = prepared.ValueKind,
                },
                .Texcoords = request.Texcoords,
                .Storage = prepared.Storage,
                .Encoding = prepared.Encoder,
                .EncodingColormap = prepared.EncodingColormap,
                .Texture = valueAsset,
                .CoverageTexture = coverageAsset,
                .ExpectedElementCount = prepared.ExpectedElementCount,
                .SourceGeneration = prepared.SourceGeneration,
                .PropertyGeneration =
                    request.ExpectedPropertyGeneration,
                .RangeMin = prepared.RangeMin,
                .RangeMax = prepared.RangeMax,
                .Width = prepared.Width,
                .Height = prepared.Height,
                .PaddingTexels = request.PaddingTexels,
                .Generation = replacing ? found->Generation : 1u,
                .State = PropertyTextureBakeOutputState::Pending,
                .Diagnostic = "GPU property texture bake pending",
                .RangePolicy = request.RangePolicy,
                .ResolvedTexcoords = prepared.Identity.ResolvedTexcoords,
                .UvFingerprint = prepared.Identity.UvFingerprint,
                .PositionFingerprint = prepared.Identity.PositionFingerprint,
                .TopologyFingerprint = prepared.Identity.TopologyFingerprint,
                .PropertyFingerprint = prepared.Identity.PropertyFingerprint,
                .Freshness = PropertyTextureBakeFreshness::Fresh,
                .CoveredTexels = prepared.Coverage.CoveredTexels,
                .ChartCount = prepared.Charts.ChartCount,
            };
            record.ObservedRevisionToken =
                ComputePropertyTextureBakeRevisionToken(
                    record,
                    MakeRevisionLookup(
                        GS::BuildConstView(Context.Scene->Raw(), entity)));
            if (replacing)
            {
                AdvanceGeneration(record.Generation);
                *found = record;
            }
            else
            {
                catalog.Records.push_back(record);
            }
            AdvanceGeneration(catalog.Generation);

            WorkItems.push_back(Work{
                .World = Context.World,
                .BindingEpoch = Context.BindingEpoch,
                .Entity = entity,
                .StableEntityId = request.StableEntityId,
                .OutputName = record.OutputName,
                .Asset = record.Texture,
                .CoverageAsset = record.CoverageTexture,
                .RecordGeneration = record.Generation,
                .Request = request,
                .Prepared = std::move(prepared),
                .Width = record.Width,
                .Height = record.Height,
            });

            return PropertyTextureBakeResult{
                .Status = PropertyTextureBakeStatus::Scheduled,
                .ExecutionMode =
                    PropertyTextureBakeExecutionMode::PropertyRasterGpu,
                .State = PropertyTextureBakeOutputState::Pending,
                .GeneratedTexture = record.Texture,
                .Generation = record.Generation,
                .SourceGeneration = record.SourceGeneration,
                .OutputName = record.OutputName,
                .Diagnostic = "GPU property texture bake scheduled",
            };
        }

        [[nodiscard]] RHI::PipelineHandle PipelineFor(const RHI::Format format)
        {
            if (Renderer == nullptr || Device == nullptr)
                return {};
            auto found = std::ranges::find(
                Pipelines,
                format,
                &PipelineEntry::Format);
            if (found != Pipelines.end() &&
                found->Raster.has_value() &&
                found->Raster->IsValid())
            {
                return Renderer->GetPipelineManager().GetDeviceHandle(
                    found->Raster->GetHandle());
            }

            auto lease = Renderer->GetPipelineManager().Create(
                Graphics::MakePropertyTextureBakePipelineDesc(
                    Core::Filesystem::GetShaderPath(
                        "shaders/property_texture_bake.vert.spv"),
                    Core::Filesystem::GetShaderPath(
                        "shaders/property_texture_bake.frag.spv"),
                    format));
            if (!lease.has_value())
                return {};
            if (found == Pipelines.end())
            {
                Pipelines.push_back(PipelineEntry{
                    .Format = format,
                });
                found = std::prev(Pipelines.end());
            }
            found->Raster.emplace(std::move(*lease));
            return Renderer->GetPipelineManager().GetDeviceHandle(
                found->Raster->GetHandle());
        }

        void MarkFailed(Work& work, std::string diagnostic)
        {
            FailGpuTextures(work);
            if (PropertyTextureBakeRecord* record =
                    FindRecord(work.Entity, work.OutputName);
                record != nullptr &&
                record->Generation == work.RecordGeneration)
            {
                record->State = PropertyTextureBakeOutputState::Failed;
                record->Diagnostic = std::move(diagnostic);
            }
            RetireWorkResources(
                work,
                work.ReadyFrame != 0u
                    ? work.ReadyFrame
                    : SafeReleaseFrame());
            work.Phase = WorkPhase::Failed;
        }

        template <class T>
        [[nodiscard]] std::optional<RHI::BufferManager::BufferLease>
            UploadStorage(
                const std::span<const T> values,
                const RHI::BufferUsage extraUsage,
                const char* const debugName,
                std::uint64_t& outAddress)
        {
            outAddress = 0u;
            RHI::BufferDesc desc{};
            desc.SizeBytes = static_cast<std::uint64_t>(values.size_bytes());
            desc.Usage = RHI::BufferUsage::Storage |
                         RHI::BufferUsage::TransferDst |
                         extraUsage;
            desc.HostVisible = true;
            desc.DebugName = debugName;
            auto lease = Renderer->GetBufferManager().Create(desc);
            if (!lease.has_value())
                return std::nullopt;
            Device->WriteBuffer(
                lease->GetHandle(),
                values.data(),
                desc.SizeBytes,
                0u);
            outAddress = Device->GetBufferDeviceAddress(lease->GetHandle());
            if (outAddress == 0u)
                return std::nullopt;
            return std::move(*lease);
        }

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            if (!Available())
                return;

            DrainRetiredResources();
            std::size_t recorded = 0u;
            for (Work& work : WorkItems)
            {
                if (work.Phase != WorkPhase::Queued || recorded >= 4u)
                    continue;
                if (work.World != Context.World ||
                    work.BindingEpoch != Context.BindingEpoch ||
                    !Context.Scene->IsValid(work.Entity))
                {
                    MarkFailed(work, "property texture bake target became stale");
                    continue;
                }
                const PropertyTextureBakeRecord* record =
                    FindRecord(work.Entity, work.OutputName);
                if (record == nullptr ||
                    record->Generation != work.RecordGeneration ||
                    record->Texture != work.Asset)
                {
                    MarkFailed(work, "property texture bake was superseded");
                    continue;
                }
                std::string sourceDiagnostic{};
                if (!SourceStillCurrent(work, sourceDiagnostic))
                {
                    MarkFailed(work, std::move(sourceDiagnostic));
                    continue;
                }

                std::uint32_t encodingColormapId = 0u;
                if (work.Prepared.GpuEncoding ==
                    Graphics::PropertyTextureBakeEncoding::ScalarColormap)
                {
                    encodingColormapId = Renderer->GetColormapSystem()
                        .GetBindlessIndex(
                            work.Prepared.EncodingColormap);
                    if (encodingColormapId == RHI::kInvalidBindlessIndex)
                    {
                        // Colormap LUT uploads are asynchronous. Keep the
                        // bake queued until the selected LUT is sampleable.
                        continue;
                    }
                }

                const PreparedPropertyBake& prepared = work.Prepared;
                BakeGpuResources resources{};
                std::uint64_t texcoordBda = 0u;
                std::uint64_t propertyBda = 0u;
                std::uint64_t indexBda = 0u;
                std::uint64_t chartBda = 0u;
                std::uint64_t gutterBda = 0u;
                resources.TexcoordBuffer = UploadStorage(
                    std::span<const glm::vec2>{prepared.Texcoords},
                    RHI::BufferUsage::None,
                    "Runtime.PropertyTextureBake.Texcoords",
                    texcoordBda);
                resources.PropertyBuffer = UploadStorage(
                    std::span<const glm::vec4>{prepared.Values},
                    RHI::BufferUsage::None,
                    "Runtime.PropertyTextureBake.Values",
                    propertyBda);
                resources.IndexBuffer = UploadStorage(
                    std::span<const std::uint32_t>{prepared.SurfaceIndices},
                    RHI::BufferUsage::Index,
                    "Runtime.PropertyTextureBake.Indices",
                    indexBda);
                resources.ChartBuffer = UploadStorage(
                    std::span<const std::uint32_t>{
                        prepared.Charts.TriangleChart},
                    RHI::BufferUsage::None,
                    "Runtime.PropertyTextureBake.Charts",
                    chartBda);
                const bool drawGutter = work.Request.PaddingTexels != 0u &&
                                        !prepared.GutterEdges.empty();
                if (drawGutter)
                {
                    resources.GutterBuffer = UploadStorage(
                        std::span<const Graphics::PropertyTextureBakeGutterEdge>{
                            prepared.GutterEdges},
                        RHI::BufferUsage::None,
                        "Runtime.PropertyTextureBake.GutterEdges",
                        gutterBda);
                }
                if (!resources.TexcoordBuffer.has_value() ||
                    !resources.PropertyBuffer.has_value() ||
                    !resources.IndexBuffer.has_value() ||
                    !resources.ChartBuffer.has_value() ||
                    (drawGutter && !resources.GutterBuffer.has_value()))
                {
                    work.Resources = std::move(resources);
                    MarkFailed(
                        work,
                        "property texture bake GPU input-buffer allocation failed");
                    continue;
                }

                const RHI::PipelineHandle pipeline =
                    PipelineFor(prepared.Format);
                if (!pipeline.IsValid())
                {
                    work.Resources = std::move(resources);
                    MarkFailed(
                        work,
                        "property texture bake raster pipeline is unavailable");
                    continue;
                }
                auto depth = Renderer->GetTextureManager().Create(
                    Graphics::MakePropertyTextureBakeDepthTextureDesc(
                        work.Width,
                        work.Height,
                        "Runtime.PropertyTextureBake.Depth"));
                if (depth.has_value())
                    resources.Depth.emplace(std::move(*depth));
                if (!resources.Depth.has_value())
                {
                    work.Resources = std::move(resources);
                    MarkFailed(
                        work,
                        "property texture bake coverage/depth allocation failed");
                    continue;
                }

                // Labels are discrete: nearest sampling never blends two
                // label colours. Other encodings filter linearly within the
                // requested gutter; there is one mip level and no mip filter.
                const RHI::FilterMode filter =
                    prepared.Encoder == PropertyTextureBakeEncoding::LabelPalette
                        ? RHI::FilterMode::Nearest
                        : RHI::FilterMode::Linear;
                Graphics::GpuProducedTextureRequest textureRequest{};
                textureRequest.Id = work.Asset;
                textureRequest.Desc = RHI::TextureDesc{
                    .Width = work.Width,
                    .Height = work.Height,
                    .MipLevels = 1u,
                    .Fmt = prepared.Format,
                    .Usage = RHI::TextureUsage::Sampled |
                             RHI::TextureUsage::ColorTarget |
                             RHI::TextureUsage::TransferSrc,
                    .InitialLayout = RHI::TextureLayout::Undefined,
                    .DebugName = "Runtime.PropertyTextureBake.Output",
                };
                textureRequest.SamplerDesc = RHI::SamplerDesc{
                    .MagFilter = filter,
                    .MinFilter = filter,
                    .MipFilter = RHI::MipmapMode::Nearest,
                    .AddressU = RHI::AddressMode::ClampToEdge,
                    .AddressV = RHI::AddressMode::ClampToEdge,
                    .AddressW = RHI::AddressMode::ClampToEdge,
                    .DebugName = "Runtime.PropertyTextureBake.Sampler",
                };
                auto pending =
                    GpuAssets->BeginGpuProducedTexture(textureRequest);
                if (!pending.has_value())
                {
                    if (pending.error() == Core::ErrorCode::ResourceBusy)
                    {
                        RetireWorkResources(resources, SafeReleaseFrame());
                        continue;
                    }
                    work.Resources = std::move(resources);
                    MarkFailed(
                        work,
                        "property texture bake output allocation failed");
                    continue;
                }

                work.CacheGeneration = pending->Generation;
                Graphics::GpuProducedTextureRequest coverageRequest{};
                coverageRequest.Id = work.CoverageAsset;
                coverageRequest.Desc = Graphics::MakePropertyTextureBakeCoverageTextureDesc(work.Width, work.Height);
                coverageRequest.SamplerDesc = textureRequest.SamplerDesc;
                coverageRequest.SamplerDesc.MagFilter = RHI::FilterMode::Nearest;
                coverageRequest.SamplerDesc.MinFilter = RHI::FilterMode::Nearest;
                auto coveragePending = GpuAssets->BeginGpuProducedTexture(coverageRequest);
                if (!coveragePending.has_value())
                {
                    work.Resources = std::move(resources);
                    MarkFailed(work, "property texture coverage allocation failed");
                    continue;
                }
                work.CoverageCacheGeneration = coveragePending->Generation;

                const Core::Result recordedResult =
                    Graphics::RecordPropertyTextureBake(
                        commandContext,
                        Graphics::PropertyTextureBakeRecordDesc{
                            .Pipeline = pipeline,
                            .OutputTexture = pending->Texture,
                            .CoverageTexture = coveragePending->Texture,
                            .DepthTexture = resources.Depth->GetHandle(),
                            .IndexBuffer = resources.IndexBuffer->GetHandle(),
                            .TexcoordBDA = texcoordBda,
                            .PropertyBDA = propertyBda,
                            .IndexBDA = indexBda,
                            .ChartBDA = chartBda,
                            .GutterEdgeBDA = gutterBda,
                            .IndexCount = static_cast<std::uint32_t>(
                                prepared.SurfaceIndices.size()),
                            .GutterEdgeCount = drawGutter
                                ? static_cast<std::uint32_t>(
                                      prepared.GutterEdges.size())
                                : 0u,
                            .Width = work.Width,
                            .Height = work.Height,
                            .PaddingTexels = work.Request.PaddingTexels,
                            .Domain = prepared.Domain,
                            .ValueKind = prepared.GpuValueKind,
                            .Encoding = prepared.GpuEncoding,
                            .ColormapID = encodingColormapId,
                            .RangeMin = prepared.RangeMin,
                            .RangeMax = prepared.RangeMax,
                        });
                work.Resources = std::move(resources);
                work.CacheGeneration = pending->Generation;
                if (!recordedResult.has_value())
                {
                    MarkFailed(
                        work,
                        "property texture bake command recording failed");
                    continue;
                }

                const std::uint64_t readyFrame = SafeReleaseFrame();
                work.ReadyFrame = readyFrame;
                if (Core::Result ready =
                        GpuAssets->SetGpuProducedTextureReadyFrame(
                            work.Asset,
                            pending->Generation,
                            readyFrame);
                    !ready.has_value())
                {
                    MarkFailed(
                        work,
                        "property texture bake ready-frame publication failed");
                    continue;
                }

                if (!GpuAssets->SetGpuProducedTextureReadyFrame(
                        work.CoverageAsset, work.CoverageCacheGeneration, readyFrame).has_value())
                {
                    MarkFailed(work, "property texture coverage ready-frame publication failed");
                    continue;
                }
                work.Phase = WorkPhase::WaitingForReadyFrame;
                ++recorded;
            }

            std::erase_if(
                WorkItems,
                [](const Work& work)
                {
                    return work.Phase == WorkPhase::Failed;
                });
        }

        void DrainCompletedTransfers()
        {
            if (Device == nullptr || GpuAssets == nullptr)
                return;
            DrainRetiredResources();
            for (std::size_t index = 0u; index < WorkItems.size();)
            {
                Work& work = WorkItems[index];
                if (work.Phase != WorkPhase::WaitingForReadyFrame ||
                    Device->GetGlobalFrameNumber() < work.ReadyFrame)
                {
                    ++index;
                    continue;
                }
                const Graphics::GpuAssetState state =
                    GpuAssets->GetState(work.Asset);
                const auto coverageState = GpuAssets->GetState(work.CoverageAsset);
                if (state == Graphics::GpuAssetState::GpuUploading ||
                    state == Graphics::GpuAssetState::CpuPending ||
                    coverageState == Graphics::GpuAssetState::GpuUploading ||
                    coverageState == Graphics::GpuAssetState::CpuPending)
                {
                    ++index;
                    continue;
                }

                PropertyTextureBakeRecord* record =
                    FindRecord(work.Entity, work.OutputName);
                const auto view = GpuAssets->GetView(work.Asset);
                const auto coverageView = GpuAssets->GetView(work.CoverageAsset);
                const bool current =
                    Context.Scene != nullptr &&
                    work.World == Context.World &&
                    work.BindingEpoch == Context.BindingEpoch &&
                    Context.Scene->IsValid(work.Entity) &&
                    record != nullptr &&
                    record->Generation == work.RecordGeneration &&
                    record->Texture == work.Asset && record->CoverageTexture == work.CoverageAsset;
                std::string sourceDiagnostic{};
                const bool sourceCurrent =
                    current && SourceStillCurrent(work, sourceDiagnostic);
                // Names why one output of the pair is not this bake's result.
                const auto outputProblem = [](
                    const Graphics::GpuAssetState outputState,
                    const auto& outputView,
                    const std::uint64_t generation) -> const char*
                {
                    if (outputState == Graphics::GpuAssetState::Failed)
                        return "failed on the GPU";
                    if (outputState != Graphics::GpuAssetState::Ready ||
                        !outputView.has_value())
                    {
                        return "is no longer resident";
                    }
                    if (outputView->Generation != generation)
                        return "was replaced by a newer GPU generation";
                    return nullptr;
                };
                const char* const valueProblem =
                    outputProblem(state, view, work.CacheGeneration);
                const char* const coverageProblem = outputProblem(
                    coverageState, coverageView, work.CoverageCacheGeneration);
                if (sourceCurrent &&
                    valueProblem == nullptr &&
                    coverageProblem == nullptr)
                {
                    record->State = PropertyTextureBakeOutputState::Ready;
                    record->Diagnostic = "GPU property texture bake ready";
                    RefreshFreshness(work.Entity, *record);
                }
                else if (current)
                {
                    record->State = PropertyTextureBakeOutputState::Failed;
                    if (!sourceCurrent)
                    {
                        record->Diagnostic = std::move(sourceDiagnostic);
                    }
                    else if (valueProblem != nullptr)
                    {
                        record->Diagnostic =
                            std::string{"GPU property texture bake value output "} +
                            valueProblem;
                    }
                    else
                    {
                        record->Diagnostic =
                            std::string{"GPU property texture bake coverage output "} +
                            coverageProblem +
                            "; the value texture is not published without it";
                    }
                }
                RetireWorkResources(work, work.ReadyFrame);
                WorkItems.erase(
                    WorkItems.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

        // Re-evaluates one record against the live source. The revision token
        // makes the per-frame check O(watched properties); content is hashed
        // again only after one of those properties was mutably borrowed.
        [[nodiscard]] static PropertyTextureBakeFreshness EvaluateFreshness(
            const GS::ConstSourceView& view,
            const PropertyTextureBakeRecord& record,
            const std::uint64_t token)
        {
            if (token != 0u && token == record.ObservedRevisionToken &&
                record.Freshness != PropertyTextureBakeFreshness::Unknown)
            {
                return record.Freshness;
            }
            const PropertyTextureBakeSourceIdentity baked =
                record.SourceIdentity();
            if (baked.UvFingerprint == 0u)
                return PropertyTextureBakeFreshness::Unknown;
            const CapturedSourceIdentity current = CaptureSourceIdentity(
                view,
                record.Source,
                record.Texcoords);
            if (current.Status != PropertyTextureBakeStatus::Success)
                return PropertyTextureBakeFreshness::SourceUnavailable;
            return ComparePropertyTextureBakeSourceIdentity(
                baked,
                current.Identity);
        }

        void RefreshFreshness(
            const ECS::EntityHandle entity,
            PropertyTextureBakeRecord& record) const
        {
            if (Context.Scene == nullptr || !Context.Scene->IsValid(entity))
                return;
            const GS::ConstSourceView view =
                GS::BuildConstView(Context.Scene->Raw(), entity);
            const std::uint64_t token =
                ComputePropertyTextureBakeRevisionToken(
                    record,
                    MakeRevisionLookup(view));
            record.Freshness = EvaluateFreshness(view, record, token);
            record.ObservedRevisionToken = token;
        }

        // Maintenance pass: keeps every catalog's stored freshness current so
        // extraction and UI read an honest state. Stale records stay for
        // inspection; binding consumers reject them.
        void RefreshAllFreshness()
        {
            if (Context.Scene == nullptr)
                return;
            auto view = Context.Scene->Raw().view<PropertyTextureBakeOutputs>();
            for (auto&& [entity, catalog] : view.each())
            {
                for (PropertyTextureBakeRecord& record : catalog.Records)
                {
                    const PropertyTextureBakeFreshness before =
                        record.Freshness;
                    RefreshFreshness(entity, record);
                    if (record.Freshness != before)
                        AdvanceGeneration(catalog.Generation);
                }
            }
        }

        [[nodiscard]] bool HasInFlightWork() const noexcept
        {
            return !WorkItems.empty() ||
                   !RetiredResources.empty();
        }

        [[nodiscard]] bool DestroyAsset(const Assets::AssetId asset)
        {
            if (!asset.IsValid())
                return true;
            if (Context.AssetService == nullptr)
                return false;
            if (Context.AssetService->IsAlive(asset))
            {
                if (const Core::Result destroyed =
                        Context.AssetService->Destroy(asset);
                    !destroyed.has_value())
                {
                    return false;
                }
            }
            if (GpuAssets != nullptr)
                GpuAssets->NotifyDestroyed(asset);
            return true;
        }

        void DetachTargets(
            const WorldHandle world,
            const std::uint64_t bindingEpoch,
            const bool destroyGeneratedAssets) noexcept
        {
            for (std::size_t index = 0u; index < WorkItems.size();)
            {
                Work& work = WorkItems[index];
                if (work.World != world || work.BindingEpoch != bindingEpoch)
                {
                    ++index;
                    continue;
                }
                FailGpuTextures(work);
                if (PropertyTextureBakeRecord* record =
                        FindRecord(work.Entity, work.OutputName);
                    record != nullptr &&
                    record->Generation == work.RecordGeneration)
                {
                    record->State = PropertyTextureBakeOutputState::Failed;
                    record->Diagnostic =
                        "GPU property texture bake cancelled because its scene binding was detached";
                }
                RetireWorkResources(
                    work,
                    work.ReadyFrame != 0u
                        ? work.ReadyFrame
                        : SafeReleaseFrame());
                WorkItems.erase(
                    WorkItems.begin() + static_cast<std::ptrdiff_t>(index));
            }

            if (Context.World == world &&
                Context.BindingEpoch == bindingEpoch &&
                Context.Scene != nullptr)
            {
                auto view = Context.Scene->Raw().view<PropertyTextureBakeOutputs>();
                for (auto&& [entity, catalog] : view.each())
                {
                    (void)entity;
                    for (const PropertyTextureBakeRecord& record :
                         catalog.Records)
                    {
                        if (destroyGeneratedAssets)
                        {
                            (void)DestroyAsset(record.Texture);
                            (void)DestroyAsset(record.CoverageTexture);
                        }
                    }
                }
            }
        }

        void DestroySceneAssets(ECS::Scene::Registry& scene) noexcept
        {
            auto view = scene.Raw().view<PropertyTextureBakeOutputs>();
            for (auto&& [entity, catalog] : view.each())
            {
                (void)entity;
                for (const PropertyTextureBakeRecord& record : catalog.Records)
                {
                    (void)DestroyAsset(record.Texture);
                    (void)DestroyAsset(record.CoverageTexture);
                }
            }
        }

        void ShutdownAfterDeviceIdle()
        {
            for (Work& work : WorkItems)
            {
                FailGpuTextures(work);
            }
            WorkItems.clear();
            RetiredResources.clear();
            Pipelines.clear();
        }

        [[nodiscard]] GpuQueueParticipantDesc MakeParticipantDesc()
        {
            return GpuQueueParticipantDesc{
                .DebugName = "Runtime.PropertyTextureBakeGpuQueue",
                .RecordFrameCommands =
                    [this](RHI::ICommandContext& commandContext)
                    {
                        RecordFrameCommands(commandContext);
                    },
                .DrainCompletedTransfers =
                    [this]
                    {
                        DrainCompletedTransfers();
                    },
                .HasInFlightWork =
                    [this]
                    {
                        return HasInFlightWork();
                    },
                .ShutdownAfterDeviceIdle =
                    [this]
                    {
                        ShutdownAfterDeviceIdle();
                    },
            };
        }

        [[nodiscard]] TextureBakeMutationResult Rename(
            const std::uint32_t stableEntityId,
            const std::string_view currentName,
            const std::string_view newName)
        {
            if (Context.Scene == nullptr)
                return {TextureBakeMutationStatus::MissingScene, "no active scene"};
            if (newName.empty())
                return {TextureBakeMutationStatus::InvalidName, "output name must not be empty"};
            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return {TextureBakeMutationStatus::StaleEntity, "entity is stale"};
            auto* catalog = Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity);
            if (catalog == nullptr)
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            if (std::ranges::any_of(
                    catalog->Records,
                    [newName](const PropertyTextureBakeRecord& record)
                    {
                        return record.OutputName == newName;
                    }))
            {
                return {TextureBakeMutationStatus::DuplicateName, "output name already exists"};
            }
            auto found = std::ranges::find(
                catalog->Records,
                currentName,
                &PropertyTextureBakeRecord::OutputName);
            if (found == catalog->Records.end())
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            const std::string oldName = found->OutputName;
            found->OutputName = std::string{newName};
            AdvanceGeneration(found->Generation);
            AdvanceGeneration(catalog->Generation);
            for (Work& work : WorkItems)
            {
                if (work.Entity == entity && work.OutputName == oldName)
                {
                    work.OutputName = found->OutputName;
                    work.Request.OutputName = found->OutputName;
                    work.RecordGeneration = found->Generation;
                }
            }
            return {TextureBakeMutationStatus::Success, "baked texture renamed"};
        }

        [[nodiscard]] TextureBakeMutationResult Remove(
            const std::uint32_t stableEntityId,
            const std::string_view outputName)
        {
            if (Context.Scene == nullptr)
                return {TextureBakeMutationStatus::MissingScene, "no active scene"};
            const ECS::EntityHandle entity = ResolveEntity(
                *Context.Scene,
                stableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return {TextureBakeMutationStatus::StaleEntity, "entity is stale"};
            auto* catalog = Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity);
            if (catalog == nullptr)
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};
            auto found = std::ranges::find(
                catalog->Records,
                outputName,
                &PropertyTextureBakeRecord::OutputName);
            if (found == catalog->Records.end())
                return {TextureBakeMutationStatus::MissingTexture, "baked texture was not found"};

            // Removal tears down the in-flight bake and attempts both
            // destroys. A partial failure keeps an unbindable record naming
            // only the surviving asset, so removing again retries just that.
            CancelWork(entity, found->OutputName);
            const bool coverageDestroyed = DestroyAsset(found->CoverageTexture);
            const bool valueDestroyed = DestroyAsset(found->Texture);
            if (!coverageDestroyed || !valueDestroyed)
            {
                if (coverageDestroyed)
                    found->CoverageTexture = {};
                if (valueDestroyed)
                    found->Texture = {};
                found->State = PropertyTextureBakeOutputState::Failed;
                found->Diagnostic = std::string{"generated "} +
                    (!valueDestroyed && !coverageDestroyed
                         ? "value and coverage assets"
                         : !valueDestroyed ? "value asset" : "coverage asset") +
                    " could not be destroyed; remove again to retry";
                AdvanceGeneration(found->Generation);
                AdvanceGeneration(catalog->Generation);
                return {
                    TextureBakeMutationStatus::AssetDestroyFailed,
                    found->Diagnostic,
                };
            }
            catalog->Records.erase(found);
            AdvanceGeneration(catalog->Generation);
            return {TextureBakeMutationStatus::Success, "baked texture removed"};
        }
    };

    TextureBakeService::TextureBakeService()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    TextureBakeService::~TextureBakeService() = default;

    bool TextureBakeService::Available() const noexcept
    {
        return m_Impl && m_Impl->Available();
    }

    PropertyTextureBakeResult TextureBakeService::Bake(
        const PropertyTextureBakeRequest& request)
    {
        if (!m_Impl)
            return UnavailableBakeResult();
        if (m_Impl->Stats != nullptr)
            ++m_Impl->Stats->BakeRequests;
        if (!m_Impl->Available())
        {
            if (m_Impl->Stats != nullptr)
                ++m_Impl->Stats->BakeRequestsRejected;
            return UnavailableBakeResult();
        }
        PropertyTextureBakeResult result = m_Impl->Schedule(request);
        if (m_Impl->Stats != nullptr)
        {
            if (result.Succeeded())
                ++m_Impl->Stats->BakeRequestsAccepted;
            else
                ++m_Impl->Stats->BakeRequestsRejected;
        }
        return result;
    }

    void TextureBakeService::SetSourceSnapshotBudgetForTest(const std::size_t bytes) noexcept
    {
        if (m_Impl)
            m_Impl->SourceSnapshotBudget = bytes;
    }

    TextureBakeModuleStats TextureBakeService::Stats() const noexcept
    {
        return m_Impl && m_Impl->Stats != nullptr
            ? *m_Impl->Stats
            : TextureBakeModuleStats{};
    }

    TextureBakeSnapshot TextureBakeService::Snapshot(
        const std::uint32_t stableEntityId) const
    {
        TextureBakeSnapshot snapshot{};
        snapshot.GpuOperational = Available();
        if (!m_Impl || m_Impl->Context.Scene == nullptr)
        {
            snapshot.Diagnostic = "texture-bake module has no active scene";
            return snapshot;
        }
        const ECS::EntityHandle entity = ResolveEntity(
            *m_Impl->Context.Scene,
            stableEntityId);
        if (entity == ECS::InvalidEntityHandle)
        {
            snapshot.Diagnostic = "selected entity is stale";
            return snapshot;
        }
        if (const auto* catalog = m_Impl->Context.Scene->Raw()
                .try_get<PropertyTextureBakeOutputs>(entity))
        {
            // Evaluate on read so a copied tab never pairs a just-changed
            // atlas with a texture that maintenance has not re-checked yet.
            snapshot.Textures = catalog->Records;
            for (PropertyTextureBakeRecord& record : snapshot.Textures)
                m_Impl->RefreshFreshness(entity, record);
        }
        return snapshot;
    }

    TextureBakeMutationResult TextureBakeService::Rename(
        const std::uint32_t stableEntityId,
        const std::string_view currentName,
        const std::string_view newName)
    {
        return m_Impl
            ? m_Impl->Rename(stableEntityId, currentName, newName)
            : TextureBakeMutationResult{
                  TextureBakeMutationStatus::MissingScene,
                  "texture-bake module is unavailable"};
    }

    TextureBakeMutationResult TextureBakeService::Remove(
        const std::uint32_t stableEntityId,
        const std::string_view outputName)
    {
        return m_Impl
            ? m_Impl->Remove(stableEntityId, outputName)
            : TextureBakeMutationResult{
                  TextureBakeMutationStatus::MissingScene,
                  "texture-bake module is unavailable"};
    }

    namespace
    {
        // Replay persisted appearance and undo/redo through the same bake producer.
        void ReconcileSurfaceAppearance(TextureBakeService& service,
                                        ECS::Scene::Registry* scene,
                                        const WorldHandle world)
        {
            if (scene == nullptr || !service.Available())
                return;
            namespace G = Graphics::Components;
            auto& raw = scene->Raw();
            auto entities = raw.view<G::VisualizationLaneOverrides>();
            for (auto&& [entity, overrides] : entities.each())
            {
                const G::VisualizationConfig* config = overrides.Surface.has_value()
                    ? &*overrides.Surface : nullptr;
                if (config == nullptr || !config->UseBakedTexture)
                    continue;
                const bool scalar = config->Source == G::VisualizationConfig::ColorSource::ScalarField;
                if (!scalar && config->Source != G::VisualizationConfig::ColorSource::PerVertexBuffer &&
                    config->Source != G::VisualizationConfig::ColorSource::PerFaceBuffer)
                    continue;
                const bool face = scalar ? config->ScalarDomain == G::VisualizationConfig::Domain::Face
                    : config->Source == G::VisualizationConfig::ColorSource::PerFaceBuffer;
                if (scalar && config->ScalarDomain == G::VisualizationConfig::Domain::Edge)
                    continue;
                GeometryPropertyRef property{
                    .Domain = face ? GeometryElementDomain::MeshFace : GeometryElementDomain::MeshVertex,
                    .Name = scalar ? config->ScalarFieldName : config->ColorBufferName,
                };
                const GS::ConstSourceView view = GS::BuildConstView(raw, entity);
                const auto availability = BuildGeometryAvailability(view);
                const auto resolved = ResolveGeometryProperty(availability, property,
                    ResolveGeometryElementCount(availability, property.Domain));
                if (!resolved.Resolved())
                    continue;
                property.ValueKind = resolved.ResolvedValueKind;
                const auto rangePolicy = config->Scalar.AutoRange
                    ? PropertyTextureBakeRangePolicy::AutoFinite : PropertyTextureBakeRangePolicy::Manual;
                const auto encoding = ResolveSurfaceAppearanceEncoding(*config, property.ValueKind);
                // Bake at the extent of the generated atlas that produced the
                // current UVs; authored or unrecorded UVs use the default
                // extent. Appearance never escalates on its own, and a
                // recorded extent above the bake cap fails without allocating.
                const std::optional<MeshUvAtlasExtent> atlas =
                    RefreshMeshUvAtlasExtent(raw, entity);
                const std::uint32_t width = atlas ? atlas->Width : PropertyTextureBakeRequest{}.Width;
                const std::uint32_t height = atlas ? atlas->Height : PropertyTextureBakeRequest{}.Height;
                const auto matchesConfig = [&](const PropertyTextureBakeRecord& record)
                {
                    return record.Width == width && record.Height == height &&
                           record.Source.Name == property.Name &&
                           record.Source.Domain == property.Domain &&
                           record.Source.ValueKind == property.ValueKind &&
                           record.Encoding == encoding && record.RangePolicy == rangePolicy &&
                           record.EncodingColormap == config->Scalar.Map &&
                           (config->Scalar.AutoRange || (record.RangeMin == config->Scalar.RangeMin &&
                                                        record.RangeMax == config->Scalar.RangeMax));
                };
                if (const auto* outputs = raw.try_get<PropertyTextureBakeOutputs>(entity))
                {
                    const auto record = std::ranges::find(outputs->Records,
                        kSurfaceAppearanceTextureOutput, &PropertyTextureBakeRecord::OutputName);
                    if (record != outputs->Records.end() && matchesConfig(*record))
                    {
                        // A Fresh record is this request's current bake; a
                        // stale or unverified one is rebaked against the
                        // current atlas instead of being left unbound.
                        if (record->Freshness == PropertyTextureBakeFreshness::Fresh)
                            continue;
                        // A rejected request is resubmitted only after one of
                        // its dependency revisions changed, never per frame.
                        if (record->State == PropertyTextureBakeOutputState::Failed &&
                            record->RejectedRevisionToken != 0u &&
                            record->RejectedRevisionToken ==
                                ComputePropertyTextureBakeRevisionToken(*record, MakeRevisionLookup(view)))
                        {
                            continue;
                        }
                    }
                }
                const auto baked = service.Bake(PropertyTextureBakeRequest{
                    .World = world,
                    .StableEntityId = SelectionController::ToStableEntityId(entity),
                    .Source = property,
                    .Storage = PropertyTextureBakeStorage::EncodedRgba,
                    .Encoding = encoding,
                    .RangePolicy = rangePolicy,
                    .RangeMin = config->Scalar.RangeMin,
                    .RangeMax = config->Scalar.RangeMax,
                    .EncodingColormap = config->Scalar.Map,
                    .Width = width,
                    .Height = height,
                    .PaddingTexels = 2u,
                    .OutputName = std::string{kSurfaceAppearanceTextureOutput},
                });
                // Only queue pressure and still-loading outputs are
                // transient; retry them next frame without recording a
                // failure. Everything else, including a snapshot larger than
                // the whole budget, is recorded once and gated below.
                if (baked.Status == PropertyTextureBakeStatus::JobSubmitFailed)
                    continue;
                if (!baked.Succeeded())
                {
                    auto& outputs = raw.get_or_emplace<PropertyTextureBakeOutputs>(entity);
                    auto failed = std::ranges::find(outputs.Records, kSurfaceAppearanceTextureOutput,
                        &PropertyTextureBakeRecord::OutputName);
                    if (failed == outputs.Records.end())
                    {
                        outputs.Records.push_back(PropertyTextureBakeRecord{
                            .OutputName = std::string{kSurfaceAppearanceTextureOutput}});
                        failed = std::prev(outputs.Records.end());
                    }
                    failed->Source = property;
                    failed->Texcoords = {};
                    failed->Storage = PropertyTextureBakeStorage::EncodedRgba;
                    failed->Encoding = encoding;
                    failed->RangePolicy = rangePolicy;
                    failed->EncodingColormap = config->Scalar.Map;
                    failed->RangeMin = config->Scalar.RangeMin;
                    failed->RangeMax = config->Scalar.RangeMax;
                    failed->Width = width;
                    failed->Height = height;
                    failed->State = PropertyTextureBakeOutputState::Failed;
                    failed->Diagnostic = baked.Diagnostic;
                    // A rejected request owns no source identity, so the
                    // record stays unverified and never binds. Its revision
                    // token gates the retry above; the generation bump
                    // supersedes any bake still in flight for this slot.
                    failed->UvFingerprint = 0u;
                    failed->PositionFingerprint = 0u;
                    failed->TopologyFingerprint = 0u;
                    failed->PropertyFingerprint = 0u;
                    failed->Freshness = PropertyTextureBakeFreshness::Unknown;
                    failed->RejectedRevisionToken =
                        ComputePropertyTextureBakeRevisionToken(*failed, MakeRevisionLookup(view));
                    AdvanceGeneration(failed->Generation);
                    AdvanceGeneration(outputs.Generation);
                    continue;
                }
            }
        }
    }

    struct TextureBakeModule::Impl
    {
        struct State
        {
            TextureBakeService Service{};
            TextureBakeModuleStats Stats{};

            KernelEventBus* Events{};
            JobService* Jobs{};
            WorldRegistry* Worlds{};
            SceneDocumentModule* Documents{};
            EditorCommandHistory* History{};
            Assets::AssetService* Assets{};
            Graphics::GpuAssetCache* GpuAssets{};
            Graphics::IRenderer* Renderer{};
            RenderExtractionCache* Extraction{};
            RHI::IDevice* Device{};

            WorldHandle BoundWorld{};
            ECS::Scene::Registry* BoundRegistry{};
            std::uint64_t BindingEpoch{1u};
            SceneReplacementParticipantHandle DocumentParticipant{};
            GpuQueueParticipantHandle GpuParticipant{};
            bool AcceptingCallbacks{false};
            bool ShutdownAnnounced{false};
            std::weak_ptr<State> Self{};

            void AdvanceBindingEpoch() noexcept
            {
                ++BindingEpoch;
                if (BindingEpoch == 0u)
                    BindingEpoch = 1u;
            }

            [[nodiscard]] bool BindingIsCurrent() const noexcept
            {
                return AcceptingCallbacks &&
                       Worlds != nullptr &&
                       BoundWorld.IsValid() &&
                       BoundRegistry != nullptr &&
                       Worlds->ActiveWorld() == BoundWorld &&
                       Worlds->Get(BoundWorld) == BoundRegistry;
            }

            void PublishBindingChanged()
            {
                ++Stats.BindingChanges;
                if (Events != nullptr)
                {
                    Events->Publish(TextureBakeBindingChanged{
                        .World = BoundWorld,
                        .BindingEpoch = BindingEpoch,
                    });
                }
            }

            void ClearTarget(const bool destroyGeneratedAssets)
            {
                const WorldHandle outgoingWorld = BoundWorld;
                const std::uint64_t outgoingEpoch = BindingEpoch;
                if (outgoingWorld.IsValid() &&
                    outgoingEpoch != 0u)
                {
                    Service.m_Impl->DetachTargets(
                        outgoingWorld,
                        outgoingEpoch,
                        destroyGeneratedAssets);
                }
                BoundWorld = {};
                BoundRegistry = nullptr;
                AdvanceBindingEpoch();
                Service.m_Impl->SetTarget({}, BindingEpoch, nullptr);
                PublishBindingChanged();
            }

            void BindTo(const WorldHandle world,
                        ECS::Scene::Registry* const registry)
            {
                // OnRegister publishes the producer target early enough for
                // AssetWorkflowModule::OnResolve.  OnResolve then adds the
                // command-history and document-replacement participants, but
                // does not represent a scene replacement.  Keep that second
                // bind idempotent so consumers do not briefly retain an
                // otherwise-identical stale producer epoch.
                if (AcceptingCallbacks &&
                    world.IsValid() &&
                    registry != nullptr &&
                    world == BoundWorld &&
                    registry == BoundRegistry)
                {
                    return;
                }

                BoundWorld = world;
                BoundRegistry = registry;
                AdvanceBindingEpoch();
                if (!AcceptingCallbacks ||
                    !BoundWorld.IsValid() ||
                    BoundRegistry == nullptr)
                {
                    Service.m_Impl->SetTarget({}, BindingEpoch, nullptr);
                    PublishBindingChanged();
                    return;
                }

                Service.m_Impl->SetTarget(
                    BoundWorld,
                    BindingEpoch,
                    BoundRegistry);
                PublishBindingChanged();
            }

            [[nodiscard]] bool ValidateBinding()
            {
                if (!AcceptingCallbacks || Worlds == nullptr)
                    return false;
                const WorldHandle active = Worlds->ActiveWorld();
                ECS::Scene::Registry* const registry = Worlds->Get(active);
                if (active != BoundWorld || registry != BoundRegistry)
                {
                    // Inactive worlds retain completed generated assets and
                    // their catalogs. The asset workflow reconciles
                    // presentation targets after the world becomes active.
                    ClearTarget(false);
                    BindTo(active, registry);
                }
                return BindingIsCurrent();
            }

            void BeforeDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (!AcceptingCallbacks)
                    return;
                if (BoundWorld == context.World &&
                    BoundRegistry == &context.Registry)
                {
                    ClearTarget(true);
                }
            }

            void AfterDocumentReplace(
                const SceneReplacementContext& context)
            {
                if (AcceptingCallbacks)
                    BindTo(context.World, &context.Registry);
            }

            void ReleaseDocumentParticipant() noexcept
            {
                if (Documents != nullptr &&
                    DocumentParticipant.IsValid())
                {
                    (void)Documents->UnregisterReplacementParticipant(
                        DocumentParticipant);
                }
                DocumentParticipant = {};
            }

            void AnnounceShutdown()
            {
                if (ShutdownAnnounced)
                    return;
                ShutdownAnnounced = true;
                AcceptingCallbacks = false;
                ClearTarget(true);
                ReleaseDocumentParticipant();
                Documents = nullptr;
                History = nullptr;
            }
        };

        std::shared_ptr<State> Shared{};
        KernelEventSubscription ActiveWorldChangedSubscription{};
        KernelEventSubscription WorldDestroyedSubscription{};
        KernelEventSubscription ShutdownSubscription{};
        bool ServicePublished{false};

        void Unsubscribe(KernelEventBus* const events) noexcept
        {
            if (events != nullptr)
            {
                if (ActiveWorldChangedSubscription.IsValid())
                    events->Unsubscribe(ActiveWorldChangedSubscription);
                if (WorldDestroyedSubscription.IsValid())
                    events->Unsubscribe(WorldDestroyedSubscription);
                if (ShutdownSubscription.IsValid())
                    events->Unsubscribe(ShutdownSubscription);
            }
            ActiveWorldChangedSubscription = {};
            WorldDestroyedSubscription = {};
            ShutdownSubscription = {};
        }

        void RollBack(KernelEventBus* const events,
                      JobService* const jobs,
                      ServiceRegistry* const services,
                      const bool waitForGpuIdle) noexcept
        {
            if (Shared != nullptr)
            {
                auto& state = *Shared;
                state.AcceptingCallbacks = false;
                state.ReleaseDocumentParticipant();
                if (jobs != nullptr && state.GpuParticipant.IsValid())
                {
                    RHI::IDevice* const device = state.Device;
                    jobs->UnregisterGpuQueueParticipant(
                        state.GpuParticipant,
                        waitForGpuIdle && device != nullptr
                            ? std::function<void()>{[device]
                              {
                                  device->WaitIdle();
                              }}
                            : std::function<void()>{});
                }
                state.GpuParticipant = {};
                state.Service.m_Impl->Unbind();
            }
            Unsubscribe(events);
            if (services != nullptr &&
                ServicePublished &&
                Shared != nullptr)
            {
                (void)services->Withdraw<TextureBakeService>(
                    Shared->Service);
            }
            ServicePublished = false;
            Shared.reset();
        }
    };

    TextureBakeModule::TextureBakeModule()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    TextureBakeModule::~TextureBakeModule() = default;

    std::string_view TextureBakeModule::Name() const noexcept
    {
        return "Runtime.TextureBakeModule";
    }

    Core::Result TextureBakeModule::OnRegister(EngineSetup& setup)
    {
        if (!m_Impl ||
            m_Impl->Shared ||
            m_Impl->ServicePublished ||
            setup.Services().Phase() != ServiceRegistryPhase::Registration ||
            setup.Services().Find<TextureBakeService>() != nullptr)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto assets = setup.Services().Require<Assets::AssetService>(Name());
        auto gpuAssets =
            setup.Services().Require<Graphics::GpuAssetCache>(Name());
        auto renderer = setup.Services().Require<Graphics::IRenderer>(Name());
        auto extraction =
            setup.Services().Require<RenderExtractionCache>(Name());
        auto device = setup.Services().Require<RHI::IDevice>(Name());
        if (!assets.has_value() ||
            !gpuAssets.has_value() ||
            !renderer.has_value() ||
            !extraction.has_value() ||
            !device.has_value())
        {
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        m_Impl->Shared = std::make_shared<Impl::State>();
        auto& state = *m_Impl->Shared;
        state.Self = m_Impl->Shared;
        state.Events = &setup.Events();
        state.Jobs = &setup.Jobs();
        state.Worlds = &setup.Worlds();
        state.Assets = &assets->get();
        state.GpuAssets = &gpuAssets->get();
        state.Renderer = &renderer->get();
        state.Extraction = &extraction->get();
        state.Device = &device->get();
        state.Service.m_Impl->Bind(
            nullptr,
            {},
            0u,
            state.Assets,
            nullptr,
            state.Jobs,
            state.Device,
            state.GpuAssets,
            state.Renderer,
            state.Extraction,
            &state.Stats);

        if (Core::Result provided =
                setup.Services().Provide<TextureBakeService>(
                    state.Service, Name());
            !provided.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return provided;
        }
        m_Impl->ServicePublished = true;

        const std::weak_ptr<Impl::State> weakState = m_Impl->Shared;
        m_Impl->ActiveWorldChangedSubscription =
            setup.Subscribe<ActiveWorldChanged>(
                [weakState](const ActiveWorldChanged&)
                {
                    if (const auto state = weakState.lock())
                        (void)state->ValidateBinding();
                });
        m_Impl->WorldDestroyedSubscription =
            setup.Subscribe<WorldWillBeDestroyed>(
                [weakState](const WorldWillBeDestroyed& event)
                {
                    if (const auto state = weakState.lock(); state)
                    {
                        if (event.World == state->BoundWorld)
                        {
                            state->ClearTarget(true);
                        }
                        else if (state->Worlds != nullptr)
                        {
                            if (ECS::Scene::Registry* const scene =
                                    state->Worlds->Get(event.World);
                                scene != nullptr)
                            {
                                state->Service.m_Impl->DestroySceneAssets(*scene);
                            }
                        }
                    }
                });
        m_Impl->ShutdownSubscription =
            setup.Subscribe<RuntimeShutdownAnnounced>(
                [weakState](const RuntimeShutdownAnnounced&)
                {
                    if (const auto state = weakState.lock())
                        state->AnnounceShutdown();
                });
        if (!m_Impl->ActiveWorldChangedSubscription.IsValid() ||
            !m_Impl->WorldDestroyedSubscription.IsValid() ||
            !m_Impl->ShutdownSubscription.IsValid())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        if (Core::Result hook = setup.RegisterFrameHook(
                FramePhase::Maintenance,
                [weakState](RuntimeFrameHookContext&)
                {
                    if (const auto state = weakState.lock())
                    {
                        if (state->ValidateBinding())
                        {
                            state->Service.m_Impl->RefreshAllFreshness();
                            ReconcileSurfaceAppearance(state->Service, state->BoundRegistry, state->BoundWorld);
                        }
                    }
                });
            !hook.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return hook;
        }

        // AssetWorkflowModule resolves before this module and retains the
        // published service pointer. Publish the active target during the
        // registration pass; command history and the GPU participant are
        // added during OnResolve before imports can run.
        state.AcceptingCallbacks = true;
        state.BindTo(
            state.Worlds->ActiveWorld(),
            state.Worlds->Get(state.Worlds->ActiveWorld()));

        return Core::Ok();
    }

    Core::Result TextureBakeModule::OnResolve(EngineSetup& setup)
    {
        if (!m_Impl ||
            !m_Impl->Shared ||
            !m_Impl->ServicePublished ||
            setup.Services().Find<TextureBakeService>() !=
                &m_Impl->Shared->Service)
        {
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        auto documents =
            setup.Services().Require<SceneDocumentModule>(Name());
        auto history =
            setup.Services().Require<EditorCommandHistory>(Name());
        if (!documents.has_value() || !history.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::ResourceNotFound);
        }

        auto& state = *m_Impl->Shared;
        state.Documents = &documents->get();
        state.History = &history->get();
        state.AcceptingCallbacks = true;
        state.Service.m_Impl->SetCommandHistory(state.History);

        const std::weak_ptr<Impl::State> weakState = m_Impl->Shared;
        auto participant = state.Documents->RegisterReplacementParticipant(
            SceneReplacementParticipantDesc{
                // Scene-document participants are ordered by name. The bake
                // target binds before AssetWorkflow recreates its handoffs,
                // while both still detach synchronously in BeforeReplace.
                .Name = "Runtime.AssetTextureBakeModule",
                .BeforeReplace =
                    [weakState](const SceneReplacementContext& context)
                    {
                        if (const auto state = weakState.lock())
                            state->BeforeDocumentReplace(context);
                    },
                .AfterReplace =
                    [weakState](const SceneReplacementContext& context)
                    {
                        if (const auto state = weakState.lock())
                            state->AfterDocumentReplace(context);
                    },
            });
        if (!participant.has_value())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(participant.error());
        }
        state.DocumentParticipant = *participant;
        state.BindTo(
            state.Worlds->ActiveWorld(),
            state.Worlds->Get(state.Worlds->ActiveWorld()));
        if (!state.ValidateBinding())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }

        state.GpuParticipant =
            state.Service.m_Impl->RegisterGpuQueueParticipant(setup.Jobs());
        if (!state.GpuParticipant.IsValid())
        {
            m_Impl->RollBack(
                &setup.Events(),
                &setup.Jobs(),
                &setup.Services(),
                true);
            return Core::Err(Core::ErrorCode::InvalidState);
        }
        return Core::Ok();
    }

    void TextureBakeModule::OnShutdown(
        RuntimeModuleShutdownContext& context)
    {
        if (!m_Impl)
            return;
        if (m_Impl->Shared)
            m_Impl->Shared->AnnounceShutdown();
        m_Impl->RollBack(
            &context.Events,
            &context.Jobs,
            &context.Services,
            false);
    }
}
