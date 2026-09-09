---
id: BUG-182
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive verification exposed an existing unavailable-backend fixture timeout."
contract_schema: 1
contracts: []
contract_review: "Existing capability-test skip/timeout policy applies; no engine API or architecture change."
---
# BUG-182 — Framed kNN smoke times out when promoted Vulkan is not compiled

## Goal
- Make the framed kNN smoke report unsupported capability before entering its frame loop when promoted Vulkan is not compiled.

## Observation — 2026-09-09
- A local focused regex accidentally selected `PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget` in `build/ci` instead of the opt-in `ci-vulkan` lane.
- The unchanged fixture logged `Promoted Vulkan device requested but not compiled into this build; using Null device fallback`, then CTest killed it at 30.04 seconds. Its internal wait lasts 40 seconds, so it cannot reach the post-run skip. No actual Vulkan query ran.
- The corrected CPU-only selector passed all five selected geometry cases. Real GPU verification belongs to `ci-vulkan`; this does not indicate a query regression or justify weakening that lane.

## Acceptance criteria
- [ ] Capability-skip the unsupported compile/backend case before entering the framed wait.
- [ ] Retain hard failures after actual Vulkan execution and preserve the current query assertions.
- [ ] Verify the non-promoted skip and the actual `ci-vulkan` case.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget$' --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.FramedKNearestReusesBuffersAndRejectsStaleTarget$' -L gpu -L vulkan --timeout 120
```
