---
id: BUG-248
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: one-file shader fix; the diff, the recorded before/revert/after GPU runs and the backtest retain evidence
contract_schema: 1
contracts: [geometry.property-coherence]
---
# BUG-248 — Keypoint GPU parity smokes fail on main

## Goal
- Restore CPU/GPU keypoint parity in the two failing `ci-vulkan` smokes, or record a reviewed
  tolerance decision, without weakening or removing their assertions.

## Context
- Observed on `ci-vulkan` at `599f02ff1` (the tip of `main`) plus the merged BUG-182 test fix
  (`2df2a2837`, which changes only the framed kNN case and one import), on an NVIDIA GeForce
  RTX 4090 with driver 580.159.04, 2026-10-24 01:57–02:02 CEST.
- The failures do not depend on the display: the same failure lines on `DISPLAY=:1` and on an
  owned Xephyr `:9` (`cohort-d1.log`, `cohort-d9.log`; they differ only in timings). In each
  run 19 of 21 selected cases passed.
- Line numbers below are at `2df2a2837`. With RUNTIME-291's three-line test addition merged,
  the resident lines are 3 higher (`:2833`, `:2836`, `:2852`, `:2858`).
- `PointLBVHGpuSmoke.KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor` fails at
  `Test.PointLBVHGpuSmoke.cpp:1981` 8 times (`KeypointCount` 5, reference 8) and at `:1989`
  8 times (`keypoints` mask differs from the reference). The log shows two groups, each of
  four `:1981` failures followed by four `:1989` failures from the per-domain mask loop: 4 of
  the 8 domains fail in 2 of the 3 `Phase<3` passes. The log names neither the domains nor
  the phases. `Config(d)` depends on the phase only through `Phase==2` (radius 0), so phases 0
  and 1 use the same configuration and are the likely failing pair. Which domains fail
  matters for diagnosis: with `NumericalEdges`, odd domains are scaled by 1e-4 and `d%4==0`
  domains are collinear.
- `PointLBVHGpuSmoke.KeypointResidentSubnormalScores` fails 12 times at `:2830` (preview mask)
  and 12 times at `:2849` (mask after accept). The saliency L∞ bound fails at `:2833` and
  `:2855`: 7.58e-42 against 1.12e-44 (8 × `denorm_min`).
- RUNTIME-291's note ([RUNTIME-291](../backlog/runtime/RUNTIME-291-scale-aware-radius-defaults.md))
  records both cases failing the same way with its `src/` change reverted, so they predate it.
- CI coverage: the cases are `gpu;vulkan`-labelled, so no CPU gate selects them. The hosted
  `ci-vulkan` workflow (pull requests, merge queue and manual dispatch; lavapipe, displayless batch)
  includes `IntrinsicPointLBVHGpuTests` in its aggregate; whether these cases skip there was
  not checked, and its last run was on 2026-09-23. The self-hosted nightly GPU job is opt-in.
- Raw logs (git-ignored): `tasks/evidence/BUG-248/cohort-d1.log` and `cohort-d9.log`, copied
  byte-for-byte from the GPU lane's `tasks/evidence/BUG-228/2026-10-24/`.

## Acceptance criteria
- [x] Reproduce both failures on `ci-vulkan` at the current integration head and name the root
      cause (GPU kernel, CPU reference or test expectation) with evidence.
- [x] Both cases pass on a Vulkan host with every existing assertion kept; any tolerance change
      is recorded with its reasoning and approved by the operator before it lands.
- [x] The other keypoint cases in the same selection still pass.

## Verification
```bash
nice -n 10 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor$' -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointResidentSubnormalScores$' -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke\.Keypoint' -L gpu -L vulkan --timeout 120
```

## Resolution — 2026-10-24
- Root cause is on the kernel side. HARDEN-090/GEOM-114 (`ffe7e8372`, 2026-10-16) changed
  `Geometry::PCADetail::Eigen` and its GLSL twin `pcaEigen`
  (`assets/shaders/include/pca_eigen_double.glsl`): both now divide by the largest absolute
  covariance coefficient before the isotropic rank test, so that test is scale-relative.
  `point_keypoints.comp` had its own Jacobi `eigenvalues()`, which kept the old absolute floor
  `scale=max(1,|a_ij|)`. On covariances scaled by 1e-4 that floor sent planar rows to the
  isotropic branch. Those rows got three equal mean eigenvalues, so their score was the mean.
  The CPU reference returns a plane for them: score 0, ties broken by lowest index. On the tiny
  fixture the same branch replaced the subnormal lambda3 with the mean. Unit-scale fixtures
  pass the rank test under both rules, and the hybrid path scores on the CPU, so only these two
  cases failed. GEOM-114 listed keypoints as affected, but its GPU run selected only normals
  cases.
