// Per-entity render-attribute source bindings: the structural vertex-channel
// overrides and the attribute x element-domain table that every binding
// command, model and consumer validates against.
module;

#include <cstdint>
#include <span>
#include <string_view>

export module Extrinsic.Runtime.VertexChannelBindings;

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
        VertexChannelSourceBinding Color{};
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
    // "vec3", "float, vec3 or vec4", ...
    [[nodiscard]] std::string_view RenderAttributeExpectedTypeText(
        const RenderAttributeRule& rule) noexcept;

    // Validates `propertyName` as the source of `attribute` on `domain` against
    // live geometry: UnsupportedDomain when the table has no such row or the
    // entity lacks the domain; otherwise the canonical property resolution
    // (missing, kind, element count and, when the row requires it, finiteness).
    [[nodiscard]] GeometryPropertyResolution ResolveRenderAttributeSource(
        const GeometryEntityAvailability& availability,
        RenderAttribute attribute,
        GeometryElementDomain domain,
        std::string_view propertyName) noexcept;

    // The structural binding slot for Position/Normal/Texcoord; nullptr for
    // attributes that are not stored in `VertexChannelBindingSet`.
    [[nodiscard]] VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        VertexChannelBindingSet& bindings, RenderAttribute attribute) noexcept;
    [[nodiscard]] const VertexChannelSourceBinding* FindVertexChannelSourceBinding(
        const VertexChannelBindingSet& bindings, RenderAttribute attribute) noexcept;
}
