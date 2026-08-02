module;

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <tuple>
#include <utility>

#include <glm/glm.hpp>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.GeometryAvailability;

namespace Extrinsic::Runtime
{
    namespace GS = GeometrySources;
    namespace G = RenderComponents;

    std::string_view ToString(const GeometryElementDomain domain) noexcept
    {
        switch (domain)
        {
        case GeometryElementDomain::Unknown:         return "Unknown";
        case GeometryElementDomain::MeshVertex:      return "MeshVertex";
        case GeometryElementDomain::MeshEdge:        return "MeshEdge";
        case GeometryElementDomain::MeshHalfedge:    return "MeshHalfedge";
        case GeometryElementDomain::MeshFace:        return "MeshFace";
        case GeometryElementDomain::GraphNode:       return "GraphNode";
        case GeometryElementDomain::GraphHalfedge:   return "GraphHalfedge";
        case GeometryElementDomain::GraphEdge:       return "GraphEdge";
        case GeometryElementDomain::PointCloudPoint: return "PointCloudPoint";
        }
        return "Unknown";
    }

    std::string_view ToString(const GeometryRenderLane lane) noexcept
    {
        switch (lane)
        {
        case GeometryRenderLane::Surface: return "Surface";
        case GeometryRenderLane::Edges:   return "Edges";
        case GeometryRenderLane::Points:  return "Points";
        }
        return "Unknown";
    }

    std::string_view ToString(const GeometryAvailabilityStatus status) noexcept
    {
        switch (status)
        {
        case GeometryAvailabilityStatus::Supported:             return "Supported";
        case GeometryAvailabilityStatus::NotRequested:          return "NotRequested";
        case GeometryAvailabilityStatus::NoGeometrySource:      return "NoGeometrySource";
        case GeometryAvailabilityStatus::UnsupportedProvenance: return "UnsupportedProvenance";
        case GeometryAvailabilityStatus::MissingPointSource:    return "MissingPointSource";
        case GeometryAvailabilityStatus::MissingEdgeSource:     return "MissingEdgeSource";
        case GeometryAvailabilityStatus::MissingHalfedgeSource: return "MissingHalfedgeSource";
        case GeometryAvailabilityStatus::MissingFaceSource:     return "MissingFaceSource";
        case GeometryAvailabilityStatus::MissingPropertySource: return "MissingPropertySource";
        }
        return "Unknown";
    }

    GeometryEntityAvailability BuildGeometryAvailability(
        const entt::registry& registry,
        const entt::entity entity)
    {
        GeometryEntityAvailability availability{};
        if (entity == entt::null || !registry.valid(entity))
            return availability;

        availability.SourceView = GS::BuildConstView(registry, entity);
        availability.Sources = GS::BuildSourceAvailability(availability.SourceView);

        if (const auto* surface = registry.try_get<G::RenderSurface>(entity))
            availability.Surface = *surface;
        if (const auto* edges = registry.try_get<G::RenderEdges>(entity))
            availability.Edges = *edges;
        if (const auto* points = registry.try_get<G::RenderPoints>(entity))
            availability.Points = *points;

        return availability;
    }

    GeometryEntityAvailability BuildGeometryAvailability(
        const GS::ConstSourceView& view) noexcept
    {
        GeometryEntityAvailability availability{};
        availability.SourceView = view;
        availability.Sources = GS::BuildSourceAvailability(view);
        return availability;
    }

