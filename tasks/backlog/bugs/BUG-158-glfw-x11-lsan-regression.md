---
id: BUG-158
theme: G
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
contract_review: "This backlog record captures a regression in an existing platform sanitizer contract and does not yet change a reusable engine contract. A claimed diagnosis must reassess catalog applicability if it changes platform lifetime, sanitizer policy, or test-gate behavior."
---
# BUG-158 — GLFW/X11 LeakSanitizer contract regressed after BUG-082

## Goal

- Restore the dedicated GLFW/X11 engine-static lifetime process to a
  repeatably leak-clean exit under the isolated `ci-asan` gate, or establish a
  new upstream/environment ownership diagnosis with a fail-closed remedy.

## Non-goals

- No broad X11, GLFW, or LeakSanitizer suppression.
- No weakening, exclusion, or quarantine of the dedicated regression test.
- No coupling to the unrelated BUG-156 curvature implementation that exposed
  this failure during repository-wide verification.

## Context

- Symptom: `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` reports one
  direct 408-byte leak allocated by `libX11.so.6`, after
  `BUG082_GLFW_STATIC_TEARDOWN: terminate_calls=1` proves the engine-static
  teardown ran.
- Expected behavior: the clean control exits zero while the named synthetic
  4,096-byte engine leak remains detectable with LeakSanitizer exit code 86,
  as established when BUG-082 retired.
- Impact: on 2026-08-12 the exact `ci-asan` CPU selector passed all other 2,743
  selected cases but failed this platform regression. Three immediate focused
  reruns reproduced the same 408-byte `libX11.so.6` allocation, so this is not
  recorded as a one-off test fluctuation.
- The same test passed in the checked-in BUG-154 ASan receipt on the prior
  source surface. The failing build uses Clang 23.0.0, GLFW 3.4, and the host
  `libX11.so.6` build ID `37a5d7bbb78e3a99ea8376a7c80ea0c62fe06494`.

## Required changes

- [ ] Reproduce from a clean isolated ASan tree and determine what changed
      since the last green BUG-154/BUG-082 receipts: source, linked dependency,
      X11 environment, or process teardown ordering.
- [ ] Prove ownership of the retained allocation and repair the responsible
      lifetime without hiding the synthetic negative control.

## Tests

- [ ] Make the existing dedicated regression pass repeatedly on a live X11
      host while its synthetic leak control still fails with the expected
      LeakSanitizer report.
- [ ] Run the complete isolated ASan CPU selector after the focused repair.

## Docs

- [ ] Update BUG-082's retired diagnosis or platform testing documentation if
      the new evidence changes its ownership or environment conclusions.

## Acceptance criteria

- [ ] The live-X11 clean process exits zero for at least ten consecutive runs.
- [ ] The unsuppressed synthetic engine leak remains detectable.
- [ ] No sanitizer gate, label, timeout, or suppression is weakened.

## Verification

```bash
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure \
  -R '^GlfwLifecycleLsan\.EngineStaticTeardownAndLeakControl$' --timeout 60
ctest --test-dir build/ci-asan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error \
  --timeout 60 --parallel 1
```

## Forbidden changes

- Shipping a suppression for the 408-byte X11 allocation without proving
  upstream ownership and preserving the synthetic engine-leak control.
- Treating `terminate_calls=1` alone as proof of leak-clean shutdown.

## Maturity

- Target: restore the retired BUG-082 `Operational` sanitizer contract; no new
  platform feature surface is intended.
