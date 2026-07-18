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
  commit and push the refreshed baseline, then dispatch one normal hosted CPU
  workflow to prove correctness reports before the hotspot gate.
- Owning surface: `tools/analysis/compile_hotspots.py`, its
  regressions/baseline, and the workflow step that publishes/enforces the report.
- Before Slice A, `SourceResolver` indexed only `src/**/*`, so test, method,
  and benchmark translation units could appear unresolved even when they
  dominated a gate's compile time.
- Before Slice A, Ninja `.pcm` and `.o` outputs from one physical compiler
  command were treated as independent edges, and source-only baseline lookup
  could silently overwrite duplicate source rows.
- At task intake, the baseline contained only
  `src/geometry/Geometry.Octree.cppm`, so a passing gate did not demonstrate
  visibility of the runtime/test hot spots observed in recent logs.
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
- Slice A and the Slice B hosted calibration are complete. One normal hosted
  CPU correctness/baseline run and retirement remain pending.
- Hosted run `29628467034` completed five clean builds at `f2c5d192`, but all
  five analyzers rejected the hosted runner's Ninja log v7 before producing
  artifacts. The run is diagnostic only and contributes no calibration data.
- Replacement run `29629549095` passed all five samples at
  `4a53c38961241ee9d5a5544882b7e162c3c95ead`. Every CPU report contained
  1,032 physical edges, 1,029 eligible resolved repository edges, zero
  resolution issues, and the identical predeclared status-level inventory
  SHA-256
  `c65b7ef3a5a0ea4393fb33cf8894c2b3ed54ba12c8ff8ff24aa707a9c05a64e5`.
  The additional full-resolution audit also matched, with SHA-256
  `5d832f6bb35910c9952766f63cf87f9e66dc096864a4c9a63e4d92436abc0efc`.
- The fixed global median ranking selected
  `Runtime.SandboxEditorFacades.cppm`, `Runtime.Engine.cppm`,
  `Runtime.RenderExtraction.cppm`, `Sandbox.MethodPanels.cppm`, and
  `Sandbox.EditorShell.cppm`. Their max/p95-derived budgets are respectively
  283000, 165000, 167000, 151000, and 155000 ms with
  `max_regression_ms=0`; the CI policy records every duration and artifact
  digest.
- Sample 1's excluded source-complete report contained 1,077 physical edges,
  1,074 resolved edges, zero issues, and configured/sampled counts of
  `src=705`, `tests=339`, `methods=9`, and `benchmarks=21`.

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
- [x] Refresh the baseline from at least five comparable clean samples after
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
- [x] Run the repaired analyzer against clean `ci` logs and validate the JSON
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
- [x] Every repository-owned C++ compile output in the test/CI graph is either
      resolved to a source or reported as unresolved with a reason.
- [x] A physical multi-output compiler command is ranked once, while genuinely
      separate phases remain individually inspectable.
- [x] Baseline lookup cannot silently overwrite two records for one source or
      pass when a required source/edge is ambiguous.
- [x] The resolver covers every declared repository root, the report states
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
