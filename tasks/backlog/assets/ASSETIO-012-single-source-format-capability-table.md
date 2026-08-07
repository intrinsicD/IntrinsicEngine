---
id: ASSETIO-012
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Format capability metadata and its single-source derivation. No geometry
  element-domain source, property, support-radius, parameterization, or
  method-integration behavior changes.
---
# ASSETIO-012 — Two hand-maintained format capability tables have already drifted

## Goal
- Derive the asset import-router format capability table from the geometry IO
  format table (or a shared declaration), so import/export capability cannot
  disagree between layers.

## Non-goals
- No new importer or exporter implementation.
- No change to the routing, payload-hint, or ambiguity-resolution behavior for
  formats both tables already agree on.
- No file-format detection-by-content work.

## Context
- Two independent, hand-maintained tables describe the same thing:
  - `Geometry.IO.cppm:76-90` — 14 formats, `{Kind, name, aliases, importDomains,
    exportDomains, binary, ...}`.
  - `Asset.ImportRouter.cpp:47-66` — 18 formats, same shape plus texture and
    glTF entries.
- They have already drifted:

  | format | `Geometry.IO` | `Asset.ImportRouter` |
  | --- | --- | --- |
  | `pwn` | point-cloud import supported | **absent** |
  | `csv` | point-cloud import supported | **absent** |
  | `3d` | point-cloud import supported | **absent** |
  | `txt` | point-cloud import supported | **absent** |
  | `off` | export `MeshOnly` | export `NoPayloads` |

- Consequence: four point-cloud formats the geometry layer can read cannot be
  imported from the Sandbox at all, because the import router does not know they
  exist. And OFF exportability is claimed in one table and denied in the other,
  which will matter as soon as `UI-046` exposes export.
- The drift is silent: nothing cross-checks the two tables, so each new format
  must be added twice correctly or capability quietly diverges again.
- Owner: `assets` owns routing; `geometry` owns the IO capability facts.
  `assets` may depend on `core` only, so the shared declaration must live
  somewhere both can reach without violating layering — resolve this explicitly
  (a `core`-level declaration, a generated header, or a build-time check) and
  record the choice in `Context`.
- Impact: medium now, higher once export ships. This is the kind of duplication
  that produces "the engine supports it but the app cannot reach it" bugs.

## Required changes
- [ ] Decide and record how the two tables are unified without breaking the
      `assets → core` / `geometry → core` layering rule.
- [ ] Make the import-router table derived from, or validated against, the
      geometry IO table.
- [ ] Resolve the five concrete disagreements above (add `pwn`/`csv`/`3d`/`txt`
      routing, settle OFF exportability).
- [ ] Add a check that fails when the tables disagree, so future drift is caught.

## Tests
- [ ] Add a contract/regression test asserting every geometry IO importable
      format is routable, and that export capability agrees between layers.
- [ ] Add a test asserting a `pwn`/`csv`/`3d`/`txt` path resolves to the
      point-cloud payload.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update the geometry IO and asset routing docs to name the single source of
      truth.
- [ ] Update `src/app/Sandbox/README.md` if the set of importable formats
      changes.

## Acceptance criteria
- [ ] There is one authoritative format capability declaration.
- [ ] The five listed disagreements are resolved.
- [ ] A drift between layers fails a check rather than shipping silently.
- [ ] No layering violation is introduced by the unification.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'ImportRouter|GeometryIo|AssetRouting' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Making `assets` import `geometry` to share the table.
- Resolving the drift by deleting importer capability that already works.
