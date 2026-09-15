---
id: RUNTIME-266
theme: F
depends_on: [BUILD-009]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive task; unattended workflow completion reports are exempt, but this task owns its benchmark manifests, results and source identities alongside review and test evidence.
contract_schema: 1
contracts: [repo.source-documentation, runtime.editor-prepared-frame-locality]
---
# RUNTIME-266 — Narrow remaining editor snapshot consumers

## Goal
Reduce the remaining scene-editing → workspace-snapshot → context-adapter
compile cost while retaining one canonical implementation of every record and adapter.

## Scope and starting point
- Explicit operator-directed continuation; RUNTIME-265 is complete. Its borrowed
  device/spatial-cache declarations and compiler fences are the starting point,
  not work to repeat. BUILD-009 supplies the refreshed cost ranking.
- Inspect `Runtime.EditorWorkspaceSnapshots.cppm`,
  `internal/Runtime.EditorFeatureContextAdapters.cpp`,
  `internal/Runtime.EditorWorkspaceSession.cpp`, and
  `Operations/Runtime.SceneEditingOperations.cppm` under `src/runtime/Editor/`.
- Follow `docs/architecture/sandbox-editor-feature-boundaries.md`: family-owned
  prepared frames, existing shared context adapters, sole record definitions,
  cached processing context, attachment epochs and visitor-bounded borrows.
- Audit consumers needing complete values versus pointer/reference borrows.
  Compare narrower existing record owners and unused-import removal first.
  A new split or private implementation needs a present consumer and a measured
  benefit. Do not copy records, introduce a generic facade or pointerize contexts
  in a way that changes lifetime. UI-037 owns readiness semantics.
- Scope is the snapshot/context side; RUNTIME-267 owns config/service records.
  RUNTIME-267 follows this task for their shared session files and freezes its
  own immediate-before config baseline after this change lands.

## Acceptance criteria
- [x] Use BUILD-009's current graph to select and document a bounded consumer
      group and exact expensive dependency; distinguish serialization from body work.
- [x] Implement the smallest beneficial change with Claude plan/fixed-diff review;
      retain caching, attachment/detach behavior, command validity and all current
      UI/config/agent workflows. No backward-compatibility wrappers are needed.
- [x] Guard the selected dependency boundary using the existing compiler-test
      helper; original metadata fails and final metadata passes. Preserve all
      existing boundaries and exercise expired-frame/query consumers.
- [x] Run focused and full CPU verification. If named-module attachment changes,
      rebuild all in-tree users and verify affected producers with fresh cache-off
      minimum-supported Clang; declare any GPU/sanitizer evidence separately.
- [x] Use the existing benchmark runner for matched before/after snapshot and
      scene-edit probes. Report source/file/dependency costs and timing separately;
      reject a harmful split, or close with an explicit no-change verdict supported
      by the experiment. Do not relabel the broader editor area as finished.
- [x] Update the canonical editor-boundary documentation and module inventory
      when changed; retire the task with review/test/measurement references.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'EditorCompilationLocality|SandboxEditor|SceneEditing|RuntimeConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Implementation experiment — 2026-09-15
- BUILD-009's source-identical current arm is frozen at `45d5a4f1f`; its repeated
  measurement runs in a separate checkout. Prepare this patch while that run
  completes; no competing compilation or testing during timed samples.
- The snapshot interface reaches 92 named modules. Ten interfaces on that graph
  import `ECS.Scene.Registry` but need only its pointer/reference type; that
  owner is the closure's sole full EnTT registry include. Reuse the existing
  C++-linkage borrow pattern from RUNTIME-265 and the sole registry definition.
- Claude reviewed all ten interfaces, including WorldRegistry's existing
  `unique_ptr` member and out-of-line lifecycle. Change owner attachment and
  borrow declarations together; preserve storage and behavior. Remove the
  unused GeometrySources import from the scene-editing interface. No new
  wrapper, source file, allocation or alternate record definition is needed.
- The original compiler metadata rejects the proposed boundary for all eleven
  checked consumers. Canonical compilation, fixed-diff review and CPU verification now pass;
  minimum-Clang proof also passes. The completed matched measurements below
  support no stable timing-gain claim; the compiler boundary itself improves.
