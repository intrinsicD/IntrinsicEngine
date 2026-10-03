// Shared read-only readiness check for outputs owned by a GPU run or its unaccepted preview.
// Callers provide EditorProcessing types and <span>; implementation lives in EditorProcessing.cpp.
#pragma once

extern "C++" {
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    [[nodiscard]] ActionReadiness PendingGpuOutputReadiness(
        const EditorProcessingContext&, std::uint32_t entity, std::span<const GeometryPropertyRef> outputs);
}
}
