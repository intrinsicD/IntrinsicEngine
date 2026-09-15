---
id: RUNTIME-265
theme: J
depends_on: [RUNTIME-264]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive compile-locality follow-up; reviewed diff, dependency checks and tests.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality, runtime.processing-compilation-locality]
---
# RUNTIME-265 — Narrow the editor snapshot and config dependency chain

## Goal
- Reduce the remaining scene-editing → workspace snapshot → context/session
  dependency cost without duplicating presentation records or changing behavior.

## Context and decision boundaries
- The 2026-09-15 BUILD-007 report identifies the workspace snapshot interface
  and context adapter as remaining expensive producers. Its config edit chain
  also passes through consolidation types and the editor session. RUNTIME-264
  removes lifecycle imports from service callers; establish a fresh baseline
  after that change rather than counting its benefit again.
- Inspect which consumers need complete records versus pointer/reference borrows.
  Reuse current family-owned prepared frames and their matching C++ linkage.
  Keep one definition per record, existing visitor lifetimes and epoch guards.
- Compare removal of unused imports, narrower existing record owners, and private
  implementation storage only where actual consumers justify it. No mandatory
  Pimpl, new generic facade, family registry, or compatibility wrapper.

## Acceptance criteria
- [x] Identify the dominating dependency edges using the actual configured
      compiler graph and current source; choose a bounded consumer group.
- [x] Remove unnecessary dependencies without copying records or transferring
      validation/config/publication authority to the app.
- [x] Preserve config-file/UI/agent behavior, snapshot caching, stale/detach
      guards and command-time validation; relevant editor tests pass.
- [x] Add/update compiler-boundary checks and architecture/owner documentation.
- [x] Use the existing BUILD-007 measurement tooling for matched before/after
      evidence before claiming a timing gain; retire with an explicit negative
      result if no beneficial refactor survives the measured comparison.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'EditorCompilationLocality|SandboxEditor|Consolidation|SpatialIndex|RuntimeConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Selected slice
- Operator explicitly requested continued compilation/reuse work with Claude.
  Baseline: `6570bc106`; one writer, read-only Claude plan/final review.
- Compiler P1689 records show `EditorProcessing` is the sole path from workspace
  snapshots to `RHI.Device` and `SpatialIndexCache` (including the route through
  geometry-processing discovery). Registry dependencies have several independent
  routes and are outside this bounded slice.
- Replace those two pointer-only imports with C++-linkage forward declarations.
  Keep the existing owning class declarations and definitions at their owners
  with matching linkage; concrete consumers import those owners explicitly.
  No record copy, facade, context pointerization, module, allocation or file added.
- Claude's first plan proposed broader forward declarations. Source review
  narrowed it to two classes and corrected the need to change the owning class
  attachment too. Snapshot model/context splitting is deferred because the models
  also require complete value types and pointerizing contexts would change lifetime.
- Diagnostic compiler tracing isolates module serialization, not algorithm bodies,
  as the dominant work in the snapshot interface. Reduced BMI is already the
  installed Clang 23 default; no compiler flag change is justified. The graph
  fence provides structural acceptance evidence. Disk headroom is about 4 GiB
  (existing BUG-195), but the existing BUILD-007 protocol uses disposable tmpfs
  storage, where about 20 GiB is available. Reuse that runner for matched clean,
  no-op and spatial-interface edit scenarios after the correctness gates settle.

## Review decisions
- Claude accepted the fixed attachment mechanics. Audited the entire wrapped
  cache span: only `SpatialIndexCache::Impl` and its owner's member definitions
  are present; all namespace helpers remain in the anonymous namespace outside.
  Repository-wide declarations/definitions (including tests/tooling) have one
  class owner each and only the new matching forward declarations.
- Both existing `WorldRegistry` and `ICommandContext` precedents use matching
  C++ linkage at their owners. No copied record or new binding implementation.
- In-tree users must rebuild because module attachment can change mangled names.
  The runtime build passed; all `IntrinsicTests` producers were reconciled,
  with clean cache-disabled runtime builds included in the matched benchmark.
- Added the explicit `<string_view>` include noted by Claude in the touched
  spatial cache interface; the final test build includes that correction.
- Clean-workshop rows 1–3 pass (permitted dependency direction, unchanged target
  links, no higher-layer exports); 4–6 n/a (no renderer/pass/recipe behavior);
  7 n/a (refactor, no capability promotion); 8 pass (no temporary exceptions).
  Fixed the predecessor retirement-log link when promoting this task.
- Benchmark protocol review: retain both raw samples per arm, not a generalized
  median-speed claim. Four ABBA samples share configure → clean → no-op →
  interface-edit ordering; deletion occurs only between complete samples.
  The runner already records actual compiler version, dependency content hashes,
  observed compiler counts, and fails on nonzero/ENOSPC. Clang 23 is the configured
  CI compiler, not a typo for the minimum supported Clang 20. Keep negative results;
  do not make a positive speedup/fan-out delta a harness precondition.

## Implementation verification
- Canonical Clang 23 `ci` configure and complete `IntrinsicTests` rebuild passed.
  Focused CTest: 335 passed. Full exclusion-only CPU gate: 4,640 passed, zero
  failures, one expected ASan-only GLFW lifecycle skip (4,641 selected).
- Strict layering, test layout, task policy and documentation-link checks pass;
  refreshed the unchanged 417-module inventory. Source synopsis audit reports
  zero errors on the three changed interfaces. Source and protocol resolutions
  reviewed with Claude, with no remaining blockers.
- Four production files changed, 1,274 → 1,284 physical lines (+10 for declaration
  and linkage boundaries). No new production file, module, service, state, facade
  or implementation is introduced. Device/cache implementations retain one owner.
- Matched measurement and final dependency-restoration reconciliation completed below.

## Completion
- Completed 2026-09-15. Commit reference: `ee647ec91b677fc1051be3b97405e3cd07b06aaf` for the engine change; the enclosing commit records measurement and retirement.
- Maturity: CPUContracted, the intended refactor endpoint. No GPU or sanitizer
  runtime capability is promoted.
- [C95 measurement report](../../ara/evidence/tables/runtime265_service_borrow_compile_measurement.md)
  retains two exact-source samples per arm: spatial-interface rebuilds
  84.131–84.456 → 39.179–39.477 seconds, 61 → 22 compiler units. No new source
  enters that rebuild. Full clean count stays 775; the small observed clean-time
  difference establishes no clean-build speedup. All results remain deliberately
  non-claim-eligible and scoped to the measured local Clang 23 host.
- BUG-197 corrected the benchmark's borrowed-dependency mutation. The complete
  cohort restarted with installation disabled and identity checks after every
  configure/build. The rejected attempt is retained and excluded in full.
- Claude's final review resolutions are complete: matching declaration attachment,
  full in-tree rebuild requirement, exact external runner provenance, explicit
  compiler scope and conservative clean-build interpretation. Nine scanner-level
  forbidden-import faults on real ci metadata are rejected; untouched metadata
  passes. No recompiled negative control is implied.
- After canonical dependency restoration: ci configure and complete IntrinsicTests
  build pass; full CPU gate has 4,640 passes, zero failures and one expected
  ASan-only skip (4,641 selected, 133.88 seconds). The 335 focused cases, 26
  tooling tests, four canonical results and strict structural checks pass.
- GRAPHICS-138 remains open with a source/compiler-backed renderer cleanup plan;
  this retirement does not complete all compilation or product convergence work.
