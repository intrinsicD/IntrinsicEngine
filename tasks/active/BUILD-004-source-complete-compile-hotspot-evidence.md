---
id: BUILD-004
theme: H
depends_on:
  - CI-003
---
# BUILD-004 — Make compile-hotspot evidence source-complete

## Goal
- Make the Ninja compile-hotspot report identify and gate real physical
  compile work across all repository-owned C++ roots without double-counting
  multi-output module edges.

## Non-goals
- No engine/module decomposition, public API change, or source optimization;
  `RUNTIME-166` and other owning-layer tasks consume this evidence.
- No performance threshold or regression claim from one local build.
- No build-tree/BMI cache policy change and no CMake File-API dependency
  planner.
- No merge of genuinely distinct compiler phases merely because they share a
  source path.

## Context
- Active on 2026-07-18; owner: Codex; branch: `main`. Next verification:
  commit and push Ninja log v4-v7 support, then dispatch one replacement set
  of five compile-only hosted samples without CTest.
- Owning surface: `tools/analysis/compile_hotspots.py`, its
  regressions/baseline, and the workflow step that publishes/enforces the report.
- Before Slice A, `SourceResolver` indexed only `src/**/*`, so test, method,
  and benchmark translation units could appear unresolved even when they
  dominated a gate's compile time.
- Before Slice A, Ninja `.pcm` and `.o` outputs from one physical compiler
  command were treated as independent edges, and source-only baseline lookup
  could silently overwrite duplicate source rows.
- The current baseline contains only `src/geometry/Geometry.Octree.cppm`, so a
  passing gate does not demonstrate visibility of the runtime/test hot spots
  observed in recent logs.
- `IntrinsicTests` is the source-complete compile-only evidence target: it
  builds every registered test producer, including method and benchmark
  translation units, without invoking the `IntrinsicBenchmarks` custom target
  that also executes the benchmark runner.
- `CI-003` supplies the historical timing methodology. Baselines must be
  refreshed from the normalized edge identity and source resolution now
  implemented by Slice A.

## Slice plan
- Slice A — repair physical-edge normalization, source resolution, unresolved
  diagnostics, and synthetic regressions without changing workflow thresholds.
- Slice B — build the representative test/method/benchmark producers, collect
  five comparable clean samples, refresh the baseline, then move the repaired
  report behind correctness feedback.

## Status
- Slice A is implemented and focused non-CMake checks pass. Slice B hosted
  sampling, baseline refresh, real-build validation, and retirement remain
  pending.
- Hosted run `29628467034` completed five clean builds at `f2c5d192`, but all
  five analyzers rejected the hosted runner's Ninja log v7 before producing
  artifacts. The run is diagnostic only and contributes no calibration data.
  Before reading any successful report, the refresh rule remains fixed:
  require an identical physical-edge identity and resolution inventory across
  all five CPU reports; rank baseline-eligible resolved repository edges
  globally by median duration with `edge_id` as the tie-breaker; retain the
  five slowest; and set each budget by rounding
  `1.25 * nearest-rank p95` up to a whole second with
  `max_regression_ms=0`. The one source-complete report proves root visibility
  only and does not enter timing thresholds.
- Inventory equality means equality of the canonical map keyed by stable
  `edge_id` with `source`, `source_root`, `edge_kind`, sorted `outputs`, and
  resolution status as values. It ignores duration-sorted report order and the
  run-local timestamp/command-hash `physical_identity`. Ranking is median
  descending, then `edge_id` lexicographically ascending. Every retained target
  carries `edge_id`, `source`, `edge_kind`, and `outputs`; the resulting
  baseline describes the required CPU cohort, not source-complete timing.

## Required changes
- [x] Define and document a deterministic physical compile-edge identity from
      Ninja evidence so outputs of one command are aggregated once while
      distinct BMI/object/compiler phases remain distinguishable.
- [x] Resolve repository-owned sources under `src/`, `tests/`, `methods/`, and
      `benchmarks/`, with deterministic behavior for ambiguous basenames and
      generated/non-source outputs.
- [x] Report unresolved outputs, counts, edge kind, all physical outputs, and
      resolution diagnostics in machine-readable results instead of silently
      omitting or overwriting them.
- [x] Compare baselines against normalized physical edge/source records and
      fail on ambiguous/missing required targets with actionable diagnostics.
- [ ] Refresh the baseline from at least five comparable clean samples after
      normalization, naming the actual top repository-owned offenders rather
      than preserving stale targets by fiat.
- [x] Move the compile-hotspot failure/report after CPU correctness tests or
      into an independent always-reporting job so a hotspot regression cannot
      suppress already-built correctness feedback.
- [x] Keep source optimization in owning tasks; this task produces trustworthy
      evidence only.

## Tests
- [x] Add synthetic Ninja-log/build fixtures for one-command `.pcm`+`.o`
      outputs, distinct module phases, duplicate basenames in different roots,
      test/method/benchmark sources, generated outputs, and unresolved outputs.
- [x] Prove one physical command contributes once to rankings and baseline
      comparison while its complete output list remains visible.
- [x] Prove distinct physical phases for the same source are retained with
      explicit identities rather than overwritten.
- [ ] Run the repaired analyzer against clean `ci` logs and validate the JSON
      result and refreshed baseline.

## Docs
- [x] Update `docs/benchmarking/ci-policy.md` and
      `docs/build-troubleshooting.md` with physical-edge semantics, covered
      source roots, unresolved-output policy, sample requirements, and baseline
      refresh procedure.
- [x] Cross-link `RUNTIME-166` and other source-owner tasks as consumers without
      claiming those sources have already been optimized.
- [ ] Update process/task indexes and regenerate `tasks/SESSION-BRIEF.md` on
      retirement.

## Acceptance criteria
- [ ] Every repository-owned C++ compile output in the test/CI graph is either
      resolved to a source or reported as unresolved with a reason.
- [ ] A physical multi-output compiler command is ranked once, while genuinely
      separate phases remain individually inspectable.
- [ ] Baseline lookup cannot silently overwrite two records for one source or
      pass when a required source/edge is ambiguous.
- [ ] The resolver covers every declared repository root, the report states
      which roots/edges were present in the sampled build graph, and the top-N
      ranking reflects the actual slowest compiled edges without artificial
      per-root quotas.
- [ ] Correctness tests report before a compile-hotspot gate failure in the
      affected workflow topology.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicTests
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40 --json-out build/ci/compile_hotspots_report.json --baseline-json tools/analysis/compile_hotspot_baseline.json
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Summing `.pcm` and `.o` wall times from one compiler command as independent
  work.
- Resolving ambiguous sources by shortest-name guess without a diagnostic.
- Refreshing thresholds until a noisy or regressed sample happens to pass.
- Mixing runtime/geometry/graphics module refactors into the analyzer patch.
