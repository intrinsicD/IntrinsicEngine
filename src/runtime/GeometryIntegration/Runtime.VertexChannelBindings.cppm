// Per-entity render-attribute source bindings: the structural vertex-channel
// overrides and the attribute x element-domain table that every binding
// command, model and consumer validates against.
module;

#include <cstdint>
#include <span>
#include <string_view>

#include <glm/vec3.hpp>

#include <entt/entity/registry.hpp>

export module Extrinsic.Runtime.VertexChannelBindings;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.GeometryAvailability;

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

    // One authored source per structural stream. `Property.Domain` names the
    // element domain the source lives on (a vertex domain, or mesh corners for
    // normals/texcoords). A binding that no longer resolves is kept as authored
    // intent; consumers draw the canonical source instead.
    struct VertexChannelBindingSet
    {
        VertexChannelSourceBinding Position{};
        VertexChannelSourceBinding Normal{};
        VertexChannelSourceBinding Texcoord{};
        std::uint64_t BindingGeneration = 1u;

        [[nodiscard]] friend bool operator==(
            const VertexChannelBindingSet&,
            const VertexChannelBindingSet&) = default;
    };

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

    // The positions a vertex domain is displayed from: drawn, culled (bounds
    // come from them), picked and used as vector-field anchors. The bound
    // Position source when it resolves on `vertexDomain` (vec3, one value per
    // element, all finite), otherwise canonical `v:position`; `Values` is empty
    // when neither exists. Every consumer resolves through this one function,
    // so the displayed and the picked geometry cannot diverge. Spatial indices
    // of geometry methods keep their own input slots (canonical by default).
    struct DisplayedPositions
    {
        std::string_view Name{};
        std::span<const glm::vec3> Values{};
        bool Bound{false};
    };
    [[nodiscard]] DisplayedPositions ResolveDisplayedPositions(
        const Geometry::PropertySet& vertices,
        GeometryElementDomain vertexDomain,
        const VertexChannelBindingSet* bindings) noexcept;

    // The vertex domain whose positions an entity of `provenance` displays.
    [[nodiscard]] GeometryElementDomain DisplayedPositionDomainFor(
        ECS::Components::GeometrySources::Domain provenance) noexcept;

    // The entity-level resolution shared by every consumer that only has the
    // registry (culling, camera focus/framing): the displayed positions of the
    // entity's vertex source and that source's property set (for revisions).
    // Empty when the entity has no bindings or no vertex source.
    struct EntityDisplayedPositions
    {
        DisplayedPositions Positions{};
        const Geometry::PropertySet* Vertices{nullptr};
    };
    [[nodiscard]] EntityDisplayedPositions ResolveEntityDisplayedPositions(
        const entt::registry& registry, entt::entity entity);

    // The structural binding slot for Position/Normal/Texcoord; nullptr for
    // attributes that are not stored in `VertexChannelBindingSet`.
    [[nodiscard]] VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        VertexChannelBindingSet& bindings, RenderAttribute attribute) noexcept;
    [[nodiscard]] const VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        const VertexChannelBindingSet& bindings, RenderAttribute attribute) noexcept;
}
