// Per-entity render-attribute source bindings: the structural vertex-channel
// overrides and the attribute x element-domain table that every binding
// command, model and consumer validates against.
module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>

#include <entt/entity/registry.hpp>

export module Extrinsic.Runtime.VertexChannelBindings;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.GeometryAvailability;
import Geometry.AABB;

export namespace Extrinsic::Runtime
{
    // Runtime-owned ECS component payload for structural vertex-channel source
    // overrides. The component is consumed by private runtime plan builders; graphics only
    // receives the resulting channel byte spans.
    struct VertexChannelSourceBinding
    {
        bool Enabled = false;
        GeometryPropertyRef Property{};

        [[nodiscard]] friend bool operator==(
            const VertexChannelSourceBinding&,
            const VertexChannelSourceBinding&) = default;
    };

    // The structural sources of one render lane. Only the attributes of the
    // lane's row (`RenderLaneAttributeRows()`) are ever set: surface Position,
    // Normal and Texcoord; edges Position; points Position and Normal.
    struct VertexChannelLaneBindings
    {
        VertexChannelSourceBinding Position{};
        VertexChannelSourceBinding Normal{};
        VertexChannelSourceBinding Texcoord{};

        [[nodiscard]] friend bool operator==(
            const VertexChannelLaneBindings&, const VertexChannelLaneBindings&) = default;
    };

    // One authored source per render lane and structural stream, so each
    // primitive can be drawn from its own positions (UI-083). `Property.Domain`
    // names the element domain the source lives on (a vertex domain, or mesh
    // corners for surface normals/texcoords). An unset slot draws the canonical
    // property; no lane reads another lane's slot. A binding that no longer
    // resolves is kept as authored intent; consumers draw the canonical source.
    struct VertexChannelBindingSet
    {
        VertexChannelLaneBindings Surface{};
        VertexChannelLaneBindings Edges{};
        VertexChannelLaneBindings Points{};
        std::uint64_t BindingGeneration = 1u;

        [[nodiscard]] friend bool operator==(
            const VertexChannelBindingSet&,
            const VertexChannelBindingSet&) = default;
    };

    [[nodiscard]] VertexChannelLaneBindings& LaneBindings(
        VertexChannelBindingSet& bindings, GeometryRenderLane lane) noexcept;
    [[nodiscard]] const VertexChannelLaneBindings& LaneBindings(
        const VertexChannelBindingSet& bindings, GeometryRenderLane lane) noexcept;

    [[nodiscard]] inline bool IsVertexChannelBindingEnabled(
        const VertexChannelSourceBinding& binding) noexcept
    {
        return binding.Enabled && binding.Property.HasName();
    }

    // Shader-facing attribute whose source property an operator can choose.
    // Position/Normal/Texcoord are structural streams (`VertexChannelBindingSet`);
    // Color is the visualization overlay; PointSize/LineWidth are per-element
    // pixel sizes of the point/line lanes.
    enum class RenderAttribute : std::uint8_t
    {
        Position,
        Normal,
        Texcoord,
        Color,
        PointSize,
        LineWidth,
    };

    // One row of the attribute x element-domain table. A source must live on
    // `Domain`, have one of the accepted value kinds and exactly the domain's
    // element count; `RequireFiniteValues` sources are refused (and fall back
    // to the default at draw time) when any value is NaN/inf.
    struct RenderAttributeRule
    {
        RenderAttribute Attribute{RenderAttribute::Position};
        GeometryElementDomain Domain{GeometryElementDomain::Unknown};
        bool AcceptsFloat{false};
        bool AcceptsVec2{false};
        bool AcceptsVec3{false};
        bool AcceptsVec4{false};
        bool RequireFiniteValues{false};
        // Color: kind and value validity belong to the visualization recipe
        // encoder that draws the overlay; the table only fixes domain/count.
        bool ValidatedByVisualizationRecipe{false};
        // Canonical property drawn without a binding ("" when the default is
        // not a property, e.g. the material color or a uniform pixel size).
        std::string_view CanonicalProperty{};
        // User-facing description of what "Default" draws.
        std::string_view DefaultDescription{};
    };

