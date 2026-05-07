# Assets Backlog
Planned work for promoted CPU asset authority, import/export orchestration,
payload registration, and runtime handoff. `assets` remains CPU-only and
GPU-agnostic; GPU residency lives under `src/graphics/assets` and is wired by
`runtime`.
- [ASSETIO-001 — Asset model, texture, and import/export ingest ownership](ASSETIO-001-asset-model-texture-ingest-ownership.md):
  defines the promoted owner split for legacy graphics IO registry, GLTF/GLB
  model ingest, CPU texture decode payloads, extension routing, and
  runtime-to-graphics asset residency handoff.
