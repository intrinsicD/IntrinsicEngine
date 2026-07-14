---
id: BUG-064
theme: G
depends_on: []
---
# BUG-064 — ci-vulkan FramePacingDiagnosticCapture cannot run headless

## Status
- Completed 2026-07-13 at `Operational` on branch
  `codex/bug-064-software-vulkan`.
- Commit: implementation `c2f0efd7` and `7e735868`; retirement evidence:
  this local completion commit.
- Three sequential hosted `ci-vulkan` runs at exact head
  `7e73586825b244cc147f0f42d0ef07d5fd9979c1` passed the strict capture under
  Xvfb and Mesa lavapipe, along with the broader gate and timing-artifact
  validation.

## Goal
- Make the `ci-vulkan` workflow's `ExtrinsicSandbox.FramePacingDiagnosticCapture`
  either runnable (virtual display) or honestly skipped on runners without
  a display, so `ci-vulkan` stops being permanently red.

## Non-goals
- No weakening of the validator's sample/interface checks on
  display-capable hosts (BUG-056 hardened these deliberately).

## Context
- Symptom: `ci-vulkan` has concluded `failure` on every recent run across
  all branches (runs 316–323, 2026-07-07..08, including markdown-only
  PRs). On PR #1010 head `e732e69` the only failing test is
  `ExtrinsicSandbox.FramePacingDiagnosticCapture`: GLFW reports
  `X11: The DISPLAY environment variable is missing`, the window
  initializes closed, `Engine::Run()` executes zero frames, and
  `tools/diagnostics/validate_frame_pacing_capture.cmake` fails with
  "frame-pacing capture produced no samples".
- Expected behavior: on a GitHub runner (X11 build headers present →
  GLFW backend selected and the test registered, but no display at
  runtime) the test either runs under a virtual display (`xvfb-run`,
  plus lavapipe if GPU smokes should execute) or self-skips with a
  documented environment-capability skip like the GPU smoke fixtures.
- Impact: `ci-vulkan` is unconditionally red on hosted runners; its
  signal value is currently zero.
- Chosen remedy: keep the existing validator strict and run only
  `ExtrinsicSandbox.FramePacingDiagnosticCapture` under an isolated Xvfb server
  with Mesa's lavapipe software Vulkan implementation in `ci-vulkan.yml`. The
  remaining `gpu;vulkan` batch keeps its current capability-skip behavior; this
  task does not implicitly promote every smoke onto a hosted software-Vulkan
  path.

## Required changes
- [x] Decide the remedy: (a) run the capture under `xvfb-run` in
      `ci-vulkan.yml`, or (b) add a documented environment skip to the
      validator/test registration when no display is available at
      runtime.
- [x] Implement the display and software-Vulkan provisioning in the workflow.
- [x] Verify `ci-vulkan` completes with the capture executing and passing.

## Tests
- [x] Hosted `ci-vulkan` evidence executes the capture on software Vulkan; all
      three retained runs pass without a "produced no samples" failure.

## Docs
- [x] Update the workflow comment/docs describing which environments
      execute versus skip the capture.

## Acceptance criteria
- [x] Three consecutive, non-overlapping `ci-vulkan` runs conclude without
      this test failing.

## Verification
```bash
grep -n "xvfb\|mesa-vulkan\|VK_DRIVER_FILES" .github/workflows/ci-vulkan.yml
python3 tools/ci/check_workflow_names.py --root .github/workflows
xvfb-run -a --server-args="-screen 0 1280x720x24" \
  ctest --test-dir build/ci-vulkan --output-on-failure \
    -R '^ExtrinsicSandbox\.FramePacingDiagnosticCapture$' \
    -L gpu -L vulkan --timeout 180
# Hosted CI evidence: ci-vulkan concludes green.
```

Closure verification on 2026-07-13:

- All three runs used GitHub-hosted `ubuntu-24.04`, installed
  `mesa-vulkan-drivers` 25.2.8-0ubuntu0.24.04.2, and scoped
  `LIBGL_ALWAYS_SOFTWARE=1` plus
  `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json` to the isolated Xvfb
  capture step. The capture therefore exercised an operational software
  Vulkan renderer instead of satisfying the gate through a capability skip.
- The retained runs were sequential and used the same exact head:

  | Run | UTC interval | Broader `gpu;vulkan` batch | Strict capture |
  | --- | --- | --- | --- |
  | [29277091536](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29277091536) | 19:05:16–19:23:46 | 59/59 CTest entries green (1.00 s) | 1/1 passed (5.74 s) |
  | [29278614647](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29278614647) | 19:28:55–19:42:28 | 59/59 CTest entries green (0.62 s) | 1/1 passed (3.19 s) |
  | [29280699135](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29280699135) | 20:00:53–20:18:06 | 59/59 CTest entries green (0.86 s) | 1/1 passed (5.28 s) |

- In every run, configure/build, the broader batch, strict frame-pacing
  capture, timing aggregation, strict timing-result validation, and artifact
  upload completed successfully. Capability-limited fixtures in the broader
  batch retained their documented `GTEST_SKIP` behavior; only the strict
  capture was forced onto lavapipe.
- Workflow naming and concurrency regressions, strict task validation/policy/
  state links, generated session-brief freshness, documentation sync/links,
  PR-contract mapping, and whitespace checks pass. No C++ source or test
  assertions changed in this slice; the hosted `ci-vulkan` builds are the
  relevant compile/backend gates.

## Completion

- Completed: 2026-07-13. Maturity: `Operational`.
- Outcome: hosted `ci-vulkan` now supplies the strict frame-pacing diagnostic
  with an isolated display and operational software Vulkan implementation,
  while the remaining GPU fixtures keep their existing capability semantics.
- No follow-up is owed for BUG-064; physical-device GPU coverage remains the
  responsibility of the existing opt-in GPU/Vulkan test policy.

## Forbidden changes
- Deleting the frame-pacing capture test or loosening its assertions on
  display-capable hosts.

## Maturity

- Closed at `Operational`: the `gpu;vulkan;integration` capture executed its
  real runtime/renderer path on Mesa lavapipe in three hosted runs and did not
  skip. No `Operational` follow-up is owed.
