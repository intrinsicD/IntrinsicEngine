# Algorithm Variant Dispatch Pattern

Runtime backend selection (CPU / GPU Compute) for parallelizable algorithms,
without virtual inheritance. Fits the existing Params/Result free-function
pattern used throughout `Geometry::*`.

---

## Core Idea

1. **`Backend` enum in Params** — same pattern as `Runtime::Selection::PickBackend`.
2. **Function overloading** — CPU-only overload (no GPU dependency) and GPU-capable overload (takes `RHI::VulkanDevice&`).
3. **`ActualBackend` in Result** — transparent fallback reporting.
4. **No inheritance, no vtable, no CRTP** — dispatch is a `switch` inside the implementation.

---

## Pattern Structure

### 1. Shared Types (module partition, no GPU dependency)

```cpp
// Geometry.KMeans.cppm  (partition of Geometry module)
export module Geometry:KMeans;

export namespace Geometry::KMeans
{
    enum class Backend : uint8_t
    {
        CPU = 0,
        GPU = 1   // Vulkan compute shader
    };

    struct Params
    {
        uint32_t K              = 8;
        uint32_t MaxIterations  = 300;
        float    ConvergenceEps = 1e-4f;
        Backend  ComputeBackend = Backend::CPU;
    };

    struct Result
    {
        std::vector<uint32_t>  Assignments;   // per-point cluster index
        std::vector<glm::vec3> Centroids;     // final centroid positions
        uint32_t               Iterations = 0;
        bool                   Converged  = false;
        Backend                ActualBackend = Backend::CPU; // what actually ran
    };
}
```

### 2. CPU Implementation (links into `IntrinsicGeometry`, testable without GPU)

```cpp
// Geometry.KMeans.cpp
module Geometry;
import :KMeans;

namespace Geometry::KMeans
{
    // CPU-only entry point. Always runs on CPU regardless of Params::ComputeBackend.
    // This is what IntrinsicGeometryTests calls directly.
    std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params)
    {
        if (points.empty() || params.K == 0) return std::nullopt;

        // ... Lloyd's algorithm implementation ...

        Result r;
        r.ActualBackend = Backend::CPU;
        return r;
    }
}
```

### 3. GPU-Capable Dispatch (links into `IntrinsicRuntime`, needs RHI)

```cpp
// Geometry.KMeansGpu.cpp  (or a Runtime-level wrapper)
module Geometry;
import :KMeans;

// Forward: GPU implementation in a separate TU
namespace Geometry::KMeans::Internal
{
    std::optional<Result> ClusterGpu(
        std::span<const glm::vec3> points,
        const Params& params,
        RHI::VulkanDevice& device);
}

namespace Geometry::KMeans
{
    // GPU-capable overload. Falls back to CPU if GPU unavailable or fails.
    std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params,
        RHI::VulkanDevice& device)
    {
        switch (params.ComputeBackend)
        {
        case Backend::GPU:
        {
            auto result = Internal::ClusterGpu(points, params, device);
            if (result) return result;
            // GPU failed — fall through to CPU
            [[fallthrough]];
        }
        case Backend::CPU:
        {
            // Delegate to the CPU-only overload
            auto result = Cluster(points, params);
            if (result) result->ActualBackend = Backend::CPU;
            return result;
        }
        }
        return std::nullopt; // unreachable
    }
}
```

### 4. GPU Compute Implementation (Vulkan compute shader)

```cpp
// Geometry.KMeansGpu.cpp
namespace Geometry::KMeans::Internal
{
    std::optional<Result> ClusterGpu(
        std::span<const glm::vec3> points,
        const Params& params,
        RHI::VulkanDevice& device)
    {
        // 1. Upload points to SSBO
        // 2. Create/cache compute pipeline (kmeans_assign.comp, kmeans_update.comp)
        // 3. Dispatch assignment step: each invocation assigns one point
        // 4. Dispatch centroid update step: parallel reduction per cluster
        // 5. Iterate until convergence or MaxIterations
        // 6. Readback assignments + centroids

        Result r;
        r.ActualBackend = Backend::GPU;
        return r;
    }
}
```

---

## Call Sites

### In Tests (no GPU, `IntrinsicGeometryTests`)

```cpp
TEST(KMeans, ConvergesOnSimpleData)
{
    std::vector<glm::vec3> points = GenerateBlobs(3, 100);

    Geometry::KMeans::Params params;
    params.K = 3;
    // No Backend field needed — CPU-only overload has no Backend dispatch
    auto result = Geometry::KMeans::Cluster(points, params);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_EQ(result->Centroids.size(), 3u);
    EXPECT_EQ(result->ActualBackend, Geometry::KMeans::Backend::CPU);
}
```

