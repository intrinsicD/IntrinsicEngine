# Assets Backlog
Planned work for promoted CPU asset authority, import/export orchestration,
payload registration, and runtime handoff. `assets` remains CPU-only and
GPU-agnostic; GPU residency lives under `src/graphics/assets` and is wired by
`runtime`.

## Retired
- [ASSETIO-001 — Asset model, texture, and import/export ingest ownership](../../done/ASSETIO-001-asset-model-texture-ingest-ownership.md):
  retired at `CPUContracted` with the owner split around legacy graphics IO
  registry, GLTF/GLB model ingest, CPU texture decode payloads, extension
  routing, runtime-to-graphics texture residency handoff, and runtime-owned
  model-scene ECS/material handoff implemented. `Operational` file/import
  sandbox proof remains owned by `RUNTIME-095` and UI follow-up work.
