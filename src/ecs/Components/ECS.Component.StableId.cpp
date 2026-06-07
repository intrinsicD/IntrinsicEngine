module;

#include <cstddef>
#include <cstdint>

module Extrinsic.ECS.Component.StableId;

namespace Extrinsic::ECS::Components
{
    std::size_t StableIdHash::operator()(StableId const& id) const noexcept
    {
        const auto mix = [](std::uint64_t v) noexcept -> std::uint64_t
        {
            v ^= v >> 33;
            v *= 0xff51afd7ed558ccdULL;
            v ^= v >> 33;
            v *= 0xc4ceb9fe1a85ec53ULL;
            v ^= v >> 33;
            return v;
        };
        const std::uint64_t h = mix(id.High);
        const std::uint64_t l = mix(id.Low);
        return static_cast<std::size_t>(h ^ (l + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
    }
}
