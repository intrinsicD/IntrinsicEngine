---
id: GRAPHICS-089
theme: none
depends_on: [GRAPHICS-088, ASSETIO-008]
maturity_target: Operational
---
# GRAPHICS-089 — Generated-UV texture sampling Vulkan smoke

## Goal
- Prove on a Vulkan-capable host that an imported renderable mesh that originally lacked authored UVs can render through `ASSETIO-008` generated UVs and a generated texture binding.

## Non-goals
- No geometry UV atlas backend implementation.
- No runtime import materialization fallback.
- No new material shader feature set beyond sampling an existing generated texture binding through resolved UVs.
- No editor controls.
- No graphics-side UV generation or shader-side UV fabrication.

## Context
- Owning subsystem/layer: opt-in graphics/runtime Vulkan smoke. `GRAPHICS-088` retired the CPU-contracted resolved-UV material and generated-bake descriptor path.
- `ASSETIO-008` must first materialize valid generated `v:texcoord` for imported renderable meshes that lack authored UVs.
- Graphics consumes only snapshots, asset IDs, bindless indices, material slots, and RHI resources. Runtime owns asset import, generated texture payloads, texture upload requests, and extraction sidecars.

## Required changes
- [ ] Add or update an opt-in `gpu;vulkan` smoke scene that imports or constructs a mesh whose source asset omitted authored UVs but whose runtime materialization now carries generated UVs from `ASSETIO-008`.
- [ ] Bind a generated texture asset to that mesh through the existing `GpuAssetCache` and material texture binding path.
- [ ] Assert the surface path samples the uploaded generated texture through the resolved UV channel rather than projection fallback UVs or shader-side fabrication.
- [ ] Keep the smoke skipped or fail-closed with explicit diagnostics when the host lacks promoted Vulkan support.

## Tests
- [ ] Add `gpu;vulkan` smoke coverage for generated-UV texture sampling.
- [ ] Preserve the default CPU-supported CTest gate.
- [ ] Preserve existing `GraphicsVisualizationPackets`, `GraphicsMaterialSystem`, and runtime bake contract coverage.

## Docs
- [ ] Update `tasks/backlog/rendering/README.md` or this task with any final Vulkan host prerequisites discovered during implementation.
- [ ] Update renderer/runtime architecture docs only if the operational path changes the current ownership contract.

## Acceptance criteria
- [ ] `ci-vulkan` builds `IntrinsicTests` on the target host.
- [ ] Opt-in `gpu;vulkan` CTest evidence shows generated UVs from `ASSETIO-008` are used to sample an uploaded generated texture.
- [ ] Graphics remains free of live `AssetService`, ECS, runtime, geometry backend, and `xatlas` dependencies.
- [ ] Missing Vulkan capability reports a deterministic skip or fail-closed diagnostic instead of being recorded as passing operational proof.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Uv|Texture|Surface' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement `xatlas` or a geometry atlas backend in graphics.
- Do not import runtime, ECS, `AssetService`, geometry algorithms, or `xatlas` into graphics or Vulkan layers.
- Do not fabricate UVs in shaders.
- Do not route generated textures around `GpuAssetCache` or material texture binding policy.

## Maturity
- Target: `Operational`.
- This task closes the generated-UV Vulkan proof deferred by `GRAPHICS-088`; no additional `Operational` follow-up is owed if the smoke passes on a Vulkan-capable host.
