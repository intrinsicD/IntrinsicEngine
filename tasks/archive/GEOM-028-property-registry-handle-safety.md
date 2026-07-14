---
id: GEOM-028
theme: none
depends_on: []
---
# GEOM-028 — Property registry handle safety

## Goal
- Make `Geometry.Properties` registry access reject invalid, removed, or
  mismatched property handles deterministically instead of relying on unchecked
  vector indexing.

## Non-goals
- No property metadata catalog; `GEOM-033` owns erased descriptors.
- No const lookup migration; `GEOM-030` owns the `const_cast` removal.
- No change to mesh, graph, or point-cloud topology ownership.
- No renderer/runtime/ECS/assets/platform/app integration.

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Migrate geometry property and topology utilities`).
- Maturity: `CPUContracted`.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- The implementation-side registry lookup currently indexes storage directly
  from `PropertyId`, so invalid IDs can turn into undefined behavior before the
  typed lookup has a chance to fail closed.
- Several geometry algorithms pass property handles across borrowed views; the
  registry should make stale or wrong-type handles safe to test and diagnose.
- Reuse existing `PropertyId`, `PropertyStorageBase`, and typed storage helpers;
  do not introduce a second handle system.

## Required changes
- [x] Audit `PropertyRegistry::Storage`, typed `Storage<T>`, remove, contains,
      and property enumeration paths for unchecked `PropertyId` indexing.
- [x] Add one shared internal validity check for registry IDs so invalid-handle
      behavior is not duplicated across methods.
- [x] Define deterministic behavior for invalid IDs, removed IDs, and
      wrong-type IDs; prefer null/invalid property handles for lookup-style APIs.
- [x] Preserve existing behavior for valid mutable and const property handles.
- [x] Keep diagnostics local to geometry; do not add runtime logging or global
      error sinks.

## Tests
- [x] Add `unit;geometry` tests for invalid `PropertyId` lookup.
- [x] Add tests for lookup after removal or storage compaction, whichever paths
      exist in the implementation.
- [x] Add tests proving wrong-type typed lookup fails without corrupting or
      mutating the property set.
- [x] Add tests that valid IDs still retrieve the expected property after
      adding several unrelated properties.

## Docs
- [x] Update
      [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md)
      with the invalid-handle behavior expected of property APIs.
- [x] Update
      [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md)
      if it documents property handle or borrowed-view validity.
- [x] Regenerate
      [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md)
      only if the exported module surface changes.

## Acceptance criteria
- [x] Invalid, removed, and wrong-type property handles fail closed in focused
      tests.
- [x] The implementation has one clear validity helper or equivalent local
      path, not repeated ad hoc bounds checks.
- [x] Valid property lookup behavior remains source-compatible.
- [x] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|Properties' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Replacing `PropertyId` with a new handle family in this task.
- Adding higher-layer dependencies from `src/geometry`.

## Maturity
- Target: `CPUContracted` (registry safety contract with CPU tests).
- No `Operational` follow-up is owed; this task has no backend seam.
