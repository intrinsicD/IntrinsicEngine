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

The [retained log](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/2026-10-01-interactive-sandbox-lsan.log)
reports 122,642 bytes in 46 allocations, including two direct 256-byte
unresolved-module allocations, XCB, DBus, and larger unresolved allocations.
This is an unsuppressed interactive observation, **not** a rerun of the pinned
shutdown contract and not proof that every allocation has the same root cause
as the original 512-byte report. Attribution remains open; no suppression or
test policy changed. The separately executed 38 selected method GPU tests
passed, which does not establish clean interactive shutdown.

The subsequent ICP/CPD/property-smoothing UI session also exited with status 1:
[retained log](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/2026-10-01-remaining-reuse-sandbox-lsan.log),
122,626 bytes in 46 allocations. Its allocation stacks likewise include XCB,
DBus and unresolved modules; attribution remains open. The run contained no
Vulkan validation errors. This observation does not establish a clean sanitizer
shutdown or change the suppression policy.

## Resolution — 2026-10-01

The original two 256-byte allocations are **not attributed to NVIDIA**. The
[loader mapping trace](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/diagnosis-2026-10-01/desktop-maps.log)
places their allocation PCs at `libvulkan_lvp.so+0x9e428` and
`libvulkan_radeon.so+0x206a08`. Both installed Mesa ICDs unload before exit,
explaining the earlier unknown-module stacks. A standalone
[Vulkan enumeration probe](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/diagnosis-2026-10-01/enumerate.c)
reproduces [512 bytes without any engine code](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/diagnosis-2026-10-01/enumerate-all-policy.log);
[selecting NVIDIA](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/diagnosis-2026-10-01/enumerate-nvidia-policy.log)
removes those allocations under the unchanged four-entry suppression policy.
This probe is diagnostic evidence, not a preset engine build.

The test already required one selected operational ICD in `tests/README.md`.
Its runner now rejects missing/multiple/invalid manifests, prints the selected
manifest and display, and still runs the synthetic engine-leak negative control
and full five-frame shutdown checks. Eleven prerequisite cases are covered by
`Test.VulkanShutdownLsanPrerequisites.py`. No allocator/driver suppressions were
added and no production teardown was changed.

The [corrected contract](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-221/diagnosis-2026-10-01/fixed-contract.log)
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

## Resolution — 2026-10-24

Retirement preparation only; no implementation change.

- Implementation: `9395eb9e6` on `main`. The runner
  `tests/regression/graphics/Run.VulkanShutdownLsanContract.cmake` and
  `tests/regression/tooling/Test.VulkanShutdownLsanPrerequisites.py` are unchanged
  since. `ci-docs`, which runs the prerequisite test, did not run on `main` between
  2026-09-15 and 2026-10-08; its first green `main` run after the commit is at
  `b4e0ca630` (2026-10-08). `ci-linux-clang` was red on `main` from after 2026-09-11,
  before this commit, until its first green run at `1c79eacbb` (2026-10-08). The run that included
  this commit failed in `full-cpu`, step "Capture CPU test selection". Both
  workflows are green at `599f02ff1` (2026-10-23).
- Checked on 2026-10-24 at `599f02ff1`: the prerequisite test passes (11 subtests).
  With only the runner restored to `9395eb9e6^` it fails with 7 failures, one per
  rejection case. The GPU contract was not rerun; its last recorded pass is the
  2026-10-01 native `DISPLAY=:1` run above.
- Durable record: the ICD-selection rationale and this attribution in
  `tests/README.md`, the runner comment, and the lesson "Attribute unknown-module
  leak frames before blaming a driver" in `docs/lessons/engineering.md`.
- The evidence links above now use exact revision `599f02ff1`; those objects are
  byte-identical to `9395eb9e6`. Prepared deletion: the 12 tracked files under
  `tasks/evidence/BUG-221/`. BUG-229's links into them were repaired the same way.

## Completion
Completed 2026-10-24. Commit: `9395eb9e6`.
Reviewer decision 2026-10-24: independent Claude review approved at `800f1c371` (implementation,
summaries, link repairs and deletion list, checked against the raw evidence before deletion):
AC1 "met", AC2 "met". For AC1 the attribution is to module and offset, not function symbols;
the engine-free reproduction plus the NVIDIA-only removal control meets its intent.
Maturity: Operational on the recorded host. The `gpu;vulkan` contract passed natively on
2026-10-01 (RTX 4090, NVIDIA 580.159.04); `Test.VulkanShutdownLsanPrerequisites.py` (11 cases,
run in `ci-docs`) pins the ICD-selection rejections. The GPU contract was not rerun at retirement.

The 12 tracked files under `tasks/evidence/BUG-221/` were deleted at retirement; the links above
resolve at Git revision `599f02ff1`. Limitations recorded before deletion:
- `fixed-contract.log`, `desktop-nvidia.log` and `legacy-contract.log` are non-verbose CTest
  summaries. They do not record `DISPLAY` or the selected ICD; "native `DISPLAY=:1`, NVIDIA
  ICD" rests on the author's command record.
- The Evidence section's "Vulkan loader 1.4.312" differs from the installed package
  `libvulkan1 1.3.296.0~rc1` recorded for BUG-229; `tests/README.md` uses the package version.
