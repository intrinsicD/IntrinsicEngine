---
id: GRAPHICS-088
theme: none
depends_on: [RUNTIME-108, RUNTIME-109]
maturity_target: Operational
---
# GRAPHICS-088 — Resolved UV rendering and bake texture residency

## Status
- Current status: in-progress; Slice A implemented and verified.
- Owner/agent: Codex.
- Branch/PR: local workspace, no PR yet.
- Current slice: Slice A (`CPUContracted`) graphics contract for resolved-UV
  material sampling, generated texture asset bindings, UV debug material, and
  UV bake packet provenance.
- Remaining blockers: the full `Operational` target still depends on
  `RUNTIME-109` for generic scalar/vector/PBR/displacement bake requests,
  `ASSETIO-008` for default atlas materialization on imported missing-UV
  assets, and an opt-in Vulkan-capable host for generated-UV texture sampling
  smoke evidence.
- Latest verification: focused graphics/runtime targets and the default
  CPU-supported correctness gate pass. Vulkan generated-UV smoke remains
  pending by design.
- 2026-06-14 rendering-artifact debug: direct/model generated material bakes are
  now gated on source-authored valid `v:texcoord`. Runtime projection fallback
  UVs still keep no-UV meshes renderable, but they are not treated as a
  material-bake atlas until the xatlas/default-atlas task replaces the fallback.
- 2026-06-14 bunny OBJ shading debug: promoted surface geometry now packs
  dedicated vertex normals alongside resolved UVs, the forward/deferred surface
  shaders use those normals when no normal texture is bound, retained
  graph/point/primitive-view UV fields no longer carry oct-encoded normals, and
  `GpuWorld` aligns mixed-stride managed geometry uploads. This fixes unshaded
  no-authored-UV OBJ imports without reusing texture coordinates for normals.

## Goal
- Wire the renderer/material path so resolved mesh UVs are the canonical surface texture coordinate source for material sampling, generated bakes, UV debug inspection, and UV-backed visualization bake atlases.

## Non-goals
- No geometry parameterization backend implementation.
- No runtime asset materialization fallback.
- No editor controls.
- No new PBR material feature set beyond consuming existing/generated texture bindings through resolved UVs.
- No GPU compute baker.

## Context
- Owning subsystem/layer: `graphics/renderer`, `graphics/assets`, and backend-local Vulkan smoke where needed. Runtime still owns extraction, bake requests, and generated texture asset handoff.
- `RUNTIME-108` guarantees the packed mesh vertex UV channel is actual texture coordinates or the mesh surface fails closed.
- `RUNTIME-109` produces generated texture payloads/bindings for algorithm outputs.
- Existing visualization packets have `ExistingTexcoords`, `ExistingHtex`, and `RecreateHtex` mapping modes. With resolved UVs, `ExistingTexcoords` should become the normal path for mesh texture bakes; Htex remains an explicit alternate mapping, not the default missing-UV rescue path.
- Graphics must consume snapshots and asset IDs/bindless indices only; it must not import geometry algorithms, xatlas, `AssetService`, ECS, runtime, platform, or app.

## Slice plan
- **Slice A (this slice).** Close the backend-neutral graphics contract: forward
  surface shaders sample albedo/normal material texture slots through the
  packed resolved-UV channel, generated texture `AssetId` bindings flow through
  `GpuAssetCache` + `MaterialSystem::ResolveTextureAssetBindings(...)`, a
  `Material.DefaultDebugUVs` material type exposes checker inspection through
  the same surface shader lane, and `FragmentBakeAtlasPacket` carries
  resolved-UV provenance plus optional generated texture asset identity. This
  preserves the default CPU gate and does not claim Vulkan operational proof.
- **Slice B.** After `RUNTIME-109`, route generic scalar/vector/PBR/displacement
  bake products into material slots or visualization texture descriptors with
  runtime-authored dirty stamps and stable generated asset keys.
- **Slice C.** After `ASSETIO-008` and on a Vulkan-capable host, add opt-in
  `gpu;vulkan` smoke proving a mesh that originally lacked authored UVs renders
  through generated UVs and a generated texture binding.

