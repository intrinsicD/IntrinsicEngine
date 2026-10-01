---
id: BUG-221
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Environmental test-gate incident on one host driver; no performance or capability claim.
contract_schema: 1
contracts: []
contract_review: Existing BUG-083 shutdown leak contract and its exact suppression policy apply; no new engine contract.
---
# BUG-221 — Vulkan shutdown LSan contract fails on NVIDIA 580.159.04 instance creation

## Goal
Restore `ExtrinsicSandbox.VulkanShutdownLsanContract` on hosts running the
NVIDIA 580.159.04 driver without broadening the four reviewed suppressions the
contract pins (BUG-083/118): either attribute the retention precisely (driver
defect vs. missing engine teardown) or record a narrowly scoped, reviewed
policy change.

## Evidence
On 2026-09-25 (host: NVIDIA GeForce RTX 4090, driver 580.159.04, Vulkan
loader 1.4.312) the contract fails in ~4 s with exit 86 and
`SUMMARY: AddressSanitizer: 512 byte(s) leaked in 2 allocation(s).`

First allocation (256 B):

```text
#0 realloc
#1..#11 (<unknown module>)
#12,#13,#17 libvulkan.so.1
#18 vkCreateInstance
#19 Extrinsic::Backends::Vulkan::VulkanDevice::Initialize
#20 Extrinsic::Runtime::Engine::Initialize()
```

Second allocation (256 B), on a worker thread and not attributed:

```text
#0 realloc
#1,#2 (<unknown module>)
#3 __pthread_once_slow
#4,#5 (<unknown module>)
#6 asan_thread_start
#7 start_thread (pthread_create)
```

The unsymbolized frames are unattributed; a driver-internal origin is a
hypothesis, not established.

- Unchanged `main` at `3a352d3ed`, built in a separate worktree
  (`ci-vulkan`, targets `ExtrinsicSandbox IntrinsicGlfwLifecycleLsanProcess`):
  fails with the same two 256-byte direct leaks, so the failure predates the
  UI-050 changes.
- The UI-050 working tree (same session): fails identically; the other 103
  `gpu;vulkan` tests pass.
- Earlier recorded passes of this contract (for example the BUG-154 closure
  runs) were on driver 590.48.01.

### 2026-10-01 interactive Sandbox observation

At `1200af6eb`, the rebuilt `ci-vulkan` Sandbox on RTX 4090 / NVIDIA
580.159.04 exited with status 1 after a normal window-close event following
mesh import, GPU k-means Accept/Discard, and GPU kernel-density Accept/Discard.
It ran in Xephyr on `DISPLAY=:9`, with default validation enabled and no
explicit LSan suppression environment supplied by the launch command:

```bash
cd build/ci-vulkan
DISPLAY=:9 ./bin/ExtrinsicSandbox --agent-socket=/tmp/intrinsic-method-review-20261001/sandbox.sock --agent-root /tmp/intrinsic-method-review-20261001
```

The [retained log](../../evidence/BUG-221/2026-10-01-interactive-sandbox-lsan.log)
reports 122,642 bytes in 46 allocations, including two direct 256-byte
unresolved-module allocations, XCB, DBus, and larger unresolved allocations.
This is an unsuppressed interactive observation, **not** a rerun of the pinned
shutdown contract and not proof that every allocation has the same root cause
as the original 512-byte report. Attribution remains open; no suppression or
test policy changed. The separately executed 38 selected method GPU tests
passed, which does not establish clean interactive shutdown.

The subsequent ICP/CPD/property-smoothing UI session also exited with status 1:
[retained log](../../evidence/BUG-221/2026-10-01-remaining-reuse-sandbox-lsan.log),
122,626 bytes in 46 allocations. Its allocation stacks likewise include XCB,
DBus and unresolved modules; attribution remains open. The run contained no
Vulkan validation errors. This observation does not establish a clean sanitizer
shutdown or change the suppression policy.

## Resolution — 2026-10-01

The original two 256-byte allocations are **not attributed to NVIDIA**. The
[loader mapping trace](../../evidence/BUG-221/diagnosis-2026-10-01/desktop-maps.log)
places their allocation PCs at `libvulkan_lvp.so+0x9e428` and
`libvulkan_radeon.so+0x206a08`. Both installed Mesa ICDs unload before exit,
explaining the earlier unknown-module stacks. A standalone
[Vulkan enumeration probe](../../evidence/BUG-221/diagnosis-2026-10-01/enumerate.c)
reproduces [512 bytes without any engine code](../../evidence/BUG-221/diagnosis-2026-10-01/enumerate-all-policy.log);
[selecting NVIDIA](../../evidence/BUG-221/diagnosis-2026-10-01/enumerate-nvidia-policy.log)
removes those allocations under the unchanged four-entry suppression policy.
This probe is diagnostic evidence, not a preset engine build.

The test already required one selected operational ICD in `tests/README.md`.
Its runner now rejects missing/multiple/invalid manifests, prints the selected
manifest and display, and still runs the synthetic engine-leak negative control
and full five-frame shutdown checks. Eleven prerequisite cases are covered by
`Test.VulkanShutdownLsanPrerequisites.py`. No allocator/driver suppressions were
added and no production teardown was changed.

The [corrected contract](../../evidence/BUG-221/diagnosis-2026-10-01/fixed-contract.log)
passes on the native X11 desktop (`DISPLAY=:1`), NVIDIA 580.159.04:

```bash
DISPLAY=:1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^ExtrinsicSandbox.VulkanShutdownLsanContract$' --timeout 180
```

The same NVIDIA-only run on Xephyr still reports 6,128 bytes/20 allocations.
Pinning the NVIDIA library for diagnosis attributes the larger allocations to
`libnvidia-glcore.so.580.159.04` and the XCB path. This separate nested-display
issue is tracked by [BUG-229](BUG-229-nvidia-xephyr-shutdown-retention.md); it is
not suppressed or represented as a clean shutdown here. Implementation is
verified in the working tree; task retirement awaits a commit reference.

## Acceptance criteria
- [x] Root cause attributed with symbolized driver frames or a driver-version
      comparison on one host.
- [x] The contract passes on the affected driver, or a reviewed decision
      records why this driver is out of support; the exact suppression list is
      not widened without that review.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGlfwLifecycleLsanProcess
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandbox.VulkanShutdownLsanContract' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
