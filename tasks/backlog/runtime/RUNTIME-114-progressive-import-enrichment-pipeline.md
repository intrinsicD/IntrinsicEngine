---
id: RUNTIME-114
theme: F
depends_on: [RUNTIME-112, RUNTIME-113, ASSETIO-008, RUNTIME-109]
maturity_target: CPUContracted
---
# RUNTIME-114 — Progressive import enrichment pipeline

## Goal
- Wire asset import so raw mesh, graph, and point-cloud leaves become visible
  immediately, then schedule UV atlas generation, normal/property computation,
  texture bakes, uploads, and binding updates as derived outputs become ready.

## Non-goals
- No new geometry algorithm beyond invoking existing UV, normal, bake, and
  upload seams.
- No editor UI controls; UI visibility is owned by `UI-015`.
- No Vulkan operational proof; backend evidence is owned by `GRAPHICS-090`.
- No graph/point texture baking requirement in the first implementation.
- No persistent serialization of transient job state.

## Context
- Owning subsystem/layer: `runtime` owns import-to-ECS materialization,
  `StreamingExecutor` scheduling, main-thread apply, and texture upload/bind
  requests. Geometry/assets provide CPU payloads; graphics owns GPU residency.
- ADR-0021 requires import to publish raw geometry before downstream derived
  work completes.
- `ASSETIO-008` provides missing-UV atlas materialization, `RUNTIME-109`
  provides generic mesh attribute texture bakes, `RUNTIME-112` provides
  derived-job snapshots, and `RUNTIME-113` provides extraction over progressive
  descriptors.

## Required changes
- [ ] Publish imported model/composition entities and child geometry leaves
      with raw decoded geometry before derived jobs finish.
- [ ] For mesh leaves with missing or invalid UVs, automatically enqueue an
      async atlas job after import while rendering with default slot values.
- [ ] For mesh leaves with missing normals, enqueue vertex-normal computation
      and apply finite `v:normal` properties on the main thread.
- [ ] When normals, UVs, and compatible slot bindings are ready, enqueue
      normal-map bake jobs and then texture upload/bind follow-up jobs.
- [ ] Add equivalent scheduling for albedo/color, scalar-field, roughness, and
      metallic source properties where compatible bindings exist.
- [ ] Preserve graph and point-cloud properties as direct property-buffer
      candidates instead of forcing texture baking.
- [ ] Use descriptor generations to discard stale atlas, normal, bake, upload,
      and bind completions after reimport, property edits, binding edits, or
      entity deletion.
- [ ] Keep previous valid generated outputs bound until replacements are ready
      and report failed jobs through derived-job snapshots.
- [ ] Update import result/status snapshots so raw entity availability and
      downstream enrichment status are visible separately.

## Tests
- [ ] Add direct mesh import tests proving the entity is returned/visible before
      UV, normal, bake, upload, and bind jobs complete.
- [ ] Add async progression tests for missing-UV atlas generation followed by
      normal computation and normal-map bake once dependencies are ready.
- [ ] Add color/scalar/roughness/metallic scheduling tests with compatible and
      incompatible property bindings.
- [ ] Add graph and point-cloud import tests proving property-buffer candidates
      are represented without mesh texture-bake assumptions.
- [ ] Add stale-result tests for reimport, property edit, binding edit, and
      deletion during atlas/normal/bake/upload/bind jobs.
- [ ] Add failure tests proving default rendering continues and previous valid
      generated outputs remain bound.

## Docs
- [ ] Update `src/runtime/README.md` with progressive import materialization
      order and derived-job dependency examples.
- [ ] Update asset import/runtime docs if import result semantics change.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module surface
      changes.

## Acceptance criteria
- [ ] Imported raw geometry is available for extraction before downstream
      enrichment completes.
- [ ] Missing UVs and missing normals schedule separate observable jobs instead
      of blocking import completion.
- [ ] Texture bakes and upload/bind follow-ups run only after their source
      property and UV dependencies are ready.
- [ ] Graph and point-cloud import paths keep equal priority and do not depend
      on mesh-specific bakes.
- [ ] Stale derived outputs cannot mutate current entity state.
- [ ] The default CPU-supported CTest gate verifies the progressive import
      pipeline contract.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicAssetUnitTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AssetImport|Progressive|DerivedJob|TextureBake|UvAtlas|Normal' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not delay entity publication until all derived outputs are ready.
- Do not compute or bake graph/point-cloud properties through mesh UV texture
  assumptions.
- Do not apply worker results off the main thread.
- Do not persist transient job state.
- Do not allocate Vulkan/RHI resources in assets, geometry, ECS, or runtime.

## Maturity
- Target: `CPUContracted`.
- This task closes the backend-neutral progressive import/enrichment contract.
  `Operational` owned by `GRAPHICS-090`.
