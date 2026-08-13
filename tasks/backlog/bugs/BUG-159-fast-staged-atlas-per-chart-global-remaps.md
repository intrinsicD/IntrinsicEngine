---
id: BUG-159
theme: J
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

## Required changes

- [ ] Replace retained per-chart global remaps with the smallest deterministic
      sparse/chart-local representation.
- [ ] Replace per-chart output global remaps with chart-local output indexing.
- [ ] Preserve source cross-references, UVs, face/chart assignments, seams,
      copied properties, diagnostics, cancellation, and failure semantics.
- [ ] Record before/after diagnostic evidence through `BENCH-001`; do not turn
      a local debug run into a performance claim.

## Tests

- [ ] Add exact output/parity coverage on existing atlas fixtures.
- [ ] Add a deterministic many-chart stress fixture that would expose global
      per-chart allocation growth and verifies finite, complete output.
- [ ] Pass geometry unit/contract tests and the default CPU gate.

## Docs

- [ ] Document the retained complexity bound near the implementation if it is
      not clear from the chosen data structure.
- [ ] Update benchmark evidence, not architecture docs, with measured impact.

## Acceptance criteria

- [ ] No source-vertex-sized container is retained or initialized inside a
      chart loop.
- [ ] Remap storage is `O(V + sum(chart-local vertices))` or better.
- [ ] Existing atlas results and deterministic diagnostics remain equivalent
      except for timing/memory observations.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'UvAtlas' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- No unordered iteration may affect published ordering or deterministic output.
- No chart-quality relaxation hidden inside a remap optimization.
- No speedup claim without `BENCH-001` matched evidence.
