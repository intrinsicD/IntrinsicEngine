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
}
