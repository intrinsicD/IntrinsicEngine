---
id: RUNTIME-291
theme: G
depends_on: []
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the operator's panel check (2026-09-29); implementation owes contract tests and a live sandbox check.
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# RUNTIME-291 — Scale-aware default radii for point operations

## Goal
- Operator check (2026-09-29, Vlasic mesh_0060, 10002 vertices, bounding-box diagonal 2.14):
  Compact Density Weights starts with `support_radius` 1.0 in absolute world units, about half
  the model. The Vulkan radius query then overflows its 256-candidate capacity, so the output
  is left unchanged. A radius of 0.03 worked (at most 49 candidates per row). Other absolute
  defaults (e.g. Outlier Analysis `radius` 1.0, Smooth Property `spatial_sigma`/`range_sigma`
  1.0, the Hoppe construction's scales) can be wrong in the same way on other models.
- Find a sensible default derived from the entity: from its bounding box / bounding volume
  (diagonal, or volume or area per point), or from mean nearest-neighbor spacing, which the
  Point Spacing operation already computes (it reports min / mean / max nearest spacing).
  Point Sampling's `elimination_radius` already follows the pattern "0 derives it from the
  bounding box"; FPFH derives its feature radius as five times the mean nearest spacing.
- Prefer one shared helper at the owner (runtime point-input capture or the point-cloud
  geometry layer) over per-panel formulas, and follow the existing "0 = automatic" convention.
  The resolved radius should be shown in the result, and a query capacity overflow should say
  "radius too large" with the suggested value.

## Acceptance criteria
- [ ] Survey every radius, sigma and bandwidth field of the point panels (Density Weights,
      Outlier Analysis radius mode, Normal Estimation radius mode, Smooth Property, Bilateral,
      Point Construction) and record which are absolute and what each scales with.
- [ ] Compact Density Weights (at least) defaults to an entity-derived radius: `support_radius`
      0 = automatic, reported in the result. On the Vlasic mesh it runs without overflow.
- [ ] The automatic rule is the same for mesh vertices, graph nodes and point clouds
      (`geometry.element-domain-sources`) and does not change under uniform scaling of the
      model (a contract test scales one fixture by 1000 and gets the same weights).
- [ ] An overflow message suggests a smaller radius instead of silently keeping the old output.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'DensityWeight|KernelDensity|PointSpacing|OutlierAnalysis|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```
