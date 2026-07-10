---
id: BUG-080
theme: G
depends_on: []
---
# BUG-080 — UV-atlas promotion smoke flakes on one-sided scheduler stalls

## Status
- Completed 2026-07-10 at `CPUContracted` on branch `main` in this workspace.
- Commit: this local fix commit.
- Focused benchmark validation, the 25-run loaded-host repetition, the complete
  default CPU gate, and strict structural checks pass.

## Goal
- Keep the strict fast-staged/xatlas promotion thresholds while making
  `geometry.uv_atlas.fast_staged_promotion.smoke` reject isolated
  host-scheduling outliers instead of false-failing the default CPU and PR-fast
  gates.

## Non-goals
- No `Geometry.UvAtlas`, xatlas, backend-selection, fixture, or default-policy
  changes.
- No threshold increase, quarantine, benchmark removal, retry-until-green, or
  conversion of failure to warning.
- No new performance/adoption claim and no rewrite of `GEOM-057`'s historical
  evidence.

## Context
- Owner is `benchmarks/geometry`; this is a measurement-harness defect, not a
  geometry behavior regression.
- On 2026-07-10 the full CPU gate failed only this promotion result:
  `open_cylinder_12` fast 29.313143 ms, xatlas 16.658950 ms, ratio 1.759603 >
  1.25; the suite mean ratio remained 0.552804 and every quality diagnostic
  passed. An immediate isolated rerun passed with max ratio 0.572443 and mean
  ratio 0.356735.
- Before this fix, `RunPromotionFixture` timed three consecutive fast calls as
  one wall-clock block, then three consecutive xatlas calls as another, and
  gated on the maximum of seven fixture ratios. Five normal local artifacts
  put the cylinder at fast 8.64–9.36 ms, xatlas 15.13–16.60 ms, ratio
  0.541–0.578. One roughly 70 ms fast invocation (about 61 ms of scheduler
  delay) explains the failed 29.3 ms block mean while the denominator stayed
  normal. Host load was about 7.7 with several CPU-saturating jobs.
- Benchmark ID, dataset, methods, and thresholds stay stable; this task owns a
  documented measurement-method correction.

## Required changes
- [x] In `Bench_UvAtlasSmoke.cpp/.hpp`, collect five individually timed
      fast/xatlas measurement pairs per promotion fixture and alternate pair
      order (`fast→xatlas`, then `xatlas→fast`) to balance temporal/order
      bias.
- [x] Gate each fixture on the median of the five paired runtime ratios; report
      each backend's median individual runtime. Keep aggregate mean/max
      calculations over those robust per-fixture ratios and keep thresholds
      1.0/1.25 unchanged.
- [x] Emit the raw per-backend and paired-ratio samples plus
      `timing_statistic`/`measurement_order` diagnostics so an ignored outlier
      remains auditable.
- [x] Add a deterministic median-of-five contract proving that up to two
      isolated outliers cannot flip the statistic while a majority slowdown
      still exceeds 1.25; do not create a general timing framework.
- [x] Update the promotion manifest's measurement parameters to match without
      renaming the stable ID.

## Tests
- [x] Build and run the smoke runner; strict manifest/result validation passes.
- [x] Run `IntrinsicBenchmarkSmoke.Run` 25 times under the currently loaded
      host without a promotion false failure, retaining all result JSON for
      inspection.
- [x] Run the full default CPU-supported gate.

## Docs
- [x] Update `docs/benchmarking/metrics.md` with the comparative-smoke rule:
      individually time, interleave baseline/probe, and gate on a declared
      robust statistic rather than two block means.
- [x] Update `benchmarks/geometry/README.md`, the bug index, session brief, and
      retirement log.

## Acceptance criteria
- [x] A minority of contaminated paired samples cannot flip the per-fixture
      gate; a sustained majority regression still fails the unchanged
      threshold.
- [x] Manifest, emitted diagnostics, and implementation agree on warmup,
      sample count, ordering, and statistic.
- [x] Quality/fallback/overlap/flip gates remain unchanged and the stable
      benchmark ID remains intact.
- [x] Focused repetition and the default CPU gate pass.

## Verification
```bash
set -euo pipefail
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
rm -rf /tmp/bug080-uv-bench
for run in $(seq 1 25); do
  build/ci/bin/IntrinsicBenchmarkSmoke "/tmp/bug080-uv-bench/$run"
done
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
for run in $(seq 1 25); do
  python3 tools/benchmark/validate_benchmark_results.py \
    --root "/tmp/bug080-uv-bench/$run" --strict
done
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

Closure verification on 2026-07-10:

- The `ci` preset selected Clang 23 with ASan/UBSan; both
  `IntrinsicBenchmarkSmoke` and `IntrinsicTests` built successfully.
- The retained loaded-host stress produced 25/25 passing promotion runs and
  475/475 passing result files. Strict result validation passed independently
  for all 25 run directories; every emitted raw sample array had length five.
- Across those runs, the mean paired-ratio statistic ranged from 0.341487 to
  0.352590 and the worst robust per-fixture ratio was 0.604228, below the
  unchanged 1.25 limit. The manifest/result policy fields agreed on one warmup
  pair, five measured pairs, alternating order, and median paired ratios.
- The post-fix default CPU-supported gate passed 3,658/3,658 tests in 389.50 s,
  including `IntrinsicBenchmarkSmoke.Run` and its strict result validator.
- Strict manifest, task-policy, task-state, documentation-sync/link, generated
  session-brief, and whitespace checks pass.

## Completion

- Completed: 2026-07-10. Maturity: `CPUContracted`.
- Outcome: the strict promotion gate rejects sustained regressions while a
  minority of scheduler-contaminated pairs cannot false-fail a fixture; raw
  samples keep every ignored outlier auditable.
- The benchmark remains CPU-only, so no `Operational` follow-up is owed.

## Forbidden changes
- Loosening or removing the 1.0 mean or 1.25 per-fixture thresholds.
- Hiding failed results via retry, warning, skip, or quarantine.
- Changing engine geometry behavior or the benchmark's stable
  identity/dataset.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; this is a CPU-only measurement-harness contract and
  no `Operational` follow-up is owed.
