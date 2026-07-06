module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>

module Extrinsic.Graphics.RenderGraph;

import :Barriers;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct BarrierPacketKey
        {
            std::uint32_t PassIndex = 0u;
            BarrierPacketStage Stage = BarrierPacketStage::BeforePass;
        };

        [[nodiscard]] constexpr auto SortTuple(const BarrierPacketKey key) noexcept
        {
            return std::tuple{key.PassIndex, BarrierPacketStageSortKey(key.Stage)};
        }

        [[nodiscard]] constexpr auto SortTuple(const BarrierPacket& packet) noexcept
        {
            return SortTuple(BarrierPacketKey{
                .PassIndex = packet.PassIndex,
                .Stage = packet.Stage,
            });
        }
    }

    BarrierPacketRange FindBarrierPacketRange(const std::span<const BarrierPacket> packets,
                                              const std::uint32_t passIndex,
                                              const BarrierPacketStage stage) noexcept
    {
        const BarrierPacketKey key{
            .PassIndex = passIndex,
            .Stage = stage,
        };

        const auto begin = packets.begin();
        const auto lower = std::lower_bound(begin, packets.end(), key, [](const BarrierPacket& packet,
                                                                          const BarrierPacketKey value) {
            return SortTuple(packet) < SortTuple(value);
        });
        const auto upper = std::upper_bound(lower, packets.end(), key, [](const BarrierPacketKey value,
                                                                          const BarrierPacket& packet) {
            return SortTuple(value) < SortTuple(packet);
        });

        return BarrierPacketRange{
            .Begin = static_cast<std::size_t>(std::distance(begin, lower)),
            .End = static_cast<std::size_t>(std::distance(begin, upper)),
        };
    }

    bool AreBarrierPacketsSortedByPassAndStage(const std::span<const BarrierPacket> packets) noexcept
    {
        return std::ranges::is_sorted(packets, [](const BarrierPacket& lhs, const BarrierPacket& rhs) {
            return SortTuple(lhs) < SortTuple(rhs);
        });
    }
}
