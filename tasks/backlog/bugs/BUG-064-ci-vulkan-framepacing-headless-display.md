---
id: BUG-064
theme: G
depends_on: []
---
# BUG-064 — ci-vulkan FramePacingDiagnosticCapture cannot run headless

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

## Required changes
- [ ] Decide the remedy: (a) run the capture under `xvfb-run` in
      `ci-vulkan.yml`, or (b) add a documented environment skip to the
      validator/test registration when no display is available at
      runtime.
- [ ] Implement it and verify `ci-vulkan` completes with the test either
      passing or explicitly skipped.

## Tests
- [ ] `ci-vulkan` run evidence on a hosted runner: no
      "produced no samples" failure; skip (if chosen) is logged with the
      documented reason.

## Docs
- [ ] Update the workflow comment/docs describing which environments
      execute versus skip the capture.

## Acceptance criteria
- [ ] Three consecutive `ci-vulkan` runs conclude without this test
      failing.

## Verification
```bash
grep -n "xvfb\|DISPLAY" .github/workflows/ci-vulkan.yml
# CI evidence: ci-vulkan concludes green or with documented skips.
```

## Forbidden changes
- Deleting the frame-pacing capture test or loosening its assertions on
  display-capable hosts.
