// RUNTIME-125 - rendering vertex-fetch layout smoke benchmark declaration.
//
// This is a benchmark gate for the optional AoS fast lane. It records the
// current uniform-SoA fetch baseline and an interleaved probe over the same
// deterministic static mesh, but it does not justify adopting the AoS lane by
// itself.
#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Rendering
{
    inline constexpr const char* kVertexFetchLayoutSmokeBenchmarkId =
        "rendering.vertex_fetch_layout.smoke";
    inline constexpr const char* kVertexFetchLayoutSmokeMethod =
        "rendering.vertex_fetch_layout";
    inline constexpr const char* kVertexFetchLayoutSmokeDataset =
        "builtin.static_grid_mesh.65536";

    struct VertexFetchLayoutSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double SoaRuntimeMilliseconds{0.0};
        double InterleavedRuntimeMilliseconds{0.0};
        double ThroughputItemsPerSecond{0.0};
        double InterleavedToSoaRuntimeRatio{0.0};
        double QualityErrorL2{0.0};
        std::size_t VertexCount{0};
        std::size_t IndexCount{0};
        bool Succeeded{false};
    };

    [[nodiscard]] VertexFetchLayoutSmokeMetrics RunVertexFetchLayoutSmoke();
} // namespace Intrinsic::Bench::Rendering
