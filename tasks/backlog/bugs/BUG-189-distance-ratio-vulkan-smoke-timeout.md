---
id: BUG-189
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnosis of a Vulkan verification timeout; no performance claim.
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-189 — Distance-ratio Vulkan smoke exceeds its inherited timeout

## Goal
Diagnose and resolve the `LocalDistanceRatioPublishesAcrossDomains` timeout
without weakening correctness, backend, sanitizer or domain coverage.

## Evidence
During RUNTIME-235 on 2026-09-13, the combined ASan/UBSan `ci-vulkan` suite
terminated this case at 30.30 seconds. Its inherited `TIMEOUT 30` overrides the
CLI's `--timeout 120`; neighboring frame-based normal/outlier/density/spacing
cases already declare 120 seconds. Those neighbors pass but currently take
43–45 seconds, compared with 7–9 seconds in the saved preceding run. Pure GPU
query cases also increased from about 4 to 13 seconds. The radius-row, density
and spacing implementations are byte-identical to the pre-turn snapshot.

The two configure logs both name Clang 23 Debug with address+undefined
instrumentation. Sampled host load was low; the selected RTX 3050 was at P8,
210 MHz and 45 C. This does not establish a root cause for the broad timing
change. At this initial checkpoint no deadline, assertion, driver setting or sanitizer
had changed; the evidence-backed correction below supersedes that status.
Raw evidence and ranked hypotheses: `/tmp/intrinsic-runtime235-remaining-20260913/gpu-timeout-ledger.md`.

## Acceptance criteria
- [x] Reproduce in isolation and distinguish initialization/test cost from a stuck processing path.
- [x] Apply only an evidence-backed correction; retain all domain/parity and sanitizer checks.
- [x] Re-run the affected registered test and record the actual backend result.

## Verification
```bash
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointLBVHGpuSmoke.LocalDistanceRatioPublishesAcrossDomains$' --no-tests=error --timeout 120
```

## Resolution — 2026-09-13

The unchanged isolated registered test reproduced the 30-second kill. A bounded
direct execution completed every assertion in 44.721 seconds, with zero reference
error and cold/warm/k63 phases of 9.589/9.902/9.857 seconds. Its process then
reported leak-enabled retention (BUG-180); it accidentally lacked the registered
CTest environment and is diagnostic evidence, not a passing gate.

Read-only `xset q` showed `Monitor is Off`. The current ICP/query times of
46.71/16.75 seconds closely match the previously controlled display-off findings
in [BUG-179](../../done/BUG-179-framed-icp-display-off-timeout.md) and
[BUG-143](../../done/BUG-143-corner-uv-gpu-smoke-exceeds-cohort-timeout.md).
Repeated phase costs rule out a cold-start-only explanation. No new controlled
on/off comparison or general performance conclusion is claimed here.

Claude confirmed the omission predates this migration: both ratio and ordinary
outlier tests use `OutlierApp`, whose internal watchdog is 95 seconds, but only
the ordinary variant had the 120-second CTest override. The correction adds the
ratio name to that same registration line. The generated registry confirms 120
seconds and identical command, environment, working directory and labels.
No assertions, tolerances, internal watchdog or sanitizer policy changed.
The corrected registered test passed in 44.72 seconds (46.619 seconds process
wall time), exercising the actual Vulkan backend. The existing test is the
regression reproducer; no duplicate test was added.

Implemented and verified, pending integration. Raw diagnosis, both failures,
registry comparison, Claude review and passing rerun are archived under
`build/analysis/remaining-processing-locality-2026-09-13/`.
