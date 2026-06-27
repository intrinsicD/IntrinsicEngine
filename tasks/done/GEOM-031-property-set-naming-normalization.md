---
id: GEOM-031
theme: none
depends_on: []
---
# GEOM-031 — Property set naming normalization

## Goal
- Normalize the public `PropertySet` shrink-to-fit API to the promoted
  geometry `PascalCase` style while preserving source compatibility during the
  migration.

## Non-goals
- No semantic changes to property storage compaction or registry lifetime.
- No const lookup migration; `GEOM-030` owns that.
- No erased property metadata catalog; `GEOM-033` owns that.
- No broad namespace, module, or file moves.

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Migrate geometry property and topology utilities`).
- Maturity: `CPUContracted`.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `Geometry.Properties` exposes `PropertySet::Shrink_to_fit()`, which conflicts
  with the promoted geometry API style documented in
  [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md).
- The inconsistency is already recorded in the 2026-05-12 geometry gap
  analysis; this task isolates the compatibility cleanup from behavior changes.

## Required changes
- [x] Add `PropertySet::ShrinkToFit()` as the canonical public spelling.
- [x] Keep `PropertySet::Shrink_to_fit()` as a temporary compatibility wrapper
      unless the audit proves there are no in-repo or documented compatibility
      obligations.
- [x] Migrate in-repo call sites and docs to `ShrinkToFit()`.
- [x] Keep storage compaction behavior unchanged and covered by existing tests.
- [x] If the compatibility wrapper remains, leave a clear source comment naming
      this task and the condition for later removal.

## Tests
- [x] Add or update `unit;geometry` coverage that calls `ShrinkToFit()`.
- [x] Keep or add coverage proving property values survive shrink-to-fit for
      multiple property types.
- [x] Build `IntrinsicTests` with the `ci` preset to catch exported module
      signature or call-site issues.

## Docs
- [x] Update
      [`docs/architecture/geometry-api-style.md`](../../docs/architecture/geometry-api-style.md)
      only if it needs a property-specific compatibility note.
- [x] Update
      [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md)
      if it names the shrink-to-fit API.
- [x] Regenerate
      [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md)
      because this changes the exported module surface.

## Acceptance criteria
- [x] `ShrinkToFit()` is the canonical spelling used by new code and docs.
- [x] Any remaining `Shrink_to_fit()` wrapper is clearly marked as
      compatibility debt, not the preferred API.
- [x] Property compaction behavior is unchanged.
- [x] The change preserves `geometry -> core` layering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryProperties|Properties' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing property storage layout or compaction semantics in this task.
- Removing a compatibility wrapper without an explicit call-site audit.

## Maturity
- Target: `CPUContracted` (API spelling cleanup with CPU tests).
- No `Operational` follow-up is owed; this task has no backend seam.