    GeometryRenderLaneAvailability ResolveRenderLaneAvailability(
        const GeometryEntityAvailability& availability,
        const GeometryRenderLane lane) noexcept
    {
        GeometryRenderLaneAvailability result{
            .Lane = lane,
            .Requested = false,
            .Supported = false,
            .Status = GeometryAvailabilityStatus::NotRequested,
            .ProvenanceDomain = availability.Sources.ProvenanceDomain,
        };

        const auto has = [&](const GS::SourceCapability capability) noexcept
        {
            return availability.Sources.Has(capability);
        };

        switch (lane)
        {
        case GeometryRenderLane::Surface:
            result.Requested = availability.Surface.has_value();
            if (!result.Requested)
                return result;
            if (!availability.HasGeometry())
            {
                result.Status = GeometryAvailabilityStatus::NoGeometrySource;
                return result;
            }
            if (availability.Sources.ProvenanceDomain != GS::Domain::Mesh)
            {
                result.Status = GeometryAvailabilityStatus::UnsupportedProvenance;
                return result;
            }
            if (!has(GS::SourceCapability::Vertices))
            {
                result.Status = GeometryAvailabilityStatus::MissingPointSource;
                return result;
            }
            if (!has(GS::SourceCapability::Halfedges))
            {
                result.Status = GeometryAvailabilityStatus::MissingHalfedgeSource;
                return result;
            }
            if (!has(GS::SourceCapability::Faces))
            {
                result.Status = GeometryAvailabilityStatus::MissingFaceSource;
                return result;
            }
            result.Supported = true;
            result.Status = GeometryAvailabilityStatus::Supported;
            return result;

        case GeometryRenderLane::Edges:
            result.Requested = availability.Edges.has_value();
            if (!result.Requested)
                return result;
            if (!availability.HasGeometry())
            {
                result.Status = GeometryAvailabilityStatus::NoGeometrySource;
                return result;
            }
            if (!availability.Sources.HasPointSource())
            {
                result.Status = GeometryAvailabilityStatus::MissingPointSource;
                return result;
            }
            if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                result.Status = GeometryAvailabilityStatus::UnsupportedProvenance;
                return result;
            }
            if (!has(GS::SourceCapability::Edges))
            {
                if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
                {
                    if (!has(GS::SourceCapability::Halfedges))
                    {
                        result.Status = GeometryAvailabilityStatus::MissingHalfedgeSource;
                        return result;
                    }
                    if (!has(GS::SourceCapability::Faces))
                    {
                        result.Status = GeometryAvailabilityStatus::MissingFaceSource;
                        return result;
                    }
                }
                else
                {
                    result.Status = GeometryAvailabilityStatus::MissingEdgeSource;
                    return result;
                }
            }
            result.Supported = true;
            result.Status = GeometryAvailabilityStatus::Supported;
            return result;

        case GeometryRenderLane::Points:
            result.Requested = availability.Points.has_value();
            if (!result.Requested)
                return result;
            if (!availability.HasGeometry())
            {
                result.Status = GeometryAvailabilityStatus::NoGeometrySource;
                return result;
            }
            if (!availability.Sources.HasPointSource())
            {
                result.Status = GeometryAvailabilityStatus::MissingPointSource;
                return result;
            }
            result.Supported = true;
            result.Status = GeometryAvailabilityStatus::Supported;
            return result;
        }

