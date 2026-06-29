# Algorithm Variant Dispatch Pattern

Status: canonical template. `Geometry.KMeans` is the first implemented exemplar
for the CPU-reference plus RHI-visible fallback seam.

This document describes the Strategy x Backend seam for geometry and method
algorithms that may later gain GPU execution. The first exemplar is
`Geometry.KMeans`: its CPU reference path is implemented in `src/geometry`, and
`Extrinsic.Runtime.KMeansBackend` provides the `RHI::IDevice`-visible overload
that falls back honestly until a real GPU kernel lands.

The seam keeps the CPU reference path testable without RHI while giving runtime
or method-integration code a clear place to request a GPU backend and fall back
honestly when that backend is unavailable.

## Axes

Dispatchable algorithm families have two independent dimensions:

| Axis | Mechanism | Meaning |
|---|---|---|
| Strategy | `std::variant<...>` or a small enum when no per-strategy payload is needed | Which algorithmic variant runs |
| Backend | `enum class Backend { CPU, GPU }` | Where execution is requested |

The backend enum is intentionally small at this seam:

- `Backend::CPU` maps to the method backend token `cpu_reference` unless a task
  explicitly introduces `cpu_optimized`.
- `Backend::GPU` maps to `gpu_vulkan_compute` for compute-style algorithm
  families, or `gpu_vulkan_graphics` for graphics-pipeline families.
- External accelerator backends may only enter through a separate method/backend
  task with its own policy and parity gate; they are not Vulkan-path tokens.

Every result must report the backend that actually ran. A requested GPU backend
that resolves to CPU is a valid fallback only when the result telemetry says so.

## Layer Boundary

The CPU reference entry point lives with the algorithm's owning lower layer. For
geometry algorithms, that means `src/geometry` and no RHI import.

The GPU-capable overload lives in the integration layer that can see RHI, usually
runtime or a declared method backend adapter. It takes `Extrinsic::RHI::IDevice&`
and must gate on `IDevice::IsOperational()`. It must not expose Vulkan backend
types or `Vk*` handles through the public seam.

```
Geometry or method CPU layer
  Algorithm.cppm  -> params/result/strategy types and CPU entry point
  Algorithm.cpp   -> deterministic CPU reference implementation

Runtime or backend adapter layer
  AlgorithmGpu.cpp -> RHI::IDevice-backed overload and GPU fallback policy
```

## Module Interface Shape

The owning algorithm module exports strategy/parameter/result types and a
CPU-only free function. This keeps unit tests and CPU CI independent of GPU
availability.

```cpp
module;

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.KMeans;

export namespace Geometry::KMeans
{
    enum class Backend : std::uint8_t
    {
        CPU = 0,
        GPU = 1,
    };

    struct Lloyd
    {
        std::uint32_t MaxIterations = 300;
        float ConvergenceEps = 1.0e-4f;
    };

    struct MiniBatch
    {
        std::uint32_t MaxIterations = 300;
        std::uint32_t BatchSize = 100;
        float ConvergenceEps = 1.0e-4f;
    };

    using Strategy = std::variant<Lloyd, MiniBatch>;

    struct Params
    {
        std::uint32_t ClusterCount = 8;
        Strategy Algorithm = Lloyd{};
        Backend Compute = Backend::CPU;
    };

    struct Result
    {
        std::vector<std::uint32_t> Labels{};
        std::vector<glm::vec3> Centroids{};
        std::uint32_t Iterations = 0;
        bool Converged = false;
        Backend RequestedBackend = Backend::CPU;
        Backend ActualBackend = Backend::CPU;
        bool FellBackToCPU = false;
    };

    [[nodiscard]] std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params);
}
```

For families that do not yet need multiple strategy payload types, the strategy
axis can be a small enum or omitted. Keep the backend and result telemetry the
same so the family can grow without changing the integration contract.

## CPU Dispatch

CPU dispatch is deterministic and exhaustive. It may use `std::visit` when the
strategy axis is a variant, but the public contract does not require inheritance
or a global registry.

