// GEOM-009 — geometry smoke benchmark workload declaration.
//
// The smoke benchmark exercises a public `Geometry` API path (mesh primitive
// build + quality summary). Both the benchmark runner and any focused gtest
// fixture share this declaration so the workload definition has one home.
#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Geometry
{
    // Stable identifier used by the manifest at
    // benchmarks/geometry/manifests/geometry_halfedge_smoke.yaml.
    inline constexpr const char* kHalfedgeSmokeBenchmarkId = "geometry.halfedge.smoke";
    inline constexpr const char* kHalfedgeSmokeMethod      = "geometry.halfedge.icosahedron";
    inline constexpr const char* kHalfedgeSmokeDataset     = "builtin.icosahedron";

    struct HalfedgeSmokeMetrics
    {
        double      RuntimeMilliseconds{0.0};
        double      TotalArea{0.0};
        std::size_t VertexCount{0};
        std::size_t FaceCount{0};
        bool        Succeeded{false};
    };

    // Runs the smoke workload once with internal warmup. Deterministic on a
    // fixed builder seed; safe to call from any thread without sharing state.
    [[nodiscard]] HalfedgeSmokeMetrics RunHalfedgeSmoke();
} // namespace Intrinsic::Bench::Geometry
