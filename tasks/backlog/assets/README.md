# Assets Backlog
Planned work for promoted CPU asset authority, import/export orchestration,
payload registration, and runtime handoff. `assets` remains CPU-only and
GPU-agnostic; GPU residency lives under `src/graphics/assets` and is wired by
`runtime`.

## Tasks

No active asset backlog tasks.

## Retired
- [ASSETIO-004 — Representative file-format visual coverage](../../done/ASSETIO-004-broad-file-format-visual-parity.md):
  retired at `CPUContracted` with a generated representative runtime import
  matrix for OBJ mesh, TGF graph, ASCII PLY point cloud, GLTF model-scene with
  embedded PNG texture, standalone PNG texture import, retryable headless upload
  deferrals, and material texture binding re-resolution after upload/reload.
- [ASSETIO-003 — KTX texture import decision and handoff](../../done/ASSETIO-003-ktx-texture-import-handoff.md):
  retired at `CPUContracted` with KTX/KTX2 explicitly unsupported for current
  promoted workflows; route recognition remains only to return deterministic
  `AssetUnsupportedFormat` diagnostics.
- [ASSETIO-002 — Asset error and reload taxonomy](../../done/ASSETIO-002-asset-error-reload-taxonomy.md):
  retired at `CPUContracted` with deterministic operation-status diagnostics,
  reload payload-ticket rollback, reload/ready event ordering, destroy-time
  same-asset event draining, and runtime handoff observation coverage.
- [ASSETIO-001 — Asset model, texture, and import/export ingest ownership](../../done/ASSETIO-001-asset-model-texture-ingest-ownership.md):
  retired at `CPUContracted` with the owner split around legacy graphics IO
  registry, GLTF/GLB model ingest, CPU texture decode payloads, extension
  routing, runtime-to-graphics texture residency handoff, and runtime-owned
  model-scene ECS/material handoff implemented. The scoped working-sandbox
  operational proof is closed by RUNTIME-095; representative file-format
  coverage is retired by ASSETIO-004.
