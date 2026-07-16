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
- Owner: `tools/analysis/compile_hotspots.py`, its regressions/baseline, and the
  workflow step that publishes/enforces the report.
- The current `SourceResolver` indexes only `src/**/*`, so test, method, and
  benchmark translation units can appear unresolved even when they dominate a
  gate's compile time.
- Ninja module builds may record `.pcm` and `.o` outputs from one physical
  compiler command. The current report treats every output record as an
  independent edge, and `row_by_source` silently keeps only one row when the
  baseline later maps duplicate source rows.
- The current baseline contains only `src/geometry/Geometry.Octree.cppm`, so a
  passing gate does not demonstrate visibility of the runtime/test hot spots
  observed in recent logs.
- `CI-003` supplies the historical timing methodology. Baselines must be
  refreshed only after edge identity and source resolution are normalized.

## Slice plan
- Slice A — repair physical-edge normalization, source resolution, unresolved
  diagnostics, and synthetic regressions without changing workflow thresholds.
- Slice B — build the representative test/method/benchmark producers, collect
  five comparable clean samples, refresh the baseline, then move the repaired
  report behind correctness feedback.

## Required changes
- [ ] Define and document a deterministic physical compile-edge identity from
      Ninja evidence so outputs of one command are aggregated once while
      distinct BMI/object/compiler phases remain distinguishable.
- [ ] Resolve repository-owned sources under `src/`, `tests/`, `methods/`, and
      `benchmarks/`, with deterministic behavior for ambiguous basenames and
      generated/non-source outputs.
- [ ] Report unresolved outputs, counts, edge kind, all physical outputs, and
      resolution diagnostics in machine-readable results instead of silently
      omitting or overwriting them.
- [ ] Compare baselines against normalized physical edge/source records and
      fail on ambiguous/missing required targets with actionable diagnostics.
- [ ] Refresh the baseline from at least five comparable clean samples after
      normalization, naming the actual top repository-owned offenders rather
      than preserving stale targets by fiat.
- [ ] Move the compile-hotspot failure/report after CPU correctness tests or
      into an independent always-reporting job so a hotspot regression cannot
      suppress already-built correctness feedback.
- [ ] Keep source optimization in owning tasks; this task produces trustworthy
      evidence only.

## Tests
- [ ] Add synthetic Ninja-log/build fixtures for one-command `.pcm`+`.o`
      outputs, distinct module phases, duplicate basenames in different roots,
      test/method/benchmark sources, generated outputs, and unresolved outputs.
- [ ] Prove one physical command contributes once to rankings and baseline
      comparison while its complete output list remains visible.
- [ ] Prove distinct physical phases for the same source are retained with
      explicit identities rather than overwritten.
- [ ] Run the repaired analyzer against clean `ci` logs and validate the JSON
      result and refreshed baseline.

## Docs
- [ ] Update `docs/benchmarking/ci-policy.md` and
      `docs/build-troubleshooting.md` with physical-edge semantics, covered
      source roots, unresolved-output policy, sample requirements, and baseline
      refresh procedure.
- [ ] Cross-link `RUNTIME-166` and other source-owner tasks as consumers without
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
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarks
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40 --json-out build/ci/compile_hotspots_report.json --baseline-json tools/analysis/compile_hotspot_baseline.json
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Summing `.pcm` and `.o` wall times from one compiler command as independent
  work.
- Resolving ambiguous sources by shortest-name guess without a diagnostic.
- Refreshing thresholds until a noisy or regressed sample happens to pass.
- Mixing runtime/geometry/graphics module refactors into the analyzer patch.
