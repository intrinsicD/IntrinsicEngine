# Reuse backlog planning review — 2026-09-21

The operator requested a codebase duplication search, actionable tasks aimed at
smaller code and neutral/improved compilation, and agreement with Claude before
filing. Codex owned all writing; Claude Code reviewed repository source and task
proposals read-only through the operator's configured account. No engine code was
changed and no compile speedup was measured or claimed in this planning session.

Source inspected: `7bdffefb3` (initial checkout clean). Discovery scanned 1,504
C++/header/Python/shell/CMake files under src, methods, benchmarks, tests, tools,
cmake and .github, plus 126 shader files under assets. Exact-line clone candidates,
Python AST-body matches and symbol/caller searches were followed by contract checks;
this is a bounded audit, not a proof that all duplication is identified.

Claude session: `2689992a-470b-43c5-91d4-cf107139451e`. Review used the configured model;
CLI reported model identifiers: `claude-fable-5-1`.

## Consensus and exclusions

Four tasks are accepted after the final text corrections. Execution preference is
GEOM-099, RUNTIME-272, GEOIO-004, GRAPHICS-146; these are suggestions, not artificial
dependency edges. All remain unstarted.

The proposed RUNTIME-271 job lifecycle abstraction was rejected by both reviewers:
typed result delivery and family-specific cancellation/publication would move
repetition into callbacks. UI-037 already records the decision against a lifecycle
template. No task with that ID was created. LSCM sparse-builder consolidation is
already owned by GEOM-070. Resource classification versus resource lookup and
platform/backend state transitions have distinct contracts; lexical similarity
alone did not justify tasks. Existing GRAPHICS-105 and LEGACY-043 retain their
material/shader ownership; this pass does not duplicate them.

## Applied final corrections

- Every task retains failed measurements and explicitly rejects an implementation
  that exceeds its frozen compile-cost gate; inconclusive evidence leaves it open.
- GEOIO-004 has corrected source ranges, a geometry-only focused test selector and
  the skill synchronization command. Its ordinary compiled owner is backed by the
  existing CMake helper and non-module source precedent.
- GEOM-099 records the added Harmonic importer as a possible recurring cost and
  includes module-inventory and skill synchronization commands.
- RUNTIME-272 includes the fresh ci-derived Clang 20 configure/build/test recipe,
  skill synchronization, narrow fixture ownership and the ninth access-helper caller.
- GRAPHICS-146 measures IntrinsicTests to cover shared-mock consumers beyond the
  focused graphics executable; the frozen event table requires explicit review of
  any later expectation changes. Codex derived this table from all selected pass
  bodies and Graphics.CullingSystem.cpp::RecordOpaqueSurfaceBucket; Claude explicitly
  accepted that source inspection rather than claiming to have repeated it.

## Reviewed task bodies

Hashes bind the final agreed technical text after Claude's authorized wording and
command corrections, before appending each task's link back to this record.

| Task | Path | SHA-256 |
| --- | --- | --- |
| GEOIO-004 | `tasks/backlog/geometry/GEOIO-004-shared-compiled-ply-header-parser.md` | `cc68f63a85f14c67feec2fe97cebffdddedb52eb23fc9d1adb17211423774a2a` |
| GEOM-099 | `tasks/backlog/geometry/GEOM-099-shared-disk-topology-preflight.md` | `9983d8ae0ebd2b32e5e0a8cbdcee22e6d9f58dc51bc9fc208a47bb525e83e21d` |
| GRAPHICS-146 | `tasks/backlog/rendering/GRAPHICS-146-reuse-command-recording-test-double.md` | `d82c0da962d5e03fbb5665041b9983a408199c2277dd59eff88a3daeba6179f8` |
| RUNTIME-272 | `tasks/backlog/runtime/RUNTIME-272-compiled-property-domain-test-fixtures.md` | `c230663a7c78a1e497cb9af589d4e65c25dd9a2901baadc2adc739df70e0022e` |

## Claude final response

# Final review of the four task drafts

All four drafts are **AGREED** once the text fixes below are applied; none is BLOCKED. The fixes are wording and commands only and change no scope, so I do not need another review round.

## What I checked

These draft claims held at `7bdffefb3`:
- The three build targets and `EditorFeatureTestSupportObjs` exist in `tests/CMakeLists.txt`.
- All three contract IDs exist in the catalog.
- Every cited test name exists, including the `LSCM`, `GraphicsTestSupport` and `AssetImportRouter` suites.
- `CurvatureBoundaryGraph.cpp` is an ordinary private source at `src/geometry/CMakeLists.txt:165`.
- The runner's per-arm path dictionaries work as you described (`benchmark_compile_iteration.py:248-250`, and `probe_sources` at `:137-139`).
- `GEOM-070` and `GRAPHICS-105` exist where the drafts say.

