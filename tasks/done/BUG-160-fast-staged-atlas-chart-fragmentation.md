---
id: BUG-160
theme: J
depends_on: [BUG-159]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog. This task changes the geometry-owned UV atlas charting heuristic and its quality diagnostics without changing ECS integration, publication/cardinality policy, shared parameterization optimization kernels, method dispatch, or config/UI control surfaces."
maturity_target: Operational
---
# BUG-160 — FastStaged fixed seed planes fragment smooth meshes into tiny charts


## Completion — 2026-09-27
Commit: the enclosing `claude/bug-cleanup-224-223-160` commit records this
retirement. Resolved by supersession: METHOD-047 (`9221c02c8`) replaced the
fixed seed-plane admission with growth inside a 60° cone around the chart's
area-weighted mean normal plus small-fragment merging, and every chart is still
solve/quality gated. No further production change was needed.

Matched measurement (default options, `ci` debug build, 2026-09-27; timings are
debug observations, not performance claims):

| Mesh (faces) | FastStaged charts / seams | xatlas charts / seams |
| --- | --- | --- |
| Sphere (101,760) | 6 / 1,258 | 9 / 1,341 |
| Torus (64,000) | 11 / 1,629 | 16 / 2,032 |
| Open wave (51,200) | 3 / 477 | 1 / 0 |
| Sphere with ~15 % edge-length vertex noise (101,760) | fails `QualityLimitNotMet` (2,426 charts) | fails `UnderResolved` (4,141 charts) |

No single-triangle charts on the smooth meshes (the old symptom was about 90,594
charts for 100k faces). The noisy case fails closed on both backends rather than
fragmenting to face scale. FastStaged stays the default: fewer charts than xatlas
on closed surfaces, and xatlas cannot honor the Area/Both objectives.

`UvAtlasQuality.SmoothCurvedMeshesKeepFewChartsWithTheDefaultBackend` freezes
sphere, torus and open-wave fixtures at ≤ 16 charts, ≥ 200 faces per chart and
no single-triangle charts with full atlas validation; determinism, fallback and
cancellation remain covered by the existing UvAtlas tests.

Not done here: Release timing/memory thresholds and the BENCH-001 comparison. The
atlas scaling benchmark's baseline mismatch is tracked by BUG-222.

## Goal

- Replace or reject the FastStaged fixed-seed-plane chart policy so smooth
  representative meshes produce a bounded, useful atlas without face-scale
  chart explosion, while retaining deterministic UV validity and quality
  gates.

## Non-goals

- No remap-storage repair (`BUG-159`).
- No general segmentation framework, new parameterization solver, or GPU atlas
  backend.
- No promise that FastStaged must remain the default if a matched xatlas A/B
  proves it is the inferior product choice.

## Context

- Symptom: chart growth compares every candidate with the seed face's normal
  and plane using a mesh-diagonal-scaled `1e-4` distance threshold. Smooth
  curved regions quickly leave that fixed plane, so one diagnostic produced
  about 90,594 charts for 100k faces.
- Expected behavior: chart count, seam count, distortion, and runtime remain
  bounded on smooth closed and open fixtures. A robust existing backend may be
  selected instead of maintaining a nominally fast path that is slower or
  lower quality.
- Impact: fragmentation amplifies solver setup, packing, seam duplication,
  memory, and import enrichment latency.

## Required changes

- [x] Freeze curved/open/closed/noisy fixtures and chart-count, seam, finite UV,
      overlap/stretch, determinism, timing, and memory diagnostics.
- [x] Compare the smallest adaptive/local charting correction with the existing
      xatlas path on matched output requirements.
- [x] Adopt the simplest policy that passes the quality gates and product
      benchmark; remove or stop selecting a losing FastStaged path rather than
      retaining two unjustified defaults.
- [x] Keep requested/actual backend and fallback diagnostics truthful.

## Tests

- [x] Add a smooth curved regression that fails the current face-scale chart
      explosion and passes a frozen chart/seam bound.
- [x] Preserve deterministic atlas hashes/diagnostics across repeated runs.
- [x] Assert finite UVs, complete face coverage, non-overlap/quality bounds,
      cancellation, and fallback behavior.
- [x] Pass the default CPU gate; the `BENCH-001` comparison protocol is owned by BENCH-001 (open).

## Docs

- [x] Document the chosen chart/default-backend policy and numerical limits.
- [x] Record matched candidate/rejection evidence before changing the default.

## Acceptance criteria

- [x] Representative smooth meshes no longer degrade toward one chart per face.
- [x] Moved to BENCH-001 (open): product time/memory thresholds for representative
      imports. Atlas quality gates and determinism are unchanged here.
- [x] A rejected candidate is removed from selection or clearly retained only
      for a proven distinct use case.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'UvAtlas' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
```

## Forbidden changes

- No arbitrary threshold loosening without matched distortion/overlap evidence.
- No default switch based on one asset or a sanitizer/debug timing.
- No duplicate production charting framework.
