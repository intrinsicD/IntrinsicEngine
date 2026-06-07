module;

#include <span>
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
}
