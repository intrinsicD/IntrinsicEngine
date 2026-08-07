---
id: GRAPHICS-135
theme: B
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
  Render-prep scheduling structure and redundant GPU buffer sync. No geometry
  element-domain source, geometry property semantics, support-radius,
  parameterization, or method-integration surface changes.
---
# GRAPHICS-135 — RenderPrepFrame dominates frame time with a per-frame task-graph rebuild

## Goal
- Establish, by measurement, what `RenderPrepFrame` actually spends its time on
  with a near-empty scene, and remove the cost if it is the per-frame task-graph
  rebuild.

## Non-goals
- No renderer architecture rewrite.
- No removal of `Core::Dag::TaskGraph` from other consumers.
- No change to render-prep step ordering or its observable results.

## Context
- Measurement: a 30-frame `--frame-pacing-report` capture with **only the
  reference triangle** in the scene shows `render_prepare_micros` flat at
  ~69 ms **every frame** — not a cold-start cost:

  | frame | total µs | render_prepare µs | render_execute µs |
  | --- | --- | --- | --- |
  | 2 | 76 990 | 69 702 | 3 490 |
  | 10 | 75 841 | 69 567 | 2 610 |
  | 29 | 74 543 | 69 154 | 2 545 |

  That is ~92% of frame time and ~13 FPS on an RTX 4090 with one triangle.
- A second capture on a different display server (Xephyr, software present)
  measured the same ~67 ms of prepare, confirming the cost is CPU-side and
  independent of presentation.
- Absolute values are inflated by the `ci-vulkan` Debug + ASan/UBSan build, so
  they are **not** a performance claim. The build-independent facts are: prepare
  is pure CPU, constant per frame, and independent of scene contents.
- Suspected mechanism, **not yet confirmed by A/B**:
  `Graphics.RenderPrepPipeline.cpp:263+` — `Run()` rebuilds a
  `Core::Dag::TaskGraph` of **nine passes every frame**, and every pass is
  declared `MainThreadOnly = true, AllowParallel = false` with hard `Read`/
  `Write` tag dependencies, i.e. a strictly sequential chain. The graph
  therefore buys nothing over calling the nine step functions in order — which
  is exactly what the existing `ExecuteSequential()` already does.
  `UseTaskGraph` defaults to `true`
  (`Graphics.RenderPrepPipeline.cppm:86`) and no production caller overrides it,
  so `ExecuteSequential` is dead outside tests.
- Second, independent redundancy: `ExecuteMaterialBaseSync` and
  `ExecuteMaterialOverrideSync`
  (`Graphics.RenderPrepPipeline.cpp:83-107`) make the identical
  `inputs.Materials->SyncGpuBuffer()` call. The second always finds a clean
  dirty set, so it is a full scan of the material slot array for nothing.
- Owner: `graphics/renderer` render-prep pipeline.
- This task must start with measurement, not with the fix. If the A/B shows the
  task graph is not the cost, the finding is a profile, not a rewrite.

## Required changes
- [ ] Run the controlled A/B: capture `--frame-pacing-report` with
      `UseTaskGraph` true and false on the same build and scene; record both in
      this task.
- [ ] If the graph is the cost, remove the per-frame rebuild — either reuse a
      compiled graph across frames or use the sequential path — while keeping
      step ordering and results identical.
- [ ] Remove or justify the duplicated `Materials->SyncGpuBuffer()` call.
- [ ] If `ExecuteSequential` becomes the production path, remove the dead
      alternative rather than leaving two paths.

## Tests
- [ ] Assert render-prep executes the same ordered step sequence before and
      after (the existing `RenderPrepStep`/`ExecutedSteps` record is the natural
      oracle).
- [ ] Assert material GPU sync is invoked the expected number of times per
      frame.
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
- [ ] No second material sync remains without a stated reason.
- [ ] No two live render-prep execution paths remain.

## Verification
```bash
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
