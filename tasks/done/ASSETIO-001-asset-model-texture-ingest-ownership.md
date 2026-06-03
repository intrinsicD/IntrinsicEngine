# ASSETIO-001 — Asset model, texture, and import/export ingest ownership

## Status
- Status: done.
- Completed: 2026-06-03.
- Owner/agent: Codex.
- Branch: `main`.
- Final implementation commit: this retirement commit.
- Maturity: `CPUContracted`; `Operational` file/import sandbox proof is owned by
  [`RUNTIME-095`](../backlog/runtime/RUNTIME-095-working-sandbox-acceptance.md).
- Summary: The CPU-only import/export route contract, asset-owned geometry
  callback bridge, promoted model/texture payload records, promoted
  model/texture decoder-callback bridge, concrete runtime GLTF/GLB and
  STB-backed PNG/JPEG/TGA/BMP/HDR decoder registrations, texture GPU-residency
  handoff, and model-scene ECS/material handoff are implemented and verified.
  Slice D.2 resolves embedded texture identity by minting deterministic child
  `AssetTexture2DPayload` assets at
  `<model-path>.embedded-texture-<image-index>.<ext>`, keyed by
  `Assets::AssetId`. Runtime materializes model primitives into ECS
  `GeometrySources` mesh entities, requests child texture uploads through the
  texture handoff, and records material texture bindings as child texture
  `AssetId`s. Generated model-scene entities intentionally do not stamp
  `AssetInstance::Source`, because current render extraction treats that
  component as the alternative asset-cache observation path and suppresses the
  `GeometrySources` mesh-residency lane.

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
- **Slice C.1.** Add `Extrinsic.Asset.ModelTexturePayload` as the CPU-only
  model-scene/texture payload contract: texture metadata/pixels, material
  texture references, model primitive payload references, external-resource
  diagnostics, validation helpers, and typed `AssetService` storage coverage.
  Defers actual GLTF/GLB and image decoder invocation to Slice C.2.
- **Slice C.2.** Add GLTF/GLB and texture decoder invocation through promoted
  asset-owned CPU APIs, including external buffer/image resolution and decode
  error mapping.
- **Slice C.2a.** Add `Extrinsic.Asset.ModelTextureIOBridge` as the CPU-only
  decoder-callback bridge for model-scene and texture payloads. Assets own
  route resolution, primary file reads through `Extrinsic.Core.IOBackend`,
  relative external-resource reads, callback dispatch, callback error
  propagation, and payload validation. Concrete tinygltf/stb decoder
  registration remains deferred to Slice C.2b.
- **Slice C.2b.** Register concrete GLTF/GLB and texture decoders against the
  promoted bridge, translate decoder-specific errors into promoted core errors,
  and map decoded embedded images/materials/geometry references into the Slice
  C.1 payload records. KTX decode remains intentionally loader-missing until a
  concrete KTX decoder slice exists.
- **Slice D.** Add runtime handoff from decoded asset payloads/events to ECS
  construction and graphics/assets residency requests, preserving runtime as the
  only layer that names both CPU assets and GPU residency.
- **Slice D.1.** Add runtime-owned texture payload handoff: subscribe to
  `AssetService` Ready events, read promoted `AssetTexture2DPayload` records,
  build `GpuTextureRequest` descriptors, and submit upload requests to
  `Graphics::GpuAssetCache`. The helper is idempotent for already-uploading or
  ready assets so Slice D.2 can request embedded child uploads without
  double-submitting when child texture `Ready` events are later observed.
- **Slice D.2.** Add runtime-owned model-scene entity construction and embedded
  texture/material handoff. Runtime mints deterministic embedded texture child
  assets, requests their texture uploads, converts mesh primitive payloads into
  ECS `GeometrySources`, creates `RenderSurface` entities, and records
  `MaterialTextureAssetBindings` by child `AssetId`. Post-upload material
  bindless re-resolution and file/import UI command execution are downstream
  `Operational` work owned by `RUNTIME-095`.

