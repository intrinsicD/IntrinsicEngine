# Algorithm Variant Dispatch Pattern

Runtime selection of **algorithm variants** and **compute backends** for
parallelizable algorithms — without virtual inheritance. Built on
`std::variant` + `std::visit` for compile-time-exhaustive, type-safe dispatch.

---

## Two Orthogonal Axes

Every dispatchable algorithm has two independent selection dimensions:

| Axis | Mechanism | Example |
|------|-----------|---------|
| **Strategy** — *what* algorithm runs | `std::variant<Lloyd, MiniBatch, KMeansPP>` | Lloyd's vs. mini-batch vs. K-Means++ init |
| **Backend** — *where* it runs | `enum class Backend : uint8_t { CPU, GPU }` | CPU sequential vs. Vulkan compute |

Separating them avoids combinatorial explosion (`LloydCPU`, `LloydGPU`, `MiniBatchCPU`, …).

---

## Pattern Structure

### 1. Module Interface (no GPU dependency)

```cpp
// Geometry.Clustering.cppm  (partition of Geometry module)
module;
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>
#include <glm/glm.hpp>

export module Geometry:Clustering;

export namespace Geometry::Clustering
{
    // -----------------------------------------------------------------
    // Backend — WHERE the algorithm executes
    // -----------------------------------------------------------------
    enum class Backend : uint8_t
    {
        CPU = 0,
        GPU = 1,          // Vulkan compute shader
        // CPUParallel = 2, // future: std::execution::par_unseq
    };

    // -----------------------------------------------------------------
    // Strategies — WHAT algorithm runs (each carries its own params)
    // -----------------------------------------------------------------
    struct Lloyd
    {
        uint32_t MaxIterations  = 300;
        float    ConvergenceEps = 1e-4f;
    };

    struct MiniBatch
    {
        uint32_t MaxIterations = 300;
        uint32_t BatchSize     = 100;
        float    ConvergenceEps = 1e-4f;
    };

    struct KMeansPP
    {
        uint32_t MaxIterations  = 300;
        float    ConvergenceEps = 1e-4f;
        // KMeans++ only changes initialization, then runs Lloyd
    };

    using Strategy = std::variant<Lloyd, MiniBatch, KMeansPP>;

    // -----------------------------------------------------------------
    // Params (combines both axes + shared config)
    // -----------------------------------------------------------------
    struct Params
    {
        uint32_t K       = 8;
        Strategy Algo    = Lloyd{};        // algorithm variant
        Backend  Compute = Backend::CPU;   // execution backend
    };

    // -----------------------------------------------------------------
    // Result (always includes diagnostics + what actually ran)
    // -----------------------------------------------------------------
    struct Result
    {
        std::vector<uint32_t>  Assignments;  // per-point cluster index [0, K)
        std::vector<glm::vec3> Centroids;    // final centroid positions
        uint32_t Iterations    = 0;
        bool     Converged     = false;
        Backend  ActualBackend = Backend::CPU;
    };

    // -----------------------------------------------------------------
    // Entry points
    // -----------------------------------------------------------------

    // CPU-only overload — links into IntrinsicGeometry, testable without GPU.
    [[nodiscard]] std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params);

    // GPU-capable overload — links into IntrinsicRuntime.
    // Falls back to CPU if GPU path fails or is unavailable.
    [[nodiscard]] std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params,
        RHI::VulkanDevice& device);
}
```

### 2. CPU Implementation — `std::visit` Dispatch

```cpp
// Geometry.Clustering.cpp
module Geometry;
import :Clustering;

namespace Geometry::Clustering
{
    namespace Internal
    {
        std::optional<Result> ClusterLloyd(
            std::span<const glm::vec3> points, uint32_t k, const Lloyd& s)
        {
            // Lloyd's iterative assignment + centroid update
            Result r;
            r.ActualBackend = Backend::CPU;
            // ...
            return r;
        }

        std::optional<Result> ClusterMiniBatch(
            std::span<const glm::vec3> points, uint32_t k, const MiniBatch& s)
        {
            // Sculley 2010: subsample BatchSize points per iteration
            Result r;
            r.ActualBackend = Backend::CPU;
            // ...
            return r;
        }

        std::optional<Result> ClusterKMeansPP(
            std::span<const glm::vec3> points, uint32_t k, const KMeansPP& s)
        {
            // D² seeding (Arthur & Vassilvitskii 2007), then Lloyd
            Result r;
            r.ActualBackend = Backend::CPU;
            // ...
            return r;
        }
    }

    std::optional<Result> Cluster(
        std::span<const glm::vec3> points, const Params& params)
    {
        if (points.empty() || params.K == 0) return std::nullopt;

        return std::visit([&](const auto& strategy) -> std::optional<Result> {
            using S = std::decay_t<decltype(strategy)>;

            if constexpr (std::same_as<S, Lloyd>)
                return Internal::ClusterLloyd(points, params.K, strategy);
            else if constexpr (std::same_as<S, MiniBatch>)
                return Internal::ClusterMiniBatch(points, params.K, strategy);
            else if constexpr (std::same_as<S, KMeansPP>)
                return Internal::ClusterKMeansPP(points, params.K, strategy);
        }, params.Algo);
    }
}
```