- Fixed-diff review found no blocking linkage/lifetime defect. Resolve its
  completeness questions through source inspection: concrete scene serialization,
  refinement, world management and operation-action units already import the
  registry; the four newly explicit imports cover the remaining dereferences.
  Context/debug/frame-only units require no complete registry. GeometrySources
  was unused in the scene-editing interface; report the combined patch rather
  than attributing every timing change to a single import. The untouched snapshot
  interface is deliberately a transitive regression consumer in the new guard.

## Refreshed baseline
BUILD-009 is complete. Use its [matched source comparison](../../ara/evidence/tables/build009_current_compile_measurement.md)
and retained producer/critical-path records; the old BUILD-007 costs are historical.
Freeze this task's immediate-before source before attributing its own changes.

## Verification progress
- Canonical `ci` configure and complete IntrinsicTests rebuild pass on Clang23.
  Focused: 408 passes. Full CPU gate: 4,663 passes, zero failures, one expected
  ASan-only leak-control skip (4,664 selected; 141.31 seconds).
- New compiler guard passes for all eleven producers; all existing compiler
  boundaries pass in the focused/full gates. Before graph: 92 dependencies;
  after: 91, with exactly `ECS.Scene.Registry` removed. No timing inference yet.
- Minimum Clang20 uses a fresh cache-off Null/headless build. Enable test
  configuration to expose the Sandbox editor-library target in headless mode;
  build that library and its full engine prerequisites. Earlier executable and
  tests-disabled app-target requests selected no target and compiled nothing.
- Reuse the existing timing runner and BUILD-009 settings for two samples per
  arm in ABBA order: clean, no-op, workspace implementation, snapshot interface,
  scene-editing interface. Freeze the after revision following verification.

- Fresh cache-off Clang20 `ExtrinsicSandboxEditor` build passes, including all
  engine prerequisites and app editor modules. All eleven new boundary producers
  pass against that fresh compiler metadata. No Clang20 tests or GPU execution
  are claimed; CPU runtime verification above uses canonical Clang23.

## Completion — 2026-09-15
- CPUContracted, the intended refactor endpoint. Commit reference: implementation
  `08728e2e1`; frozen timing protocol `cc3a8ea86`.
- [Report](../../ara/evidence/tables/runtime266_registry_borrow_measurement.md) and
  [evidence index](../../ara/evidence/diagnostics/runtime266_registry_borrows/evidence-index.json)
  retain all four canonical samples, exact source/command/dependency identities,
  compiler guards, CPU/minimum-compiler verification and Claude reviews.
- Retain the dependency cut with no stable timing-gain claim; the slower final
  baseline sample limits the two-sample medians. C98 owns these bounded
  observations and the separate diagnostic trace/control, not a general speedup.
- Net production change is +29 declaration/comment lines; no new production
  file, module, allocation, duplicate implementation or compatibility path.
- Trace and synthetic control implicate snapshot declaration shape and reachable
  types in BMI serialization. RUNTIME-268 owns isolating that dominant cost;
  RUNTIME-267 owns config coupling. Broader editor cleanup remains open.

## Clean-workshop review
`tools/ci/run_clean_workshop_review.sh . --strict` passes. Manual review of the
fixed diff finds no new dependency edge, duplicated owner or lifetime change.

| Row | Result | Evidence |
|---|---|---|
| 1: layer imports | pass | Strict layering check; the patch removes imports. |
| 2: CMake links | pass | No target-link changes. |
| 3: public type direction | pass | Same ECS registry owner; runtime declarations borrow that lower-layer type. |
| 4: renderer ownership | n/a | No renderer state or subsystem change. |
| 5: typed pass identity | n/a | No frame-pass change. |
| 6: recipe dependencies | n/a | No recipe or ordering change. |
| 7: maturity closure | pass | CPUContracted endpoint verified; RUNTIME-268 retains the broader serialization work. |
| 8: exceptions | pass | No new exception; strict allowlist check passes. |
