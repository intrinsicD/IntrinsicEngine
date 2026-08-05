module;

#include <cstdint>

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
    };

    struct VertexChannelBindingSet
    {
        VertexChannelSourceBinding Normal{};
        VertexChannelSourceBinding Color{};
        std::uint64_t BindingGeneration = 1u;
    };

    [[nodiscard]] inline bool IsVertexChannelBindingEnabled(
        const VertexChannelSourceBinding& binding) noexcept
    {
        return binding.Enabled && binding.Property.HasName();
    }
}
