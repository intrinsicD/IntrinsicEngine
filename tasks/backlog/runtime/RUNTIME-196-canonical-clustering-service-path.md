---
id: RUNTIME-196
theme: I
depends_on: [RUNTIME-192, RUNTIME-194, RUNTIME-195]
maturity_target: Retired
---
# RUNTIME-196 — Canonical clustering-service CPU/GPU path

## Goal

- Make `ClusteringService::RunKMeans` the sole typed clustering operation for
  CPU and GPU requests, keep backend implementation and GPU recording private,
  migrate every UI/config/agent workflow, and delete the parallel
  K-Means backend wrapper and Sandbox-private job queue.

## Non-goals

- No universal method service or backend registry.
- No K-Means numerical change, new clustering algorithm, or GPU parity claim
  without the existing CPU reference and actual-device evidence.
- No backend-specific DTO exposed through `SandboxEditorFacades`.

## Context

- `ClusteringModule`/`ClusteringService` is already the durable feature owner,
  but `Runtime.KMeansBackend`, `Runtime.KMeansGpuBackend`, the private
  `RuntimeKMeansGpuJobQueue`, and Sandbox result/backend records provide a
  second production route around it.
- `RUNTIME-194` and `RUNTIME-195` provide the one work and readback mechanisms.
  The feature service should own typed request/result semantics while its
  Vulkan implementation remains private module state.
- Geometry keeps the CPU reference and algorithm data; runtime owns backend
  selection, RHI access, ECS snapshot/writeback, and honest fallback.

## Slice plan

- **Slice A — sole typed operation.** Complete the service request/result with
  property references, backend request/actual identity, diagnostics, and
  fail-closed validation.
- **Slice B — GPU adoption.** Move the existing GPU implementation behind the
  service using canonical work/readback and prove CPU/GPU/fallback parity.
- **Slice C — cleanup.** Migrate app/config/agent callers, then delete the
  wrapper modules, private queue, duplicate DTOs, facade entry points, and
  obsolete tests in a separate cleanup commit.

## Required changes

- [ ] Define one `RunKMeans` request/result on `ClusteringService`, using
      `GeometryPropertyRef` for input/output property identity and plain
      backend/parameter/result records.
- [ ] Route CPU reference, operational Vulkan, and honest CPU fallback through
      that operation without exposing a backend interface or queue.
- [ ] Record GPU work through the service/module's private JobService
      participant and drain results through the shared transfer operation.
- [ ] Route config, UI, agent/CLI, selected-entity writeback, visualization
      result processing, cancellation, and stale completion through the same
      operation.
- [ ] Delete `Runtime.KMeansBackend`, the public
      `Runtime.KMeansGpuBackend` wrapper if it has no independent caller, the
      Sandbox-private GPU job queue, duplicated Sandbox backend/result enums,
      and direct K-Means commands after parity.

## Tests

- [ ] Service contracts cover validation, deterministic CPU results,
      cancellation, stale source/property generation, writeback, and
      UI/config/agent request parity.
- [ ] Null tests assert honest requested/actual backend and fallback reasons.
- [ ] Existing opt-in `gpu;vulkan` parity/smoke evidence is rerouted through
      `ClusteringService` and shared readback.
- [ ] Structural tests prove no production K-Means route bypasses the service
      and no deleted queue/wrapper name remains.

## Docs

- [ ] Update clustering/runtime/backend-dispatch docs with the sole typed
      operation and private backend ownership.
- [ ] Regenerate module inventory and remove documentation for the old queue
      and facade records.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Every K-Means caller invokes one `ClusteringService::RunKMeans`
      operation and observes the same typed result.
- [ ] CPU/GPU selection is request data; GPU implementation/work/readback
      details are private and fallback reporting is truthful.
- [ ] The parallel backend wrapper, private job queue, duplicate DTOs, and old
      facade command are deleted after parity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Clustering|KMeans' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'KMeans|Clustering' --timeout 180
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Another K-Means service, queue, facade DTO family, backend registry, or
  direct app-to-backend call.
- Moving the geometry CPU reference into runtime or RHI into geometry.
- Retaining the old route as a permanent compatibility path.

## Maturity

- Target: `Retired`; CPU contracts and operational Vulkan parity must precede
  deletion of every parallel production route.
