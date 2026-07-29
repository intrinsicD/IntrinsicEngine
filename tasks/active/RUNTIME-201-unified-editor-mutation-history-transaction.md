---
id: RUNTIME-201
theme: F
depends_on: [RUNTIME-192, RUNTIME-193, RUNTIME-194]
maturity_target: Retired
---
# RUNTIME-201 — Unified editor mutation and history transaction

## Goal

- Route every undoable runtime entity/geometry edit through one internal
  generation-validated mutation transaction and `EditorCommandHistory`, then
  delete feature-specific history builders, `GizmoUndoStack`, and the unused
  parallel CommandBus inverse-history hook.

## Non-goals

- No database transaction framework, reflection serializer, universal command
  hierarchy, or undo of non-editor runtime lifecycle.
- No app-owned state snapshot or UI-private mutation path.
- No change to algorithm mathematics; feature owners still prepare typed
  before/after state.

## Context

- Status: in progress; owner `Codex`; branch
  `codex/runtime-201-unified-editor-mutation`; Slice A is complete and Slice B
  feature adoption is next. The next verification step is the first
  owner-scoped adoption contract plus the focused
  `EditorCommandHistory|Mutation` run.
- `EditorCommandHistory` is already the durable editor undo/redo owner but also
  contains feature-specific builders. Geometry/method facades duplicate
  snapshot/validate/apply/dirty/history rules, gizmo drag commits use a
  separate `GizmoUndoStack`, and `CommandBus::SetHistoryHook` /
  `RecordInverse` have tests but no production history consumer.
- The common behavior is small: capture typed before state, compute/receive
  typed after state, revalidate entity/source generation, apply atomically,
  stamp dirty generations, and record one deterministic undo/redo entry.
- Asynchronous work remains on `JobService`; only current completions may enter
  the mutation transaction.

## Right-sizing

- **Element:** the common mutation transaction shape triggers the
  interface/service and plumbing-ratio heuristics if modeled as another public
  runtime service or module.
- **Simpler alternative:** keep one include-only internal function template
  beside `EditorCommandHistory`; feature owners supply typed identity,
  generation, state, validation, atomic-apply, and dirty-stamp callbacks.
- **Blast radius:** Slice A touches only the internal helper and focused runtime
  contracts. Later feature adoption is owner-by-owner; import/include census
  and `check_layering.py` guard every slice.
- **Reintroduction trigger:** add a public abstraction only when a present
  second history backend, cross-layer caller, or independently replaceable
  transaction implementation requires one.

## Slice plan

- **Slice A — transaction helper (complete 2026-07-29).** Add an
  implementation/internal
  `ExecuteUndoableEntityMutation<TState>` shape and focused atomicity/staleness
  contracts without a new public service.
- **Slice B — feature adoption.** Migrate transform/gizmo, geometry,
  visualization/presentation, registration, clustering, parameterization,
  import postprocess, and destructive conversion commits.
- **Slice C — cleanup.** Move specialized builders to feature owners and
  delete `GizmoUndoStack`, CommandBus inverse-history API, duplicate apply
  paths, and compatibility tests after all real workflows use history.

## Required changes

- [x] Define one internal mutation helper taking stable entity/world identity,
      expected generations, typed before/after state, pure validation, atomic
      apply, dirty stamping, and deterministic undo/redo callbacks.
- [ ] Fail closed without mutation/history on stale entity, source/property/
      presentation generation, validation failure, cancelled work, or failed
      apply.
- [ ] Keep feature-specific state capture and restoration code with the owning
      feature; `EditorCommandHistory` stores generic records only.
- [ ] Route gizmo drag commit and keyboard/property transform edits through the
      same history owner and preserve drag coalescing semantics.
- [ ] Migrate geometry/property, presentation/visualization, registration,
      clustering/method, parameterization, import postprocess, and destructive
      domain-conversion mutations.
- [ ] Delete `GizmoUndoStack`, feature-specific builders from
      `EditorCommandHistory`, and `CommandBus::SetHistoryHook` /
      `RecordInverse` after production adoption and parity.

## Tests

- [x] Generic contracts cover success, failed validation, stale generation,
      apply failure, exact one-entry commit, deterministic undo/redo, and no
      partial mutation.
- [ ] Feature matrix covers transform/gizmo, topology/property,
      presentation/material, async method completion, import enrichment, and
      domain conversion through the same helper.
- [ ] Gizmo drag coalescing and exact transform restoration remain covered
      through `EditorCommandHistory`.
- [ ] Structural tests prove no second undo stack, CommandBus history hook, or
      feature-owned parallel history path remains.

## Docs

- [ ] Update editor/runtime command-history docs with transaction ownership,
      stale completion, and feature-specific state responsibilities.
- [ ] Regenerate module inventory if public surfaces are removed.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Every undoable production edit commits exactly once through
      `EditorCommandHistory` using the same generation-validated transaction
      shape.
- [ ] UI and agent callers submit the same typed operation; neither owns
      snapshots or bypasses runtime validation.
- [ ] `GizmoUndoStack`, CommandBus inverse history, and specialized history
      builders are deleted after behavior parity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|Gizmo|Undo|Redo|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^EditorCommandHistory\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

## Forbidden changes

- A second history service/stack, app-owned snapshot, or generic serialized
  object graph.
- Applying an async result before generation validation.
- Deleting specialized paths before their exact undo/redo parity tests use the
  common transaction.

## Maturity

- Target: `Retired`; common atomicity contracts and all real workflow
  migrations must pass before the parallel history mechanisms are removed.
- Slice A establishes only the internal transaction contract; production
  feature adoption and all parallel-history removals remain open.