```cpp
module Geometry.KMeans;

namespace Geometry::KMeans
{
    namespace
    {
        [[nodiscard]] std::optional<Result> ClusterLloyd(
            std::span<const glm::vec3> points,
            const Params& params,
            const Lloyd& strategy);

        [[nodiscard]] std::optional<Result> ClusterMiniBatch(
            std::span<const glm::vec3> points,
            const Params& params,
            const MiniBatch& strategy);
    }

    std::optional<Result> Cluster(
        std::span<const glm::vec3> points,
        const Params& params)
    {
        if (points.empty() || params.ClusterCount == 0)
            return std::nullopt;

        auto result = std::visit(
            [&](const auto& strategy) -> std::optional<Result>
            {
                using StrategyType = std::decay_t<decltype(strategy)>;

                if constexpr (std::same_as<StrategyType, Lloyd>)
                    return ClusterLloyd(points, params, strategy);
                else if constexpr (std::same_as<StrategyType, MiniBatch>)
                    return ClusterMiniBatch(points, params, strategy);
            },
            params.Algorithm);

        if (result)
        {
            result->RequestedBackend = params.Compute;
            result->ActualBackend = Backend::CPU;
            result->FellBackToCPU = params.Compute != Backend::CPU;
        }
        return result;
    }
}
```

The CPU entry point may accept a backend request in its params so callers can use
one config struct everywhere. `Geometry.KMeans` uses the existing
`KMeansParams::Compute` field for that request. The CPU-only function still
executes the CPU reference path and reports that fact through `ActualBackend`.

## GPU-Capable Overload

The GPU-capable overload is declared and built only where RHI is an allowed
dependency. It accepts `Extrinsic::RHI::IDevice&`, checks operational readiness,
tries the requested GPU path only when supported, and falls back to the CPU
reference otherwise.

```cpp
import Extrinsic.RHI.Device;
import Geometry.KMeans;

namespace Extrinsic::Runtime
{
    [[nodiscard]] std::optional<Geometry::KMeans::KMeansResult> ClusterKMeans(
        std::span<const glm::vec3> points,
        const Geometry::KMeans::KMeansParams& params,
        Extrinsic::RHI::IDevice& device)
    {
        namespace KMeans = Geometry::KMeans;

        if (params.Compute == KMeans::Backend::GPU &&
            device.IsOperational())
        {
            // Future parity-gated GPU kernel hook. GEOM-052 intentionally
            // installs only the seam, so the current exemplar falls through.
        }

        auto cpuParams = params;
        cpuParams.Compute = KMeans::Backend::CPU;
        auto cpuResult = KMeans::Cluster(points, cpuParams);
        if (cpuResult)
        {
            cpuResult->ActualBackend = KMeans::Backend::CPU;
            cpuResult->RequestedBackend = params.Compute;
            cpuResult->FellBackToCPU = params.Compute == KMeans::Backend::GPU;
        }
        return cpuResult;
    }
}
```

Fallback is not silent. Tests must assert requested-vs-actual backend telemetry,
especially when `Backend::GPU` is requested on a null or non-operational device.

## Config And Agent Lane

The backend field on the algorithm params is the supported override surface for
runtime config, CLI, editor, or agent-authored configuration. It is not a
hardcoded constant inside the algorithm implementation.

Recommended flow:

1. Config or command selects `Backend::CPU` or `Backend::GPU` for one dispatch
   family.
2. Runtime translates that value into the algorithm params.
3. The GPU-capable overload checks `RHI::IDevice::IsOperational()`.
4. The result reports `ActualBackend` and fallback state.
5. UI/agent diagnostics display requested and actual backends separately.

This keeps early CPU-only algorithms honest while preserving a stable control
surface for later GPU work.

## Applying To A New Algorithm Family

Use this checklist when adding a new dispatchable family:

- Define strategy payload structs only when the algorithm has real strategy
  variants with distinct parameters.
- Define `Backend::CPU` and `Backend::GPU`; map them to method backend-policy
  tokens in docs or diagnostics.
- Put shared config, strategy selection, and requested backend in the params
  struct.
- Put output payload, convergence/diagnostics, requested backend,
  `ActualBackend`, and fallback telemetry in the result struct.
- Export a CPU-only free function from the owning layer with no RHI dependency.
- Add a GPU-capable overload only in a layer that may import RHI, using
  `Extrinsic::RHI::IDevice&`.
- Gate GPU execution on `IDevice::IsOperational()` and explicit strategy support.
- Fall back to the CPU reference path with honest telemetry.
- Add CPU unit tests for each strategy and fallback/telemetry tests for any
  RHI-backed overload.

## Current Exemplar Status

`Geometry.KMeans` is the first exemplar. Its promoted geometry API exposes
`Backend::CPU` and `Backend::GPU`, accepts a backend request through
`KMeansParams::Compute`, and reports `RequestedBackend`, `ActualBackend`, and
`FellBackToCPU` in `KMeansResult`.

The CPU entry point always runs the CPU reference implementation. The runtime
adapter `Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)` accepts
`Extrinsic::RHI::IDevice&`, evaluates `IDevice::IsOperational()` for GPU
requests, and falls back to the CPU reference with honest telemetry because no
KMeans GPU kernel exists yet.
