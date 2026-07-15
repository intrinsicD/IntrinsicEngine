# Algorithm Variant Dispatch Pattern

Status: canonical template. `Geometry.KMeans` is the first implemented exemplar
for the CPU-reference plus RHI-visible seam; GEOM-056 adds the opt-in
Vulkan-compute execution surface behind that seam.

This document describes typed strategy dispatch and the optional backend seam
for geometry and method algorithms. A backend selector is justified only when a
second implementation exists or an active task owns it; future possibility
alone does not justify a CPU/GPU token or fallback branch. The first full
Strategy × Backend exemplar is
`Geometry.KMeans`: its CPU reference path is implemented in `src/geometry`,
`Extrinsic.Runtime.KMeansBackend` provides the `RHI::IDevice`-visible
convenience overload that falls back honestly, and
`Extrinsic.Runtime.KMeansGpuBackend` exposes the explicit command-recording
Vulkan-compute path for callers that own pipelines, cache, and async readback
lifetime.

The seam keeps the CPU reference path testable without RHI while giving runtime
or method-integration code a clear place to request a GPU backend and fall back
honestly when that backend is unavailable.

## Axes

Dispatchable algorithm families have a strategy dimension and, when justified
by real implementations, an independent backend dimension:

| Axis | Mechanism | Meaning |
|---|---|---|
| Strategy | `std::variant<...>` or a small enum when no per-strategy payload is needed | Which algorithmic variant runs |
| Backend (optional) | A small enum owned at the lowest layer that can execute or route every advertised value | Where execution is requested |

Do not expose a backend selector merely to report that the only implementation
ran. Add requested/actual backend telemetry when a second backend exists or is
owned by an active task; keep GPU availability and fallback at the RHI-visible
integration boundary. When that trigger is met, keep the backend enum small:

- `Backend::CPU` maps to the method backend token `cpu_reference` unless a task
  explicitly introduces `cpu_optimized`.
- `Backend::GPU` maps to `gpu_vulkan_compute` for compute-style algorithm
  families, or `gpu_vulkan_graphics` for graphics-pipeline families.
- External accelerator backends may only enter through a separate method/backend
  task with its own policy and parity gate; they are not Vulkan-path tokens.

For a family with a backend seam, every result must report the backend that
actually ran. A requested GPU backend that resolves to CPU is a valid fallback
only when the result telemetry says so.

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
axis can be a small enum or omitted. For families with only one real backend,
omit backend values and telemetry until the reintroduction trigger above is met.

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

Once a real backend seam exists, the CPU entry point may accept a backend request
in its params so callers can use one config struct everywhere.
`Geometry.KMeans` uses the existing `KMeansParams::Compute` field for that
request. A CPU-only family without that seam should expose neither the request
nor synthetic `ActualBackend` telemetry.

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

Reusable GPU building blocks should stay in graphics-owned modules rather than
inside individual method adapters. For scan/compaction-style compute workloads,
`Extrinsic.Graphics.ComputeParallelPrimitives` is the shared GRAPHICS-108 seam:
Slice A provides the deterministic CPU reference and fail-closed GPU request
contract, Slice B pins the shader assets plus backend-neutral dispatch/scratch
planning contract, Slice C records RHI compute commands with opt-in
`gpu;vulkan` parity, and Slice D publishes compacted counts as explicit readback
copies and dispatch-indirect argument buffers for downstream GPU consumers.
Method adapters such as METHOD-013 should consume that seam instead of declaring
private CUB-equivalent primitives; later tasks own method-specific GPU backends
and runtime UI routing.

GPU backends should also drain their results through the runtime-owned
`Extrinsic.Runtime.AsyncBufferReadback` helper (RUNTIME-137) rather than
`RHI::IDevice::ReadBuffer`, which performs a device-wide `vkDeviceWaitIdle` on
every call. The helper composes `Graphics::GpuTransfer` (record barrier →
non-blocking download → `Poll()` for delivery) and pools the host destination
across drains. `IDevice::ReadBuffer` remains the explicit-stall escape hatch. See
`docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 1.

## Config And Agent Lane

For a family with a justified backend seam, the backend field on the algorithm
params is the supported override surface for runtime config, CLI, editor, or
agent-authored configuration. It is not a hardcoded constant inside the
algorithm implementation.

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
- Add backend tokens only when a second implementation exists or an active task
  owns it; map real tokens to method backend policy in docs or diagnostics.
- Put shared config and strategy selection in the params struct; add a requested
  backend only after that backend trigger is met.
- Put output payload and convergence/diagnostics in the result struct; add
  requested/actual/fallback telemetry only for a real backend seam.
- Export a CPU-only free function from the owning layer with no RHI dependency.
- When GPU execution is owned, add its overload only in a layer that may import
  RHI, using `Extrinsic::RHI::IDevice&`.
- Gate an owned GPU path on `IDevice::IsOperational()` and explicit strategy
  support, and fall back to the CPU reference with honest telemetry.
- Add CPU unit tests for each strategy and fallback/telemetry tests for any
  RHI-backed overload.

## Current Exemplar Status

`Geometry.KMeans` is the first exemplar. Its promoted geometry API exposes
`Backend::CPU` and `Backend::GPU`, accepts a backend request through
`KMeansParams::Compute`, and reports `RequestedBackend`, `ActualBackend`, and
`FellBackToCPU` in `KMeansResult`.

The Sandbox K-Means panel exposes that backend request to users as CPU reference
vs Vulkan compute. `SandboxEditorKMeansResult` reports stable requested and
actual backend ids (`cpu_reference` / `gpu_vulkan_compute`), display names, and
CPU fallback reason when applicable, so the UI and agent callers can choose a
backend for one run without scraping diagnostics text.

The CPU entry point always runs the CPU reference implementation. The runtime
adapter `Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)` accepts
`Extrinsic::RHI::IDevice&`, evaluates `IDevice::IsOperational()` for GPU
requests, and falls back to the CPU reference with honest telemetry because that
synchronous convenience overload does not own the command context, pipeline set,
persistent buffer cache, or async readback set required to execute GPU work.
The explicit runtime GPU surface lives in
`Extrinsic.Runtime.KMeansGpuBackend`: callers supply those dependencies to
`RecordKMeansGpuExecution(...)`, which reuses persistent `(n,k)` buffers, uploads
SoA positions plus seed centroids once, records the reset/assign/update pass
loop, and returns cache-owned result resources. Callers enqueue
`KMeansGpuAsyncReadbacks` after the producing command submission has retired;
the helper then drains labels/distances/centroids through
`AsyncBufferReadback` without a device-wide readback stall. The Sandbox editor
uses `Extrinsic.Runtime.KMeansGpuJobQueue`, a runtime queue registered with the
`JobService` `GpuQueue` participant registry so those command/readback
dependencies record inside the normal renderer frame context and never create an
extra swapchain present. The queue publishes completed GPU labels and colors
back through the same ECS property path as CPU K-Means. GEOM-056 proves
the explicit GPU path with an opt-in `gpu;vulkan` parity smoke and
`IntrinsicKMeansGpuBenchmarkSmoke`, which emits GPU timing, CPU-reference
baseline timing, and parity diagnostics without making a speedup claim.