        return result;
    }

    bool SupportsGeometryElementDomain(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain) noexcept
    {
        return ResolveGeometryPropertySet(availability, domain) != nullptr;
    }

    const Geometry::PropertySet* ResolveGeometryPropertySet(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain) noexcept
    {
        const GS::ConstSourceView& view = availability.SourceView;
        const GS::SourceAvailability& sources = availability.Sources;

        switch (domain)
        {
        case GeometryElementDomain::MeshVertex:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.VertexSource
                ? &view.VertexSource->Properties
                : nullptr;
        case GeometryElementDomain::MeshEdge:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.EdgeSource
                ? &view.EdgeSource->Properties
                : nullptr;
        case GeometryElementDomain::MeshHalfedge:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.HalfedgeSource
                ? &view.HalfedgeSource->Properties
                : nullptr;
        case GeometryElementDomain::MeshFace:
            return sources.ProvenanceDomain == GS::Domain::Mesh && view.FaceSource
                ? &view.FaceSource->Properties
                : nullptr;
        case GeometryElementDomain::GraphNode:
            return sources.ProvenanceDomain == GS::Domain::Graph && view.VertexSource
                ? &view.VertexSource->Properties
                : nullptr;
        case GeometryElementDomain::GraphHalfedge:
            return sources.ProvenanceDomain == GS::Domain::Graph && view.HalfedgeSource
                ? &view.HalfedgeSource->Properties
                : nullptr;
        case GeometryElementDomain::GraphEdge:
            return sources.ProvenanceDomain == GS::Domain::Graph && view.EdgeSource
                ? &view.EdgeSource->Properties
                : nullptr;
        case GeometryElementDomain::PointCloudPoint:
            return sources.ProvenanceDomain == GS::Domain::PointCloud && view.VertexSource
                ? &view.VertexSource->Properties
                : nullptr;
        case GeometryElementDomain::Unknown:
            return nullptr;
        }
        return nullptr;
    }

    std::size_t ResolveGeometryElementCount(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain) noexcept
    {
        const Geometry::PropertySet* properties = ResolveGeometryPropertySet(availability, domain);
        return properties ? properties->Size() : 0u;
    }

    // ---- RUNTIME-192 — canonical geometry-property vocabulary ----

    namespace
    {
        // Every element domain a property can actually live on, in the
        // deterministic order the catalog reports them.
        constexpr std::array<GeometryElementDomain, 8> kPropertyElementDomains{
            GeometryElementDomain::MeshVertex,
            GeometryElementDomain::MeshEdge,
            GeometryElementDomain::MeshHalfedge,
            GeometryElementDomain::MeshFace,
            GeometryElementDomain::GraphNode,
            GeometryElementDomain::GraphHalfedge,
            GeometryElementDomain::GraphEdge,
            GeometryElementDomain::PointCloudPoint,
        };

        // Scans a floating-point property for NaN/inf. Templated over the
        // concrete element type so glm vectors reuse glm's componentwise
        // predicate rather than a hand-rolled loop per component.
        template <class T>
        [[nodiscard]] bool ScalarsFinite(
            const Geometry::PropertySet& properties,
            const std::string_view name) noexcept
        {
            const auto property = properties.Get<T>(name);
            if (!property.IsValid())
                return true;

            const auto& values = property.Vector();
            if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
            {
                return std::all_of(values.begin(), values.end(),
                                   [](const T value) noexcept
                                   { return std::isfinite(value); });
            }
            else
            {
                // glm vector: check each component with std::isfinite rather
                // than glm::isfinite, which lives behind GLM_ENABLE_EXPERIMENTAL.
                return std::all_of(values.begin(), values.end(),
                                   [](const T& value) noexcept
                                   {
                                       for (glm::length_t i = 0;
                                            i < T::length();
                                            ++i)
                                       {
                                           if (!std::isfinite(value[i]))
                                               return false;
                                       }
                                       return true;
                                   });
            }
        }
    }

    bool operator==(const GeometryPropertyRef& lhs,
                    const GeometryPropertyRef& rhs) noexcept
    {
        return lhs.Domain == rhs.Domain
            && lhs.ValueKind == rhs.ValueKind
            && lhs.Name == rhs.Name;
    }

    bool operator!=(const GeometryPropertyRef& lhs,
                    const GeometryPropertyRef& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    std::string_view ToString(
        const GeometryPropertyResolutionStatus status) noexcept
    {
        switch (status)
        {
        case GeometryPropertyResolutionStatus::Resolved:
            return "Resolved";
        case GeometryPropertyResolutionStatus::UnsupportedDomain:
            return "UnsupportedDomain";
        case GeometryPropertyResolutionStatus::MissingName:
            return "MissingName";
        case GeometryPropertyResolutionStatus::MissingProperty:
            return "MissingProperty";
        case GeometryPropertyResolutionStatus::ValueKindMismatch:
            return "ValueKindMismatch";
        case GeometryPropertyResolutionStatus::ElementCountMismatch:
            return "ElementCountMismatch";
        case GeometryPropertyResolutionStatus::NonFiniteValues:
            return "NonFiniteValues";
        }
        return "UnsupportedDomain";
    }

    const char* DebugNameForGeometryPropertyValueKind(
        const Geometry::PropertyValueKind kind) noexcept
    {
        switch (kind)
        {
        case Geometry::PropertyValueKind::Unknown: return "Unknown";
        case Geometry::PropertyValueKind::Bool:    return "Bool";
        case Geometry::PropertyValueKind::Int32:   return "Int32";
        case Geometry::PropertyValueKind::UInt32:  return "UInt32";
        case Geometry::PropertyValueKind::UInt64:  return "UInt64";
        case Geometry::PropertyValueKind::Float:   return "Float";
        case Geometry::PropertyValueKind::Double:  return "Double";
        case Geometry::PropertyValueKind::Vec2:    return "Vec2";
        case Geometry::PropertyValueKind::Vec3:    return "Vec3";
        case Geometry::PropertyValueKind::Vec4:    return "Vec4";
        }
        return "Unknown";
    }

    const char* DebugNameForGeometryPropertyValueKindFilter(
        const GeometryPropertyValueKindFilter filter) noexcept
    {
        return filter.has_value()
                   ? DebugNameForGeometryPropertyValueKind(*filter)
                   : "Any";
    }

    bool MatchesGeometryPropertyValueKind(
        const GeometryPropertyValueKindFilter expected,
        const Geometry::PropertyValueKind actual) noexcept
    {
        return !expected.has_value() || *expected == actual;
    }

    Geometry::PropertyValueKind DetectGeometryPropertyValueKind(
        const Geometry::PropertySet& properties,
        const std::string_view propertyName) noexcept
    {
        if (propertyName.empty())
            return Geometry::PropertyValueKind::Unknown;

        for (const Geometry::PropertyDescriptor& descriptor :
             properties.Descriptors())
        {
            if (descriptor.Name == propertyName)
                return descriptor.ValueKind;
        }
        return Geometry::PropertyValueKind::Unknown;
    }

    bool GeometryPropertyValuesAreFinite(
        const Geometry::PropertySet& properties,
        const std::string_view propertyName) noexcept
    {
        switch (DetectGeometryPropertyValueKind(properties, propertyName))
        {
        case Geometry::PropertyValueKind::Float:
            return ScalarsFinite<float>(properties, propertyName);
        case Geometry::PropertyValueKind::Double:
            return ScalarsFinite<double>(properties, propertyName);
        case Geometry::PropertyValueKind::Vec2:
            return ScalarsFinite<glm::vec2>(properties, propertyName);
        case Geometry::PropertyValueKind::Vec3:
            return ScalarsFinite<glm::vec3>(properties, propertyName);
        case Geometry::PropertyValueKind::Vec4:
            return ScalarsFinite<glm::vec4>(properties, propertyName);
        default:
            // Integral and boolean kinds cannot be non-finite; a missing
            // property is reported by resolution, not by this predicate.
            return true;
        }
    }

    GeometryPropertyCatalogSnapshot BuildGeometryPropertyCatalogSnapshot(
        const GeometryEntityAvailability& availability,
        const std::uint32_t sourceStableId,
        const std::uint64_t sourceGeneration)
    {
        GeometryPropertyCatalogSnapshot snapshot{
            .SourceStableId = sourceStableId,
            .SourceGeneration = sourceGeneration,
        };

        for (const GeometryElementDomain domain : kPropertyElementDomains)
        {
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, domain);
            if (properties == nullptr)
                continue;

            const std::size_t elementCount = properties->Size();
            for (const Geometry::PropertyDescriptor& descriptor :
                 properties->Descriptors())
            {
                snapshot.Entries.push_back(GeometryPropertyCatalogEntry{
                    .Ref = GeometryPropertyRef{
                        .Domain = domain,
                        .Name = descriptor.Name,
                        .ValueKind = descriptor.ValueKind,
                    },
                    .ElementCount = elementCount,
                    .PropertyGeneration = sourceGeneration,
                });
            }
        }

        // Deterministic order regardless of property insertion order, so two
        // consumers observing the same source agree entry-for-entry.
        std::sort(snapshot.Entries.begin(), snapshot.Entries.end(),
                  [](const GeometryPropertyCatalogEntry& lhs,
                     const GeometryPropertyCatalogEntry& rhs) noexcept
                  {
                      return std::tie(lhs.Ref.Domain, lhs.Ref.Name)
                           < std::tie(rhs.Ref.Domain, rhs.Ref.Name);
                  });
        return snapshot;
    }

    const GeometryPropertyCatalogEntry* FindGeometryPropertyCatalogEntry(
        const GeometryPropertyCatalogSnapshot& snapshot,
        const GeometryElementDomain domain,
        const std::string_view propertyName) noexcept
    {
        for (const GeometryPropertyCatalogEntry& entry : snapshot.Entries)
        {
            if (entry.Ref.Domain == domain && entry.Ref.Name == propertyName)
                return &entry;
        }
        return nullptr;
    }

    GeometryPropertyResolution ResolveGeometryProperty(
        const GeometryEntityAvailability& availability,
        const GeometryElementDomain domain,
        const std::string_view propertyName,
        const GeometryPropertyValueKindFilter expectedValueKind,
        const std::optional<std::size_t> expectedElementCount,
        const bool requireFiniteValues,
        const std::uint64_t observedSourceGeneration) noexcept
    {
        GeometryPropertyResolution resolution{
            .ObservedSourceGeneration = observedSourceGeneration,
        };

        if (domain == GeometryElementDomain::Unknown)
        {
            resolution.Status =
                GeometryPropertyResolutionStatus::UnsupportedDomain;
            return resolution;
        }
        if (propertyName.empty())
        {
            resolution.Status = GeometryPropertyResolutionStatus::MissingName;
            return resolution;
        }

        const Geometry::PropertySet* properties =
            ResolveGeometryPropertySet(availability, domain);
        if (properties == nullptr)
        {
            resolution.Status =
                GeometryPropertyResolutionStatus::UnsupportedDomain;
            return resolution;
        }

        const Geometry::PropertyValueKind actualKind =
            DetectGeometryPropertyValueKind(*properties, propertyName);
        if (actualKind == Geometry::PropertyValueKind::Unknown)
        {
            resolution.Status =
                GeometryPropertyResolutionStatus::MissingProperty;
            return resolution;
        }

        resolution.ResolvedValueKind = actualKind;
        resolution.ElementCount = properties->Size();

        if (!MatchesGeometryPropertyValueKind(expectedValueKind, actualKind))
        {
            resolution.Status =
                GeometryPropertyResolutionStatus::ValueKindMismatch;
            return resolution;
        }
        if (expectedElementCount.has_value()
            && *expectedElementCount != resolution.ElementCount)
        {
            resolution.Status =
                GeometryPropertyResolutionStatus::ElementCountMismatch;
            return resolution;
        }
        if (requireFiniteValues
            && !GeometryPropertyValuesAreFinite(*properties, propertyName))
        {
            resolution.Status =
                GeometryPropertyResolutionStatus::NonFiniteValues;
            return resolution;
        }

        resolution.Status = GeometryPropertyResolutionStatus::Resolved;
        return resolution;
    }

    GeometryPropertyResolution ResolveGeometryProperty(
        const GeometryEntityAvailability& availability,
        const GeometryPropertyRef& ref,
        const std::optional<std::size_t> expectedElementCount,
        const bool requireFiniteValues,
        const std::uint64_t observedSourceGeneration) noexcept
    {
        const GeometryPropertyValueKindFilter filter =
            ref.ValueKind == Geometry::PropertyValueKind::Unknown
                ? GeometryPropertyValueKindFilter{}
                : GeometryPropertyValueKindFilter{ref.ValueKind};
        return ResolveGeometryProperty(availability,
                                       ref.Domain,
                                       ref.Name,
                                       filter,
                                       expectedElementCount,
                                       requireFiniteValues,
                                       observedSourceGeneration);
    }
}