## Goal
- Define and implement the promoted CPU asset ingest/export orchestration that replaces legacy `Graphics.IORegistry`, `Graphics.Importers.GLTF`, `Graphics.TextureLoader`, `Graphics.ModelLoader`, and `Graphics.Model` ownership without moving file IO or decode policy into final graphics layers.
## Non-goals
- No direct GPU upload implementation inside `src/assets`.
- No renderer pass, shader, or pipeline work.
- No geometry parser/exporter implementation except through public geometry APIs.
- No live ECS ownership inside `src/graphics/*` and no graphics/RHI imports inside `src/assets/*`.
- No post-upload material bindless re-resolution, file/import UI command
  execution, or visible sandbox acceptance proof; those are downstream
  `Operational` work.
## Context
- Owner: `assets` for CPU asset identity, routing, typed payload registration, path lookup, load-state transitions, and import/export orchestration; `runtime` for wiring decoded payloads to ECS and graphics residency; `graphics/assets` for GPU-side residency keyed by `Assets::AssetId`.
- `GRAPHICS-019` records that legacy graphics IO/model/texture loaders must retire through assets/geometry/runtime ownership, not promoted graphics imports.
- `GEOIO-001` promoted minimal geometry-owned mesh, graph, and point-cloud loaders. `GEOIO-002` retired with geometry importer/exporter parity hardening and confirmed geometry-owned codecs/domain metadata are sufficient for this task's asset/runtime routing.
- `GRAPHICS-028` tracks the ECS renderable-to-`GpuWorld` runtime residency bridge that consumes asset IDs and geometry payloads.
## Required changes
- [x] Define a CPU-only asset import/export routing contract for extension lookup, import hints, typed payload selection, and error reporting. Slice A adds `Extrinsic.Asset.ImportRouter`.
- [x] Define how asset ingest invokes geometry-owned decoders/encoders for OBJ/OFF/STL/PLY/PCD/XYZ/TGF without importing graphics or RHI. Slice B adds `Extrinsic.Asset.GeometryIOBridge` plus runtime-owned `RegisterPromotedGeometryIOCallbacks(...)`; `src/assets` dispatches callbacks by resolved route, while `src/runtime` imports geometry and translates decoder errors into promoted core errors.
- [x] Define and implement GLTF/GLB CPU ingest ownership for model/scene payloads, external buffer/image resolution through `Core.IOBackend` or promoted asset path services, embedded image payloads, material texture references, and mesh primitive extraction through geometry-owned helpers. Slice C.1 adds the CPU-only model-scene payload records, material texture references, embedded-image records, and external-resource diagnostic contract. Slice C.2a adds the promoted bridge for primary/external resource reads, callback dispatch, error propagation, and payload validation. Slice C.2b adds runtime-owned GLTF/GLB decoder registration, embedded image/material mapping, external-resource diagnostics, and mesh geometry payload extraction into `AssetGeometryPayload` records.
- [x] Define and implement CPU texture decode payload ownership and metadata (`dimensions`, format/color space, component count, source path/generation) without creating GPU resources in `assets`. Slice C.1 adds the texture payload metadata/pixel records and validation helpers. Slice C.2a adds promoted texture callback invocation and decoded-payload validation. Slice C.2b registers runtime-owned STB-backed PNG/JPEG/TGA/BMP/HDR texture decoders; KTX remains loader-missing until a dedicated decoder exists.
- [x] Define runtime handoff from decoded asset payloads/events to ECS entity construction and `graphics/assets::GpuAssetCache` upload requests. Slice D.1 owns decoded texture payload Ready-event upload requests into `GpuAssetCache`; Slice D.2 owns model-scene ECS entity construction, deterministic embedded texture child assets, child texture upload requests, and material `AssetId` binding records.
- [x] Add diagnostics for unsupported formats, ambiguous domain selection, external-resource failures, decode failures, and payload registration failures. Slice A covers route-level missing/unsupported/ambiguous/payload-mismatch diagnostics; Slice B maps geometry decode/write failures through promoted core error codes. Slice C.2a covers primary/external read failures, model/texture callback decode errors, and invalid decoded payload diagnostics. Slice C.2b maps tinygltf/stb decode failures to promoted core errors and records GLTF external-resource diagnostics. Slices D.1/D.2 record texture upload failures, unsupported texture formats, invalid payloads, model-scene materialization failures, embedded child texture load/upload failures, and material texture-binding resolution failures.
## Tests
- [x] Add `unit;assets` tests for extension routing, import hints, typed payload registration, failure mapping, texture metadata decode, and deterministic error diagnostics. Slice A adds `AssetImportRouter` routing/failure diagnostics coverage; Slice B adds `AssetGeometryIOBridge` typed callback registration/dispatch and failure-path coverage. Slice C.1 adds `AssetModelTexturePayload` validation, route-fit, diagnostic consistency, and typed `AssetService` storage coverage. Slice C.2a adds `AssetModelTextureIOBridge` coverage for primary file reads, external resource reads, missing callbacks, invalid registrations, read/decode failures, and invalid decoded payloads. Slice C.2b adds `contract;runtime` coverage for concrete model/texture decoder registration, PNG decode, GLTF external-buffer decode, embedded image/material mapping, geometry payload extraction, and promoted decode failure mapping.
- [x] Add `integration;runtime;assets` tests for decoded geometry/model payloads flowing to runtime-owned ECS setup or residency requests without graphics owning file IO. Slice B adds `contract;runtime` coverage for promoted geometry decoder/encoder callback registration and real mesh/point-cloud import/export dispatch; Slice C.2b adds concrete model/texture decoder dispatch coverage. Slice D.1 adds texture GPU-residency handoff coverage in `Test.AssetModelTextureHandoff.cpp`. Slice D.2 adds model-scene materialization, deterministic child texture asset, embedded upload request, `GeometrySources` entity, and material `AssetId` binding coverage in `Test.AssetModelSceneHandoff.cpp`.
- [x] Add `contract;graphics;assets` tests or existing `GpuAssetCache` coverage only for GPU-side cache behavior; do not make `src/assets` depend on graphics.
## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` when ownership is implemented or staged. Slices A/B record the promoted route-contract and geometry callback surfaces; Slice C.1 records the model/texture payload contract; Slice C.2a records the promoted model/texture bridge; Slice C.2b records runtime-owned concrete GLTF/GLB and STB-backed texture decoder registration. Slice D.1 records runtime-owned texture GPU-residency handoff; Slice D.2 records model-scene ECS/material handoff, deterministic child texture assets, and remaining `Operational` blockers.
- [x] Cross-link `GRAPHICS-019`, `GRAPHICS-020`, `GRAPHICS-028`, and `GEOIO-002` from implementation notes. Slices A/B update `src/assets/README.md`, `src/runtime/README.md`, and parity/gate docs for the route and geometry callback contracts. Slices D.1/D.2 update runtime/assets/graphics-assets docs for GPU-residency and model-scene handoff boundaries.
## Acceptance criteria
- [x] Legacy graphics IO/model/texture responsibilities have promoted CPU asset/routing owners with explicit runtime and graphics-residency handoff seams.
- [x] `src/assets/*` remains CPU-only and imports no `graphics/*`, `graphics/rhi`, `runtime`, or live ECS modules.
- [x] Rendering tasks can consume asset IDs, runtime-submitted snapshots, geometry GPU views, or `graphics/assets` residency APIs without direct model/texture parsing.
- [x] Tests cover supported routing/failure states and prove layer boundaries for the new ingest path.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'Asset|Runtime.*Asset|GpuAssetCache' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
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
- Slice C.1 extends `CPUContracted` for model/texture payload ownership:
  `src/assets` exposes CPU-only texture/model-scene records, validation helpers,
  external-resource diagnostics, and typed payload storage coverage without
  invoking decoders or touching GPU/RHI types.
- Slice C.2a extends `CPUContracted` for model/texture decoder-callback
  invocation: `src/assets` owns route resolution, primary file reads, relative
  external-resource reads, callback dispatch, callback error propagation, and
  payload validation without importing concrete decoder, geometry, runtime,
  graphics, or RHI modules.
- Slice C.2b extends `CPUContracted` by registering concrete model/texture
  decoders in runtime, proving texture/model metadata produced from real
  bytes, and mapping GLTF geometry/images/materials into promoted CPU payloads.
- Slice D.1 extends `CPUContracted` by proving runtime-owned texture
  GPU-residency handoff from `AssetService` Ready events to `GpuAssetCache`
  upload requests without importing graphics/RHI into `src/assets`.
- Slice D.2 extends `CPUContracted` by proving runtime-owned model-scene ECS
  construction and material/embedded-texture handoff.
- `Operational` file-backed sandbox proof remains owned by
  [`RUNTIME-095`](../backlog/runtime/RUNTIME-095-working-sandbox-acceptance.md)
  after UI and runtime handoff slices compose the route contract.
