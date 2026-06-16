# Geometry Backlog

Geometry IO parity, method-readiness seeds, and algorithm hardening.
`geometry -> core` only; `src/geometry/*` must not import
`assets`/`runtime`/`graphics`/`rhi`.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [GEOM-013 — Feature-preserving dual contouring](GEOM-013-feature-preserving-dual-contouring.md).
- [GEOM-014 — Feature-aware quadric error mesh simplification](GEOM-014-feature-aware-quadric-error-simplification.md).
- [GEOM-016 — Point-cloud filtering and density diagnostics contracts](GEOM-016-point-cloud-filtering-density-contracts.md).
- [GEOM-017 — Point-cloud descriptors and registration seams](GEOM-017-point-cloud-descriptors-registration-seams.md).
- [GEOM-019 — Harmonic/Tutte parameterization and boundary constraints](GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md).
- [GEOM-020 — Sparse direct factorization solver seam (LDLT/LLT)](GEOM-020-sparse-direct-factorization-seam.md)
  (follow-up to retired `GEOM-008`; gates `methods/METHOD-002` and the
  shift-invert inner solve of `GEOM-024`).
- [GEOM-023 — Sparse non-symmetric iterative solver seam (BiCGSTAB)](GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md)
  (gates `methods/METHOD-003` variant A; promote when METHOD-003 is the
  next-priority method).
- [GEOM-024 — Sparse symmetric generalized eigensolver seam](GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md)
  (gates `methods/METHOD-006` variant B; depends on `GEOM-020`; promote when
  METHOD-006 is the next-priority method).
- [RORG-031E — Geometry and method-readiness backlog seed](RORG-031-geometry-method-readiness.md).

## Convergence

- The foundational tasks from the
  [`src/geometry` gap analysis](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md)
  — mesh/soup contracts (GEOM-006), robust predicates (GEOM-007), and linear
  algebra (GEOM-008) — are all retired. GEOM-009 (benchmark manifests) is
  retired in `tasks/done/` and provides the manifest-driven smoke harness
  future geometry method packages plug into.
- GEOM-020 is the named follow-up to retired GEOM-008 for the direct
  sparse SPD factorization (LDLT/LLT) seam that GEOM-008 deferred but
  that methods/METHOD-002 (step 2) and METHOD-003 (step 5) already
  reference as "the LDLT path from GEOM-008". Method tasks gated on the
  LDLT path must wait on GEOM-020, not on retired GEOM-008; the gates are
  encoded in the method tasks' `depends_on` front-matter.
- GEOM-023 (non-symmetric BiCGSTAB) and GEOM-024 (generalized symmetric
  eigensolver, Spectra-backed, gated on GEOM-020) complete the solver-seam
  family: they own the gaps GEOM-020 explicitly deferred and gate
  METHOD-003 variant A and METHOD-006 variant B respectively. Promote each
  only when its consuming method is the next-priority method.
- GEOM-021 is a module-hygiene follow-up for retired GEOM-006: it keeps the
  `Geometry.MeshSoup` public module interface declarative by moving
  non-trivial validation/container bodies into a matching implementation unit
  and trimming interface-only includes/imports.
- GEOM-022 is the same module-interface hygiene pass for the remaining
  promoted geometry cleanup targets found by the 2026-06-06 implementation-body
  audit, excluding `Geometry.MeshSoup` which is owned by GEOM-021.
- GEOM-012 ensures mesh, graph, and point-cloud algorithms can share compatible
  property storage through explicit borrowed views instead of accidental copies.
- GEOM-013 and GEOM-014 are seeded by the geometry paper survey
  [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md).
  Each lists explicit algorithm variants with a marked default (Manifold DC
  for GEOM-013, FA-QEM for GEOM-014). GEOM-013 (dual contouring) is a
  peer of the existing `Geometry.MarchingCubes`; GEOM-014 (FA-QEM) is an in-place
  extension of `Geometry.HalfedgeMesh.Simplification`.