### In Runtime (with GPU available)

```cpp
void SomeSystem::ClusterPointCloud(std::span<const glm::vec3> points,
                                   RHI::VulkanDevice& device)
{
    Geometry::KMeans::Params params;
    params.K = 16;
    params.ComputeBackend = Geometry::KMeans::Backend::GPU;

    auto result = Geometry::KMeans::Cluster(points, params, device);
    if (!result) return;

    if (result->ActualBackend != params.ComputeBackend)
    {
        LOG_WARN("KMeans: GPU unavailable, fell back to CPU");
    }
    // Use result->Assignments, result->Centroids ...
}
```

---

## Key Design Decisions

### Why function overloading instead of `std::variant`?

- `std::variant<CpuCtx, GpuCtx>` introduces a type not used elsewhere for polymorphism in this codebase.
- Function overloading is already the primary dispatch mechanism (`Support()` overloads, `TestOverlap()` overloads).
- The "has GPU" vs "no GPU" distinction maps naturally to an overload with/without `VulkanDevice&`.

### Why not templates + concepts?

- Templates would force compile-time backend selection. The user explicitly needs **runtime** dispatch (e.g., GPU availability detected at startup, user toggle in UI).
- Concepts are still useful to constrain what qualifies as valid input data (e.g., a `ClusterableData` concept for different point representations).

### Why enum in Params, not a separate argument?

- Matches the existing `PickBackend` pattern in `Runtime::Selection::PickRequest`.
- Params structs with sensible defaults are the canonical pattern — `Backend = CPU` is the safe default.
- Result carries `ActualBackend` so callers always know what ran, enabling fallback transparency.

### Why separate CPU and GPU TUs?

- **Link boundary isolation.** `IntrinsicGeometryTests` links against `IntrinsicGeometry` (no RHI). The CPU implementation lives in the Geometry library. The GPU dispatch wrapper lives in Runtime.
- **Incremental compilation.** Touching a compute shader or the GPU codepath doesn't rebuild the CPU algorithm.

---

## Applying to a New Algorithm

1. **Create the partition**: `Geometry.YourAlgo.cppm` with `Backend` enum, `Params`, `Result`.
2. **Write CPU impl**: `Geometry.YourAlgo.cpp` — free function, `std::optional<Result>` return, no GPU dependency.
3. **Write GPU dispatch**: `Geometry.YourAlgoGpu.cpp` (or in Runtime) — overload with `VulkanDevice&`, switch on `Backend`, fallthrough to CPU.
4. **Write compute shader**: `shaders/youralgo.comp`, register in ShaderRegistry.
5. **Add to CMakeLists.txt**: `.cppm` in `FILE_SET CXX_MODULES`, `.cpp` in `PRIVATE`.
6. **Tests**: `IntrinsicGeometryTests` tests CPU path directly. `IntrinsicTests` tests GPU path with device.

### Checklist for new algorithm variants

- [ ] `Backend` enum with `CPU` and `GPU` values
- [ ] `Params` struct with `Backend ComputeBackend = Backend::CPU` default
- [ ] `Result` struct with `Backend ActualBackend` diagnostic field
- [ ] CPU-only overload (no `VulkanDevice&` parameter)
- [ ] GPU-capable overload with automatic CPU fallback
- [ ] GPU failure → fallthrough to CPU (never crash on missing GPU)
- [ ] Tests for CPU path in `IntrinsicGeometryTests`
- [ ] Tests for GPU path + fallback in `IntrinsicTests`

---

## Future Extensions

### Additional Backends

```cpp
enum class Backend : uint8_t
{
    CPU        = 0,
    GPU        = 1,  // Vulkan compute
    CPUParallel = 2, // std::execution::par_unseq
};
```

Add a new case to the switch. No interface change for callers using `Backend::CPU` or `Backend::GPU`.

### Backend Capabilities Query

```cpp
// At startup or on demand
struct BackendCapabilities
{
    bool HasGpuCompute = false;
    uint32_t MaxWorkgroupSize = 0;
    uint64_t MaxSSBOSize = 0;
};

BackendCapabilities QueryCapabilities(RHI::VulkanDevice& device);
```

Callers can check capabilities before selecting `Backend::GPU`, or just request it and check `ActualBackend` in the result.

### Shared Concept for Input Data

```cpp
template <typename T>
concept ClusterableData = requires(const T& data)
{
    { data.size() }         -> std::convertible_to<std::size_t>;
    { data[0] }             -> std::convertible_to<glm::vec3>;
    { std::span(data) }; // must be contiguous for GPU upload
};
```

This keeps the algorithm generic over `std::vector<vec3>`, `std::span<vec3>`, or custom point containers.
