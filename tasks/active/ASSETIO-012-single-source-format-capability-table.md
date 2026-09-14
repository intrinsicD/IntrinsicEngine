---
id: ASSETIO-012
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive operator-directed reuse and routing repair; fixed source review, CPU integration tests and explicit capability limits.
contract_schema: 1
contracts: [repo.source-documentation, repo.task-contract-discovery, io.geometry-format-capabilities]
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
- [x] Decide and record how the two tables are unified without breaking the
      `assets → core` / `geometry → core` layering rule.
- [x] Make the import-router table derived from, or validated against, the
      geometry IO table.
- [x] Resolve the five concrete disagreements above (add `pwn`/`csv`/`3d`/`txt`
      routing, settle OFF exportability).
- [x] Add a check that fails when the tables disagree, so future drift is caught.

## Tests
- [x] Add a contract/regression test asserting every geometry IO importable
      format is routable, and that export capability agrees between layers.
- [x] Add a test asserting a `pwn`/`csv`/`3d`/`txt` path resolves to the
      point-cloud payload.
- [x] Default CPU gate stays green.

## Docs
- [x] Update the geometry IO and asset routing docs to name the single source of
      truth.
- [x] Update `src/app/Sandbox/README.md` if the set of importable formats
      changes.

## Acceptance criteria
- [x] There is one authoritative format capability declaration.
- [x] The five listed disagreements are resolved.
- [x] A drift between layers fails a check rather than shipping silently.
- [x] No layering violation is introduced by the unification.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'AssetFormatCapabilities|AssetWorkflowModule|AssetImportRouter|GeometryIO|RuntimeAssetImportFormatCoverage' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Making `assets` import `geometry` to share the table.
- Resolving the drift by deleting importer capability that already works.

## Current plan — 2026-09-14
Operator requests continuing cleanup/open points with Claude. This existing
Theme J duplication blocks REVIEW-004 and is the next bounded reuse slice.
The code still has the five recorded disagreements and an additional `edgelist`
alias mismatch. Runtime also lacks dispatch cases for the four missing formats.
Reuse each existing strict loader; preserve current PTS/XYZ/XYZRGB behavior.

The core-owned `Core.GeometryFormatCatalog.inc` is the selected single owner.
Both layers retain their distinct typed domains and asset-only formats. Avoid a
new catalog service/module or adapters that merely translate a third type system.
Move nontrivial geometry lookup bodies into the matching implementation unit.
New files must pay for two real data consumers or implementation locality.
Root owns documentation and gates; Claude reviews only bounded source snapshots.
No renewed sharing of internal retirement-note batches is needed.

CPU integration checks properties, geometry residency and selectable tags.
This task does not add an export UI; UI-046 remains the owner. A new pixel-level
claim requires an actual Vulkan readback run; CPU materialization alone will not
be described as a rendered-frame proof.

Claude source review confirms the data-only include is reachable through existing
core include directories. A neutral typed catalog would add a third projection
or unnecessary public API migration. No new core module/BMI is introduced.
`WriteOFF` is implemented and tested, so OFF mesh export remains supported and
the asset router correction makes its metadata agree. The edgelist alias is
shared in both tables. Runtime export UI remains out of scope.

## Review — 2026-09-14
Claude implemented the seven production files in temporary copies; the checkout
writer added tests and reconciled the source. Its first implementation attempt
spent the bounded tool budget on discovery without editing. Narrowing the
assignment to production files completed the next attempt. A fresh read-only
Claude review consumed a fixed ten-file source/test snapshot and found no
blocking defect. The snapshot and transcript are under
`build/analysis/assetio012-format-catalog-2026-09-14/` after final verification.

Review notes were resolved by inspecting current consumers: `SourceFormat` has
no numeric persistence path; query callers link geometry; no constant-expression
callers exist. Removing `constexpr` is intentional implementation locality.
No compatibility aliases are needed under the operator's explicit API policy.
The optional `NoPayloads` naming alias was declined: the local empty array
already serves both table expansions and an alias would add no behavior.

Scope is one catalog/routing repair, with no parser or algorithm replacement.
The core data declaration has two present consumers, no runtime indirection and
no new module, target or dependency edge. Runtime owns loader selection; source
arrays have static lifetime and public spans remain read-only. Config/UI paths
continue through the existing router and import recipe. No new tuning axis,
shader, frame recipe, lifetime or concurrency mechanism is introduced.

Clean-workshop rows 1–3 and 8 pass (layering/links/public types/no exceptions);
rows 4–6 are not applicable (renderer/passes/recipe dependencies unchanged).
Row 7 closes at CPUContracted for this metadata/routing scope; UI-046 owns
export UI and ASSETIO-011 owns broader real-widget import coverage. No new
Vulkan-rendering, compatibility, or compile-performance claim is made.

Verification setup correction: the first native link omitted `Geometry.IO.cpp`
because `ci` had been configured before the temporary proposal was copied in
with preserved file timestamps. Re-running `cmake --preset ci` regenerated the
producer list; the freshly configured sanitizer trees already included it.
The native link is rerun against the corrected generation. The initial test
compile also needed an explicit `VisualizationConfig` import. Neither failure
was hidden by changing a test expectation or weakening a gate.

The first focused post-change run passed 271/272 tests. The remaining new test
incorrectly requested `v:color`; `Cloud::EnableColors` declares `p:color` and
`PopulateFromCloud` preserves its property set. Corrected the assertion to the
existing property name, preserving the same cardinality and RGBA value checks;
no color alias or materialization workaround was added.
A bounded Claude follow-up confirmed the color assertion correction retains the
exact value checks and exposes no dropped payload. A uniform-prefix migration
is not required: canonical property references carry an explicit element domain
and preserve arbitrary authored names. This slice adds no compatibility alias.

## Verification result — 2026-09-14
Maturity: CPUContracted, the intended endpoint for the shared capability and
runtime routing contract. All acceptance criteria are satisfied. Export UI
remains UI-046; broader real-widget import coverage remains ASSETIO-011.

- Clang 23 preset `ci`: built `IntrinsicTests`; focused native 272/272 passed.
- Full CPU selector: 4,605 passed, six skipped, zero failures (4,611 cases).
  All five display-dependent skips then passed with host access. The remaining
  native skip is the explicit ASan-only `GlfwLifecycleLsan` control.
- Fresh configured `ci-asan` and `ci-ubsan`: built `IntrinsicAssetUnitTests`,
  `IntrinsicGeometryIoTests`, `IntrinsicRuntimeContractTests`. Both unchanged
  focused CPU selectors passed 272/272 serially; no sanitizer finding.
- The ASan sandbox discovery failure is recorded under BUG-188. Host retry
  preserved sanitizer settings and the exclusion selector.
- Strict task/layering/allowlist/docs/test-layout/root-hygiene checks pass.
  Module inventory remains 418; skill mirrors are current. Selected source-doc
  audit has no objective error; its long-synopsis heuristic was reviewed.
- Source review hashes and all command/exit records, including failed setup and
  regression attempts, are archived under
  `build/analysis/assetio012-format-catalog-2026-09-14/`.

The sanitizer command uses `--test-dir build/ci-asan` or `build/ci-ubsan`,
`-R 'AssetFormatCapabilities|AssetWorkflowModule|AssetImportRouter|GeometryIO|RuntimeAssetImportFormatCoverage'`,
`-LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1`.
No full sanitizer-suite or new GPU-rendering result is claimed for this slice.