- Backtest: a standalone harness (`tasks/evidence/BUG-248/probe/`, `backtest-output.txt`)
  replayed both fixtures under the old floor and under the normalized rule.
  - Planar fixture: masks differ only for d = 1, 3, 5, 7 (the 1e-4-scaled domains), in phases
    0 and 1, with 5 keypoints against 8. In phase 2 (automatic radius) the masks are equal
    under both rules. This is the observed 4 domains × 2 phases.
  - Tiny fixture: 12 mask mismatches and L∞ 7.5810246919972604e-42, bit for bit the observed
    failure.
- Change: `point_keypoints.comp` now includes `include/pca_eigen_double.glsl` and calls
  `pcaEigen` in `finishScore`. The private `eigenvalues()` is deleted (4 lines added,
  37 deleted).
  - Unchanged: the binary32 rounding of eigenvalues, the NaN/Inf `fail(4)`, the roundoff
    zeroing and the gamma tests.
  - `pcaEigen` returns descending eigenvalues on every path: the closed form sorts, the
    isotropic and zero paths return equal values, and `pcaStable` sorts too. So the deleted
    sort is not needed.
  - `docs/architecture/keypoint-analysis.md` now states the shared eigensolver's work bound and
    the scale-relative cutoff.
  - No CPU, test, tolerance or timeout change.
- Reuse decision: call the shared CPU twin rather than patch the private copy. jscpd
  (`--min-tokens 40`, the two shader files) found 2 clones (13 lines) between the keypoint
  Jacobi and `pcaStable` before the change and 0 after (`jscpd-before.log`,
  `jscpd-after.log`). The prepared
  fallback, wrapping the old Jacobi in a normalization like `PCADetail::Eigen`, was not needed.
- Verification ran on an RTX 4090 with the NVIDIA ICD and `DISPLAY=:1` in `build/ci-vulkan`.
  Logs are in `tasks/evidence/BUG-248/`.
  - Before, at `45efeaf7d`: `ctest ... -R '^PointLBVHGpuSmoke\.Keypoint'` passed 9 of 11
    (`baseline-keypoint-all.log`). Planar failures: `:1981` ×8 (`KeypointCount` 5 vs 8) and
    `:1989` ×8. Resident subnormal failures: `:2833` ×12 and `:2852` ×12, plus `:2836` and
    `:2858` with Delta 7.5810246919972604e-42 against the bound 1.1210387714598537e-44.
  - After: the incremental `IntrinsicPointLBVHGpuTests` build recompiles
    `point_keypoints.comp.spv` without errors (`build-fix.log`). glslc prints two `precise`
    warnings from the unchanged include. It prints the same two for `point_normals.comp`
    (`glslc-point-normals-warnings.log`). Both cases pass
    (`fix-two-cases.log`). All 11 `Keypoint*` cases report gtest `[  PASSED  ]`, with none
    skipped (`fix-keypoint-all.log`).
  - Revert check: with only the shader change reverted and rebuilt, both cases fail with the
    baseline signature again (`revert-two-cases.log`): `:1981`/`:1989`/`:2833`/`:2852`
    ×8/8/12/12, "Which is: 5" ×8, and Delta 7.5810246919972604e-42 ×2. With the change restored
    and rebuilt, all 11 cases pass (`restore-keypoint-all.log`).
  - Each of these exits 0 (`checks.log`):
    - `check_shader_outputs.py --require point_keypoints.comp.spv` (100 outputs)
    - `check_docs_sync.py --strict` on the three touched files (0 rules triggered)
    - `check_task_policy.py --strict`
    - `check_doc_links.py`
- Remains:
  - No regular gate selects these `gpu;vulkan` cases, and nobody has checked whether they skip
    on hosted lavapipe `ci-vulkan`. This slice does not change that.
  - The test name `...IsotropicFloor` still describes the pre-GEOM-114 rule. It is kept here;
    renaming it is for the operator to decide.
  - The GPU divides the covariance by the count, while the CPU multiplies by `1/count`. This
    is pre-existing and left alone: the accumulation order already differs.
