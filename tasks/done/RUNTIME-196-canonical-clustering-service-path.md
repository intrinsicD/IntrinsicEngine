---
id: RUNTIME-196
theme: I
depends_on: [RUNTIME-192, RUNTIME-194, RUNTIME-195]
maturity_target: Retired
---
# RUNTIME-196 — Canonical clustering-service CPU/GPU path

## Status

- Retired on 2026-07-28 at `Retired` after Slices A-C converged every
  production CPU/GPU caller on `ClusteringService::RunKMeans` and deleted the
  parallel wrapper, queue, facade DTO, command, and dedicated-test surfaces.
- Retirement commit reference: this commit plus the Slice A/B commits recorded
  below.
- Promoted to active on 2026-07-27 after `RUNTIME-192`, `RUNTIME-194`, and
  `RUNTIME-195` retired the property-vocabulary, work-lifecycle, and shared
  GPU-result-readback prerequisites.
- Intake census found four production routes or surfaces for one operation:
  `ClusteringService::RunKMeans` owns the CPU lifecycle, the synchronous
  `Runtime.KMeansBackend` wrapper advertises fallback only,
  `Runtime.KMeansGpuBackend` exports the Vulkan recorder/readback machinery,
  and `SandboxEditorSession` owns a separate single-flight GPU queue plus
  duplicate command/result/backend records. The Vulkan smoke and benchmark
  also import the backend surface directly.
- Slice A completed on 2026-07-27. `RunKMeans` now carries canonical typed
  input/output `GeometryPropertyRef` records plus one nested parameter record;
  `KMeansRunCompleted` returns those identities, requested/actual backend,
  and an explicit backend diagnostic. Invalid requests report
  `ActualBackend::None`, while a Vulkan request on the Null device reports the
  CPU reference fallback truthfully. The focused service target compiled and
  `ClusteringModule.*` passed 4/4. No proven GPU path has been deleted yet.
- Slice B completed on 2026-07-27. `ClusteringModule` now owns one private
  single-flight Vulkan state registered as a `JobService` GPU participant and
  drains its typed result through `Graphics::GpuTransfer`; failures re-enter
  the same CPU-reference completion path with an explicit diagnostic. The
  opt-in smoke now submits through `ClusteringService`, writes the canonical
  label/color properties, matches CPU labels/inertia, and passed 1/1 on the
  promoted Vulkan device with one GPU completion and zero fallbacks. This is
  the parity gate for Slice C deletion; the legacy surfaces remain only until
  their callers are migrated.
- Slice C completed on 2026-07-28. `sandbox.clustering` now round-trips the
  full request through the app-owned config registry and the editor invokes
  the same service operation directly. The Vulkan backend is a non-exported
  clustering partition; the two public wrapper modules, Sandbox-private GPU
  queue, duplicate facade records/command, and wrapper-only tests are deleted.
  The Vulkan benchmark preserves its stable ID but now measures the
  service-command-to-applied-event route and explicitly makes no speedup
  claim. Focused CPU coverage passed 48/48. The complete CPU-supported selector
  passed 4,210 cases with zero failures plus the expected GLFW/LSan self-skip.
  The ASan+UBSan promoted-Vulkan selector passed 3/3 on an NVIDIA GeForce RTX
  3050 with driver 590.48.01: service compute parity, benchmark execution, and
  strict result validation. Layering, test layout, task policy, docs links,
  root hygiene, benchmark manifests, whitespace, and the 382-module generated
  inventory are clean; the ARA C11 record binds the capability scope and its
  no-performance-claim boundary.

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

## Right-sizing decision

- **Elements:** `Runtime.KMeansBackend`, public
  `Runtime.KMeansGpuBackend`, `RuntimeKMeansGpuJobQueue`, and the Sandbox
  K-Means request/result/backend family trigger the shallow-wrapper,
  role-fragmentation, pure-forwarding, and parallel-pipeline heuristics. They
  split one feature across two owners and retain an API primarily for direct
  tests and one benchmark.
- **Simpler alternative:** deepen the existing `ClusteringModule` and its
  `ClusteringService::RunKMeans` operation. Export plain request/completion
  records with canonical `GeometryPropertyRef` identities; keep one optional
  in-flight Vulkan state, command recording, resource cache, and typed shared
  readback private in the module implementation. Use the existing
  `JobService` GPU participant and `Graphics.GpuTransfer`; add no backend
  interface, factory, registry, service, or queue.
- **Blast radius:** clustering module interface/implementation, runtime and
  test CMake registration, Sandbox session/facade/panel models, clustering
  contracts, the opt-in Vulkan smoke and benchmark, runtime/geometry/backend
  dispatch docs, task indexes, and the generated module inventory. Strict
  source-search and layering checks must close the census.
- **Reintroduction trigger:** extract a reusable backend adapter only when a
  second independent runtime feature needs the same K-Means recorder with a
  different lifecycle owner. A second UI, test, benchmark, or parameter
  variant remains a caller of `ClusteringService` and does not justify another
  route.

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

- [x] Define one `RunKMeans` request/result on `ClusteringService`, using
      `GeometryPropertyRef` for input/output property identity and plain
      backend/parameter/result records.
- [x] Route CPU reference, operational Vulkan, and honest CPU fallback through
      that operation without exposing a backend interface or queue.
- [x] Record GPU work through the service/module's private JobService
      participant and drain results through the shared transfer operation.
- [x] Route config, UI, agent/CLI, selected-entity writeback, visualization
      result processing, cancellation, and stale completion through the same
      operation.
- [x] Delete `Runtime.KMeansBackend`, the public
      `Runtime.KMeansGpuBackend` wrapper if it has no independent caller, the
      Sandbox-private GPU job queue, duplicated Sandbox backend/result enums,
      and direct K-Means commands after parity.

## Tests

- [x] Service contracts cover validation, deterministic CPU results,
      cancellation, stale source/property generation, writeback, and
      UI/config/agent request parity.
- [x] Null tests assert honest requested/actual backend and fallback reasons.
- [x] Existing opt-in `gpu;vulkan` parity/smoke evidence is rerouted through
      `ClusteringService` and shared readback.
- [x] Structural tests prove no production K-Means route bypasses the service
      and no deleted queue/wrapper name remains.

## Docs

- [x] Update clustering/runtime/backend-dispatch docs with the sole typed
      operation and private backend ownership.
- [x] Regenerate module inventory and remove documentation for the old queue
      and facade records.
- [x] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [x] Every K-Means caller invokes one `ClusteringService::RunKMeans`
      operation and observes the same typed result.
- [x] CPU/GPU selection is request data; GPU implementation/work/readback
      details are private and fallback reporting is truthful.
- [x] The parallel backend wrapper, private job queue, duplicate DTOs, and old
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

## Clean-workshop review

- Rows 1-2: **pass** — strict module/CMake layering is clean; the private
  partition and smoke-target rename add no cross-layer target link.
- Row 3: **pass** — the public runtime service exposes only runtime/geometry
  value records; Vulkan/RHI implementation types remain non-exported.
- Rows 4-6: **n/a** — no renderer member, frame-graph pass, or recipe edge was
  added.
- Row 7: **pass** — the task retires only after actual Vulkan service parity,
  so no scaffold/parity maturity follow-up is owed.
- Row 8: **pass** — no allowlist entry, temporary exception, or compatibility
  shim remains.
