module;

#include <cstdint>
#include <span>
#include <string_view>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.VertexAttributeBinding;

import Geometry.Properties;

export namespace Extrinsic::Runtime
{
    // Logical GPU vertex channel that a named geometry property can feed. This is
    // the structural vertex stream (what the surface/line/point shaders read per
    // vertex), distinct from the graphics-layer `VisualizationConfig` sci-vis
    // colormap overlays.
    enum class VertexChannel : std::uint8_t
    {
        Position,
        Normal,
        Texcoord,
        Color,
        Tangent,
        Custom,
    };

    // Element type expected from the source property for a channel binding.
    enum class AttributeSourceType : std::uint8_t
    {
        Float32,
        Vec2,
        Vec3,
        Vec4,
    };

    // Terminal reason a binding resolved (or failed to resolve) against a
    // property set. Per-element repairs (non-finite / degenerate values replaced
    // by the fallback) do not change a `Bound` status; they are recorded in the
    // `AttributeBindResult` counters instead.
    enum class AttributeBindStatus : std::uint8_t
    {
        Bound,            // property present, correctly typed, and count-matched.
        EmptyBinding,     // binding carried no source property name.
        PropertyMissing,  // no property by that name exists.
        TypeMismatch,     // a property by that name exists but with a different element type.
        CountMismatch,    // property present and typed, but element count != vertexCount.
    };

    [[nodiscard]] const char* DebugNameForVertexChannel(VertexChannel channel) noexcept;
    [[nodiscard]] const char* DebugNameForAttributeBindStatus(AttributeBindStatus status) noexcept;

    // Declarative request: bind `SourceProperty` to `Channel`, with explicit
    // policy for what happens when the property cannot be resolved or an
    // individual element is unusable.
    struct VertexAttributeBinding
    {
        VertexChannel Channel = VertexChannel::Custom;
        AttributeSourceType SourceType = AttributeSourceType::Vec3;
        std::string_view SourceProperty{};
        // When true, an unresolved whole-property binding and per-element
        // non-finite/degenerate values are filled from `Fallback`, leaving the
        // output span fully populated. When false, an unresolved binding leaves
        // the output untouched and `AttributeBindResult::Ok()` is false.
        bool AllowFallback = true;
        // Renormalize finite vec3 source values; a zero/degenerate-length vector
        // is treated as unusable and replaced by the (normalized intent of the)
        // `Fallback`. Only meaningful for vec3 channels.
        bool Normalize = false;
        glm::vec4 Fallback{0.0f, 0.0f, 0.0f, 1.0f};
    };

    // Fail-closed diagnostics for one resolved binding.
    struct AttributeBindResult
    {
        AttributeBindStatus Status = AttributeBindStatus::PropertyMissing;
        // True when the output span was fully written with usable values (either
        // from the source property or from the fallback). False only when the
        // binding was unresolved and `AllowFallback` was false, or the output
        // span was smaller than `vertexCount`.
        bool FullyPopulated = false;
        std::uint32_t SourceCount = 0;     // elements taken from the source property.
        std::uint32_t FallbackCount = 0;   // elements written from the fallback value.
        std::uint32_t NonFiniteCount = 0;  // source elements rejected as non-finite (subset of FallbackCount).

        [[nodiscard]] bool Ok() const noexcept { return FullyPopulated; }
    };

    // Resolve a vec3 channel (Position / Normal / Tangent) into `out`. `out` must
    // hold at least `vertexCount` elements. With `Normalize`, finite vectors are
    // renormalized and zero-length vectors fall back; this reproduces the mesh
    // packer's normalize-or-default-normal behavior when `Fallback = {0,0,1,_}`.
    [[nodiscard]] AttributeBindResult ResolveVec3Channel(
        const Geometry::PropertySet& properties,
        const VertexAttributeBinding& binding,
        std::uint32_t vertexCount,
        std::span<glm::vec3> out);

    // Resolve a vec2 channel (Texcoord) into `out`. Non-finite source elements
    // fall back to `Fallback.xy`; reproduces the packer's finite-or-zero UV.
    [[nodiscard]] AttributeBindResult ResolveVec2Channel(
        const Geometry::PropertySet& properties,
        const VertexAttributeBinding& binding,
        std::uint32_t vertexCount,
        std::span<glm::vec2> out);

    // Resolve a color channel into packed unorm8 ABGR ints compatible with the
    // surface shader's `unpackUnorm4x8` (byte 0 = R, 1 = G, 2 = B, 3 = A). The
    // source is read as vec4 RGBA when `SourceType == Vec4`, otherwise as vec3
    // RGB with alpha = 1. Components are clamped to [0,1]; non-finite source
    // elements fall back to the packed `Fallback`.
    [[nodiscard]] AttributeBindResult ResolveColorChannelPackedUnorm8(
        const Geometry::PropertySet& properties,
        const VertexAttributeBinding& binding,
        std::uint32_t vertexCount,
        std::span<std::uint32_t> out);
}
