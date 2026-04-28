module;

#include <span>
#include <limits>
#include <unordered_map>
#include <vector>
#include <cstdint>

export module Extrinsic.Core.Dag.Scheduler:Hazards;

import :Types;

export namespace Extrinsic::Core::Dag
{
    enum class HazardKind : uint8_t
    {
        None = 0,
        Raw,
        Waw,
        War,
    };

    struct HazardEdge
    {
        uint32_t from = 0;
        uint32_t to = 0;
        HazardKind kind = HazardKind::None;
    };

    class ResourceHazardBuilder
    {
    public:
        [[nodiscard]] std::vector<HazardEdge> Build(
            std::span<const PendingTaskDesc> tasks) const;
    };

    inline std::vector<HazardEdge> ResourceHazardBuilder::Build(
        std::span<const PendingTaskDesc> tasks) const
    {
        constexpr uint32_t kInvalidNode = std::numeric_limits<uint32_t>::max();

        struct ResourceState
        {
            uint32_t lastWriter = kInvalidNode;
            std::vector<uint32_t> currentReaders{};
        };

        std::unordered_map<ResourceId, ResourceState, StrongHandleHash<ResourceTag>> resourceState{};
        resourceState.reserve(tasks.size());

        std::vector<HazardEdge> edges{};
        edges.reserve(tasks.size());

        std::unordered_map<uint64_t, std::size_t> dedupe{};
        dedupe.reserve(tasks.size());

        const auto encodeEdge = [](uint32_t from, uint32_t to) noexcept -> uint64_t
        {
            return (static_cast<uint64_t>(from) << 32) | static_cast<uint64_t>(to);
        };

        const auto emitEdge = [&](uint32_t from, uint32_t to, HazardKind kind)
        {
            if (from == to)
                return;
            const uint64_t key = encodeEdge(from, to);
            if (dedupe.contains(key))
                return;
            dedupe.emplace(key, edges.size());
            edges.push_back(HazardEdge{.from = from, .to = to, .kind = kind});
        };

        for (uint32_t node = 0; node < tasks.size(); ++node)
        {
            for (const auto& access : tasks[node].resources)
            {
                auto& state = resourceState[access.resource];
                const bool hasWriter = (state.lastWriter != kInvalidNode);
                switch (access.mode)
                {
                case ResourceAccessMode::Read:
                    if (hasWriter)
                        emitEdge(state.lastWriter, node, HazardKind::Raw);
                    state.currentReaders.push_back(node);
                    break;
                case ResourceAccessMode::WeakRead:
                    if (hasWriter)
                        emitEdge(state.lastWriter, node, HazardKind::Raw);
                    break;
                case ResourceAccessMode::Write:
                case ResourceAccessMode::ReadWrite:
                    if (hasWriter)
                        emitEdge(state.lastWriter, node, HazardKind::Waw);
                    for (const uint32_t reader : state.currentReaders)
                        emitEdge(reader, node, HazardKind::War);
                    state.currentReaders.clear();
                    state.lastWriter = node;
                    break;
                }
            }
        }

        return edges;
    }
}
