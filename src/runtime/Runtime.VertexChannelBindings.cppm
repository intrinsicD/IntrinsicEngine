module;

#include <cstdint>
#include <string>

export module Extrinsic.Runtime.VertexChannelBindings;

import Extrinsic.Runtime.VertexAttributeBinding;

export namespace Extrinsic::Runtime
{
    // Runtime-owned ECS component payload for structural vertex-channel source
    // overrides. The component is consumed by runtime packers; graphics only
    // receives the resulting channel byte spans.
    struct VertexChannelSourceBinding
    {
        bool Enabled = false;
        AttributeSourceType SourceType = AttributeSourceType::Vec3;
        std::string SourceProperty{};
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
        return binding.Enabled && !binding.SourceProperty.empty();
    }
}
