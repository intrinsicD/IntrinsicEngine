---
id: RUNTIME-267
theme: F
depends_on: [RUNTIME-266]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive task; unattended workflow completion reports are exempt, but this task owns its benchmark manifests, results and source identities alongside review and test evidence.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality, runtime.processing-compilation-locality]
---
# RUNTIME-267 — Isolate processing config edits from unrelated editor consumers

## Goal
Shorten the remaining consolidation-config → service records/operations → editor
session rebuild chain without duplicating configuration, validation or service ownership.

## Scope and starting point
- Explicit operator-directed continuation of completed RUNTIME-264/265. Borrowed
  clustering/consolidation services already live in their Types owners, lifecycle
  modules retain binding, and exact property comparisons already share one helper.
- `Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationTypes.cppm`
  re-exports its Config owner; `Editor/Operations/Runtime.PointCloudServiceOperations.cppm`
  exposes Config and Types. Audit complete-value uses in the prepared frame and
  `Editor/internal/Runtime.EditorWorkspaceSession.cpp` before changing visibility.
- Use the current BUILD-009 config-interface probe. The historical 13-source,
  33.6-second result is not evidence of the remaining cost after later refactors.
- Reuse family prepared frames, canonical serialized configs and existing service
  lifetimes. Separate unrelated consumers only where the current dependency graph
  justifies it; do not add alternate DTO/config copies, serialization wrappers,
  a service registry, or reintroduce lifecycle-module imports.
- Follow `docs/architecture/sandbox-editor-feature-boundaries.md`. Preserve
  validation/apply parity, defaults and round-trips, selected property domains,
  backend reporting, subscriptions and stale/expired-command guards.
  RUNTIME-266 and RUNTIME-268 completed the broad snapshot/context changes.
  Freeze a new immediate-before config baseline on their integrated source;
  BUILD-009 timings are context, not the comparison arm.
  UI-037 retains readiness behavior ownership.

## Acceptance criteria
- [x] Identify exact remaining config-dependent consumers from fresh compiler
      metadata and classify mandatory by-value dependencies versus avoidable ones.
- [x] Implement a bounded ownership/import change using current canonical owners;
      review the plan and fixed diff with Claude and resolve findings.
- [x] Extend existing compiler-boundary checks with baseline-negative/final-positive
      evidence. Preserve service binding/completion/detach tests, config-file
      round-trips and apply-time validation; no UI/agent feature loss.
- [x] Pass focused and full CPU gates; if module attachment changes, verify fresh
      cache-off minimum-supported Clang producers and all in-tree callers.
- [x] Rerun the exact consolidation-config implementation/interface scenarios
      before and after on matched sources. Report actual compiler units and
      critical-path/elapsed results; retain a no-change verdict if all remaining
      dependencies are necessary or a proposed split adds cost.
- [x] Synchronize canonical editor-boundary docs and changed module inventory;
      retire with the comparison and explicit remaining gaps.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'EditorCompilationLocality|Consolidation|Clustering|SandboxEditorSessionLifecycle|RuntimeConfig|EngineConfigControl' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Refreshed baseline
BUILD-009 is complete. Use its [matched source comparison](../../ara/evidence/tables/build009_current_compile_measurement.md)
and retained producer/critical-path records; the old BUILD-007 costs are historical.
Freeze this task's immediate-before source before attributing its own changes.

RUNTIME-266 is complete at `08728e2e1`: the registry import cut is retained and
fully verified; its noisy two-sample timings imply no stable speedup. RUNTIME-268
is complete at `ca164c10c`: preserve the standard-declaration owner and exact
snapshot types. Its focused compile comparison does not measure this config
rebuild chain. Freeze a fresh config baseline for this task.

## Ownership audit — 2026-09-16
The post-268 config closure still includes the session because it owns typed
completion results containing config values. Its two subscriptions, cache
invalidation and epoch/unsubscribe ordering form one lifecycle; an opaque owner
would add allocation and forwarding. Domain and mesh panels also inherit the
config through PanelSupport's by-value family frame and consolidation helper
records. A header split alone cannot remove that frame's type dependency.
Changing it to a borrow of the current temporary Prepare*Frame arguments would
dangle. Resolve a concrete storage/lifetime design before claiming that split
isolates consumers. No implementation or no-change closure is claimed by this audit.

