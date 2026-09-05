---
id: BUG-159
theme: J
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive repair; evidence is the diff, CPU/sanitizer tests, and scoped diagnostic benchmark."
owner: codex
branch: codex/bug-159-chart-local-remaps
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog. This is an implementation-local UV-atlas remap data-structure repair that preserves input/output domains, topology policy, parameterization numerics, public method integration, and control surfaces. geometry.parameterization-optimization does not apply because no shared energy, gradient, solver, or locally-injective step changes."
maturity_target: Operational
---
# BUG-159 — FastStaged atlas allocates global remaps for every chart

## Goal

- Remove the `O(chart_count × source_vertex_count)` initialization and retained
  storage from FastStaged UV parameterization while preserving deterministic
  atlas output and diagnostics.

## Non-goals

- No chart-admission or packing-policy change (`BUG-160`).
- No parameterization energy/solver change.
- No default-backend switch or new container abstraction.

## Context

- Symptom: `BuildFastChartParameterization` stores an input-vertex-sized
  `SourceVertexToLocal` vector in every retained chart. Output assembly then
  allocates a second input-vertex-sized `sourceVertexToOutput` vector per chart.
- Expected behavior: remap work and storage scale with source vertices plus
  chart-local vertices/corners, not the Cartesian product of charts and the
  full mesh.
- Impact: a 100k-face diagnostic producing about 90k charts implies billions
  of initialized entries and multi-gigabyte transient/retained traffic before
  useful UV output.

## Implementation note

- Interactive work started on 2026-09-05 at `e6350ca56` on
  `codex/bug-159-chart-local-remaps`.
- The geometry-owned defect is explicit in the two source-sized remaps; no
  solver, chart-policy, or backend-selection hypothesis is needed for this
  bounded repair. The allocation regression exercises the public CPU atlas
  entry point before and after the change.
- Use one source-to-local scratch lookup for chart construction, clear only
  touched entries, and publish vertices/faces through existing chart-local
  indices. Preserve first-seen order.
- The isolated allocation benchmark is required to avoid replacing allocators
  in the shared geometry test binary. It records resident-input UV enrichment
  diagnostics for BENCH-001, not Framework24 comparisons or a product speedup.
  The checked-in manifest freezes the fixture, warmup, repeats, and allocation
  budget; the complete BENCH-001 product harness remains separate work.

## Required changes

- [x] Replace retained per-chart global remaps with the smallest deterministic
      sparse/chart-local representation.
- [x] Replace per-chart output global remaps with chart-local output indexing.
- [x] Preserve source cross-references, UVs, face/chart assignments, seams,
      copied properties, diagnostics, cancellation, and failure semantics.
- [x] Record before/after diagnostic evidence through `BENCH-001`; do not turn
      a local debug run into a performance claim.

## Tests

- [x] Add exact output/parity coverage on existing atlas fixtures.
- [x] Add a deterministic many-chart stress fixture that would expose global
      per-chart allocation growth and verifies finite, complete output.
- [x] Pass geometry unit/contract tests and the default CPU gate.

## Docs

- [x] Document the retained complexity bound near the implementation if it is
      not clear from the chosen data structure.
- [x] Update benchmark evidence, not architecture docs, with measured impact.

## Acceptance criteria

- [x] No source-vertex-sized container is retained or initialized inside a
      chart loop.
- [x] Remap storage is `O(V + sum(chart-local vertices))` or better.
- [x] Existing atlas results and deterministic diagnostics remain equivalent
      except for timing/memory observations.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'UvAtlas' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Additional verification commands run

```bash
cmake --build --preset ci --target IntrinsicUvAtlasRemapSmoke
cmake --preset ci-asan
cmake --build --preset ci-asan --target IntrinsicGeometryTests IntrinsicUvAtlasRemapSmoke
ctest --test-dir build/ci-asan --output-on-failure \
  -R 'UvAtlas|IntrinsicGeometryTests' --timeout 120 --parallel 1
cmake --preset ci-ubsan
cmake --build --preset ci-ubsan --target IntrinsicGeometryTests IntrinsicUvAtlasRemapSmoke
ctest --test-dir build/ci-ubsan --output-on-failure \
  -R 'UvAtlas|^IntrinsicGeometryTests.Grouped$' --timeout 120 --parallel 1
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes

- No unordered iteration may affect published ordering or deterministic output.
- No chart-quality relaxation hidden inside a remap optimization.
- No speedup claim without `BENCH-001` matched evidence.

## Verification record

- Clang 23, unsanitized `ci`: configured and built `IntrinsicGeometryTests`,
  `IntrinsicUvAtlasRemapSmoke`, and `IntrinsicTests`. The original allocation
  regression failed with valid deterministic output; the candidate passed.
- All 16 atlas-focused CTest entries passed. The full CPU selector had zero
  failures across 4,267 registered entries, with one expected unsanitized
  GLFW/LSan skip.
- The isolated `ci-asan` allocation regression and the grouped geometry suite
  passed; the group contains 1,394 underlying GoogleTest cases. ASan exposed
  a nothrow-new mismatch in the initial probe, which was fixed before the
  final matched before/after collection. The corrected probe passed the
  focused `ci` selector again. No production gate or threshold was relaxed.
- The matching `ci-ubsan` allocation regression and grouped geometry suite
  also passed (1,394 underlying geometry cases). Both sanitizer selectors
  ran serially with `--parallel 1`.
- Exact small-fixture and many-chart output snapshots match before/after.
  [Local diagnostics and reproducible invocation](../../../docs/benchmarking/bug159-atlas-remap-diagnostics.md)
  bind the source/harness hashes and preserve `claim_eligible: false`.
- Strict layering, test-layout, benchmark-manifest, benchmark-result, and ARA
  ledger checks passed. No public module surface or dependency edge changed.

## Maturity

- Scope: the concrete built-in FastStaged CPU backend, exercised by the
  `benchmark;regression;geometry` allocation test and geometry tests.
- No GPU behavior or product performance claim is made. BENCH-001 retains
  matched representative workflow evidence; BUG-160 owns chart quality.
