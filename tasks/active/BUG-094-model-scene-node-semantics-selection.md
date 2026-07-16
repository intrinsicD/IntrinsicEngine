---
id: BUG-094
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-094 — Model-scene import drops node semantics and standard selection behavior

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `agent/bug-094-model-scene-semantics`.
- Slice A starts regression-first with the CPU scene payload and active-scene
  traversal contract. Runtime hierarchy/completion and capable-host Vulkan
  proof remain required before retirement at `Operational`.

## Goal
- Make a successful glTF model-scene import preserve active-scene membership,
  node hierarchy and local transforms, and repeated mesh instances, then pass
  every created primitive through the standard renderable/selectable import
  contract so it is visible, focusable, hierarchy-selectable, and click-pickable.

## Non-goals
- No animation, skinning, morph-target, camera, or light import.
- No renderer-owned scene graph or live asset-service traffic below runtime.
- No model-specific duplicate of the existing mesh authoring/completion policy
  lane.
- No flattening of repeated instances into duplicated CPU geometry solely to
  avoid representing node semantics.
- No production enablement of unrelated progressive enrichment or Vulkan
  features.

## Context
- Owner: the CPU model payload in `assets`, model decoding and ECS composition
  in `runtime`, and the existing runtime-to-renderer snapshot seam. `assets`
  remains CPU-only; runtime owns ECS hierarchy, world transforms, import
  policies, completion, selection, and focus.
- `AssetModelScenePayload` is currently a flat list of geometry payloads and
  primitives. `BuildScenePayload()` iterates `tinygltf::Model::meshes` directly
  and never traverses `scenes` or `nodes`; it therefore ignores the default
  scene, unreachable meshes, parent/child relationships, node matrix/TRS
  transforms, and multiple nodes that instance one mesh.
- The checked-in `assets/models/Duck.gltf` exposes the defect: its root node
  carries a `0.01` matrix scale while the mesh accessor is in roughly
  hundred-unit coordinates. The flat decoder imports the raw mesh scale rather
  than the authored scene scale.
- `Runtime.AssetModelSceneHandoff` materializes one flat entity per primitive
  with `RenderSurface` and `GeometrySources`. It does not establish node
  hierarchy or apply node-local transforms, and it omits `SelectableTag` and
  `VisualizationConfig`. Mouse readback intentionally rejects entities without
  `SelectableTag`, while a hierarchy row can still force a programmatic
  selection, producing inconsistent selection behavior.
- Standard direct mesh, graph, and point-cloud imports install authoring
  policies and invoke import-completed handlers. The queued and synchronous
  model-scene routes bypass both lanes, so successful model imports do not
  select the first primitive or focus the camera on their aggregate world-space
  bounds.
- Regression provenance is covered today only by an identity-node glTF fixture
  and a handoff assertion for geometry/material upload. Neither test can detect
  active-scene, transform, instancing, authoring-component, focus, or pick
  regressions.

## Slice plan
- **Slice A — CPU scene contract.** Extend the assets payload and decoder with
  active-scene traversal, hierarchy, local transforms, shared-geometry
  instances, malformed-input diagnostics, and focused CPU contracts.
- **Slice B — Runtime materialization.** Compose the retained node/instance
  data into ECS hierarchy and standard import authoring/completion, with Null
  contracts for components, transforms, selection, and aggregate focus.
- **Slice C — Operational proof.** Add the capable-host Vulkan visible and
  click-pickable smoke; do not claim `Operational` from Slices A/B alone.

## Required changes
- [ ] Add a minimal CPU payload representation for scene roots, node parentage,
      node-local transforms, and primitive instances that reference shared
      geometry/material payloads without introducing a live scene service into
      `assets`.
- [ ] Traverse the glTF default scene, or the deterministic documented fallback
      when no default is declared, and retain only reachable node instances.
      Preserve child order and repeated references to one mesh; reject invalid
      node/mesh indices, cycles, and non-finite matrix/TRS data with actionable
      diagnostics.
- [ ] Materialize node and primitive entities under runtime ownership with the
      authored hierarchy and local transforms. Reuse shared CPU geometry for
      repeated mesh instances and ensure their world matrices differ according
      to their node paths.
- [ ] Route each renderable primitive through the same default authoring policy
      contract as a direct mesh import, including `GeometrySources`,
      `RenderSurface`, `SelectableTag`, and `VisualizationConfig`, without
      creating a parallel model-only policy implementation.
- [ ] Invoke model-scene completion once after the complete materialization:
      select the deterministic first created primitive and focus the camera on
      the finite aggregate world-space bounds of all created primitives.
