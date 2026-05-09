# Geometry Backlog

Geometry IO parity, method-readiness seeds, and algorithm hardening.
`geometry -> core` only; `src/geometry/*` must not import
`assets`/`runtime`/`graphics`/`rhi`.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [GEOIO-002 — Geometry IO parity hardening and exporters](GEOIO-002-geometry-io-parity-hardening.md).
- [RORG-031E — Geometry and method-readiness backlog seed](RORG-031-geometry-method-readiness.md).

## Convergence

- GEOIO-002 contributes to **Theme E — Geometry IO completion** and is the
  upstream gate for [`assets/ASSETIO-001`](../assets/ASSETIO-001-asset-model-texture-ingest-ownership.md)
  and asset-backed mesh residency in **Theme A — Shortest path to sandbox
  visible geometry** (`rendering/GRAPHICS-034`).
- RORG-031E is part of **Theme F — Architecture/runtime/UI foundation seeds**.
- Future geometry algorithm packages should follow
  [`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
  CPU reference first, correctness tests, benchmark harness, optimized CPU,
  GPU only after reference parity.
