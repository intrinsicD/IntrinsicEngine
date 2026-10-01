---
id: BUG-228
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of an environment-dependent verification failure; no performance claim.
contract_schema: 1
contracts: []
contract_review: Test display environment and timeout diagnosis only; no catalog engine integration contract is changed.
---
# BUG-228 — Vulkan smoke timeout depends on the display environment

## Goal
Make the supported local Vulkan verification environment explicit and diagnose
why the desktop display stalls small runtime smoke tests.

## Context
On 2026-10-01 the unchanged 30-second CTest limit expired for
`PointLBVHGpuSmoke.KeypointResidentPreviewDiscardRepeatPagedParity` on `DISPLAY=:1`,
including a retry after the sandbox and builds stopped. The same executable and
CTest case passed in 10.06 seconds on an owned Xephyr `:9` server. This comparison
isolates a display-environment dependency; it does not identify the compositor,
Vulkan presentation, or driver as the cause. No timeout was raised.

The [desktop log](../../evidence/BUG-228/2026-10-01-desktop-display.log) records an
interrupted focused run, not a full suite verdict. The
[isolated-display log](../../evidence/BUG-228/2026-10-01-xephyr-display.log) records
the completed single-case comparison. Other timeouts in the earlier broad run
were mixed with build/render load and are not sufficient to attribute a cause.

## Acceptance criteria
- [ ] Reproduce the display dependence and locate the presentation/frame wait responsible.
- [ ] Document or implement the supported display setup without weakening timeout or validation gates.
- [ ] Re-run the affected keypoint, scalar, normals, sampling and resident LOP tests in that environment.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
DISPLAY=:1 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointResidentPreviewDiscardRepeatPagedParity$'
# In a separate terminal, start an owned display server:
DISPLAY=:1 Xephyr :9 -screen 1280x900 -noreset -ac
# With that server available:
DISPLAY=:9 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointResidentPreviewDiscardRepeatPagedParity$'
python3 tools/agents/check_task_policy.py --root . --strict
```