- Code review: an independent Claude review ran on 2026-10-24 as a fresh read-only session
  limited to Read/Grep/Glob, so recursion protection was enforced. Session
  `594e180a-c5fb-42f8-8b03-2eab81b54903`. It reviewed commit `1852942c9` (diff sha256
  `c81a6fb3438e6e2205eeda312469a58c88f50cabfa82ce8bc384f4518192376b`).
  - Verdict, verbatim: "VERDICT: approve".
  - It also wrote, verbatim: "I judge acceptance criteria 1, 2 and 3 met."
  - Its optional findings on evidence claims are addressed in the follow-up commit: the jscpd
    before-log, the glslc log, the exit codes and the phase-2 wording.
  - Two findings were left alone, and the reviewer accepted both reasons:
    - The cohort logs belong to the earlier Context section and are outside this change.
    - `keypoint-analysis.md:43` stays because GLSL fp64 rounding is not guaranteed to match
      the CPU.
  - A delta review in the same session of `efb83a73e` (diff sha256
    `af8a4df3f9ed0e38b245a52afac8ae878beaf495c67b040b8d3893e110d15bc9`) gave, verbatim,
    "VERDICT: approve".
  - Packets and answers: `review-*.{md,json}` and `delta-*.{md,json}`.

## Completion — 2026-10-24
Completed 2026-10-24. Commits: `1852942c9` (the fix), `efb83a73e` and `7f122a51c` (evidence
and review records), on `45efeaf7d`, branch `claude/bug-248-keypoint-gpu-parity`, merged into
the night integration branch `claude/night-2026-10-24-bug-sweep` as `6cec3dcb0`. Not yet on
`main`; hosted CI has not run on these commits.

Root cause: `ffe7e8372` (HARDEN-090/GEOM-114) made the CPU PCA isotropic cutoff
scale-relative; the keypoint kernel kept a private Jacobi with the old absolute floor. Fix:
`point_keypoints.comp` calls the shared `pcaEigen`; no test, tolerance or timeout changed.

Verification, as recorded in the Resolution above: `ci-vulkan` on an RTX 4090 with NVIDIA
driver 580.159.04 and `DISPLAY=:1`. `PointLBVHGpuSmoke.Keypoint*` passed 9 of 11 at
`45efeaf7d` and 11 of 11 after the fix; reverting only the shader restored both failures with
the baseline signature; restoring it gave 11 of 11 again.

Reviewer decisions (coordinator's local review record `wfE.json`, lane `BUG-248`, outside the
repository), round 1 on `7f122a51cef9f2853c835277f3c1acc1cb6ed004`
(`45efeaf7d..7f122a51c`):
- Independent Claude review `approve`, no blocking findings; criteria 1, 2 and 3 "met". It
  reran the two-case `ctest` on the existing `build/ci-vulkan` tree: 2/2 passed, none skipped.
- Independent Codex review (thread `01a16bdc-9b47-78e3-88cd-38221dbcce20`) `approve`, no
  blocking findings. Its table rates criterion 1 "Met — reproduce and identify the cause",
  2 "Met — both cases pass without weakening assertions" and 3 "Met — remaining keypoint cases
  pass". It ran no builds or GPU tests.

Boxes: all three are ticked, as both round-1 reviews decided.

Limits:
- No regular gate selects these `gpu;vulkan` cases; whether they skip on hosted lavapipe
  `ci-vulkan` is unchecked, and so is parity on other drivers.
- The shared include `pca_eigen_double.glsl` puts `precise` on input parameters (`:38`,
  `:77`), where glslc says it has no effect, so FP contraction inside the Jacobi fallback is not
  ruled out. The keypoint kernel now inherits this from `point_normals.comp` (optional finding
  of the Claude review; no owning task).
- The duplication check covered the two shader files, not the whole repository.

Open for the operator: whether these `gpu;vulkan` keypoint cases get gate coverage (or a
hosted lavapipe check), and whether
`KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor` is renamed, since its name describes
the pre-GEOM-114 rule.

Evidence: the cohort logs cited in Context are git-ignored in the integration worktree
(`tasks/evidence/BUG-248/cohort-d1.log`, `cohort-d9.log`; `cmp` at retirement found them
byte-identical to the GPU lane's `tasks/evidence/BUG-228/2026-10-24/` copies). The Resolution's
logs, probe and review packets are git-ignored in the `gpu` lane worktree
(`tasks/evidence/BUG-248/`). Nothing was deleted.
