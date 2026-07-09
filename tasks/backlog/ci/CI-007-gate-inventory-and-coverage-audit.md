---
id: CI-007
theme: none
depends_on: []
---
# CI-007 — Gate-inventory and executed-coverage audit

## Goal
- Turn "did we silently lose test coverage?" from a worry into a failing check: statically prove the union of CI tiers selects every registered test, ledger the selected-test counts against a baseline, and watchdog the nightly deep lane so a silent skip is impossible.

## Non-goals
- Do not remove, retier, or reselect any test — this task only *observes*.
- Do not modify `tests/CMakeLists.txt` registrations except where day-one findings are split into follow-ups (see Docs/Acceptance).
- Do not build the engine; the audit is pure-Python and runs in the `ci-docs` lane.

## Context
- Owner: CI/tooling; adds `tools/ci/check_gate_inventory.py`, a regression test, and a step in `ci-docs.yml` (the 11-52s lane); adds `if: always()` bookkeeping to `nightly-deep.yml`.
- This is the coverage-detectability keystone the re-tiering tasks (CI-005, CI-008, CI-010) cite as their mitigation: it makes any coverage loss — mislabeled tests, dead label selections, disabled lanes — detectable rather than merely unlikely.
- Real gaps exist today that this audit would flag: (1) the `regression` label is selected by `nightly-deep.yml:100/114` but carried by zero CTest entries (`tests/CMakeLists.txt:40` allow-list entry has no users; the 9 `tests/regression/tooling/*.py` scripts are registered in no CMakeLists and no workflow); (2) `IntrinsicRuntimeIntegrationTests` (`gpu vulkan slow`, `tests/CMakeLists.txt:914-920`) runs only in the `INTRINSIC_ENABLE_GPU_NIGHTLY`-gated lane; (3) `IntrinsicGraphicsUnitTests`' HARDEN-041 label debt (`unit graphics gpu vulkan`, `:1079-1083`, comment `:746-750`) excludes ~20 mock-driven CPU tests from every CPU gate.
- 14 of the last 30 `nightly-deep` runs died at Configure in <60s (e.g. run 28846130583), silently forfeiting all slow/ASan/UBSan/SLO coverage those nights with no alert.

## Required changes
- [ ] Add `tools/ci/check_gate_inventory.py` (build-free) that parses the 28 `intrinsic_test_executable` registrations + their `LABELS`, the raw `add_test` at `tests/CMakeLists.txt:1064-1077`, the 4 raw benchmark `add_test` entries in `benchmarks/CMakeLists.txt`, and the `-L`/`-LE` ctest expressions across all 7 workflow files; fail if any registered executable's label set is excluded from *every* tier (PR/merge/nightly).
- [ ] Add a test-count ledger: each gate job appends its ctest selected-test count to a JSON artifact; the audit compares the merge-tier count against a checked-in baseline (seed: 3,514 entries per `docs/reports/full-ctest-baseline-2026-04-29.md`) and fails on drops beyond tolerance without a baseline-update commit.
- [ ] Add a nightly watchdog: an `if: always()` step at the end of `nightly-deep.yml` recording which stages executed, plus a cheap scheduled check that fails loudly if the nightly's slow/sanitizer/SLO stages have not succeeded in N days.
- [ ] Wire `check_gate_inventory.py` as a step in `ci-docs.yml` (strict, or warning-mode only with a referenced tightening task per `AGENTS.md`).

## Tests
- [ ] Add `tests/regression/tooling/Test.CheckGateInventory.py` with fixtures: a deliberately orphaned label must fail; a workflow losing its trigger must fail; a valid inventory must pass.
- [ ] Run the new validator and its regression test locally; confirm they flag the three real gaps above (or that those are resolved by their split-out follow-ups first).

## Docs
- [ ] Document the audit in workflow docs / `docs/architecture/test-strategy.md` per `docs/agent/docs-sync-policy.md`.
- [ ] Open follow-up tasks for the day-one findings the audit surfaces (wire-or-retire the `regression` label and its 9 tooling scripts; the HARDEN-041 CPU-relabel follow-up that `docs/architecture/test-strategy.md:106` still assigns to the retired HARDEN-042) rather than fixing them inline here.

## Acceptance criteria
- [ ] Every registered test executable's label set is selected by at least one live tier, or the gap is an explicitly-tracked follow-up — enforced by a check that fails otherwise.
- [ ] A future PR that mislabels a test out of all gates, kills a workflow trigger, or drops the merge-tier test count fails `ci-docs` within ~1 min.
- [ ] The nightly watchdog fails loudly (not silently) when a deep-coverage stage has not run successfully within the window.

## Verification
```bash
python3 tools/ci/check_gate_inventory.py --root . --strict
python3 tests/regression/tooling/Test.CheckGateInventory.py
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Silencing a real gap by adding a test's labels to the allow-list without making the test run.
- Lowering the count-baseline tolerance to pass without a diagnosed reason.
- Making the audit itself an untracked warning-mode check (`AGENTS.md` requires a referenced tightening task).
