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
- Completed on 2026-07-18 at `Operational`; owner: Codex; branch: `main`.
- Commit: `3a4f1998` repairs normalized physical-edge evidence, commit
  `4a53c389` admits current Ninja log formats without weakening fail-closed
  parsing, and commit `e46056bb` installs the five-sample calibrated baseline.
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
- Slice A, Slice B calibration, and the final hosted correctness/baseline gate
  are complete.
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
- Final normal CPU run `29630624415` passed at
  `e46056bbd28b92dfcf3b541245b53fe2538dba4f`: it reported the complete
  4,062-case logical selection before executing 2,716 physical CTest records,
  then ran and passed the five-target compile-hotspot gate.

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
- [x] Update process/task indexes and regenerate `tasks/SESSION-BRIEF.md` on
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
- [x] Correctness tests report before a compile-hotspot gate failure in the
      affected workflow topology.

## Evidence
- Hosted calibration run
  [`29629549095`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29629549095)
  passed all five clean compile-only samples at `4a53c389`. Each CPU report
  contained 1,032 physical edges, 1,029 eligible resolved repository edges,
  zero resolution issues, and identical predeclared status-level inventory
  digest
  `c65b7ef3a5a0ea4393fb33cf8894c2b3ed54ba12c8ff8ff24aa707a9c05a64e5`.
  The supplemental full-resolution digest was also identical:
  `5d832f6bb35910c9952766f63cf87f9e66dc096864a4c9a63e4d92436abc0efc`.
- The global median ranking calibrated five exact physical edges:
  `Runtime.SandboxEditorFacades.cppm` at 283000 ms,
  `Runtime.Engine.cppm` at 165000 ms,
  `Runtime.RenderExtraction.cppm` at 167000 ms,
  `Sandbox.MethodPanels.cppm` at 151000 ms, and
  `Sandbox.EditorShell.cppm` at 155000 ms. The baseline keeps
  `max_regression_ms=0`; every input duration and artifact digest is retained
  in `docs/benchmarking/ci-policy.md`.
- Final normal CPU run
  [`29630624415`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29630624415)
  passed at `e46056bb`. Its 28 producers selected 4,062 logical cases at digest
  `e8dc6d95d59c30c2317ea3a27b0395011583fd11a6e3e5118d870f47d064e540`.
  The uploaded diagnostics reconstruct 4,055 passed and seven skipped logical
  cases with zero failures, errors, or disabled cases; the grouped execution
  used 2,716 physical CTest records.
- The CPU suite completed at `04:57:23.9507483Z`; the hotspot step began at
  `04:57:23.9595624Z`, reported 1,032 physical edges and zero resolution
  issues, and passed all five baseline targets. This proves correctness
  feedback is emitted first in the affected topology.
- Final artifacts expire on 2026-10-16: timing artifact `8425582987`
  (`sha256:dd35120e7773707574f1ff968c6e6f88eb4394a7bc870e47c05f71500ff9d056`),
  selection artifact `8425583089`
  (`sha256:707a6fec7de4a6df138014d5bccd8b96ba9516d36cec8b132137750b1ff0266d`),
  and test-results artifact `8425583186`
  (`sha256:d6f9671248ce6354a715c7ba5fd625aa6862f6f31826d3c4262d86d6336531e3`).

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
