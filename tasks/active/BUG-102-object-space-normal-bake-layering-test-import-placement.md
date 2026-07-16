---
id: BUG-102
theme: G
depends_on:
  - RUNTIME-178
maturity_target: CPUContracted
---
# BUG-102 — Object-space bake layering test asserts pre-ratchet import placement

## Status

- Implementation and focused verification completed on 2026-07-16; owner:
  Codex; branch: `agent/sandbox-model-workflow-completion`; awaiting commit and
  task retirement by the owning workflow.
- Before the correction, the exact test failed identically in `build/ci` and
  `build/ci-vulkan`; the tested import-placement and assertion lines matched
  `origin/main`.
- After the correction, the exact test passed 3/3 in each preset and the full
  `RuntimeEngineLayering` selection passed 21/21.

## Goal
- Restore the object-space normal-bake source-layering contract by recognizing
  the current `RUNTIME-178` placement of the public CPU request-queue import
  without weakening the ban on GPU queue composition in `Engine`.

## Non-goals
- No object-space normal bake, asset residency, Engine lifecycle, GPU queue, or
  test-label behavior change.
- No change to the ratcheted `Runtime.Engine.cppm` convergence budget.
- No broad rewrite of source-reading layering contracts.

## Context
- Symptom: `RuntimeEngineLayering.ObjectSpaceNormalBakeServiceKeepsGpuQueueCompositionOutOfEngine`
  fails because it expects `Extrinsic.Runtime.ObjectSpaceNormalBakeQueue` to be
  imported directly by `Runtime.Engine.cppm` and absent from
  `Runtime.Engine.cpp`; current source has the inverse placement.
- Expected behavior: `Runtime.Engine.cppm` imports
  `Extrinsic.Runtime.ObjectSpaceNormalBakeService`, whose public service
  contract re-exports the CPU request queue. `Runtime.Engine.cpp` directly
  imports that queue because its sole-owner private asset-residency glue names
  `RuntimeObjectSpaceNormalBakeQueue`; GPU queue ownership and composition stay
  private to `ObjectSpaceNormalBakeService`.
- Impact: the exact source-layering test is red in both configured presets. Its
  containing integration test is labeled `gpu;vulkan;slow`, so the default CPU
  gate excludes this deterministic source-only regression.
- Pre-fix origin evidence: `git diff --exit-code
  -G'ObjectSpaceNormalBake(Queue|Service)' origin/main HEAD --` for
  `Runtime.Engine.cpp`, `Runtime.Engine.cppm`, and the failing test is clean;
  the branch-only Engine delta is unrelated BUG-100 shutdown code. Commit
  `109af4bd` (`RUNTIME-178`) moved the direct queue import from the interface to
  the implementation while ratcheting the interface to 42 plain imports, but
  did not update these two assertions.
- Stale-build triage: a ccache-disabled focused target rebuild completed and
  the exact failure remained unchanged; because the test reads source files at
  runtime, BMI state cannot explain the two observed string mismatches.

## Required changes
- [x] Update only the two stale CPU request-queue import-placement assertions
      to match the `RUNTIME-178` ownership shape.
- [x] Preserve every positive service-delegation check and every negative GPU
      queue ownership/composition check.

## Tests
- [x] Reproduce the exact failing test in both `ci` and `ci-vulkan` before the
      fix and pass it afterward.
- [x] Run the complete `RuntimeEngineLayering` selection after the correction.
- [x] Run the strict kernel-convergence and source-layering checks.

## Docs
- [x] Record the diagnosis and verification in this task; no production
      architecture documentation changes because runtime ownership and behavior
      do not change.

## Acceptance criteria
- [x] The exact test passes in both configured presets without production code
      changes or a relaxed GPU queue ownership assertion.
- [x] The Engine interface remains at the current convergence ratchet and
      strict task/layering/diff checks pass.

## Evidence

- Root cause: `RUNTIME-178` deliberately moved the public CPU queue import from
  `Runtime.Engine.cppm` to `Runtime.Engine.cpp` when the asset-residency private
  declaration became implementation-only. The test retained the pre-ratchet
  placement assertions. This was test drift, not a runtime ownership defect.
- Ruled out: BUG-100 branch changes (the tested import/assertion lines matched
  `origin/main`), stale build state (ccache-disabled target build plus identical
  post-build failure), and missing service delegation (all surrounding
  positive/negative checks already passed).
- The patch changes only the two CPU queue placement predicates and adds a
  concise rationale. Assertions excluding
  `RuntimeObjectSpaceNormalBakeGpuQueue`, its dependencies, participant-desc
  construction, and ready-frame policy from Engine remain unchanged.
- Strict kernel convergence reports 42 plain imports / 21 domain imports / two
  re-exports / 31 public getter names with no debt. Strict layering, task
  policy, task validation, test layout, debug-probe cleanup, and diff checks
  passed.

## Verification
```bash
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests -j 2
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeEngineLayering\.ObjectSpaceNormalBakeServiceKeepsGpuQueueCompositionOutOfEngine$' \
  --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^RuntimeEngineLayering\.ObjectSpaceNormalBakeServiceKeepsGpuQueueCompositionOutOfEngine$' \
  --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeEngineLayering\.' --timeout 120
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
git diff --check
```

## Forbidden changes
- Changing production import placement solely to satisfy stale test text.
- Restoring a direct queue import to `Runtime.Engine.cppm` and regressing the
  kernel-convergence budget.
- Relabeling, skipping, or weakening the failing test's GPU queue assertions.
- Folding unrelated runtime or graphics changes into this bug fix.

## Maturity
- Target: `CPUContracted`; this is a backend-neutral source-ownership contract,
  and no `Operational` follow-up is owed.
