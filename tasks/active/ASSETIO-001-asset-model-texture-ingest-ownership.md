# ASSETIO-001 — Asset model, texture, and import/export ingest ownership

## Status
- Status: in-progress.
- Owner/agent: Codex.
- Branch: `main`.
- Current slice: Slice B verified. The CPU-only import/export route contract
  and asset-owned geometry callback bridge are promoted; runtime registers the
  concrete geometry decoders/encoders without importing geometry into
  `src/assets`.
- Next verification step: continue Slice C for model/scene and texture CPU
  payload ownership.

## Slice plan
- **Slice A.** Add `Extrinsic.Asset.ImportRouter` as a CPU-only routing
  contract for extension lookup, import/export operation, payload/domain hints,
  typed payload selection, and deterministic route diagnostics. The slice does
  not decode files or register payloads; it keeps geometry/model/texture decode
  callbacks deferred behind later slices.
- **Slice B.** Wire promoted geometry decoder/encoder callbacks for OBJ, OFF,
  STL, PLY, PCD, XYZ/PTS/XYZRGB, TGF, and edge-list routes through the asset
  routing contract without importing geometry into `src/assets`.
- **Slice C.** Add model/scene and texture CPU payload types plus GLTF/GLB and
  texture decode ownership, including external resource diagnostics and metadata
  payloads.
- **Slice D.** Add runtime handoff from decoded asset payloads/events to ECS
  construction and graphics/assets residency requests, preserving runtime as the
  only layer that names both CPU assets and GPU residency.

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
- `GEOIO-001` promoted minimal geometry-owned mesh, graph, and point-cloud loaders. `GEOIO-002` retired with geometry importer/exporter parity hardening and confirmed geometry-owned codecs/domain metadata are sufficient for this task's asset/runtime routing.
- `GRAPHICS-028` tracks the ECS renderable-to-`GpuWorld` runtime residency bridge that consumes asset IDs and geometry payloads.
## Required changes
- [x] Define a CPU-only asset import/export routing contract for extension lookup, import hints, typed payload selection, and error reporting. Slice A adds `Extrinsic.Asset.ImportRouter`.
- [x] Define how asset ingest invokes geometry-owned decoders/encoders for OBJ/OFF/STL/PLY/PCD/XYZ/TGF without importing graphics or RHI. Slice B adds `Extrinsic.Asset.GeometryIOBridge` plus runtime-owned `RegisterPromotedGeometryIOCallbacks(...)`; `src/assets` dispatches callbacks by resolved route, while `src/runtime` imports geometry and translates decoder errors into promoted core errors.
- [ ] Define and implement GLTF/GLB CPU ingest ownership for model/scene payloads, external buffer/image resolution through `Core.IOBackend` or promoted asset path services, embedded image payloads, material texture references, and mesh primitive extraction through geometry-owned helpers.
- [ ] Define and implement CPU texture decode payload ownership and metadata (`dimensions`, format/color space, component count, source path/generation) without creating GPU resources in `assets`.
- [ ] Define runtime handoff from decoded asset payloads/events to ECS entity construction and `graphics/assets::GpuAssetCache` upload requests.
- [ ] Add diagnostics for unsupported formats, ambiguous domain selection, external-resource failures, decode failures, and payload registration failures. Slice A covers route-level missing/unsupported/ambiguous/payload-mismatch diagnostics; Slice B maps geometry decode/write failures through promoted core error codes. External-resource and model/texture payload-registration diagnostics remain for later slices.
## Tests
- [ ] Add `unit;assets` tests for extension routing, import hints, typed payload registration, failure mapping, texture metadata decode, and deterministic error diagnostics. Slice A adds `AssetImportRouter` routing/failure diagnostics coverage; Slice B adds `AssetGeometryIOBridge` typed callback registration/dispatch and failure-path coverage. Texture metadata decode remains for later slices.
- [ ] Add `integration;runtime;assets` tests for decoded geometry/model payloads flowing to runtime-owned ECS setup or residency requests without graphics owning file IO. Slice B adds `contract;runtime` coverage for promoted geometry decoder/encoder callback registration and real mesh/point-cloud import/export dispatch; ECS setup and residency requests remain for Slice D.
- [ ] Add `contract;graphics;assets` tests or existing `GpuAssetCache` coverage only for GPU-side cache behavior; do not make `src/assets` depend on graphics.
## Docs
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` when ownership is implemented or staged. Slices A/B record the promoted route-contract and geometry callback surfaces; later model/texture and handoff slices still need updates.
- [ ] Cross-link `GRAPHICS-019`, `GRAPHICS-020`, `GRAPHICS-028`, and `GEOIO-002` from implementation notes. Slices A/B update `src/assets/README.md`, `src/runtime/README.md`, and parity/gate docs for the route and geometry callback contracts.
## Acceptance criteria
- [ ] Legacy graphics IO/model/texture responsibilities have promoted CPU asset/routing owners with explicit runtime and graphics-residency handoff seams.
- [ ] `src/assets/*` remains CPU-only and imports no `graphics/*`, `graphics/rhi`, `runtime`, or live ECS modules.
- [ ] Rendering tasks can consume asset IDs, runtime-submitted snapshots, geometry GPU views, or `graphics/assets` residency APIs without direct model/texture parsing.
- [ ] Tests cover supported routing/failure states and prove layer boundaries for the new ingest path.
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

## Maturity
- Slice A closes `Scaffolded` for asset-owned route selection and diagnostics:
  callers can classify supported import/export operations without graphics,
  runtime, ECS, or geometry imports in `src/assets`.
- Slice B closes `CPUContracted` for promoted geometry callback invocation:
  `src/assets` resolves and dispatches geometry import/export callbacks without
  importing geometry, while `src/runtime` registers the concrete promoted
  geometry codecs. `Operational` owned by `RUNTIME-095`.
- Later slices extend `CPUContracted` by registering model/texture payloads,
  proving runtime handoff, and covering texture/model metadata.
- `Operational` file-backed sandbox proof remains owned by
  [`RUNTIME-095`](../backlog/runtime/RUNTIME-095-working-sandbox-acceptance.md)
  after UI and runtime handoff slices compose the route contract.
