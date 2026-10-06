---
id: GRAPHICS-135
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Render-prep scheduling measurement and material-buffer synchronization. No geometry
  element-domain source, geometry property semantics, support-radius,
  parameterization, or method-integration surface changes. A fix that caches visualization
  encodes keyed on property revisions (H3) must add `geometry.property-coherence` first.
---
# GRAPHICS-135 — Measure current render-prep scheduling and material-sync overhead

## Goal
- Establish, by measurement, what `RenderPrepFrame` actually spends its time on
  with a near-empty scene on the current revision, and remove only overhead
  established by controlled A/B evidence. A disproven hypothesis may close
  with a profile and no production rewrite.

## Non-goals
- No renderer architecture rewrite.
- No removal of `Core::Dag::TaskGraph` from other consumers.
- No change to render-prep step ordering or its observable results.

## Context
- Historical measurement (2026-08-07): a 30-frame `--frame-pacing-report` capture with **only the
  reference triangle** in the scene shows `render_prepare_micros` flat at
  ~69 ms **every frame** — not a cold-start cost:

  | frame | total µs | render_prepare µs | render_execute µs |
  | --- | --- | --- | --- |
  | 2 | 76 990 | 69 702 | 3 490 |
  | 10 | 75 841 | 69 567 | 2 610 |
  | 29 | 74 543 | 69 154 | 2 545 |

  That is ~92% of frame time and ~13 FPS on an RTX 4090 with one triangle.
- A second historical capture on another display server (Xephyr, software
  present) measured ~67 ms of prepare. This motivates CPU-side attribution;
  it does not by itself prove the cause or independence from presentation.
- The historical `ci-vulkan` Debug + ASan/UBSan timings are diagnostic only;
  they do not establish today's cost, its cause, or build/scene independence.
- `CORE-008` already introduced compiled-plan reuse. The current
  `Graphics.RenderPrepPipeline.cpp` retains `m_Impl->Graph`, rebinds callbacks,
  and resets for replay. Measure registration/rebinding, plan reuse,
  scheduler execution, and step bodies separately; do not assume fresh graph
  compilation every frame or that sequential execution is necessarily better.
- Material sync is now one step: `ExecuteMaterialSync` (`RenderPrepStep::MaterialSync`,
  `Graphics.RenderPrepPipeline.cpp`) runs once, after `VisualizationSync`, which can call
  `matSys.SetParams`/`Patch`. The former base/override pair was merged (re-verified at
  `665c693dd`); the 2026-10-01 audit's mention of two functions was stale. Keep it a single
  step unless the hypotheses below show the writes it covers are the cost.
- Operator decision (2026-09-05): retain a bounded measurement-first task;
  negative A/B evidence may close it without deleting an execution path.
- Owner: `graphics/renderer` render-prep pipeline.
- This task must start with measurement, not with the fix. If the A/B shows the
  task graph is not the cost, the finding is a profile, not a rewrite.

## Hypotheses to measure first (2026-10-01 duplication/consistency audit)
Source: the audit read the code at `78480fe3b` and re-verified at `665c693dd`. Nothing here is
measured. Each is an inferred cost, not a result, and none is a performance claim under
AGENTS.md §8/8b until a matched Release A/B exists. Measure in this order, before the
TaskGraph A/B, because queue drains would otherwise mask the graph cost.

- [ ] **H1 — synchronous queue drains inside render_prepare (highest suspicion).**
      Evidence: `VulkanDevice::WriteBuffer` on a non-host-visible buffer takes the staging slow
      path (`Backends.Vulkan.Device.cpp` ~4546-4593): create staging buffer, one-shot copy,
      `EndOneShot` (`:5796`, `vkQueueWaitIdle` at `:5827`), destroy. Per-frame callers:
      `Graphics.CullingSystem.cpp:610` writes the whole `GpuCullBucketTable` into a device-local
      buffer (`HostVisible=false`, created ~`:346`) every frame although its content changes
      only on resize; `Graphics.MaterialSystem.cpp:693` writes once per dirty range (`:219`,
      `:261` are the allocation-time writes). Each call drains every in-flight frame.
      The comment at `Device.cpp:4546` ("only used for scene loading and rare writes") is stale.
      Measure: count `EndOneShot` calls and wall time per frame during `RenderPrepFrame`
      (scoped timer or counter around `WriteBuffer`), then A/B with the bucket-table write
      skipped when unchanged. Confirms if the per-frame drain count is nonzero and removing it
      moves `render_prepare_micros` on the reference scene.
- [ ] **H2 — material slots re-dirtied every frame.**
      Evidence: `MaterialSystem::SetParams` (`Graphics.MaterialSystem.cpp:461-476`) sets
      `Dirty`/`DirtySet` and repacks the slot with no comparison against the existing slot.
      Per-frame callers: `Runtime.RenderExtraction.cpp` `ResolveMaterialTextureBindingRecord`
      (`:910`, called per bound entity at `:1459`) -> `ResolveTextureAssetBindings` ->
      `SetParams`; `Graphics.VisualizationSyncSystem.cpp:510` `Patch` for every `TintOverride`
      entity; `StripUnbindableBakeTextures` (`:970`, called at `:1453`) recomputes bake
      revision tokens by name-keyed lookup per entity. If H2 holds, H1 fires every frame.
      Measure: count dirty ranges flushed by `SyncGpuBuffer` per frame on a static scene
      (expected 0; any nonzero value is the finding), and count `SetParams` calls whose packed
      `GpuMaterialSlot` is byte-equal to the previous one.
