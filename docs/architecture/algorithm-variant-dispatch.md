# Algorithm Variant Dispatch Pattern

Status: canonical template. `Geometry.KMeans` is the first implemented exemplar
for a lower-layer CPU reference plus one runtime-owned typed operation;
GEOM-056 supplies Vulkan compute and RUNTIME-196 keeps its recorder, cache, and
readback lifecycle private behind `ClusteringService`.

This document describes typed strategy dispatch and the optional backend seam
for geometry and method algorithms. A backend selector is justified only when a
second implementation exists or an active task owns it; future possibility
alone does not justify a CPU/GPU token or fallback branch. The first full
Strategy × Backend exemplar is
`Geometry.KMeans`: its CPU reference path is implemented in `src/geometry`, and
`Extrinsic.Runtime.ClusteringService::RunKMeans` is the sole operation that
selects CPU reference or Vulkan compute, owns ECS snapshot/writeback, and
reports fallback honestly. Its Vulkan recorder, persistent resources, and
typed readback are implementation-only module state rather than another caller
surface.

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

The GPU-capable implementation lives in the integration layer that can see RHI,
usually runtime or a declared method backend adapter. The public integration
surface is one typed feature operation; it carries backend selection as request
data and must not expose `RHI::IDevice`, command contexts, Vulkan backend types,
resource caches, readback adapters, or `Vk*` handles. The private implementation
gates on `IDevice::IsOperational()` and rejoins the canonical result path.

```
Geometry or method CPU layer
  Algorithm.cppm  -> params/result/strategy types and CPU entry point
  Algorithm.cpp   -> deterministic CPU reference implementation

Runtime or backend adapter layer
  FeatureService.cppm -> one typed request/completion operation
  FeatureGpu.cpp      -> private RHI recording/readback and fallback policy
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

## Runtime Operation And Private GPU Backend

Once a GPU implementation exists, expose one typed feature operation in the
RHI-owning integration layer. The request carries the selected source/output
identities, algorithm parameters, and requested backend; the completion carries
the same identities plus actual backend and fallback diagnostics. The operation
captures an immutable CPU snapshot before either backend runs and owns the sole
main-thread validation/writeback boundary.

The GPU implementation is private module state. It checks operational readiness,
records through the runtime's existing frame command context, retains only the
resources needed across frames, drains one shared result batch, and publishes
into the same completion path as the CPU reference. Callers—including UI,
config/agent control, tests, and benchmarks—never receive the recorder or cache.

```cpp
const RunKMeans request{
    .StableEntityId = selected,
    .Properties = MakeKMeansPropertyRefs(domain),
    .Parameters = configuredParameters,
    .Backend = ClusteringBackend::VulkanCompute,
};
const CommandCorrelationId correlation = service.RunKMeans(request);
```

Fallback is not silent. Contracts assert requested-vs-actual backend telemetry,
especially when Vulkan compute is requested on a null or non-operational device.
Cancellation, stale world/entity/property state, GPU failure, CPU fallback, and
successful writeback all terminate through the same typed completion event.

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

GPU backends drain final CPU-visible results through
`Graphics::GpuTransfer::ScheduleReadbackBatch(...)` (RUNTIME-195) rather than
`RHI::IDevice::ReadBuffer`, which performs a device-wide `vkDeviceWaitIdle` on
every call. One logical batch owns copied storage for all result ranges,
revalidates the exact handles and byte ranges before exactly-once consumption,
and composes with the runtime `JobService` readiness/cancellation lifecycle.
Feature adapters own typed parsing; the transport owns no method semantics.
`IDevice::ReadBuffer` remains the explicit-stall escape hatch. See
`docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 1.

Capability evidence remains method-specific. K-Means has an actual-Vulkan
compute-to-result parity smoke over its shared batch. Progressive Poisson's
RUNTIME-195 smoke instead seeds a CPU-reference-shaped payload into the three
production-shaped output buffers and proves only the real transfer queue plus
typed parser; METHOD-014 still owns compute execution and public CPU/GPU parity.
Do not promote transport evidence into an algorithm-parity claim.

## Config And Agent Lane

For a family with a justified backend seam, the backend field on the algorithm
params is the supported override surface for runtime config, CLI, editor, or
agent-authored configuration. It is not a hardcoded constant inside the
algorithm implementation.

Recommended flow:

1. Config or command selects `Backend::CPU` or `Backend::GPU` for one dispatch
   family.
2. Runtime translates that value into the sole typed feature request.
3. Private module state checks `RHI::IDevice::IsOperational()` and either
   accepts GPU work or invokes the CPU reference with a fallback diagnostic.
4. The typed completion reports `ActualBackend` and fallback state.
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
- When GPU execution is owned, expose one typed feature operation in a layer
  that may import RHI; keep device, recorder, resources, and readback private.
- Gate private GPU work on `IDevice::IsOperational()` and explicit strategy
  support, and rejoin the CPU reference completion path with honest telemetry.
- Add CPU unit tests for each strategy and fallback/telemetry tests for any
  RHI-backed overload.

## Current Exemplar Status

`Geometry.KMeans` is the first exemplar. Its promoted geometry API exposes
`Backend::CPU` and `Backend::GPU`, accepts a backend request through
`KMeansParams::Compute`, and reports `RequestedBackend`, `ActualBackend`, and
`FellBackToCPU` in `KMeansResult`.

The Sandbox K-Means panel exposes CPU reference vs Vulkan compute through the
validated `sandbox.clustering` config section. UI and agent/CLI hot apply both
map `ClusteringConfig` to the same `RunKMeans` request, and
`KMeansRunCompleted` reports stable requested/actual backend identity plus an
explicit fallback diagnostic without requiring callers to scrape text.

The geometry entry point always runs the deterministic CPU reference.
`Extrinsic.Runtime.ClusteringService::RunKMeans` is the sole integration
operation: it snapshots typed geometry properties, routes CPU work through
world-scoped `JobService`, or accepts Vulkan work into one private clustering
GPU participant. The non-exported backend partition reuses persistent `(n,k)`
buffers, records the reset/assign/update loop, and drains labels, distances,
and centroids as one copied `Graphics.GpuTransfer` batch after producer
retirement. Both paths rejoin one stale/cancellation/writeback gate, publish the
same typed completion, and commit label/color properties before visualization
refresh. No Sandbox facade DTO, backend module, direct benchmark import, or
second queue bypasses the service. GEOM-056/RUNTIME-196 prove this operation
with an opt-in `gpu;vulkan` service parity smoke and the stable
`IntrinsicKMeansGpuBenchmarkSmoke`, which reports end-to-end
command-to-applied-event timing, CPU-reference parity, and no speedup claim.
