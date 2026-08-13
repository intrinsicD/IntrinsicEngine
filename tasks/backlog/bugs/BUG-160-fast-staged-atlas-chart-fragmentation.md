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

- [ ] Freeze curved/open/closed/noisy fixtures and chart-count, seam, finite UV,
      overlap/stretch, determinism, timing, and memory diagnostics.
- [ ] Compare the smallest adaptive/local charting correction with the existing
      xatlas path on matched output requirements.
- [ ] Adopt the simplest policy that passes the quality gates and product
      benchmark; remove or stop selecting a losing FastStaged path rather than
      retaining two unjustified defaults.
- [ ] Keep requested/actual backend and fallback diagnostics truthful.

## Tests

- [ ] Add a smooth curved regression that fails the current face-scale chart
      explosion and passes a frozen chart/seam bound.
- [ ] Preserve deterministic atlas hashes/diagnostics across repeated runs.
- [ ] Assert finite UVs, complete face coverage, non-overlap/quality bounds,
      cancellation, and fallback behavior.
- [ ] Pass the default CPU gate and `BENCH-001` comparison protocol.

## Docs

- [ ] Document the chosen chart/default-backend policy and numerical limits.
- [ ] Record matched candidate/rejection evidence before changing the default.

## Acceptance criteria

- [ ] Representative smooth meshes no longer degrade toward one chart per face.
- [ ] The selected path meets the product time/memory thresholds without
      violating atlas quality or determinism.
- [ ] A rejected candidate is removed from selection or clearly retained only
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