    [[nodiscard]] std::string_view ToString(RenderAttribute attribute) noexcept;
    [[nodiscard]] bool TryParseRenderAttribute(
        std::string_view value, RenderAttribute& out) noexcept;

    // The full table, ordered by attribute then domain.
    [[nodiscard]] std::span<const RenderAttributeRule> RenderAttributeRules() noexcept;
    [[nodiscard]] const RenderAttributeRule* FindRenderAttributeRule(
        RenderAttribute attribute, GeometryElementDomain domain) noexcept;
    [[nodiscard]] bool RenderAttributeAcceptsValueKind(
        const RenderAttributeRule& rule, Geometry::PropertyValueKind kind) noexcept;
    // "vec3", "vec2", "float" or "scalar or vector" (recipe-validated rows).
    [[nodiscard]] std::string_view RenderAttributeExpectedTypeText(
        const RenderAttributeRule& rule) noexcept;

    // One row of the Appearance panel: an attribute a render lane draws and
    // the element domains whose properties may feed it, in preference order
    // (a corner normal or a face color is drawn by the surface only). Each
    // (attribute, domain) pair is also a RenderAttributeRules() row.
    struct RenderLaneAttributeRow
    {
        GeometryRenderLane Lane{GeometryRenderLane::Surface};
        RenderAttribute Attribute{RenderAttribute::Position};
        std::span<const GeometryElementDomain> Domains{};
        // User-facing description of what "Default" draws on this lane.
        std::string_view DefaultDescription{};
    };

    // Ordered by lane (Surface, Edges, Points), then by attribute.
    [[nodiscard]] std::span<const RenderLaneAttributeRow> RenderLaneAttributeRows() noexcept;
    [[nodiscard]] const RenderLaneAttributeRow* FindRenderLaneAttributeRow(
        GeometryRenderLane lane, RenderAttribute attribute) noexcept;
    [[nodiscard]] bool RenderLaneReadsDomain(
        GeometryRenderLane lane, RenderAttribute attribute, GeometryElementDomain domain) noexcept;
    // The lane a Color property of `domain` colors when no lane is chosen
    // (show_property): mesh vertices and faces the surface, mesh edges and
    // graph nodes and edges the edges, points the points; none for halfedges.
    [[nodiscard]] std::optional<GeometryRenderLane> DefaultColorLaneFor(GeometryElementDomain domain) noexcept;

    // Validates `propertyName` as the source of `attribute` on `domain` against
    // live geometry: UnsupportedDomain when the table has no such row or the
    // entity lacks the domain; otherwise the canonical property resolution
    // (missing, kind, element count and, when the row requires it, finiteness).
    // Recipe-validated rows (Color) check only presence and count here; the
    // editor completes them with the visualization recipe encoder.
    [[nodiscard]] GeometryPropertyResolution ResolveRenderAttributeSource(
        const GeometryEntityAvailability& availability,
        RenderAttribute attribute,
        GeometryElementDomain domain,
        std::string_view propertyName) noexcept;

    // The positions a render lane is displayed from: drawn, culled (bounds
    // come from them), picked and highlighted. The lane's bound Position source
    // when it resolves on `vertexDomain` (vec3, one value per element, all
    // finite), otherwise canonical `v:position`; `Values` is empty when neither
    // exists. Every consumer resolves through this one function, so the
    // displayed and the picked geometry of a lane cannot diverge. Spatial
    // indices of geometry methods keep their own input slots (canonical by default).
    struct DisplayedPositions
    {
        std::string_view Name{};
        std::span<const glm::vec3> Values{};
        bool Bound{false};
    };
    // The positions `lane` displays: its bound Position source when it
    // resolves on `vertexDomain`, otherwise canonical `v:position`.
    [[nodiscard]] DisplayedPositions ResolveDisplayedPositions(const Geometry::PropertySet& vertices,
        GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings,
        GeometryRenderLane lane) noexcept;

