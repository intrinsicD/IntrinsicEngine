---
id: BUG-083
theme: G
depends_on: [BUG-082]
---
# BUG-083 — Vulkan Sandbox shutdown reports driver and DBus leaks under LeakSanitizer

## Status
- Completed on 2026-07-16 at maturity `Operational`; owner: Codex; branch:
  `codex/arch-006-completion`; implementation commit: `3cb91b98`.
- A fresh sanitizer-enabled `ci-vulkan` rebuild reproduced the issue on the
  exact current head: the NVIDIA process wrote five renderer-completed samples
  and ended with an operational device, then exited 86 for 116,425 leaked bytes
  in 35 allocations. The symbolized stacks retain the previously diagnosed
  Vulkan push-constant driver, VMA buffer-bind, and DBus paths.
- The narrow-policy contract executes without a skip and passes on both the
  NVIDIA RTX 3050 / driver `590.48.01` path (`1/1` in 14.81 seconds) and Mesa
  lavapipe (`1/1` in 4.06 seconds). Each run validates five
  renderer-completed samples and an operational final device, and first proves
  the named 4,096-byte unrelated engine leak still exits 86 under the exact
  same three-entry policy.
- The exact-head `IntrinsicTests` build and default CPU-supported gate passed
  `3,788/3,788` in 402.61 seconds. With the general compiled suppression default
  now empty, the original GLFW-backed close-before-first-frame runtime process
  also passed 10/10 with leak detection enabled and no suppression file.

## Goal
- Make the bounded Vulkan `ExtrinsicSandbox` smoke exit cleanly under the
  sanitizer-enabled `ci-vulkan` preset, or establish precise external ownership
  and a narrow suppression policy whose unrelated engine-leak negative control
  remains visible.

## Non-goals
- No expansion of the existing general CTest leak-disable environment, broad
  third-party suppression, or sanitizer-gate weakening.
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
- The exact-head 2026-07-16 reproduction used the NVIDIA RTX 3050 with driver
  `590.48.01`. Six 18,960-byte direct groups enter unloaded proprietary-driver
  frames through
  `VulkanCommandContext@Extrinsic.Backends.Vulkan::PushConstants`; the smaller
  retained trees resolve through `VmaAllocator_T::BindVulkanBuffer` and the
  shared `dbus_connection_send_with_reply_and_block` function. The process
  wrote five renderer-completed samples and
  ended with an operational device before LeakSanitizer reported 116,425 bytes
  in 35 allocations and exited 86. Exact group accounting assigns 113,760
  bytes / 18 allocations to
  the push-constant call-site family, 784 bytes / 14 allocations to the VMA
  bind family, and 1,881 bytes / 3 allocations to symbolized DBus 1.14.10
  code, with no unmatched group. No `_XimOpenIM` path remains in this report.
- Prior debugger and loader-trace evidence on the same driver established that
  engine device, swapchain, surface, instance, GLFW, and logging shutdown had
  completed and that the NVIDIA DSOs had unloaded before LeakSanitizer's
  sweep. The current engine/frame-report/Vulkan shutdown implementations are
  unchanged at those seams, so a production lifetime edit is not justified.

## Required changes
- [x] Resolve the unsymbolized allocation modules from the exact app process
      and identify whether they belong to the Vulkan loader, selected ICD,
      desktop integration, or engine-owned code.
- [x] Verify Vulkan device, swapchain, GLFW, and logging/DBus shutdown order
      before LeakSanitizer's sweep.
- [x] Fix engine lifetime ordering when owned here; if externally retained,
      add only symbol/module-specific documented suppressions with a synthetic
      engine-leak control.

## Tests
- [x] Add or retain a bounded promoted-Vulkan app smoke that writes its report,
      completes renderer frames, shuts down, and exits zero under LeakSanitizer.
- [x] Prove a synthetic engine-owned leak remains detectable under any adopted
      suppression configuration.
- [x] Keep the Null-backend CPU gate and the `BUG-082` focused GLFW reproducer
      independently diagnosable.

## Docs
- [x] Record allocation ownership and the narrow sanitizer policy in the
      Vulkan/platform testing notes.
- [x] Update the retirement log and task indexes when verified.

## Acceptance criteria
- [x] The exact five-frame `ExtrinsicSandbox` command exits zero after writing
      a report with five completed renderer frames on a Vulkan-capable host.
- [x] The same exact suppression policy still reports the named, unrelated
      4,096-byte synthetic engine allocation, and contains no loader-, ICD-,
      unknown-module-, pthread-, or GLFW/X11-wide entry.
- [x] General GoogleTest binaries embed no default LeakSanitizer suppression;
      all three BUG-083 exceptions are scoped to the explicit process runner.
- [x] The 408-byte `_XimOpenIM` allocation remains owned and verifiable through
      `BUG-082` rather than being silently absorbed here.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGlfwLifecycleLsanProcess
ctest --test-dir build/ci-vulkan -V --output-on-failure \
  -R '^ExtrinsicSandbox\.VulkanShutdownLsanContract$' \
  -L gpu -L vulkan --no-tests=error --timeout 180
env LIBGL_ALWAYS_SOFTWARE=1 \
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ctest --test-dir build/ci-vulkan -V --output-on-failure \
    -R '^ExtrinsicSandbox\.VulkanShutdownLsanContract$' \
    -L gpu -L vulkan --no-tests=error --timeout 180
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Disabling LeakSanitizer globally or for the complete Sandbox process.
- Treating a written frame report or passing frame assertions as a clean
  process exit while LeakSanitizer remains nonzero.
- Folding the fix into `UI-034`, `ARCH-006`, or unrelated renderer work.

## Maturity
- Achieved: `Operational` on the NVIDIA RTX 3050 / driver `590.48.01` sanitizer
  path and the Mesa lavapipe CI path. CPU/null behavior remains independently
  covered and was not used as a substitute for the shutdown gate.