I did not re-derive the GRAPHICS-146 event table from the pass bodies. I accept your inspection, and the draft already requires a recheck at implementation time.

## On the disagreement

You are right, and I withdraw the "one-time cost" framing. An edit to `Utils.cppm` is a cost every time that interface is touched, and it belongs under the same gate as everything else. One consequence of that gate is predictable and must appear in the draft (GEOM-099, fix 1).

## Must-fix for all four drafts

- **Gate-failure outcome.** Each draft says to stop on a regression but does not say what happens next. Add one sentence to each:
  - restore the source;
  - retain the raw runs and a negative record, following the precedent in `ara/evidence/diagnostics/mock_lifetime_rejected/`;
  - retire the task as rejected.
- The existing wording that "inconclusive keeps the task open" stays as it is.

## GEOIO-004: AGREED

Must-fix:
1. **Line ranges.**
   - The mesh `LoadPLY` is at `:1688-1820`; `LoadSTL` starts at `:1822`. The cloud `LoadPLY` is at `:1731-1863`.
   - The duplicated header loops are at `:1696-1809` and `:1739-1852`.
   - The draft's `1691-1835` and `1731-1867` run past the functions.
2. **Focused build and test mismatch.**
   - The focused build makes only `IntrinsicGeometryTests`, but the focused regex also selects `AssetImportRouter`.
   - That suite lives in `tests/unit/assets/` and appears to belong to another executable, so the focused build leaves its binary missing or stale.
   - Either add that target to the focused build or drop `AssetImportRouter` from the focused regex. The full gate covers it either way.
3. **Skill sync.** The acceptance criteria mention syncing the skill mirrors, but the verification block never runs `python3 tools/agents/sync_skills.py --write`. Add it.

## GEOM-099: AGREED

Must-fix:
1. **Harmonic becomes a Utils importer.**
   - Once `Harmonic.cpp` imports Utils, the after arm's interface-edit probe recompiles one extra sparse-solver translation unit.
   - I have not measured this, but it could push the compiler-duration sum past your 2% limit.
   - Say so up front and freeze the outcome before measuring. The gate-failure sentence above covers this.
   - Do not allow a relaxation of the limit after the results are in.
2. **Inventory and skill sync.** The acceptance criteria require both, but the verification block lacks the commands. Add:
   - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
   - `python3 tools/agents/sync_skills.py --write`

Optional: list `minimum_interface_importers` separately for each arm (31 before, 32 after), so the runner's fan-out assertion records the change.

## GRAPHICS-146: AGREED

Must-fix: only the shared gate-failure sentence.

Optional:
- **Header probe target.**
  - `MockRHI.hpp` has includers outside `IntrinsicGraphicsContractCpuTests`. The `RUNTIME-270` measurement was taken on the runtime-contract target.
  - By your own recurring-cost principle, the header-edit probe should run against a target that covers those includers, or `IntrinsicTests`.
  - If you keep the narrower target, the draft should state that fan-out outside it is unmeasured.
- **Frozen event table.** Record in the draft that the event table is frozen at `7bdffefb3`, and that any difference found at implementation time becomes a recorded edit to test expectations.

## RUNTIME-272: AGREED

Must-fix:
1. **Clang 20 build command.**
   - The acceptance criteria require a Clang 20 build of the producer and the runtime-contract target, but the verification block has no command for it.
   - Add the exact recipe, or cite the `BUG-205` one. That recipe is a fresh Null/headless configure derived from `ci` with Clang 20 set explicitly, followed by `--target IntrinsicRuntimeContractTests`.
2. **Skill sync.** Add `python3 tools/agents/sync_skills.py --write` to the verification block, for the same reason as GEOIO-004.

Optional: reword "Reuse the property-access helper in `Test.RegistrationDomains.cpp`". It reads as though the helper lives in that file. The intent is that `Test.RegistrationDomains.cpp` becomes the ninth consumer of the shared `Properties` helper while its topology setup stays untouched.

## After the fixes

After the shared fix and the per-draft items above, I agree to filing all four. My suggested execution order is GEOM-099, then RUNTIME-272, then GEOIO-004, then GRAPHICS-146. Dropping RUNTIME-271 is agreed and consistent with the decision already recorded in `UI-037`.
