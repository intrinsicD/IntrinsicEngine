---
id: BUG-064
theme: G
depends_on: []
---
# BUG-064 — ci-vulkan FramePacingDiagnosticCapture cannot run headless

## Status
- Status: in progress.
- Owner: Codex.
- Branch: `codex/bug-064-software-vulkan`.
- Xvfb now provides a display, but hosted-run evidence shows the capture still
  cannot complete its first renderer frame because the runner has no
  operational Vulkan implementation. The current slice provisions and scopes
  Mesa lavapipe to this capture.

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
- [ ] Verify `ci-vulkan` completes with the test either
      passing or explicitly skipped.

## Tests
- [ ] `ci-vulkan` run evidence on a hosted runner: no
      "produced no samples" failure; skip (if chosen) is logged with the
      documented reason.

## Docs
- [x] Update the workflow comment/docs describing which environments
      execute versus skip the capture.

## Acceptance criteria
- [ ] Three consecutive `ci-vulkan` runs conclude without this test
      failing.

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

## Forbidden changes
- Deleting the frame-pacing capture test or loosening its assertions on
  display-capable hosts.