## Required changes
- [x] Audit forward/deferred surface shader inputs and material binding code so the surface UV channel is consistently treated as resolved texture coordinates.
- [x] Add or update renderer diagnostics that report missing/invalid UV attempts as upstream extraction failures rather than shader-side fallbacks.
- [x] Add a UV debug/checker material or debug-view mode for inspecting resolved UVs and atlas seams.
- [x] Ensure generated normal and albedo texture bindings flow through existing `GpuAssetCache` and material texture slots when source UVs are valid, and skip material-bake bindings for projection fallback UVs.
- [x] Add explicit visualization texture asset descriptors/provenance for generated UV-backed bake atlases.
- [ ] Extend generic scalar, vector, future PBR, and displacement generated texture bindings after `RUNTIME-109` exposes the runtime bake request contract.
- [x] Update UV-backed `FragmentBakeAtlasPacket` handling so resolved-UV mesh bakes do not require Htex recreation and report provenance/diagnostics when available.
- [x] Keep Htex preview/recreate as an explicit user-selected mapping path; do not silently prefer Htex over valid resolved UVs.
- [ ] Add opt-in Vulkan smoke coverage for at least one generated-UV mesh sampling an uploaded generated texture.

## Tests
- [x] Add graphics/material contract tests proving surface material sampling consumes the resolved UV channel for albedo and normal generated bindings.
- [x] Add visualization packet tests proving `ExistingTexcoords` packets with resolved UV metadata are accepted and missing-UV packets are rejected before backend use.
- [x] Add CPU renderer tests for UV debug/checker mode selection and diagnostics.
- [x] Add runtime/graphics regression coverage proving surface vertex normals
      are packed separately from UVs and mixed 20-byte/32-byte geometry strides
      stay aligned in `GpuWorld`.
- [ ] Add `gpu;vulkan` smoke proving a mesh that originally lacked authored UVs renders through generated UVs and a generated texture binding.
- [x] Add regression coverage proving Htex recreation remains explicit and is not silently triggered when resolved UVs exist.

## Docs
- [x] Update `docs/architecture/graphics.md` and `src/graphics/renderer/README.md` with the resolved-UV renderer contract.
- [x] Update `docs/adr/0009-visualization-packets-and-overlay-upload.md` if the fragment-bake mapping policy wording changes.
- [x] Update `docs/architecture/rendering-three-pass.md` if a UV debug/checker pass or material variant is added.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Renderer-visible mesh UVs always mean resolved texture coordinates, never encoded normals or placeholder zero UVs.
- [x] Generated normal/albedo texture bindings produced by runtime bakes are sampled through the normal graphics texture residency/material path; projection fallback UVs are render-valid only and do not trigger material-bake bindings.
- [x] UV-backed visualization bakes use resolved UVs by default; Htex is retained as an explicit alternate mapping.
- [ ] A Vulkan-capable host has opt-in smoke evidence for generated-UV texture sampling.
- [x] Graphics layering remains free of live `AssetService`, ECS, runtime, geometry backend, or `xatlas` dependencies.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SurfacePass|MaterialSystem|VisualizationPackets|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Uv|Texture|Surface' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A verification completed locally:

- `cmake --build --preset ci --target IntrinsicGraphicsRendererCpuUnitTests IntrinsicGraphicsContractTests IntrinsicRuntimeContractTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'GraphicsMaterialSystem|GraphicsVisualizationPackets|RendererFrameLifecycle.ForwardSurfacePipeline|VisualizationAdapters|MeshGeometryExtraction|SurfacePass' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 86/86.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — regenerated module inventory.
- `python3 tools/agents/generate_session_brief.py` and `python3 tools/agents/generate_session_brief.py --check` — passed.
- `cmake --build --preset ci --target IntrinsicTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 3017/3017.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --strict --files $(git diff --name-only --diff-filter=ACMR) $(git ls-files --others --exclude-standard)` — passed.
- `python3 tools/repo/check_layering.py --root src --strict` — passed.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `git diff --check` — passed.

Not run for Slice A: `ci-vulkan` and `gpu;vulkan` UV smoke. That remains a
Slice C acceptance item after `ASSETIO-008` and a Vulkan-capable host are
available.

## Forbidden changes
- Do not import `xatlas`, geometry algorithm modules, runtime, ECS, or `AssetService` into graphics or Vulkan layers.
- Do not add shader-side UV fabrication.
- Do not route generated textures around `GpuAssetCache` or material texture binding policy.
- Do not make Htex regeneration implicit when resolved UVs are available.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- This task closes render-path use of resolved UVs. UI controls are owned by `UI-014`.
