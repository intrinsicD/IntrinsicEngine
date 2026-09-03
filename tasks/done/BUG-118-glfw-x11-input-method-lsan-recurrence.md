---
id: BUG-118
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-03T11:06:37+02:00"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation]
contract_review: "The repair changes only the sanitizer proof boundary for an upstream libX11 1.7.5 allocation already fixed by xorg/libX11 commit 1d118226. It admits one exact _XimRegisterIMInstantiateCallback suppression under an exact-file guard and retains the independent 4096-byte engine leak control. It does not change GLFW lifetime, platform behavior, Vulkan ownership, backend selection, or broad sanitizer policy."
maturity_target: Operational
---
# BUG-118 — GLFW X11 input-method LeakSanitizer recurrence

## Status

- Completed and staged for retirement on 2026-09-03 at `Operational`.
  Implementation commit: `da893465`. Final high-risk acceptance is bound by
  the completion report and its terminal independent review.
- The exact recurrence blocked the first BUG-154 retirement cohort at 53/54.
  The standalone process reproduced deterministically after proving
  `glfwTerminate()` ran exactly once.
- The host uses libX11 `1.7.5-1ubuntu0.3` (`libX11.so.6.4.0`, build ID
  `37a5d7bb...`) with `XMODIFIERS=@im=ibus`. The 408-byte stack is the upstream
  `_XimRegisterIMInstantiateCallback` temporary-XIM leak fixed by xorg/libX11
  commit `1d118226`; changing the input-method environment does not repair the
  affected library.
- One exact function suppression removes only that upstream row. The same
  runner still requires the named 4,096-byte synthetic engine allocation to
  exit with LeakSanitizer code 86, so the remedy does not disable leak
  detection or admit a broad X11/GLFW suppression.

## Goal
- Explain why the standalone GLFW lifetime contract again retains the
  408-byte X11 input-method allocation after proving process-static
  `glfwTerminate()` ran, and restore a fail-closed contract that admits only a
  precisely identified upstream allocation while keeping engine leaks visible.

## Non-goals
- No global `detect_leaks=0`, broad X11/GLFW suppression, retry wrapper,
  quarantine, label exclusion, or weakening of the synthetic-leak control.
- No changes to texture baking, renderer material behavior, or `RUNTIME-190`.

## Context
- `BUG-082` closed this exact allocation path on 2026-07-16 after the
  unchanged GLFW 3.4/Xlib teardown passed cleanly and the standalone process
  proved `glfwTerminate()` executed exactly once.
- On 2026-07-21 the canonical CPU selector failed the standalone contract,
  an immediate exact rerun failed identically, and both post-fix complete CPU
  selectors reproduced it. All four runs reported
  `BUG082_GLFW_STATIC_TEARDOWN: terminate_calls=1` followed by one direct
  408-byte allocation retained through `libX11.so.6`; the synthetic control
  still behaved as intended.
- Exact reproducer:
  `ctest --test-dir build/ci-asan --output-on-failure -R '^GlfwLifecycleLsan\.EngineStaticTeardownAndLeakControl$' --timeout 60`.
- A fresh Clang 23 `ci-asan` configure and helper rebuild preserved the original
  finding before the narrow remedy. With the exact function suppression and
  explicit symbolizer, ten consecutive standalone contract runs passed while
  every synthetic-control subprocess still exited through LeakSanitizer 86.
- BUG-082's accepted host used the fixed libX11 1.8.7 path. This host's 1.7.5
  package predates the upstream 2022 correction: the registration helper
  closes its temporary XIM protocol state but omits the `XFree(xim)` added by
  `1d118226`. GLFW's later unregister/close path therefore cannot recover that
  already-lost temporary allocation.
- The same initial full Vulkan receipt exposed two 256-byte rows while probing
  installed but nonselected lavapipe and Radeon ICDs. Runtime map capture bound
  their addresses to `libvulkan_lvp.so` and `libvulkan_radeon.so`; selecting
  the operational NVIDIA ICD explicitly made the exact shutdown contract pass
  without adding a Mesa, loader, unknown-module, or pthread suppression.
  Hosted operational Vulkan already selects one ICD with `VK_DRIVER_FILES`.

## Required changes
- [x] Reproduce from a fresh `ci` configure/build and capture the active X11
      display, locale, input-method, libX11, and GLFW identities.
- [x] Compare the current unregister/close call path and process teardown
      ordering with the `BUG-082` proof, including whether `XCloseIM` executes.
- [x] Determine whether this is environment-dependent upstream retention,
      stale build state, or a regressed engine lifetime before changing code.
- [x] Apply only the narrowest ownership-correct remedy that preserves the
      unsuppressed synthetic engine-leak control.

## Tests
- [x] Make the exact standalone contract pass repeatedly on the reproducing
      live-X11 host without retry or a broad suppression; admit only the exact
      upstream registration allocation.
- [x] Run the GLFW/platform intersection and the complete CPU-supported gate.

## Docs
- [x] Record the ownership diagnosis and current environment comparison here.
- [x] Update the platform, Vulkan shutdown, test, bug-index, and retirement
      documentation with the exact current contract.

## Acceptance criteria
- [x] The standalone clean process exits zero after initializing and
      terminating GLFW/X11, while its named 4,096-byte synthetic leak still
      exits with the expected LeakSanitizer failure.
- [x] The fix or environment contract explains why `BUG-082` passed and this
      recurrence fails; no unrelated sanitizer finding is hidden.

## Verification
```bash
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan \
  --target IntrinsicGlfwLifecycleLsanProcess IntrinsicPlatformGlfwSmokeTests
ctest --test-dir build/ci-asan --output-on-failure \
  -R '^GlfwLifecycleLsan\.EngineStaticTeardownAndLeakControl$' \
  --repeat until-fail:10 --timeout 60
ctest --test-dir build/ci-asan --output-on-failure \
  -L platform -L glfw --timeout 60

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan
env VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ctest --test-dir build/ci-vulkan --output-on-failure \
  -L 'gpu' -L 'vulkan' --timeout 180
```

Recorded on 2026-09-03:

- A fresh Clang 23 `ci-asan` build passed the standalone contract ten
  consecutive times and the complete GLFW/platform intersection 2/2. Every
  contract iteration first required the named 4,096-byte synthetic leak to
  produce LeakSanitizer exit 86.
- The direct clean helper reported exactly one used suppression: one 408-byte
  `_XimRegisterIMInstantiateCallback` allocation. It also proved
  `glfwTerminate()` ran exactly once.
- The canonical CPU-supported selector passed 4,258/4,258; its unsanitized
  LeakSanitizer capability case skipped as designed.
- With the operational NVIDIA ICD selected explicitly, the exact standalone +
  Vulkan-shutdown selector passed 2/2 and the complete promoted-Vulkan cohort
  passed 54/54. The earlier 53/54 multi-ICD diagnostic remains preserved in
  BUG-154 evidence; no loader, Mesa, ICD, unknown-module, pthread, or broad
  X11/GLFW suppression was added.

## Forbidden changes
- Reopening the gate by disabling leak detection, accepting exit 86 for the
  clean process, or suppressing all X11/GLFW allocations.
- Treating `terminate_calls=1` alone as proof that every XIM-owned allocation
  was released.
- Mixing the platform diagnosis into `RUNTIME-190` production code.

## Maturity
- Target: `Operational`; this issue is a regression of the retired `BUG-082`
  operational contract.
- Current: completed at `Operational`; the exact affected-libX11 exception is
  bounded by the standalone synthetic control, the Vulkan process contract,
  and current CPU/GPU verification.