- RORG-031E is part of **Theme F — Architecture/runtime/UI foundation seeds**.
- Future geometry algorithm packages should follow
  [`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
  CPU reference first, correctness tests, benchmark harness, optimized CPU,
  GPU only after reference parity.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [GEOIO-002 — Geometry IO parity hardening and exporters](../../done/GEOIO-002-geometry-io-parity-hardening.md) (done).
- [GEOM-006 — Indexed mesh/soup container and conversion contracts](../../done/GEOM-006-indexed-mesh-soup-conversion-contracts.md) (done).
- [GEOM-007 — Robust predicates and intersection classification foundation](../../done/GEOM-007-robust-predicates-intersection-classification.md) (done).
- [GEOM-008 — Geometry linear algebra and solver infrastructure](../../done/GEOM-008-linear-algebra-solver-infrastructure.md) (done).
- [GEOM-009 — Geometry benchmark manifests, fixtures, and smoke benchmark](../../done/GEOM-009-benchmark-manifests-fixtures-smoke.md) (done).
- [GEOM-010 — Point-cloud algorithm pack roadmap](../../done/GEOM-010-point-cloud-algorithm-pack-roadmap.md) (done; roadmap: [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../../docs/architecture/point-cloud-algorithm-roadmap.md)).
- [GEOM-011 — Parameterization and mapping roadmap](../../done/GEOM-011-parameterization-mapping-roadmap.md) (done; roadmap: [`docs/architecture/parameterization-mapping-roadmap.md`](../../../docs/architecture/parameterization-mapping-roadmap.md)).
- [GEOM-012 — Symmetric mesh, graph, and point-cloud domain views](../../done/GEOM-012-symmetric-domain-views-property-sharing.md) (done).
- [GEOM-018 — Parameterization distortion and map-quality diagnostics](../../done/GEOM-018-parameterization-distortion-map-quality-diagnostics.md) (done).
- [GEOM-015 — GJK termination diagnostics and scale-aware tolerance policy](../../done/GEOM-015-gjk-termination-diagnostics.md) (done).
- [GEOM-021 — MeshSoup module implementation split](../../done/GEOM-021-meshsoup-module-implementation-split.md).
- [GEOM-022 — Remaining geometry module implementation splits](../../done/GEOM-022-remaining-geometry-module-implementation-splits.md).
- [GEOM-025 — UV atlas backend contract and xatlas default](../../done/GEOM-025-uv-atlas-backend-xatlas.md) (done).
- GEOIO-002 is retired in [`tasks/done`](../../done/GEOIO-002-geometry-io-parity-hardening.md)
  and contributed to **Theme E — Geometry IO completion** as the upstream gate
  for retired [`ASSETIO-001`](../../done/ASSETIO-001-asset-model-texture-ingest-ownership.md)
  and asset-backed mesh residency in **Theme A — Shortest path to sandbox
  visible geometry** (`rendering/GRAPHICS-034`).
- [GEOM-005](../../done/GEOM-005-api-style-and-numeric-policy.md) is retired in
  `tasks/done` and provides the canonical geometry API/numeric policy for future
  work.
- GEOM-010 is retired in [`tasks/done`](../../done/GEOM-010-point-cloud-algorithm-pack-roadmap.md)
  and records the point-cloud method-compliant roadmap in
  [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../../docs/architecture/point-cloud-algorithm-roadmap.md).
  Its first implementation packs are GEOM-016 (filtering/density diagnostics)
  and GEOM-017 (descriptors/registration seams).
- GEOM-011 is retired in [`tasks/done`](../../done/GEOM-011-parameterization-mapping-roadmap.md)
  and records the parameterization/mapping method-compliant roadmap in
  [`docs/architecture/parameterization-mapping-roadmap.md`](../../../docs/architecture/parameterization-mapping-roadmap.md).
  Its first implementation packs are GEOM-018 (distortion/map-quality
  diagnostics) and GEOM-019 (harmonic/Tutte parameterization and boundary
  constraints).
