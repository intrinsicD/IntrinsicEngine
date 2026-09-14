---
id: BUG-188
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnosis of host test-discovery restrictions; no engine capability or performance claim.
contract_schema: 1
contracts: []
contract_review: Existing sanitizer verification and local execution policies apply; no new reusable engine contract.
---
# BUG-188 — Sandbox blocks LeakSanitizer during CTest discovery

## Goal
Give agent verification a deterministic route for sanitizer test discovery on
hosts whose filesystem sandbox prevents LeakSanitizer thread inspection.

## Evidence
- During RUNTIME-233, `ci-asan` configured and built `IntrinsicCpuTests` successfully.
  The unchanged full CPU CTest command then exited 8 during GoogleTest discovery
  of `IntrinsicRuntimeIntegrationTests`, before any test ran.
- LeakSanitizer reported that it cannot operate under the sandbox's ptrace
  restriction. Log: `/tmp/intrinsic-runtime233-20260912/ci-asan-test.log`.
- The same limitation was previously noted under BUG-180; it is distinct from
  that task's allocation-retention investigation.
- The identical command passed all 2,916 registered tests outside the sandbox, retaining the test
  selector, serial execution and sanitizer environment. Host retry log:
  `/tmp/intrinsic-runtime233-20260912/ci-asan-test-host.log`.

## Acceptance criteria
- [x] Record the identical host retry outcome and distinguish discovery failure from a sanitizer finding.
- [ ] Document or route required discovery through a supported local execution context.
- [ ] Preserve test selectors, dedicated leak controls and sanitizer settings; do not alter host ptrace policy or add suppressions to hide this limitation.

## Verification
```bash
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```
Run outside the restrictive filesystem sandbox when reproducing the host control.