- [ ] Keep graphics consumers snapshot/view based. Do not expose glTF node
      types or `Vk*` types through asset, ECS, RHI, or renderer APIs.

## Tests
- [ ] Regression first: extend
      `tests/contract/runtime/Test.AssetModelTextureIO.cpp` with
      `RuntimeAssetModelTextureIO.PreservesActiveSceneNodeTransformsAndMeshInstances`.
      Its in-memory glTF must contain a nested transformed node, two nodes
      referencing one mesh, an unused mesh outside the active scene, and a
      non-default scene. Assert retained roots, parentage, local transforms,
      deterministic instance order, shared geometry reference, and exclusion
      of unreachable meshes.
- [ ] Add malformed-payload cases for cyclic node references, out-of-range
      scene/node/mesh indices, and non-finite transforms; each must fail closed
      with a diagnostic instead of partially materializing a scene.
- [ ] Extend `tests/contract/runtime/Test.AssetModelSceneHandoff.cpp` with
      `RuntimeAssetModelSceneHandoff.MaterializesHierarchyTransformsAndSelectablePrimitiveInstances`.
      Assert entity count, hierarchy, local/world transforms, shared geometry,
      and component parity with a reference direct-mesh import.
- [ ] Add real `Engine` import coverage in
      `tests/contract/runtime/Test.AssetImportFormatCoverage.cpp` named
      `RuntimeAssetImportFormatCoverage.ModelSceneCompletionSelectsAndFramesCreatedPrimitives`.
      Exercise both synchronous and queued routes and assert one completion,
      deterministic selected entity, and a focus target enclosing the
      world-space primitive bounds.
- [ ] Add opt-in Vulkan acceptance coverage in
      `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`
      named
      `RuntimeSandboxAcceptanceGpuSmoke.ImportedModelSceneIsVisibleAndClickPickable`.
      Import a checked-in transformed, instanced fixture, prove non-black
      readback at a projected primitive, and prove mouse readback selects that
      primitive. Capability absence may skip; a capable operational device must
      fail on wrong pixels or selection.

## Docs
- [ ] Document the model payload's scene/node/instance semantics and the
      assets-to-runtime ownership boundary in the relevant asset/runtime
      architecture documentation.
- [ ] Update the runtime import documentation to state that model-scene
      completion uses the standard authoring, selection, and aggregate-focus
      contract.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the exported model
      payload surface changes, then update task indexes, session brief, and
      retirement records when the implementation is verified.

## Acceptance criteria
- [ ] The regression fixture produces exactly the reachable active-scene
      instances, including two distinct transformed instances of one shared
      mesh, and imports no unreachable mesh.
- [ ] Nested matrix/TRS transforms produce the expected finite world transforms;
      the Duck fixture is no longer rendered at raw accessor scale.
- [ ] Every materialized primitive has the standard render-critical and
      selection-critical components and can be selected consistently from both
      the hierarchy and mouse readback.
- [ ] Both synchronous and queued model-scene imports run completion exactly
      once, select the deterministic first primitive, and frame aggregate
      world-space bounds.
- [ ] The CPU contracts pass under the Null backend and the opt-in Vulkan smoke
      proves the promoted path visible and click-pickable on a capable host.
- [ ] Layering remains compliant: assets holds CPU data, runtime owns ECS
      composition, and graphics sees only snapshots/views and asset IDs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^(RuntimeAssetModelTextureIO|RuntimeAssetModelSceneHandoff|RuntimeAssetImportFormatCoverage)\.(PreservesActiveSceneNodeTransformsAndMeshInstances|MaterializesHierarchyTransformsAndSelectablePrimitiveInstances|ModelSceneCompletionSelectsAndFramesCreatedPrimitives)$' \
  --timeout 60

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^RuntimeSandboxAcceptanceGpuSmoke\.ImportedModelSceneIsVisibleAndClickPickable$' \
  -L 'gpu' -L 'vulkan' --timeout 120

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src \
  --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Treating all `model.meshes` entries as scene instances or silently applying
  identity transforms when node data is present but invalid.
- Baking node transforms destructively into shared geometry in a way that
  breaks repeated instances or loses hierarchy.
- Adding selection only to the hierarchy UI while leaving GPU mouse-pick
  eligibility absent.
- Claiming completion at `CPUContracted`; this bug targets `Operational` and
  owns the capable-host visible-and-pickable smoke.

## Maturity
- Target: `Operational`.
- `CPUContracted` requires deterministic decoder, malformed-input, hierarchy,
  transform, authoring-component, selection, and focus contracts through the
  real runtime import routes.
- `Operational` additionally requires the opt-in promoted Vulkan smoke to prove
  a transformed imported primitive is visible and click-pickable on a capable
  host.
