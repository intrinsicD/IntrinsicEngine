# ASSETIO-001 — Asset model, texture, and import/export ingest ownership
## Goal
- Define and implement the promoted CPU asset ingest/export orchestration that replaces legacy `Graphics.IORegistry`, `Graphics.Importers.GLTF`, `Graphics.TextureLoader`, `Graphics.ModelLoader`, and `Graphics.Model` ownership without moving file IO or decode policy into final graphics layers.
## Non-goals
- No direct GPU upload implementation inside `src/assets`.
- No renderer pass, shader, or pipeline work.
- No geometry parser/exporter implementation except through public geometry APIs.
- No live ECS ownership inside `src/graphics/*` and no graphics/RHI imports inside `src/assets/*`.
## Context
- Owner: `assets` for CPU asset identity, routing, typed payload registration, path lookup, load-state transitions, and import/export orchestration; `runtime` for wiring decoded payloads to ECS and graphics residency; `graphics/assets` for GPU-side residency keyed by `Assets::AssetId`.
- `GRAPHICS-019` records that legacy graphics IO/model/texture loaders must retire through assets/geometry/runtime ownership, not promoted graphics imports.
- `GEOIO-001` promoted minimal geometry-owned mesh, graph, and point-cloud loaders. `GEOIO-002` tracks geometry importer/exporter parity hardening.
- `GRAPHICS-028` tracks the ECS renderable-to-`GpuWorld` runtime residency bridge that consumes asset IDs and geometry payloads.
## Required changes
- Define a CPU-only asset import/export routing contract for extension lookup, import hints, typed payload selection, and error reporting.
- Define how asset ingest invokes geometry-owned decoders/encoders for OBJ/OFF/STL/PLY/PCD/XYZ/TGF without importing graphics or RHI.
- Define and implement GLTF/GLB CPU ingest ownership for model/scene payloads, external buffer/image resolution through `Core.IOBackend` or promoted asset path services, embedded image payloads, material texture references, and mesh primitive extraction through geometry-owned helpers.
- Define and implement CPU texture decode payload ownership and metadata (`dimensions`, format/color space, component count, source path/generation) without creating GPU resources in `assets`.
- Define runtime handoff from decoded asset payloads/events to ECS entity construction and `graphics/assets::GpuAssetCache` upload requests.
- Add diagnostics for unsupported formats, ambiguous domain selection, external-resource failures, decode failures, and payload registration failures.
## Tests
- Add `unit;assets` tests for extension routing, import hints, typed payload registration, failure mapping, texture metadata decode, and deterministic error diagnostics.
- Add `integration;runtime;assets` tests for decoded geometry/model payloads flowing to runtime-owned ECS setup or residency requests without graphics owning file IO.
- Add `contract;graphics;assets` tests or existing `GpuAssetCache` coverage only for GPU-side cache behavior; do not make `src/assets` depend on graphics.
## Docs
- Update `docs/migration/nonlegacy-parity-matrix.md` when ownership is implemented or staged.
- Cross-link `GRAPHICS-019`, `GRAPHICS-020`, `GRAPHICS-028`, and `GEOIO-002` from implementation notes.
## Acceptance criteria
- Legacy graphics IO/model/texture responsibilities have promoted CPU asset/routing owners with explicit runtime and graphics-residency handoff seams.
- `src/assets/*` remains CPU-only and imports no `graphics/*`, `graphics/rhi`, `runtime`, or live ECS modules.
- Rendering tasks can consume asset IDs, runtime-submitted snapshots, geometry GPU views, or `graphics/assets` residency APIs without direct model/texture parsing.
- Tests cover supported routing/failure states and prove layer boundaries for the new ingest path.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Asset|Runtime.*Asset|GpuAssetCache' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```
## Forbidden changes
- Moving importer/exporter ownership into `src/graphics/*`.
- Adding GPU/RHI types to `src/assets/*` payload APIs.
- Introducing runtime or ECS dependencies into `src/assets/*`.
- Mixing mechanical legacy deletion with semantic ingest implementation.