    // The vertex domain whose positions an entity of `provenance` displays.
    [[nodiscard]] GeometryElementDomain DisplayedPositionDomainFor(
        ECS::Components::GeometrySources::Domain provenance) noexcept;

    // The first lane, in Surface, Edges, Points order, that an entity of
    // `provenance` draws and whose row has `attribute`: mesh Surface; graph
    // Edges for Position and Points for Normal; point cloud Points. Its slot
    // is the one scene files store under the attribute's bare key, and the
    // positions vector fields and glyph orders anchor to.
    [[nodiscard]] std::optional<GeometryRenderLane> PrimaryLaneFor(
        ECS::Components::GeometrySources::Domain provenance, RenderAttribute attribute) noexcept;

    // The lane a selected element of `domain` is shown on (highlights, the
    // missing-hint pick fallback): faces the surface; edges the edges lane when
    // the entity draws edges, else the surface; vertices, nodes and points the
    // points lane when the entity draws points, else its primary lane.
    [[nodiscard]] GeometryRenderLane SelectionLaneFor(
        const entt::registry& registry, entt::entity entity, GeometryElementDomain domain);

    // The lanes whose positions an entity of `provenance` displays, indexed by
    // `GeometryRenderLane`: each lane whose render hint is present, or its
    // primary lane when it draws none. All false for an unsupported provenance.
    using DisplayedLaneMask = std::array<bool, 3>;
    [[nodiscard]] DisplayedLaneMask DisplayedPositionLanes(
        const entt::registry& registry, entt::entity entity, ECS::Components::GeometrySources::Domain provenance);

    // The distinct position sources `lanes` display, in lane order: lanes that
    // show the same source (by name) share one entry.
    [[nodiscard]] std::vector<DisplayedPositions> ResolveDisplayedPositionSources(const Geometry::PropertySet& vertices,
        GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings,
        const DisplayedLaneMask& lanes);

    // The entity-level resolution shared by every consumer that only has the
    // registry (culling, camera focus/framing, gizmo pivot): each distinct
    // source of the `DisplayedPositionLanes`, in lane order, and the vertex
    // property set (for revisions). Empty when the entity has no bindings or
    // no vertex source.
    struct EntityDisplayedPositions
    {
        std::vector<DisplayedPositions> Sources{};
        const Geometry::PropertySet* Vertices{nullptr};

        [[nodiscard]] bool AnyBound() const noexcept
        {
            return std::ranges::any_of(Sources,
                [](const DisplayedPositions& source)
                {
                    return source.Bound;
                });
        }
    };
    [[nodiscard]] EntityDisplayedPositions ResolveEntityDisplayedPositions(
        const entt::registry& registry, entt::entity entity);

    // Names and revisions of the displayed sources, for caches that must
    // follow any of them. Empty when a source has no revision (untracked
    // storage), which no cache may trust.
    [[nodiscard]] std::string DisplayedPositionsSignature(const EntityDisplayedPositions& displayed);

    // Local display bounds: the union of the drawn lanes' displayed positions;
    // canonical bounds need no position traversal.
    [[nodiscard]] std::optional<Geometry::AABB> ComputeEntityLocalDisplayBounds(
        const entt::registry& registry, entt::entity entity);

    // The structural binding slot of (`lane`, `attribute`); nullptr when the
    // lane's row has no such structural attribute.
    [[nodiscard]] VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        VertexChannelBindingSet& bindings, GeometryRenderLane lane, RenderAttribute attribute) noexcept;
    [[nodiscard]] const VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        const VertexChannelBindingSet& bindings, GeometryRenderLane lane, RenderAttribute attribute) noexcept;
}
