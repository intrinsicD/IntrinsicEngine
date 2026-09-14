---
id: BUG-180
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive test-harness diagnosis; no new engine capability or performance claim."
contract_schema: 1
contracts: []
contract_review: "Existing GPU correctness and sanitizer test policies apply; no new engine API or ownership contract."
---
# BUG-180 — Leak-enabled framed ICP process reports 240 retained bytes

## Goal
- Isolate the three 80-byte allocations reported at process exit by the direct leak-enabled framed ICP diagnostic before classifying ownership or changing suppressions.

## Evidence
- The combined ASan/UBSan `ci-vulkan` test completed all registration assertions in 46.715 s, then exited 1 with `SUMMARY: AddressSanitizer: 240 byte(s) leaked in 3 allocation(s)`.
- Allocation stacks start in unloaded/unknown modules reached through `VulkanDevice::CreatePipeline`; this suggests a driver boundary but does not establish external ownership. Full log: `/tmp/icp-gpu-display-off-diagnostic.log`.
- The normal general GPU CTest cohort intentionally uses `detect_leaks=0`; dedicated Vulkan shutdown/negative-control tests own leak evidence. The diagnostic instead used `LSAN_OPTIONS=suppressions=.../lsan.supp`, enabling a distinct evidence class. No existing environment or suppression is changed by the ICP implementation.
- [ICP review](../../../docs/reviews/2026-09-08-icp-spatial-integration.md) records the distinction. `BUG-083` is the existing dedicated shutdown/retention context; `BUG-179` owns present pacing, not these allocations.

- A subsequent read-only CTest discovery inside the filesystem sandbox could not run LeakSanitizer under its ptrace restriction. Repeating discovery outside that sandbox succeeded. This is a separate tooling limitation and supplies no allocation-ownership evidence.

## Additional diagnostic — 2026-09-13

RUNTIME-235's direct distance-ratio timing probe also completed all assertions
(44.721 s, zero reference error), then exited 1 with 115,869 bytes in 24
allocations. Its runner omitted the registered CTest environment; the resulting
leak-enabled run is not a passing sanitizer gate. Stacks include unloaded modules,
Vulkan instance creation and libdbus. This is a new observation in the existing
retention investigation, not proof that those allocations share the ICP cause or
are exclusively driver-owned. The general cohort environment is unchanged;
no suppression was added. Raw log:
`build/analysis/remaining-processing-locality-2026-09-13/ratio-diagnostic.log`.

## Additional diagnostic — 2026-09-14

The BUG-194 actual-dragon scalar-display probe completed its assertions, then
the direct executable exited 1 with 115,665 retained bytes in 21 allocations.
Stacks include Vulkan instance creation, libdbus and unloaded modules. The
runner omitted the registered general GPU cohort environment, so this remains
a leak-enabled diagnostic rather than a passing sanitizer gate. No ownership
classification or suppression change follows. Raw log:
`build/analysis/bug194-saliency-followup-2026-09-14/dragon-gpu.log`.

## Acceptance criteria
- [ ] Reproduce with the same executable, working directory, source and leak-enabled environment, comparing against the dedicated Vulkan shutdown control.
- [ ] Preserve the relevant loaded driver/module identity before unload and classify ownership using allocation/free evidence.
- [ ] Fix engine-owned retention or document a narrowly evidenced external-retention decision through the existing shutdown contract; retain the engine-allocation negative control.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
LSAN_OPTIONS=suppressions="$PWD/lsan.supp" timeout 120s build/ci-vulkan/bin/IntrinsicPointLBVHGpuTests --gtest_filter=PointLBVHGpuSmoke.FramedRegistrationReusesTargetAcrossRunsAndMatchesCpuSolve
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ExtrinsicSandbox.VulkanShutdownLsanContract' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
