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
- [ ] Identify exact remaining config-dependent consumers from fresh compiler
      metadata and classify mandatory by-value dependencies versus avoidable ones.
- [ ] Implement a bounded ownership/import change using current canonical owners;
      review the plan and fixed diff with Claude and resolve findings.
- [ ] Extend existing compiler-boundary checks with baseline-negative/final-positive
      evidence. Preserve service binding/completion/detach tests, config-file
      round-trips and apply-time validation; no UI/agent feature loss.
- [ ] Pass focused and full CPU gates; if module attachment changes, verify fresh
      cache-off minimum-supported Clang producers and all in-tree callers.
- [ ] Rerun the exact consolidation-config implementation/interface scenarios
      before and after on matched sources. Report actual compiler units and
      critical-path/elapsed results; retain a no-change verdict if all remaining
      dependencies are necessary or a proposed split adds cost.
- [ ] Synchronize canonical editor-boundary docs and changed module inventory;
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
BUILD-009 is complete. Use its [matched source comparison](../../../ara/evidence/tables/build009_current_compile_measurement.md)
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
