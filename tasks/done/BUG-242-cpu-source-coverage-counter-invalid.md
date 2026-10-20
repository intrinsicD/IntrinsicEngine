---
id: BUG-242
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive CI-gate triage; raw evidence is the CI job artifacts and the local collection under tasks/evidence/BUG-242/.
contract_schema: 1
contracts: []
contract_review: Existing coverage collection failure; no method or architecture change.
---
# BUG-242 — CPU coverage rejects an invalid LLVM execution counter

## Goal
Restore source coverage collection on main without weakening its validity checks.

## Evidence
[Main run 38200249201](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38200249201),
commit `bbdf1af795ba064099689d6d591b1ee310f46b74`, job `114651839727`,
2026-10-12 04:20:04 UTC: `CPU source coverage: error: src/graphics/renderer/Graphics.Renderer.cpp:
segment: saturated or negative LLVM execution counter; coverage builds require atomic profile updates`.
Collection exits 1; timing aggregation also reports failure. Retrieved via the job logs API.
No PR exists for the current local METHOD-083 branch.

## Acceptance criteria
- [x] Diagnose profile collection/counter invalidity from this job's raw artifacts.
- [x] Re-run collection successfully without suppressing invalid counters.

## Verification
Reproduce the failing workflow command with its saved profile artifacts, then run the
owning coverage-tool tests and strict task policy checks.

## Status (2026-10-19)
- Diagnosis (`ca66d4261`): not atomics, overflow or profile merging. In the run 39194424027
  export every invalid count is the false arm of a `?:` inside a braced initializer whose
  true count equals the function-entry count. Clang's PGO counter map walks only the
  syntactic `InitListExpr`; when a `?:` with lvalue operands, one a named constant, is
  rebuilt to mark the constant non-odr-used, the semantic node gets default counter 0.
  Reproduced standalone on Clang 15/22/23; `CodeGenPGO.cpp` in `release/20.x` shows the default.
- Fix: the 11 such conditionals (6 files) found by a frontend-IR scan of all 1282 instrumented
  coverage TUs are bound to locals; the rescan finds none. The collector's counter error now
  names the location and both causes (`tools/ci/README.md`).
- Local run (Clang 23, workflow command): collection `ok` (33 targets, 5869 cases, 0 failures),
  self-parity ok, 0 invalid counters. Raw files: `tasks/evidence/BUG-242/`.
- Review: Codex (requested `gpt-6-astra`, `xhigh`; thread `01a153d1-6780-7231-b226-465d51cbf8e1`;
  read-only sandbox, recursion protection instruction-only) on `ca66d4261`, diff SHA-256
  `ead732073167781a1e15bbe93fad1c2a5204c0f27910cfea9e14dfc52c19f743`: **approve**;
  criterion 1 met; criterion 2 "met locally, pending Clang-20 CI confirmation".
- Follow-ups, not in this slice: the same gap skews branch counts of 33 defaulted comparison
  operators (never negative in this cohort); no committed guard against reintroducing the pattern.

## Completion
Completed 2026-10-20. Commit: `ca66d4261`, merged to `main` in `eb1ee134f`.
Clang-20 confirmation: `ci-source-coverage` run 39246934388 (workflow_dispatch on `eb1ee134f`):
all executed steps succeeded (the optional grouped-CTest steps were skipped), tooling
regressions 35 OK, collection `ok: targets=33 objects=33 raw_profiles=33`, self-parity ok.
Criterion 2: Codex, same thread, delta round on this retirement diff: "met"; its two wording
corrections are applied.
`tasks/evidence/BUG-242/` stays local until the follow-up session on defaulted comparison
operators and a reintroduction guard, which reads its `ir_scan.py` and inventories, has finished.

## Follow-up
Both follow-ups landed in `df3849db8` (merged to `main` in `c14da0062`; operator decisions,
hosted run 39399689989 and Codex approval are in the merge message): the collector omits branch
records on `= default` comparisons, and `ci-source-coverage` runs
`tools/ci/check_coverage_counter_map.py` on the built production TUs. The follow-up session has
finished with `tasks/evidence/BUG-242/`.
