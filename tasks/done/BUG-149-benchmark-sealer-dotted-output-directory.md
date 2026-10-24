---
id: BUG-149
theme: G
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
contract_review: "Reviewed the full catalog; this is a benchmark-tool path classification and exit-status defect and changes no engine layer, geometry domain, property publication, method integration, or reusable agent-workflow contract."
---
# BUG-149 — Benchmark sealer escapes dotted output directories

## Goal
- Make `run_and_seal.py` treat every producer directory as the result root regardless of dots in its final path component, and propagate sealing failure as a nonzero process result.

## Non-goals
- No benchmark schema, manifest, threshold, producer, claim-eligibility, or canonical result-policy change.
- No cleanup or mutation of unrelated JSON files outside the requested output root.

## Context
- Symptom: during METHOD-037 verification, `--output /tmp/method037-bench.E4nKT7` was passed to a producer that correctly emitted 30 result files into that directory. `run_and_seal.py` classified the directory as a file because `Path.suffix` was non-empty, selected `/tmp` as `result_root`, and attempted to reseal unrelated JSON trees across the entire temporary directory. The sealer printed `Benchmark result sealing FAILED`, yet the wrapper command returned exit code zero.
- Expected behavior: producer output type is determined from actual path semantics/the producer contract, not filename suffix. Sealing is confined to the exact requested directory and any producer or sealer failure makes the wrapper fail.
- Impact: dotted temporary/result directories can trigger broad unintended reads/writes, enormous diagnostics, false-green automation, and failure to seal the intended isolated result set. The METHOD-037 files themselves validated when the validator was pointed directly at the exact directory.

## Required changes
Current source already propagates producer/sealer failures and exits via
`SystemExit(main())`. Preserve and regression-test that delivered behavior;
the remaining defect is suffix-based output-root classification. The earlier
zero-exit observation above is historical, not the current implementation.

- [x] Resolve the producer output root without using a directory name's suffix as a file/directory discriminator; validate the exact target before invoking the sealer.
- [x] Preserve the existing nonzero producer/sealer exit propagation while
      fixing root resolution; add explicit regression coverage for both stages.
- [x] Keep sealing confined to the exact requested root and refuse ambiguous/missing outputs without walking a parent such as `/tmp`.

## Tests
- [x] Add `tests/regression/tooling/Test.BenchmarkRunAndSeal.py` (not yet
      present) covering dotted and non-dotted output directories, an explicit
      JSON output file if supported, producer failure, sealer failure, and
      proof that sibling JSON is untouched.

## Docs
- [x] Clarify accepted `--output` path forms in the benchmark workflow/help if both directory and explicit file forms remain supported.

## Acceptance criteria
- [x] The captured dotted-directory repro seals only its producer results and returns zero only when production and sealing both succeed.
- [x] A forced sealer failure is nonzero, and no sibling or parent JSON is inspected or modified.
- [x] Benchmark tooling regressions and strict manifest/result validation pass.

## Verification

After implementing the regression file named above:

```bash
python3 tests/regression/tooling/Test.BenchmarkRunAndSeal.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root <isolated-result-dir> --manifests-root benchmarks --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Inferring that every suffix-bearing path is a file.
- Falling back to the output parent when the requested result root is ambiguous.
- Returning success after either the producer or sealer reports failure.
- Weakening result validation or recursively processing unrelated parent directories.

## Resolution — 2026-10-24

- Root cause: `run_and_seal.py` chose
  `output if output.suffix == "" else output.parent`. A dotted directory such as
  `method037-bench.E4nKT7` has a suffix, so `seal_benchmark_results.py --replace`
  walked the parent with `rglob("*.json")`. The smoke and GPU-smoke producers
  already classify the path by what exists (`is_directory || !has_extension`),
  so only the wrapper disagreed. `CoherentPointDriftScalingRunner` and
  `PointSamplingComparisonRunner` check only `!has_extension` and are not run
  through the wrapper today; given a dotted directory the wrapper still fails
  closed. Exit propagation (sealer rc first, then producer rc) was already
  present and is unchanged.
- Change: after the producer runs, the wrapper seals an existing directory
  exactly and an existing `.json` file's parent (explicit-file form used by
  `IntrinsicBenchmarks`, `IntrinsicUvAtlasRemapSmoke.Run` and nightly-deep).
  Anything else (missing output, non-JSON file) is refused before the sealer
  starts with `producer rc or 2`. `--help` names both forms and that a dotted
  directory must exist before the run. New regression
  `tests/regression/tooling/Test.BenchmarkRunAndSeal.py` (8 cases, each with a
  sealable and a malformed sibling JSON in the parent whose bytes must not
  change) runs in the ci-docs step that holds the other benchmark tooling tests.
- Defaults chosen under the delegated night run: keep the explicit-`.json` form;
  refuse a non-JSON output file instead of sealing its parent; seal after a
  producer failure so raw failed results stay inspectable; no README change
  (the help text covers the Docs item).
- Regression first: against the unfixed wrapper 6 of 8 cases fail on
  "sealable-sibling.json changed" (dotted dir, missing dotted output x2,
  non-JSON dotted file, producer failure, sealer failure). The plain-dir and
  explicit-file cases pass, as predicted. After the fix: 8/8 OK.
- Mutations of the fix (each restored and byte-compared): dropping the sealer rc
  fails only the sealer case; dropping the producer rc fails only the producer
  case; dropping the `.json` condition fails only the non-JSON case; a bare
  `return 2` fails only the missing-output-with-producer-rc case; a parent
  fallback for missing output fails both missing cases and the non-JSON case.
  That last prediction was corrected after the first run because the non-JSON
  file shares that branch.
- Real producer (main-checkout `build/ci` binaries, executed only; dedicated
  `/tmp` parent holding a malformed and a sealable sibling). Before the fix:
  wrapper rc 1, the sibling was the only error. After the fix:
  - `mktemp -d .../method037-bench.XXXXXX` with `IntrinsicBenchmarkSmoke`:
    producer rc 0, wrapper rc 0, `Sealed 41`, strict validation of that directory
    passes for 41 files.
  - `.../run/result.json` with the same binary: rc 0, `Sealed 41`, strict pass.
  - `.../uv/result.json` with `IntrinsicUvAtlasRemapSmoke`: rc 0, `Sealed 1`,
    `.snapshot` bytes unchanged, strict pass.
  - The siblings were unchanged in all three runs.
- Also green: Test_BenchmarkResultValidator (17), Test_BenchmarkManifestValidator
  (2), Test.BenchmarkComparison (3), Test.CiTiming (20), Test.WorkflowConcurrency
  (20), Test.WorkflowRouting (12), Test.CheckTaskStateLinks (7),
  Test.SanitizerPresets (8), Test.CcacheWorkflow (23), Test.CheckKernelConvergence
  (24), Test.SourceDocumentationAudit (9), Test.TouchedScope (37),
  Test.ExperimentCustody (40), `check_workflow_names.py`, strict manifest
  validation (123 files). Raw logs: `tasks/evidence/BUG-149/` (git-ignored).
- Remaining:
  - Operator decision: the explicit-`.json` form still seals the file's whole
    parent by contract (`--output /tmp/result.json` would walk `/tmp`). Keep it
    (current default, stated in `--help`), or move the three callers to the
    directory form and drop it.
  - `tools/benchmark/*.py` has no touched-scope route to its regressions. This
    is unchanged and out of scope.
- Review (round 1, reviewed `771f95368ad1c4bf8cdcf30630d1788a1d631666`,
  implementation commit `771f95368`): the boxes under Required changes, Tests,
  Docs and Acceptance criteria are ticked as both reviewers decided.
  - Independent Claude review (correctness and ponytail): verdict approve;
    every required change, the test, the docs item, all three acceptance
    criteria and the forbidden-change check decided `met`.
  - Independent Codex review (correctness and ponytail, thread
    `01a16b29-7478-7280-bdcf-8c0e78f1830b`): `VERDICT: approve`; all three
    acceptance criteria **Met**; "The required root validation, exit
    propagation, regression coverage, and output-form documentation are
    satisfied." It reran strict manifest validation (123 files) and strict
    result validation of the retained dotted real-producer directory (41 files).
  - Optional Claude findings: the producer-classification claim above was
    narrowed after the review. Not adopted: the `run_and_seal.py` refusal hint
    names the dotted-directory case for every refusal; this section was not
    shortened; the `ara` heuristic H12 code-ref list omits the new test.

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `4b43ae0c1` (head, review record), with `771f95368`
(fix, regression, `ci-docs` step), parent `599f02ff1`, merged into the night integration
branch `claude/night-2026-10-24-bug-sweep` in `630d0f980` and `cafa5cb90`. Not yet
on `main`; hosted CI has not run the new `ci-docs` step.
Reviewer decisions:
- Round 1 on `771f95368`: independent Claude review `approve` (every required change,
  the test, the docs item and all three acceptance criteria `met`); independent Codex
  review "VERDICT: approve", all three acceptance criteria **Met**. The boxes above are
  ticked from these decisions (see Resolution).
- Completion chain on `4b43ae0c1`: the standard-profile report (`status: complete`,
  content digest `0ae36edd4f76208505a4232a88a98fb6c20f248069a4187f5efdb6fc1a863db5`,
  five required receipts) was reviewed with verdict `approve` (relayed by the night
  workflow coordinator). Before the move, `workflow_evidence.py validate --root <bug-149
  lane> --require-complete BUG-149` gave 0 errors and 1 warning: the justified skip of
  re-receipting strict result validation of the `/tmp` dotted directory.
Open for the operator: the `--output` form. A (current default, stated in `--help`):
keep the explicit-`.json` form, which seals the file's whole parent by contract.
B: move `IntrinsicBenchmarks`, `IntrinsicUvAtlasRemapSmoke.Run` and nightly-deep to the
directory form and drop file outputs. The report, receipts and raw logs stay git-ignored
in the `bug-149` lane worktree under `tasks/evidence/BUG-149/`; nothing was deleted.