### 3. GPU-Capable Overload — Backend × Strategy Dispatch

```cpp
// In Runtime (links against RHI)
std::optional<Result> Cluster(
    std::span<const glm::vec3> points,
    const Params& params,
    RHI::VulkanDevice& device)
{
    if (params.Compute == Backend::GPU)
    {
        // Not every strategy has a GPU implementation.
        // std::visit returns nullopt for unimplemented GPU paths.
        auto gpuResult = std::visit([&](const auto& strategy) -> std::optional<Result> {
            using S = std::decay_t<decltype(strategy)>;

            if constexpr (std::same_as<S, Lloyd>)
                return Internal::ClusterLloydGpu(points, params.K, strategy, device);
            else
                return std::nullopt;  // no GPU impl for this strategy
        }, params.Algo);

        if (gpuResult) return gpuResult;
        // GPU failed or unavailable → fall through to CPU
    }

    // CPU fallback
    return Cluster(points, params);
}
```

---

## Call Sites

### Test (no GPU, `IntrinsicGeometryTests`)

```cpp
TEST(Clustering, LloydConvergesOnBlobs)
{
    auto points = GenerateBlobs(3, 100);

    Clustering::Params p;
    p.K = 3;
    p.Algo = Clustering::Lloyd{ .MaxIterations = 500 };

    auto result = Clustering::Cluster(points, p);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_EQ(result->Centroids.size(), 3u);
}

TEST(Clustering, MiniBatchMatchesLloyd)
{
    auto points = GenerateBlobs(3, 1000);

    Clustering::Params lloyd;
    lloyd.K = 3;
    lloyd.Algo = Clustering::Lloyd{};
    auto rLloyd = Clustering::Cluster(points, lloyd);

    Clustering::Params mini;
    mini.K = 3;
    mini.Algo = Clustering::MiniBatch{ .BatchSize = 200 };
    auto rMini = Clustering::Cluster(points, mini);

    // Both should find roughly the same centroids
    ASSERT_TRUE(rLloyd && rMini);
    EXPECT_EQ(rLloyd->Centroids.size(), rMini->Centroids.size());
}
```

### Runtime (GPU with fallback)

```cpp
Clustering::Params p;
p.K = 16;
p.Algo = Clustering::Lloyd{};
p.Compute = Clustering::Backend::GPU;

auto result = Clustering::Cluster(points, p, device);
if (result && result->ActualBackend != p.Compute)
    LOG_WARN("Clustering: fell back to CPU");
```

### UI-Driven Strategy Selection

```cpp
// strategies array for ImGui combo box
constexpr std::array<Clustering::Strategy, 3> kStrategies = {
    Clustering::Lloyd{},
    Clustering::MiniBatch{ .BatchSize = 200 },
    Clustering::KMeansPP{},
};
constexpr std::array<const char*, 3> kNames = { "Lloyd", "Mini-Batch", "K-Means++" };

// In UI code
static int selected = 0;
ImGui::Combo("Algorithm", &selected, kNames.data(), kNames.size());

Clustering::Params p;
p.K = userK;
p.Algo = kStrategies[selected];
p.Compute = useGpu ? Clustering::Backend::GPU : Clustering::Backend::CPU;
auto result = Clustering::Cluster(points, p, device);
```

---

## Why `std::variant` (and Not the Alternatives)

### Considered Alternatives

