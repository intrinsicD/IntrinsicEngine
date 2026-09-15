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
- [ ] Use BUILD-009's current graph to select and document a bounded consumer
      group and exact expensive dependency; distinguish serialization from body work.
- [ ] Implement the smallest beneficial change with Claude plan/fixed-diff review;
      retain caching, attachment/detach behavior, command validity and all current
      UI/config/agent workflows. No backward-compatibility wrappers are needed.
- [ ] Guard the selected dependency boundary using the existing compiler-test
      helper; original metadata fails and final metadata passes. Preserve all
      existing boundaries and exercise expired-frame/query consumers.
- [ ] Run focused and full CPU verification. If named-module attachment changes,
      rebuild all in-tree users and verify affected producers with fresh cache-off
      minimum-supported Clang; declare any GPU/sanitizer evidence separately.
- [ ] Use the existing benchmark runner for matched before/after snapshot and
      scene-edit probes. Report source/file/dependency costs and timing separately;
      reject a harmful split, or close with an explicit no-change verdict supported
      by the experiment. Do not relabel the broader editor area as finished.
- [ ] Update the canonical editor-boundary documentation and module inventory
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
