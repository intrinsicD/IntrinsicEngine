---
id: BUG-083
theme: G
depends_on: [BUG-082]
---
# BUG-083 — Vulkan Sandbox shutdown reports driver and DBus leaks under LeakSanitizer

## Status
- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- A fresh sanitizer-enabled `ci-vulkan` rebuild reproduced the issue on the
  exact current head: the five-frame NVIDIA process remained operational and
  wrote five completed samples, then exited 86 for 116,425 leaked bytes in 35
  allocations. The symbolized stacks retain the previously diagnosed Vulkan
  push-constant driver, VMA buffer-bind, and DBus paths.

## Goal
- Make the bounded Vulkan `ExtrinsicSandbox` smoke exit cleanly under the
  sanitizer-enabled `ci-vulkan` preset, or establish precise external ownership
  and a narrow suppression policy that preserves detection of engine-owned
  leaks.

## Non-goals
- No global `detect_leaks=0`, broad third-party suppression, or sanitizer-gate
  weakening.
- No editor-window, ImPlot, frame-recipe, or rendering feature changes.
- No duplication of the 408-byte GLFW/X11 input-method leak owned by `BUG-082`.

## Context
- Observed on 2026-07-13 while closing the bounded `UI-034` app-level smoke:
  `build/ci/bin/ExtrinsicSandbox --frame-pacing-report
  /tmp/ui-034-sandbox-smoke.json --frame-pacing-frames 5`.
- The report was written with `frame_count=5`,
  `final_device_operational=true`, and all five samples reporting completed
  renderer frames. LeakSanitizer then exited nonzero with 117,114 bytes in 32
  allocations.
- The report included 113,816 direct bytes and 392 indirect bytes from an
  unsymbolized loaded module, two 256-byte reallocations from unsymbolized
  modules, and DBus allocations totaling 1,986 bytes. It also included the
  separately tracked 408-byte `_XimOpenIM` allocation from `BUG-082`.
- Null-backend runtime/editor contract processes exit cleanly under the same
  sanitizer build. The additional allocations therefore require a focused
  Vulkan loader/driver/window shutdown ownership diagnosis rather than an
  editor workaround.

## Required changes
- [ ] Resolve the unsymbolized allocation modules from the exact app process
      and identify whether they belong to the Vulkan loader, selected ICD,
      desktop integration, or engine-owned code.
- [ ] Verify Vulkan device, swapchain, GLFW, and logging/DBus shutdown order
      before LeakSanitizer's sweep.
- [ ] Fix engine lifetime ordering when owned here; if externally retained,
      add only symbol/module-specific documented suppressions with a synthetic
      engine-leak control.

## Tests
- [ ] Add or retain a bounded promoted-Vulkan app smoke that writes its report,
      completes renderer frames, shuts down, and exits zero under LeakSanitizer.
- [ ] Prove a synthetic engine-owned leak remains detectable under any adopted
      suppression configuration.
- [ ] Keep the Null-backend CPU gate and the `BUG-082` focused GLFW reproducer
      independently diagnosable.

## Docs
- [ ] Record allocation ownership and any narrow sanitizer suppression in the
      Vulkan/platform testing notes.
- [ ] Update the retirement log and task indexes when verified.

## Acceptance criteria
- [ ] The exact five-frame `ExtrinsicSandbox` command exits zero after writing
      a report with five completed renderer frames on a Vulkan-capable host.
- [ ] No engine-owned allocation is hidden by the remedy.
- [ ] The 408-byte `_XimOpenIM` allocation remains owned and verifiable through
      `BUG-082` rather than being silently absorbed here.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGlfwLifecycleLsanProcess
build/ci-vulkan/bin/ExtrinsicSandbox \
  --frame-pacing-report /tmp/bug-083-sandbox-smoke.json \
  --frame-pacing-frames 5
ctest --test-dir build/ci-vulkan -V --output-on-failure \
  -R '^ExtrinsicSandbox\.VulkanShutdownLsanContract$' \
  -L gpu -L vulkan --no-tests=error --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Disabling LeakSanitizer globally or for the complete Sandbox process.
- Treating a written frame report or passing frame assertions as a clean
  process exit while LeakSanitizer remains nonzero.
- Folding the fix into `UI-034`, `ARCH-006`, or unrelated renderer work.

## Maturity
- Target: Operational on a Vulkan-capable sanitizer host. CPU/null behavior is
  already covered elsewhere and is not a substitute for this shutdown gate.
