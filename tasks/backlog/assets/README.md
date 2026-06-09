# Assets Backlog
Planned work for promoted CPU asset authority, import/export orchestration,
payload registration, and runtime handoff. `assets` remains CPU-only and
GPU-agnostic; GPU residency lives under `src/graphics/assets` and is wired by
`runtime`.

## Tasks

- [ASSETIO-002 — Asset error and reload taxonomy](ASSETIO-002-asset-error-reload-taxonomy.md):
  promotes deterministic asset error/load-state/reload/destroy-order contracts
  without recreating the legacy asset manager.
- [ASSETIO-003 — KTX texture import decision and handoff](ASSETIO-003-ktx-texture-import-handoff.md):
  value-gates KTX/KTX2 before adding any decode route or runtime-to-GPU handoff.
- [ASSETIO-004 — Representative file-format visual coverage](ASSETIO-004-broad-file-format-visual-parity.md):
  proves representative promoted import/materialization/visual workflows for
  current mesh, graph, point-cloud, model-scene, and texture needs.

## Retired
- [ASSETIO-001 — Asset model, texture, and import/export ingest ownership](../../done/ASSETIO-001-asset-model-texture-ingest-ownership.md):
  retired at `CPUContracted` with the owner split around legacy graphics IO
  registry, GLTF/GLB model ingest, CPU texture decode payloads, extension
  routing, runtime-to-graphics texture residency handoff, and runtime-owned
  model-scene ECS/material handoff implemented. The scoped working-sandbox
  operational proof is closed by RUNTIME-095; representative file-format visual coverage
  remains future asset/UI follow-up work.
