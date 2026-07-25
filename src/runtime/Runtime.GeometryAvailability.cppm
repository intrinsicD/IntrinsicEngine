module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Extrinsic.Runtime.GeometryAvailability;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
// Re-exported: this module's public surface names Geometry::PropertySet and
// Geometry::PropertyValueKind, so consumers (including `app`, which may
// import runtime only) must be able to name them through this module.
export import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    namespace GeometrySources = Extrinsic::ECS::Components::GeometrySources;
    namespace RenderComponents = Extrinsic::Graphics::Components;

    enum class GeometryElementDomain : std::uint8_t
    {
        Unknown,
        MeshVertex,
        MeshEdge,
        MeshHalfedge,
        MeshFace,
        GraphNode,
        GraphEdge,
        PointCloudPoint,
    };

    enum class GeometryRenderLane : std::uint8_t
    {
        Surface,
        Edges,
        Points,
    };

    enum class GeometryAvailabilityStatus : std::uint8_t
    {
        Supported,
        NotRequested,
        NoGeometrySource,
        UnsupportedProvenance,
        MissingPointSource,
        MissingEdgeSource,
        MissingHalfedgeSource,
        MissingFaceSource,
        MissingPropertySource,
    };

    struct GeometryRenderLaneAvailability
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        bool Requested{false};
        bool Supported{false};
        GeometryAvailabilityStatus Status{GeometryAvailabilityStatus::NotRequested};
        GeometrySources::Domain ProvenanceDomain{GeometrySources::Domain::None};

        [[nodiscard]] bool Ready() const noexcept
        {
            return Requested && Supported;
        }
    };

    struct GeometryEntityAvailability
    {
        GeometrySources::ConstSourceView SourceView{};
        GeometrySources::SourceAvailability Sources{};

        std::optional<RenderComponents::RenderSurface> Surface{};
        std::optional<RenderComponents::RenderEdges> Edges{};
        std::optional<RenderComponents::RenderPoints> Points{};

        [[nodiscard]] bool HasGeometry() const noexcept
        {
            return Sources.Capabilities != GeometrySources::SourceCapability::None;
        }

        [[nodiscard]] bool HasRenderLaneRequest() const noexcept
        {
            return Surface.has_value() || Edges.has_value() || Points.has_value();
        }
    };

    [[nodiscard]] std::string_view ToString(GeometryElementDomain domain) noexcept;
    [[nodiscard]] std::string_view ToString(GeometryRenderLane lane) noexcept;
    [[nodiscard]] std::string_view ToString(GeometryAvailabilityStatus status) noexcept;

    [[nodiscard]] GeometryEntityAvailability BuildGeometryAvailability(
        const entt::registry& registry,
        entt::entity entity);

    [[nodiscard]] GeometryEntityAvailability BuildGeometryAvailability(
        const GeometrySources::ConstSourceView& view) noexcept;

    [[nodiscard]] GeometryRenderLaneAvailability ResolveRenderLaneAvailability(
        const GeometryEntityAvailability& availability,
        GeometryRenderLane lane) noexcept;

    [[nodiscard]] bool SupportsGeometryElementDomain(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain) noexcept;

    [[nodiscard]] const Geometry::PropertySet* ResolveGeometryPropertySet(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain) noexcept;

    [[nodiscard]] std::size_t ResolveGeometryElementCount(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain) noexcept;

    // ========================================================================
    // RUNTIME-192 — canonical geometry-property vocabulary.
    //
    // One way to name a geometry property and to enumerate resolved
    // properties of a source. This replaces the duplicated
    // progressive/editor/bake domain and value-kind enums, which mirrored
    // subsets of `GeometryElementDomain` + `Geometry::PropertyValueKind` and
    // forced conversion switches between otherwise identical identities.
    //
    // Vocabularies deliberately NOT merged here, because they answer
    // different questions: `GeometrySources::Domain` (provenance),
    // mesh sampling/raster domains, `Runtime::VertexChannel` (structural
    // stream), `Graphics::MaterialChannel` (material slot), and
    // visualization output meaning.
    // ========================================================================

    // A constraint on a property's value kind. `std::nullopt` means "any kind
    // is acceptable".
    //
    // This is intentionally an optional rather than an extra enumerator:
    // "unconstrained" is a property of a *query*, not a value type a property
    // can actually have. The retired `ProgressivePropertyValueKind` conflated
    // the two by adding an `Any` member alongside real kinds, which made every
    // switch over it carry an unreachable case.
    using GeometryPropertyValueKindFilter =
        std::optional<Geometry::PropertyValueKind>;

    // Canonical, authoring-safe reference to one geometry property.
    //
    // It names WHICH property and nothing else: no entity, pointer, element
    // count, generation, storage, provenance, material channel, or
    // visualization semantics. That makes it safe to place in a desired-state
    // authoring recipe and to compare across bake, presentation,
    // visualization, and selected-analysis consumers.
    struct GeometryPropertyRef
    {
        GeometryElementDomain       Domain{GeometryElementDomain::Unknown};
        std::string                 Name{};
        Geometry::PropertyValueKind ValueKind{
            Geometry::PropertyValueKind::Unknown};

        [[nodiscard]] bool HasName() const noexcept { return !Name.empty(); }
    };

    [[nodiscard]] bool operator==(const GeometryPropertyRef& lhs,
                                  const GeometryPropertyRef& rhs) noexcept;
    [[nodiscard]] bool operator!=(const GeometryPropertyRef& lhs,
                                  const GeometryPropertyRef& rhs) noexcept;

    enum class GeometryPropertyResolutionStatus : std::uint8_t
    {
        Resolved,
        UnsupportedDomain,
        MissingName,
        MissingProperty,
        ValueKindMismatch,
        ElementCountMismatch,
        NonFiniteValues,
    };

    // One resolved property of a source: the plain reference plus the facts
    // that only exist once it has been resolved against live geometry.
    struct GeometryPropertyCatalogEntry
    {
        GeometryPropertyRef Ref{};
        std::size_t         ElementCount{0u};
        std::uint64_t       PropertyGeneration{0u};
    };

    // Pointer-free, copied snapshot of the properties a source exposes.
    //
    // Holds no live `PropertySet`, ECS handle, or borrowed view, so it stays
    // valid after the source mutates; callers revalidate by comparing
    // `SourceGeneration` rather than by dereferencing anything.
    // `Entries` is ordered deterministically by (domain, name) so two
    // consumers observing the same source agree on order.
    struct GeometryPropertyCatalogSnapshot
    {
        std::uint32_t SourceStableId{0u};
        std::uint64_t SourceGeneration{0u};
        std::vector<GeometryPropertyCatalogEntry> Entries{};

        [[nodiscard]] bool Empty() const noexcept { return Entries.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return Entries.size();
        }
    };

    struct GeometryPropertyResolution
    {
        GeometryPropertyResolutionStatus Status{
            GeometryPropertyResolutionStatus::UnsupportedDomain};
        Geometry::PropertyValueKind ResolvedValueKind{
            Geometry::PropertyValueKind::Unknown};
        std::size_t   ElementCount{0u};
        std::uint64_t ObservedSourceGeneration{0u};

        [[nodiscard]] bool Resolved() const noexcept
        {
            return Status == GeometryPropertyResolutionStatus::Resolved;
        }
    };

    [[nodiscard]] std::string_view ToString(
        GeometryPropertyResolutionStatus status) noexcept;

    // Shared display name for the canonical value kind. Returns `const char*`
    // because every current caller feeds it to a printf-style UI format.
    [[nodiscard]] const char* DebugNameForGeometryPropertyValueKind(
        Geometry::PropertyValueKind kind) noexcept;

    // Display name for a kind *constraint*; unconstrained renders as "Any".
    [[nodiscard]] const char* DebugNameForGeometryPropertyValueKindFilter(
        GeometryPropertyValueKindFilter filter) noexcept;

    [[nodiscard]] bool MatchesGeometryPropertyValueKind(
        GeometryPropertyValueKindFilter expected,
        Geometry::PropertyValueKind actual) noexcept;

    // Value kind of `name` in `properties`, or `Unknown` when absent.
    [[nodiscard]] Geometry::PropertyValueKind DetectGeometryPropertyValueKind(
        const Geometry::PropertySet& properties,
        std::string_view propertyName) noexcept;

    // False when any element of a floating-point property is NaN/inf.
    // Non-floating-point kinds are finite by construction and return true.
    [[nodiscard]] bool GeometryPropertyValuesAreFinite(
        const Geometry::PropertySet& properties,
        std::string_view propertyName) noexcept;

    [[nodiscard]] GeometryPropertyCatalogSnapshot
        BuildGeometryPropertyCatalogSnapshot(
            const GeometryEntityAvailability& availability,
            std::uint32_t sourceStableId = 0u,
            std::uint64_t sourceGeneration = 0u);

    [[nodiscard]] const GeometryPropertyCatalogEntry*
        FindGeometryPropertyCatalogEntry(
            const GeometryPropertyCatalogSnapshot& snapshot,
            GeometryElementDomain domain,
            std::string_view propertyName) noexcept;

    // Resolve one reference against live geometry, applying the same
    // domain/name/kind/count/finite checks every consumer previously
    // reimplemented. `expectedElementCount == std::nullopt` skips the count
    // check; `requireFiniteValues` is opt-in because scanning every value is
    // O(n) and only bake-like consumers need it.
    [[nodiscard]] GeometryPropertyResolution ResolveGeometryProperty(
        const GeometryEntityAvailability& availability,
        GeometryElementDomain domain,
        std::string_view propertyName,
        GeometryPropertyValueKindFilter expectedValueKind = std::nullopt,
        std::optional<std::size_t> expectedElementCount = std::nullopt,
        bool requireFiniteValues = false,
        std::uint64_t observedSourceGeneration = 0u) noexcept;

    [[nodiscard]] GeometryPropertyResolution ResolveGeometryProperty(
        const GeometryEntityAvailability& availability,
        const GeometryPropertyRef& ref,
        std::optional<std::size_t> expectedElementCount = std::nullopt,
        bool requireFiniteValues = false,
        std::uint64_t observedSourceGeneration = 0u) noexcept;
}