## Selected implementation — 2026-09-16
- Operator-directed continuation, immediate-before `be4066391`; one root writer,
  Claude reviewing fixed packets under standing source-sharing authorization.
- Canonical Clang metadata shows three avoidable app consumers: DomainPanels,
  MeshProcessingPanels and PanelSupport. Session and service operations retain
  necessary typed config/results dependencies; do not add a facade to hide them.
- The shell's existing prepared-frame storage now owns the service frame; the
  app context borrows it through a const pointer and rejects construction from
  frame temporaries via a non-const lvalue parameter. Context resets before
  prepared storage at draw completion and detach. The type's sole definition
  joins its existing globally attached family records.
- Move consolidation-specific declarations into one app-private header and
  definitions into existing MethodPanels.cpp. No added compiled file/module or
  DTO. Remove two duplicate availability flags; only MethodPanels reads the
  remaining clustering flag directly from its frame. A private empty-frame
  fallback shared by three concrete consumers preserves default-context behavior.
- Baseline-negative/final-positive compiler checks demonstrate that the three
  consumers no longer reach consolidation Config or PointCloudServiceOperations.
  Existing canonical validators, service callbacks/epochs and typed results remain.
- Claude approved the design subject to lifetime order, complete caller builds
  and real target accounting. Verify fresh Clang20 app/runtime closure. Named
  free-function return types are not generally encoded in their mangled name;
  do not repeat the review's blanket ABI statement. Proper dependency-driven
  Clang23 rebuild plus fresh Clang20 verification is the required gate.
- Freeze matched editor-target config implementation/interface probes only after
  final source verification. Runtime-only probes cannot measure the app cut;
  report actual counts and per-producer costs, including relocated method helpers.

## Source verification checkpoint
- Canonical ci configure and IntrinsicTests build pass with Clang23. Focused
  CPU: 158 passes (14.33 s). Full CPU: 4,666 passed, one expected ASan-only
  lifecycle skip, zero failures out of 4,667 selected (150.51 s). Strengthened
  valid-request/null-context coverage was rebuilt and all nine consolidation/
  context-draw cases passed afterward (0.51 s); production source unchanged.
- Fresh cache-off Clang20 Null/headless editor/runtime closure plus all three
  actual caller test objects passed, with a final no-op reconciliation. This is
  minimum-compiler build evidence, not Clang20 test execution. No GPU execution
  or sanitizer claim. Strict layering/task/docs checks pass; regenerated module
  inventory remains 419 modules with no serialized inventory difference.
- Source accounting: +34 net C++ lines, one private declaration header, one
  CMake header entry, no new compiled file/module/state owner or layer edge.
  This slice isolates compilation and removes duplicate availability fields;
  it is not a source-line reduction claim.
- Claude reviewed plan/fixed diff/complete linkage and guard context; findings
  resolved against source. Matched config editor-target comparison remains
  necessary before retirement.

## Completion — 2026-09-16
Retired at CPUContracted, the intended compile-refactor endpoint. Source commit:
`6fb0807f49a8d660d820470e82613c1ad1a9c424`; accompanying evidence/retirement commit
binds the six retained samples and Claude reviews. See the
[matched comparison](../../ara/evidence/tables/runtime267_processing_config_measurement.md):
local config-interface editor-target median 34.245 → 27.372 s (20.1% lower),
19 → 16 compilers. Receiving MethodPanels cost rises 8.132 → 8.238 s; included
in target timing. Implementation/no-op ranges overlap; no improvement claimed.
All records remain claim_eligible:false; C102 records only these bounded observations.
Full CPU and fresh minimum-compiler evidence are recorded above. No deferred work
inside this bounded task. Mandatory typed config/session consumers remain;
UI-037, GRAPHICS-105, LEGACY-043 and BUILD-006 retain their independent scopes.
BUG-199 fixes the observed CMake metadata-accounting defect; both rejected attempts
remain archived, outside the six accepted samples. No GPU/sanitizer execution claim.
