---
id: RUNTIME-126
theme: B
depends_on: [GRAPHICS-096, GRAPHICS-098]
maturity_target: Operational
---
# RUNTIME-126 — GPU readback jobs and result→property write-back in the derived-job graph

## Goal
- Close the GPU→CPU leg of the transfer foundation at the runtime scheduling
  layer: add a **readback job kind** to the existing `DerivedJobRegistry` that
  drives a GRAPHICS-096 async `DownloadBuffer`, and a **readback→property
  write-back binding** that writes the result into a target `PropertyRegistry`
  property (dimensions must match, validated through GRAPHICS-095), so an
  algorithm can chain follow-ups on a GPU-computed result — e.g. "compute on GPU
  → read back → derive a per-vertex color / vector-field property → re-upload →
  make visible" — using the same `SubmitFollowUp` / `DependsOn` edges that
  derived geometry jobs already use.

## Non-goals
- No new scheduler, task graph, or executor: reuse `DerivedJobRegistry`
  (RUNTIME-112), `StreamingExecutor`, and `Core.Dag`.
- No new forward (CPU→GPU) binding: reuse RUNTIME-120's `VertexAttributeBinding`
  resolver, RUNTIME-123's editor binding, and GRAPHICS-084's visualization
  property buffers. This task adds only the reverse (readback) direction.
- No per-attribute *upload* streaming changes (owned by RUNTIME-122/124).
- No async readback transport changes (owned by GRAPHICS-096/097); this task
  consumes that transport.
- No editor UI surface (a UI follow-up can consume this later).

## Context
- Owning subsystem/layer: `src/runtime` (`runtime -> graphics/geometry/core/ecs`).
- The chaining/scheduling spine already exists: RUNTIME-112's
  `Runtime::DerivedJobRegistry` (`src/runtime/Runtime.DerivedJobGraph.cppm`)
  exposes `Submit(DerivedJobDesc)`, `SubmitFollowUp(...)`, `DependsOn`,
  `DerivedJobOutput`, `DerivedJobApplyContext`, and `TaskKind`, backed by
  `StreamingExecutor` with explicit dependencies, follow-up scheduling, and
  stale/cancel/failure diagnostics. Today its jobs are CPU/geometry derivations
  with a main-thread apply phase; none read back from the GPU.
- The forward direction is covered: RUNTIME-120 `VertexAttributeBinding` (done)
  binds any feasible typed, count-matched property to a vertex channel;
  RUNTIME-123 exposes the editor "bind any property as normals/colors";
  GRAPHICS-084 (done) binds properties to scalar/color/vector-field visualization
  buffers and already enforces the `ElementCount × stride == bytes` rule.
- The missing piece is the readback leg: GRAPHICS-096 adds the async
  `DownloadBuffer` ring (no caller-thread fence wait, drain-time delivery) and
  GRAPHICS-098 the imperative `GpuTransfer` facade; this task wires those into a
  derived-job kind plus the readback→property write-back binding, validated by
  GRAPHICS-095's property-agnostic dimension-match primitive.
- ADR-0023 records the layering and the reuse-not-reinvent decision.

## Slice plan
- **Slice A (CPU contract).** Add the readback→property write-back binding
  (validate a GPU buffer range against a target property via GRAPHICS-095; write
  bytes into the property on apply) and a readback `DerivedJobDesc` kind that
  delivers a GRAPHICS-096 readback result through the existing main-thread apply
  phase. Prove against a mock transfer queue / mock graphics seams. Preserves the
  default CPU gate.
- **Slice B (operational evidence).** Reuse the GRAPHICS-096 (and/or
  visualization) `gpu;vulkan` smoke to prove a device-computed buffer is read
  back, written into a CPU property, and a chained follow-up re-uploads a derived
  property that the renderer consumes.

## Required changes
- [ ] Add a readback→property write-back binding (runtime, backend-neutral):
      given a source GPU buffer range and a target `PropertyRegistry` property,
      validate dimensional compatibility through `RHI::BufferTransfer`
      (GRAPHICS-095) and write the readback bytes into the property; fail closed
      with precise diagnostics on mismatch.
- [ ] Add a readback `DerivedJobDesc` kind / submission path that issues a
      GRAPHICS-096 `DownloadBuffer` (or GRAPHICS-098 facade readback) and lands
      the result via `DerivedJobApplyContext` on the main-thread apply phase,
      reusing the existing dependency / follow-up / stale-cancel machinery.
- [ ] Ensure chaining works end to end: a follow-up job (`SubmitFollowUp` /
      `DependsOn`) can consume the written-back property to derive and re-upload a
      color / vector-field property through the existing forward binding.
- [ ] Add readback-job diagnostics counters to the derived-job snapshot
      (readbacks issued/completed/failed/stale).

## Tests
- [ ] CPU contract `tests/contract/runtime/Test.GpuReadbackJob.cpp`
      (labels `contract;runtime`): readback→property write-back binds a matching
      range and fails closed on dimension mismatch; a readback job delivers its
      result through the apply phase against a mock transfer queue; a
      `SubmitFollowUp` chain ("readback → derive color/vector-field → re-upload")
      executes in dependency order; stale/cancel paths are diagnosed.
- [ ] Default CPU gate stays green; existing `DerivedJobRegistry` and
      `VertexAttributeBinding` tests remain green (no forward-path behavior change).

## Docs
- [ ] Update `src/runtime/README.md` (derived-job / transfer section) describing
      the readback job kind and the readback→property write-back binding.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Cross-link ADR-0023 and RUNTIME-112 / GRAPHICS-096 / GRAPHICS-098.

## Acceptance criteria
- [ ] An algorithm can submit a readback job whose GPU result is written into a
      CPU property (dimension-checked, fail-closed) without blocking a caller
      thread, and can chain follow-up derive+upload jobs on that result.
- [ ] No new scheduler/task-graph is introduced; the work composes
      `DerivedJobRegistry` + `StreamingExecutor` + GRAPHICS-096/098.
- [ ] Default-gate contract tests pass; the opt-in `gpu;vulkan` round-trip is
      cited as run for `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GpuReadbackJob|DerivedJob|VertexAttributeBinding' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
# Operational: cite a ci-vulkan gpu;vulkan readback round-trip run here.
```

## Forbidden changes
- Introducing a new scheduler/task-graph/executor instead of reusing
  `DerivedJobRegistry` / `StreamingExecutor` / `Core.Dag`.
- Blocking a caller thread on a GPU fence (readback rides the GRAPHICS-096 drain).
- Duplicating the forward property→channel binding (reuse RUNTIME-120/123/084).
- Adding Vulkan/RHI-specific knowledge to runtime beyond the graphics public API.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `RUNTIME-126`
  (this task's Slice B) via the cited `gpu;vulkan` readback round-trip.
