---
id: ARCH-012
theme: F
depends_on:
  - ARCH-011
---
# ARCH-012 — ClusteringModule: proving extraction onto the kernel seams

## Goal
- Extract the k-means machinery (`Runtime.KMeansBackend`,
  `Runtime.KMeansGpuBackend`, `Runtime.KMeansGpuJobQueue`) out of the Engine
  god-object surface into a `ClusteringModule` composed by Sandbox, driving
  it end-to-end through the kernel seams — `RunKMeans` command → JobService
  CPU job on a snapshot → completion event → main-thread commit of cluster
  labels → visualization refresh via a standing event reaction — closing the
  `Operational` gate that `ARCH-007`..`ARCH-011` defer here
  ([ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md) D12,
  migration step ⑥).

## Non-goals
- No new clustering algorithms or quality changes to k-means itself.
- No `GpuQueue` job target requirement: the CPU path proves the seams; the
  GPU dispatch path keeps its current participant wiring until the
  `RUNTIME-137` line lands, and its move is recorded as a follow-up in this
  task on retirement.
- No UI panel work beyond a minimal contribute-if-present hook — the full
  panel-contribution registry belongs to the EditorUiModule extraction
  (`ARCH-006`/`UI-034`).
- No generalization to other backends (Poisson, bake queues) — each is its
  own extraction after this proof.

## Context
- Owner/layer: `runtime` module unit (first `RuntimeModule`); location
  decision (e.g. `src/runtime/Modules/Clustering/`) is recorded in this task
  when implementation starts and reflected in the module inventory.
- ADR-0024 D9 litmus test: "KMeans" is a domain noun — its scheduling
  logic, batching policy, and result handling do not belong in the kernel.
- This is deliberately the **first** extraction because it exercises every
  seam at once: command drain (ARCH-007), event pumps + worker publish
  (ARCH-008), snapshot-in/result-out job with world-scoped cancellation
  (ARCH-009), active-world access via handles (ARCH-010), and module
  lifecycle/registration (ARCH-011).
- The standing-reaction rule (ADR-0024 D6): the visualization refresh
  subscribes to a `ClusterLabelsChanged`-style event; no command chain.

## Required changes
- [ ] `ClusteringModule` implementing `IRuntimeModule`: registers the
      `RunKMeans` command handler, subscribes to its completion event,
      commits `ClusterLabels` (or the existing component equivalent) at the
      pump, publishes the attribute-changed event consumed by the existing
      visualization path.
- [ ] Command handler snapshots input positions (copy) and submits a
      world-scoped `CpuPool` job; no live-world references cross into the
      job.
- [ ] Sandbox composition registers the module; the k-means capability is
      absent when the module is not composed.
- [ ] Remove the extracted k-means entry points from the Engine
      interface/implementation surface; `Runtime.Engine.cppm` no longer
      imports the k-means modules.
- [ ] Record follow-up ownership for the GPU dispatch path move.

## Tests
- [ ] Integration/contract test (headless): compose kernel + ClusteringModule,
      enqueue `RunKMeans`, run frames until completion, assert labels
      committed on the main thread and the attribute event observed.
- [ ] Cancellation test: destroy/switch the world mid-job; no commit occurs.
- [ ] Composition-absence test: without the module, `RunKMeans` drains
      fail-closed (diagnostic, no crash).
- [ ] Existing k-means correctness tests keep passing against the moved code.

## Docs
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Update the runtime architecture doc's Engine-surface description and
      ADR-0024's validation note (import-count reduction evidence).

## Acceptance criteria
- [ ] `KMeans*` no longer appears in the Engine module's imports or public
      surface.
- [ ] The end-to-end flow (command → job → event → commit → visualization
      event) runs in Sandbox composition via `Engine::Run()` — this closes
      `Operational` for `ARCH-007`..`ARCH-011`.
- [ ] Default CPU gate green; layering gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- Keeping a parallel k-means path inside the Engine "for safety" — the
  extraction replaces, not duplicates.
- Algorithm/behavior changes to k-means beyond mechanical relocation and
  seam adaptation.
- Introducing module-to-module pointers to reach clustering results.

## Maturity
- Target: `Operational` (wired into `Engine::Run()` via Sandbox composition
  and exercised by the headless integration test named above). This task is
  the named `Operational` owner for `ARCH-007`..`ARCH-011`.