- [ ] **H3 — scalar/color visualization re-encoded on the CPU every frame.**
      Evidence: `AppendVisualizationRecipe` (`Runtime.RenderExtraction.Recipes.cpp:164`) ->
      `EncodeVisualizationRecipe` (`Runtime.VisualizationRecipes.cpp:963`), reached from
      `Runtime.RenderExtraction.cpp` (~`:1236`, `:2116`, `:2189`, `:2214`). Per visualized
      entity per frame: full property byte copy (`CopyBytes`, `:170-180`), an N-sized `finite`
      vector with `minmax_element` and two `nth_element` passes for auto-range (`:236-300`),
      and an allocating surface-remap gather (`Recipes.cpp`). The GPU side skips unchanged
      buffers; the CPU encode is not gated.
      Measure: time `EncodeVisualizationRecipe` on a 1M-vertex mesh with an auto-range scalar
      across 120 static frames; confirms if the cost is flat per frame while property,
      config and remap revisions are unchanged.

Also observed by the audit, lower priority, same rules (measure before acting):
- [ ] H4: extraction pushes a `TransformSyncRecord` and calls `SetEntityConfig` for every
      renderable every frame; `GpuWorld` setters (`Graphics.GpuWorld.cpp` ~2061-2126) mark dirty
      without comparison, so `SyncFrame` re-uploads instance/entity-config/bounds buffers each
      frame, and an exhausted staging belt falls back to the H1 synchronous path.
      Measured alternative only (not a default): `assets/shaders/scene_update.comp @ 087e6e17b`
      (deleted by REVIEW-007 G01) scattered sparse slot updates on the GPU; compare it
      against change-gated CPU uploads only after H4 is measured.
- [ ] H5: `VulkanDevice::ReadBuffer` calls `vkDeviceWaitIdle` even for host-visible buffers;
      production callers are the pick drain (three reads per pick) and the histogram drain
      (every frame only while `EnableHistogram`, default off).
- [ ] H6: the `alreadyEncoded` lambda in `Runtime.RenderExtraction.cpp` (~`:2141`) is
      `any_of` over the frame-global batch with string-key compares per entity per lane
      (O(entities^2)), and `BuildVisualizationPropertySourceKey` builds up to 8 `to_string`
      keys per entity per frame.

A missing prerequisite for claim-grade evidence: `benchmarks/rendering` has only framegraph and
recipe-compile smokes. Add an extraction/render-prep benchmark (1M-vertex mesh with an
auto-range scalar; 1k instances) following the benchmark skill before any improvement is
claimed; the UI-030 report was an ASan, non-operational capture.

## Required changes
- [ ] Run the controlled A/B: capture `--frame-pacing-report` with
      `UseTaskGraph` true and false on the same build and scene; record both in
      this task.
- [ ] Attribute current cost among callback registration/rebinding, retained
      plan execution, scheduler dispatch, and individual step/material work.
- [ ] If A/B confirms removable overhead, optimize the measured stage while
      keeping step ordering and results identical. Reusing the already-retained
      compiled plan is not a new implementation result.
- [ ] Record which writers mark material slots dirty each frame (hypothesis H2) and
      what `SyncGpuBuffer` uploads as a result. Change the sync only with proof that
      no current-frame material update is lost.
- [ ] If `ExecuteSequential` becomes the production path, remove the dead
      alternative rather than leaving two paths.

## Tests
- [ ] Assert render-prep executes the same ordered step sequence before and
      after (the existing `RenderPrepStep`/`ExecutedSteps` record is the natural
      oracle).
- [ ] Assert the material GPU sync runs once per frame and that changes from
      visualization reach the same frame. A call-count reduction alone is not a
      correctness oracle.
- [ ] Default CPU gate stays green.
- [ ] Opt-in `gpu;vulkan` gate stays green.

## Docs
- [ ] Record the measured before/after in the task and, if the change is
      material, in the benchmarking evidence docs.
- [ ] Update the render-prep description in the graphics architecture docs if
      the execution path changes.

## Acceptance criteria
- [ ] The A/B measurement is recorded, with the conclusion stated either way.
- [ ] If the graph was the cost, `render_prepare_micros` on the reference scene
      drops materially and the step sequence is unchanged.
- [ ] Each hypothesis below is recorded as confirmed, refuted or not-measurable, with the
      measurement used; none is claimed as an improvement without a matched A/B.
- [ ] If production execution switches to the sequential path, remove the
      superseded path. If the cost hypothesis is disproven, retain existing
      paths and close with the recorded profile; no forced deletion is owed.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'RenderPrep|Renderer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox
build/ci-vulkan/bin/ExtrinsicSandbox \
  --frame-pacing-report /tmp/intrinsic-renderprep-before.json \
  --frame-pacing-frames 120
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R 'RenderPrep|Renderer' --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Claiming a speedup from the Debug+ASan numbers above; any performance claim
  needs an unsanitized Release measurement.
- Changing render-prep step ordering or results as part of a performance change.

## Maturity
- Target: `Operational` — the change is only proven by a live frame-pacing
  capture on the promoted Vulkan path, not by contract tests alone.
- Negative closure is evidence-only and claims no new capability or speedup.