| Approach | Verdict |
|----------|---------|
| **Virtual base class** (`IClusterStrategy`) | Works, but user explicitly wants to avoid inheritance. Also: vtable overhead, heap allocation for polymorphic ownership. |
| **`InplaceFunction` type erasure** | Open extension (good for plugins), but loses type information — can't inspect "what strategy is active?", no compile-time exhaustiveness. |
| **Enum + switch** | Flat — all strategy params must live in one struct with unused fields. No per-variant parameter types. |
| **Concepts + templates** | Compile-time only. Doesn't support runtime strategy selection (UI combo, config file). |
| **`entt::dispatcher`** | Broadcast mechanism (all listeners fire), no return value. Wrong abstraction for "call one strategy, get a result." |

### Why `std::variant` Wins

1. **Each variant carries its own parameters.** `MiniBatch` has `BatchSize`, `Lloyd` doesn't. No unused fields, no `std::optional<uint32_t> BatchSize` in a flat struct.

2. **Compile-time exhaustiveness.** When you add `DBSCAN{}` to the variant, `std::visit` produces a compile error at every dispatch point until you handle it. No silent fallthrough.

3. **No vtable, no heap, no inheritance.** Just tagged union + visitor. Aligns with the engine's value-type philosophy.

4. **Runtime selection is trivial.** `params.Algo = strategies[comboIndex]` — works with UI, config files, command-line args.

5. **Two axes stay orthogonal.** Strategy (`variant`) × Backend (`enum`) — adding a new strategy doesn't touch backend dispatch, adding a new backend doesn't touch strategy logic.

6. **Testable in isolation.** Each `Internal::ClusterX()` function is independently testable. The `std::visit` layer is pure dispatch, trivially correct.

---

## Applying to a New Algorithm Family

### Template

```
Namespace:  Geometry::<AlgorithmFamily>
Strategies: std::variant<VariantA, VariantB, VariantC>
Backend:    enum class Backend : uint8_t { CPU, GPU }
Params:     { shared config + Strategy Algo + Backend Compute }
Result:     { output data + diagnostics + Backend ActualBackend }
Entry:      std::optional<Result> Execute(input, Params)           // CPU-only
            std::optional<Result> Execute(input, Params, Device&)  // GPU-capable
```

### Examples of Algorithm Families

| Family | Strategies | Notes |
|--------|-----------|-------|
| `Clustering` | `Lloyd`, `MiniBatch`, `KMeansPP`, `DBSCAN` | Point cloud segmentation |
| `Simplification` | `QEM`, `EdgeLength`, `Voxel` | Different quality/speed tradeoffs |
| `NormalEstimation` | `PCA`, `JetFitting`, `VoronoiBased` | Different robustness profiles |
| `SpatialIndex` | `Octree`, `KDTree`, `BVH` | Different query patterns |
| `Parameterization` | `LSCM`, `ARAP`, `Tutte` | UV unwrapping variants |

### Checklist

- [ ] Define strategy structs with variant-specific parameters
- [ ] Define `using Strategy = std::variant<...>` in the module interface
- [ ] `Params` struct with `Strategy Algo` + `Backend Compute` + shared config
- [ ] `Result` struct with `Backend ActualBackend` diagnostic
- [ ] CPU-only free function (no RHI dependency)
- [ ] GPU-capable overload with automatic CPU fallback
- [ ] `std::visit` dispatch — compiler enforces exhaustiveness
- [ ] `Internal::` namespace for per-variant implementations
- [ ] Tests for each strategy in `IntrinsicGeometryTests`
- [ ] Tests for GPU path + fallback in `IntrinsicTests`

---

## Link Boundary Design

```
┌─────────────────────────────────┐
│  IntrinsicGeometry              │  ← No RHI dependency
│                                 │
│  Clustering.cppm   (types)      │
│  Clustering.cpp    (CPU impls)  │
│  ClusteringLloyd.cpp            │  ← One TU per complex strategy
│  ClusteringMiniBatch.cpp        │
└──────────────┬──────────────────┘
               │ links into
┌──────────────▼──────────────────┐
│  IntrinsicRuntime               │  ← Has RHI
│                                 │
│  ClusteringGpu.cpp  (GPU impls) │
│  kmeans_assign.comp (shader)    │
│  kmeans_update.comp (shader)    │
└─────────────────────────────────┘

┌─────────────────────────────────┐
│  IntrinsicGeometryTests         │  ← Tests CPU path directly
│  IntrinsicTests                 │  ← Tests GPU path + fallback
└─────────────────────────────────┘
```

CPU implementations live in the Geometry library (no GPU dependency). GPU
dispatch lives in Runtime. `IntrinsicGeometryTests` tests all strategies on
CPU without requiring a Vulkan device. `IntrinsicTests` adds GPU path and
fallback coverage.
