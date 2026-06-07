module;

#include <cstdint>
#include <string_view>

module Extrinsic.RHI.Profiler;

namespace Extrinsic::RHI
{
    std::uint64_t GpuTimestampFrame::FindScopeDurationNs(std::string_view name) const
    {
        for (const auto& scope : Scopes)
        {
            if (scope.Name == name)
                return scope.DurationNs;
        }
        return 0;
    }
}
