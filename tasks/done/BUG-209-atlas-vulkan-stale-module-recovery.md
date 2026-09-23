---
id: BUG-209
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive build-state diagnosis; unchanged-source failing and rebuilt commands are retained with METHOD-047
contract_schema: 1
contracts: []
contract_review: Build-artifact recovery only; no engine source, compiler policy, dependency or architecture change.
---
# BUG-209 — Recover stale Vulkan module state after atlas integration

## Goal

Restore coherent `ci-vulkan` build artifacts after METHOD-047 integration. The
incremental compiler rejects `TextureBakeService::SetSourceSnapshotBudgetForTest`
as undeclared, although the current interface declares it at line 380. Its class
diagnostic points to line 362, which is now a field of a different struct. This
is evidence of an outdated module view, not a reason to change the source API.

## Acceptance criteria

- [x] Preserve the failing command and diagnostic; rebuild changed inputs with
  refreshed timestamps and ccache disabled, without changing their contents.
- [x] Build `IntrinsicTests` and `ExtrinsicSandbox` and execute the complete
  Vulkan selection against the regenerated artifacts.
- [x] Record the observed recovery and its limits; do not infer an upstream
  ccache/compiler defect or weaken any gate.

## Verification

```bash
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox -j2
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120 -j1
```

The recovery refreshes modification times of changed source/test/shader inputs
so Ninja rebuilds their module closure. Source bytes remain unchanged. The
METHOD-047 verification archive retains `vulkan-final-build-2.log` (failed),
`vulkan-refreshed-inputs.json`, and `vulkan-final-build-3.log` (recovery).

## Status

PR/commit: enclosing implementation commit on `codex/method-047-property-guided-atlas`.

Retired 2026-09-23 at the build-state recovery endpoint. The enclosing METHOD-047
implementation/evidence commit records retirement. The complete cache-disabled
build compiles the previously rejected definition and all selected Vulkan tests
pass afterward. Source-tree identity is unchanged across recovery. This establishes
an outdated incremental artifact as the failure mechanism, without proving whether
Ninja timestamps, a copied artifact or ccache originally supplied it. No source
workaround, compiler suppression, relaxed assertion or reduced test selection was
used. The [verification archive](../../ara/evidence/diagnostics/method047_property_atlas/verification-runs.tar.gz)
retains both build attempts, the refreshed-input list and final execution logs.
