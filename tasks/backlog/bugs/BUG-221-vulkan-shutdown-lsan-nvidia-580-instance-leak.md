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

## Acceptance criteria
- [ ] Root cause attributed with symbolized driver frames or a driver-version
      comparison on one host.
- [ ] The contract passes on the affected driver, or a reviewed decision
      records why this driver is out of support; the exact suppression list is
      not widened without that review.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGlfwLifecycleLsanProcess
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandbox.VulkanShutdownLsanContract' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
