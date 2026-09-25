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

## Reproduction — 2026-09-14
ASSETIO-012's focused `ci-asan` CTest selector again failed in discovery of the
unchanged `IntrinsicRuntimeIntegrationTests`, before executing any selected test.
The same 272-test selector passed with host access, with serial execution and
sanitizer settings unchanged. Logs: `focused-asan.log` and
`focused-asan-host.log` under
`build/analysis/assetio012-format-catalog-2026-09-14/`. Keep this workflow task
open; the host retry does not implement automatic supported-context routing.

## Reproduction — 2026-09-25
Scalar-gradient UI integration reproduced the same discovery-only failure in
`IntrinsicRuntimeIntegrationTests` after a successful fresh `ci-asan` configure
and `IntrinsicCpuTests` build. LeakSanitizer reported its ptrace restriction
before executing tests (`/tmp/scalar-gradient-asan-tests.log`). Host access allowed the unchanged serial CPU selector to complete the
3,310-registration run (`/tmp/scalar-gradient-asan-host-tests.log`), with no
sanitizer runtime findings. Two stale menu expectations and their compilation
metadata check required rebuilding the edited test binary; the final recheck is
recorded in `/tmp/scalar-gradient-asan-final-tests.log` (46/46 passed). No sanitizer flags,
suppressions, test labels or host ptrace policy were changed.

Property-smoothing verification on the same date reproduced the discovery
restriction (`/tmp/property-smoothing-asan-tests.log`). The unchanged serial
selector then passed all 3,322 registrations with host access
(`/tmp/property-smoothing-asan-host-tests.log`; two native-window checks skipped
in the Null/headless configuration). No sanitizer flags or suppressions changed.
