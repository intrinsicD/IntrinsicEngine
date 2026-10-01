---
id: BUG-229
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Environmental nested-display shutdown failure; no method or performance claim.
contract_schema: 1
contracts: []
contract_review: Existing BUG-083 shutdown negative control and four-entry suppression policy remain authoritative; no new engine contract.
---
# BUG-229 — NVIDIA shutdown retention on a nested Xephyr display

## Goal
Attribute and eliminate the NVIDIA/XCB retention on the nested-display WSI path
without broadening the shutdown leak suppressions.

## Context
During BUG-221 diagnosis on 2026-10-01, the exact same built Sandbox and NVIDIA
580.159.04 ICD passed the shutdown contract on native `DISPLAY=:1` and failed on
Xephyr `DISPLAY=:9` (6,128 bytes/20 allocations with the existing suppressions).
The original unrelated 512 bytes from enumeration of unselected Mesa drivers
are separately resolved by BUG-221's explicit ICD prerequisite.

[NVIDIA-only nested-display failure](../../evidence/BUG-221/diagnosis-2026-10-01/nvidia-only.log),
[native pass](../../evidence/BUG-221/diagnosis-2026-10-01/fixed-contract.log), and
[diagnostic library-pinning stacks](../../evidence/BUG-221/diagnosis-2026-10-01/pinned-driver.log)
retain the evidence. The latter attributes the large allocations to
`libnvidia-glcore.so.580.159.04` and the XCB path; library pinning is not the fix.
No production teardown change or suppression was introduced. Do not substitute
Xephyr for native-driver shutdown evidence or claim every unsuppressed UI leak
is an engine-owned leak.

## Diagnosis and disposition — 2026-10-01

No engine-owned teardown defect was found. The exact tested combination is
RTX 4090, NVIDIA 580.159.04, Xephyr package
`2:21.1.4-2ubuntu1.7~22.04.16`, libxcb 1.14-3ubuntu3 and Vulkan loader
1.3.296.0~rc1. It is unsupported **for leak-clean shutdown qualification**;
this is a repository qualification boundary, not a claim about every driver
version or all nested rendering. Native X11 remains the qualified shutdown
path. The vendor leak still exists; no driver upgrade was performed or claimed
as a fix. No production code, suppression, test label or failure condition changed.

Independent [Vulkan/XCB](../../evidence/BUG-229/wsi-closefirst.c) and
[Vulkan/Xlib](../../evidence/BUG-229/wsi-xlib-closefirst.c) probes contain no
engine or GLFW code. The [reproduction script](../../evidence/BUG-229/reproduce.sh)
compiles these small diagnostic programs with Clang/ASan; it is not an engine
preset build or substitute for the shutdown contract.

| Diagnostic stage | NVIDIA / Xephyr | NVIDIA / native X11 |
| --- | --- | --- |
| Instance and device enumeration | Clean under existing policy | Clean under existing policy |
| Surface create/destroy | Clean under existing policy | Clean under existing policy |
| One XCB capabilities query | 64 bytes / 2 allocations | Clean under existing policy |
| Ten XCB capabilities queries | 640 bytes / 20 allocations | Clean under existing policy |
| XCB swapchain create/destroy | 5,840 bytes / 11 allocations | Clean under existing policy |
| Xlib swapchain create/destroy | 5,584 bytes / 3 allocations | Clean under existing policy |

The 1,392 + 1,392 + 2,800 byte allocations originate during
`vkDestroySwapchainKHR`, with the same driver frames as the engine failure.
The per-query 32-byte replies scale with query count; they are not simply a
one-time cache. Enabling Xephyr glamor gives the same XCB result. Moving X
connection cleanup before instance destruction also preserves the same leak
counts. The original Xlib cleanup order exposed an unloaded callback crash on
the native display; the retained reproduction uses connection-close-first and
keeps the original-order log as evidence rather than claiming that run passed.

The [validation-enabled probe](../../evidence/BUG-229/validation.log) loads the
Khronos instance/device validation layers, completes, and reports no VUIDs.
The Mesa control has the same known 256-byte enumeration leak at
[stage 0](../../evidence/BUG-229/mesa-0.log) and
[stage 3](../../evidence/BUG-229/mesa-3.log), with no additional WSI retention;
it is not represented as a wholly leak-free run.

The [full native shutdown contract](../../evidence/BUG-229/native-contract.log)
passes, including its synthetic 4,096-byte engine-leak negative control and
unchanged four-entry suppressions. Xephyr's same contract remains an expected
failure. [Claude Opus 5.5 medium's independent review](../../evidence/BUG-229/independent-review.txt)
accepts the unsupported-combination resolution and identifies no safe missing
engine correction; its validation/control suggestions were subsequently run.
The remaining remedy is an upstream driver correction validated against this
reproducer. Broad suppression, skipping swapchain destruction, or freeing
opaque driver memory is not a fix. Task retirement awaits a commit reference.

## Acceptance criteria
- [x] Reproduce the nested WSI allocation lifetime in a minimal surface/swapchain probe, or identify an engine lifetime defect.
- [x] Correct an engine-owned cause if found; otherwise validate an upstream driver fix or document the unsupported nested-driver combination with independent review.
- [x] Retain the engine-leak negative control and unchanged narrow suppression policy.

## Verification
With an owned Xephyr server already running on `:9`, the diagnostic script and
nested contract intentionally return nonzero on this affected driver. The native
contract must pass:
```bash
bash tasks/evidence/BUG-229/reproduce.sh
DISPLAY=:9 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ctest --test-dir build/ci-vulkan --output-on-failure -R '^ExtrinsicSandbox.VulkanShutdownLsanContract$' --timeout 180
DISPLAY=:1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json ctest --test-dir build/ci-vulkan --output-on-failure -R '^ExtrinsicSandbox.VulkanShutdownLsanContract$' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
