# Geometry Backlog

Geometry IO parity, method-readiness seeds, and algorithm hardening.
`geometry -> core` only; `src/geometry/*` must not import
`assets`/`runtime`/`graphics`/`rhi`.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [GEOIO-002 — Geometry IO parity hardening and exporters](../../done/GEOIO-002-geometry-io-parity-hardening.md) (done).
- [GEOM-006 — Indexed mesh/soup container and conversion contracts](../../done/GEOM-006-indexed-mesh-soup-conversion-contracts.md) (done).
- [GEOM-007 — Robust predicates and intersection classification foundation](../../active/GEOM-007-robust-predicates-intersection-classification.md) (active).
- [GEOM-008 — Geometry linear algebra and solver infrastructure](GEOM-008-linear-algebra-solver-infrastructure.md).
- [GEOM-009 — Geometry benchmark manifests, fixtures, and smoke benchmark](../../done/GEOM-009-benchmark-manifests-fixtures-smoke.md) (done).
- [GEOM-010 — Point-cloud algorithm pack roadmap](GEOM-010-point-cloud-algorithm-pack-roadmap.md).
- [GEOM-011 — Parameterization and mapping roadmap](GEOM-011-parameterization-mapping-roadmap.md).
- [GEOM-012 — Symmetric mesh, graph, and point-cloud domain views](GEOM-012-symmetric-domain-views-property-sharing.md).
- [GEOM-013 — Feature-preserving dual contouring](GEOM-013-feature-preserving-dual-contouring.md).
- [GEOM-014 — Feature-aware quadric error mesh simplification](GEOM-014-feature-aware-quadric-error-simplification.md).
- [RORG-031E — Geometry and method-readiness backlog seed](RORG-031-geometry-method-readiness.md).

## Convergence

- GEOIO-002 is retired in [`tasks/done`](../../done/GEOIO-002-geometry-io-parity-hardening.md)
  and contributes to **Theme E — Geometry IO completion** as the upstream gate
  for [`assets/ASSETIO-001`](../assets/ASSETIO-001-asset-model-texture-ingest-ownership.md)
  and asset-backed mesh residency in **Theme A — Shortest path to sandbox
  visible geometry** (`rendering/GRAPHICS-034`).
- [GEOM-005](../../done/GEOM-005-api-style-and-numeric-policy.md) is retired in
  `tasks/done` and provides the canonical geometry API/numeric policy for future
  work.
- GEOM-006 through GEOM-008 are the remaining foundational tasks from the
  [`src/geometry` gap analysis](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md):
  mesh/soup contracts, robust predicates, and linear algebra. GEOM-009
  (benchmark manifests) is retired in
  [`tasks/done/`](../../done/GEOM-009-benchmark-manifests-fixtures-smoke.md)
  and provides the manifest-driven smoke harness future geometry method
  packages plug into.
- GEOM-010 and GEOM-011 turn point-cloud and parameterization gaps into
  method-compliant roadmap packs before algorithm implementation begins.
- GEOM-012 ensures mesh, graph, and point-cloud algorithms can share compatible
  property storage through explicit borrowed views instead of accidental copies.
- GEOM-013 and GEOM-014 are seeded by the geometry paper survey
  [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md).
  Each lists explicit algorithm variants and requires the maintainer to mark one
  as the default before implementation begins. GEOM-013 (dual contouring) is a
  peer of the existing `Geometry.MarchingCubes`; GEOM-014 (FA-QEM) is an in-place
  extension of `Geometry.HalfedgeMesh.Simplification`.
- RORG-031E is part of **Theme F — Architecture/runtime/UI foundation seeds**.
- Future geometry algorithm packages should follow
  [`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
  CPU reference first, correctness tests, benchmark harness, optimized CPU,
  GPU only after reference parity.
