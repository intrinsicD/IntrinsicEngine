---
id: CI-010
theme: none
depends_on:
  - CI-007
  - BUG-063
---
# CI-010 — Collapse per-case process spawns and reintroduce the shared engine fixture

## Goal
- Shrink the test phase itself (not the build ahead of it) by amortizing the ~3,500 per-case Debug+ASan process spawns via the dormant `intrinsic_grouped_test` helper, then re-sharing the ~150 per-`TEST` full-engine boots through a per-process fixture — the pattern CI-001 measured at ≥3x on this exact hot path.

## Non-goals
- Do not merge test executables into one binary, delete assertions, or weaken `GTEST_SKIP` paths (CI-001 forbidden list, verbatim).
- Do not add new CTest labels; grouped entries carry the same labels as their source executable.
- Do not group the GPU/Vulkan or benchmark/SLO executables (isolation and contention matter there).

## Context
- Owner: CI/tooling + test infrastructure; touches `tests/CMakeLists.txt`, `tests/support/`, and the boot-heavy `tests/contract/runtime/` sources.
- Every executable registers one CTest entry per gtest case via `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)` (`tests/CMakeLists.txt:102-108`) → ~3,500 Debug+ASan process spawns per full CPU gate (3,851 `TEST` macros) plus ~24 `--gtest_list_tests` discovery launches on every ctest invocation. Estimated ~2-9 CPU-min of pure fixed spawn overhead per gate.
- The `intrinsic_grouped_test` helper (`tests/CMakeLists.txt:121-147`, runs a whole suite in one process) has **zero call sites** — dead infrastructure since CI-001's grouped entries were deleted with the legacy suites (LEGACY-042). There are zero `SetUpTestSuite`/`::testing::Environment` uses in `tests/` today, and ~160 `Engine engine(` boot sites (each boots a worker-thread pool, window, device, renderer, ImGui adapter via `src/runtime/Runtime.Engine.cpp:2984-3060`), ~150 of them in PR-gated `contract/runtime` files.
- CI-001 recorded a measured ≥3x wall-clock reduction on the executable migrated to the shared-fixture pattern (`tasks/done/CI-001-slim-engine-test-runtime.md` acceptance section) — the on-record evidence this overhead class dominates.
- Depends on CI-007 (case-count ledger backstops the isolation change) and BUG-063 (its flaky streaming tests live in `Test.AssetImportFormatCoverage.cpp`, a migration target — flakes must not be misattributed to the fixture change).

## Slice plan
- **Slice A (grouping).** Convert the highest-case-count cheap executables — `IntrinsicGeometryTests` (~1,561 cases, `:997-1001`), `IntrinsicRuntimeContractTests` (795 cases, `:952-958`), `IntrinsicGraphicsContractCpuTests` (~500 cases, `:1127-1131`) — to `NO_DISCOVER` (flag already parsed at `:73`) plus unfiltered grouped entries carrying the same labels, sharded by suite prefix so `ctest -j` parallelism is preserved. Add the case-count audit (below). Defers the fixture to Slice B.
- **Slice B (shared fixture).** Recreate the deleted lazy shared environment (template: `tests/support/RuntimeRhiTestEnvironment.hpp`) as a lazy `::testing::Environment` booting one headless `Engine` per process with a documented scene-reset contract; migrate the boot-heaviest PR-gated files first (`Test.MeshGeometryExtraction.cpp` 19 boots, `Test.SandboxEditorUi.cpp` 17, `Test.AssetImportFormatCoverage.cpp` 16). The fixture only pays off once grouping lands, so Slice B follows Slice A.

## Required changes
- [ ] Slice A: add grouped, `NO_DISCOVER`, same-label entries for the three named executables, sharded to preserve `-j` parallelism; keep the `TEST_INCLUDE_FILES` label-fixup machinery working.
- [ ] Slice A: add a case-count audit comparing each grouped entry's gtest `[ PASSED ] N tests` summary against `--gtest_list_tests` inventory and the 3,514-entry baseline; a mismatch fails the job (complements CI-007's ledger).
- [ ] Slice B: add the lazy shared-`Engine` `::testing::Environment` under `tests/support/` with an explicit per-suite reset assertion that fails loudly on residue; migrate the boot-heaviest files onto it.

## Tests
- [ ] Run the default CPU gate over the converted executables and confirm the case-count audit matches the baseline (no silently dropped cases).
- [ ] Confirm nightly-deep still runs the same binaries with per-case PRE_TEST discovery, so grouped-vs-isolated divergence (state leakage) surfaces daily.
- [ ] Confirm a dedicated boot-cycle test keeps the cold `Initialize`/`Shutdown` path exercised in every gate.

## Docs
- [ ] Update `tests/README.md` and `docs/architecture/test-strategy.md` per `docs/agent/docs-sync-policy.md` to document the grouped-entry pattern and the shared-fixture scene-reset contract.

## Acceptance criteria
- [ ] Grouping the three executables removes ~2,850 of ~3,500 per-gate spawns; the measured PR/full test phases drop materially (target: roughly halved on the affected executables).
- [ ] The shared fixture reproduces CI-001's ≥3x reduction on the boot-heavy runtime executable, with assertions untouched (only boot ownership moves).
- [ ] Case-count audit + daily per-case nightly run make any dropped case or state-leak divergence detectable, not silent.

## Verification
```bash
# CPU gate over converted executables (requires a configured build tree):
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicGeometryTests|IntrinsicRuntimeContractTests|IntrinsicGraphicsContractCpuTests' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
# Case-count audit and label check:
python3 tools/ci/check_gate_inventory.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Merging executables into one binary, deleting assertions, or weakening `GTEST_SKIP` (CI-001 forbidden list).
- Grouping GPU/Vulkan or benchmark/SLO executables.
- Landing Slice B before Slice A, or attributing BUG-063's pre-existing flake to the fixture migration.

## Maturity
- Target: `Operational` — the grouped entries and shared fixture must run in the real CPU gate with the case-count audit green, not merely compile.
- Slice A closes the grouping/audit; Slice B closes the shared-fixture migration. Both are gated behind CI-007 and BUG-063.
