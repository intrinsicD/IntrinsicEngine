// Describes geometry capabilities and typed property references for runtime consumers.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <span>

#include <entt/entity/fwd.hpp>

export module Extrinsic.Runtime.GeometryAvailability;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    // Exact storage snapshots keep untouched/deleted values and undo data unchanged.
    struct GeometryScalarPropertySnapshot
    {
        bool Exists{};
        std::variant<std::vector<bool>, std::vector<std::int32_t>,
                     std::vector<std::uint32_t>, std::vector<std::uint64_t>,
                     std::vector<float>, std::vector<double>> Values{};
    };
    [[nodiscard]] GeometryScalarPropertySnapshot CaptureGeometryScalarProperty(
        const Geometry::PropertySet&, const GeometryPropertyRef&);
    // Values are indexed by source slot. Failure leaves the snapshot unchanged;
    // non-finite, out-of-range and inexact converted live values are rejected.
    [[nodiscard]] bool PrepareGeometryScalarProperty(
        GeometryScalarPropertySnapshot&, Geometry::PropertyValueKind,
        std::size_t count, std::span<const std::uint32_t> slots, std::span<const float> values);
    [[nodiscard]] bool PrepareGeometryScalarProperty(
        GeometryScalarPropertySnapshot&, Geometry::PropertyValueKind,
        std::size_t count, std::span<const std::uint32_t> slots, std::span<const double> values);
    [[nodiscard]] bool PrepareGeometryScalarProperty(
        GeometryScalarPropertySnapshot&, Geometry::PropertyValueKind,
        std::size_t count, std::span<const std::uint32_t> slots, std::span<const std::uint32_t> values);
    void ApplyGeometryScalarProperty(Geometry::PropertySet&, const GeometryPropertyRef&,
                                     const GeometryScalarPropertySnapshot&);

    namespace GeometrySources = Extrinsic::ECS::Components::GeometrySources;
    namespace RenderComponents = Extrinsic::Graphics::Components;

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
