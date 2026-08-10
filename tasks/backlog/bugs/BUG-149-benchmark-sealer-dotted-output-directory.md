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
- [ ] Resolve the producer output root without using a directory name's suffix as a file/directory discriminator; validate the exact target before invoking the sealer.
- [ ] Ensure producer and sealer exit statuses are both retained and any failure returns nonzero, with a concise diagnostic naming the failed stage.
- [ ] Keep sealing confined to the exact requested root and refuse ambiguous/missing outputs without walking a parent such as `/tmp`.

## Tests
- [ ] Add regression coverage for dotted and non-dotted output directories, an explicit JSON output file if supported, producer failure, sealer failure, and proof that sibling JSON is untouched.

## Docs
- [ ] Clarify accepted `--output` path forms in the benchmark workflow/help if both directory and explicit file forms remain supported.

## Acceptance criteria
- [ ] The captured dotted-directory repro seals only its producer results and returns zero only when production and sealing both succeed.
- [ ] A forced sealer failure is nonzero, and no sibling or parent JSON is inspected or modified.
- [ ] Benchmark tooling regressions and strict manifest/result validation pass.

## Verification
```bash
python3 -m unittest tests.regression.tooling.Test.BenchmarkRunAndSeal
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root <isolated-result-dir> --manifests-root benchmarks --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Inferring that every suffix-bearing path is a file.
- Falling back to the output parent when the requested result root is ambiguous.
- Returning success after either the producer or sealer reports failure.
- Weakening result validation or recursively processing unrelated parent directories.
