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
  parameterization, or method-integration surface changes.
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
- `ExecuteMaterialBaseSync` and `ExecuteMaterialOverrideSync` surround
  `VisualizationSync`, which can call `matSys.SetParams`. The second sync is
  not universally redundant. Preserve it unless all intervening writes are
  accounted for or GRAPHICS-105's override-material removal makes it redundant.
- Operator decision (2026-09-05): retain a bounded measurement-first task;
  negative A/B evidence may close it without deleting an execution path.
- Owner: `graphics/renderer` render-prep pipeline.
- This task must start with measurement, not with the fix. If the A/B shows the
  task graph is not the cost, the finding is a profile, not a rewrite.

## Required changes
- [ ] Run the controlled A/B: capture `--frame-pacing-report` with
      `UseTaskGraph` true and false on the same build and scene; record both in
      this task.
- [ ] Attribute current cost among callback registration/rebinding, retained
      plan execution, scheduler dispatch, and individual step/material work.
- [ ] If A/B confirms removable overhead, optimize the measured stage while
      keeping step ordering and results identical. Reusing the already-retained
      compiled plan is not a new implementation result.
- [ ] Record the writes between both material syncs. Remove a sync only with
      proof that no current-frame material update is lost; otherwise retain
      both with the reason documented.
- [ ] If `ExecuteSequential` becomes the production path, remove the dead
      alternative rather than leaving two paths.

## Tests
- [ ] Assert render-prep executes the same ordered step sequence before and
      after (the existing `RenderPrepStep`/`ExecutedSteps` record is the natural
      oracle).
- [ ] Assert material GPU sync is invoked the expected number of times per
      frame and that changes from visualization between syncs reach the same
      frame. A call-count reduction alone is not a correctness oracle.
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
