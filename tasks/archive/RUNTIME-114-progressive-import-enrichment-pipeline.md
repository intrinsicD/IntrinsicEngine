---
id: RUNTIME-114
theme: F
depends_on: [RUNTIME-112, RUNTIME-113, ASSETIO-008, RUNTIME-109]
maturity_target: CPUContracted
---
# RUNTIME-114 — Progressive import enrichment pipeline

## Completion
- Completed: 2026-06-16. Commit/PR: this retirement commit.
- Maturity: `CPUContracted`.
- Fix summary: added the progressive raw-geometry-first model-scene handoff
  path. Mesh primitive entities publish immediately from decoded geometry,
  receive data-only progressive surface bindings, and enqueue observable UV
  atlas, vertex-normal, normal-map bake, and albedo/property-color bake jobs
  through `DerivedJobRegistry`. Main-thread applies mutate only current ECS
  entities and generated presentation descriptors; the CPU-contract bake jobs
  mark deterministic generated texture handles ready without allocating GPU
  resources.
- Evidence: focused runtime contract tests prove raw entity publication before
  generated textures, separate UV/normal/bake jobs and dependencies,
  descriptor readiness updates after async progression, generated albedo/normal
  materialization through the existing non-progressive path, generated atlas
  ordering, material binding re-resolution, and reload invalidation.
- Follow-up boundary: graph and point-cloud presentation stay represented by
  descriptor/extraction property-buffer paths; the current model-scene payload
  materializer still accepts mesh primitives only. Roughness/metallic/scalar
  source-policy scheduling beyond defaults remains a future value-gated import
  policy, not graphics ownership.
- Operational proof: `GRAPHICS-090` supplies the opt-in Vulkan smoke for the
  renderer-consuming side of the progressive outputs.

## Goal
- Wire asset import so raw mesh, graph, and point-cloud leaves become visible
  immediately, then schedule UV atlas generation, normal/property computation,
  texture bakes, and binding descriptor updates as derived outputs become
  ready; GPU upload/bind residency remains on the existing texture handoff and
  material binding re-resolution path.

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
- [x] Publish imported model/composition entities and child geometry leaves
      with raw decoded geometry before derived jobs finish.
- [x] For mesh leaves with missing or invalid UVs, automatically enqueue an
      async atlas job after import while rendering with default slot values.
- [x] For mesh leaves with missing normals, enqueue vertex-normal computation
      and apply finite `v:normal` properties on the main thread.
- [x] When normals, UVs, and compatible slot bindings are ready, enqueue
      normal-map bake jobs and update generated texture descriptors; upload and
      material binding residency stay on the existing texture handoff path.
- [x] Add albedo/color property-bake scheduling where a compatible configured
      vertex color property exists; keep scalar-field, roughness, and metallic
      importer source-policy scheduling deferred while their descriptor/default
      slots remain represented.
- [x] Preserve graph and point-cloud properties as direct property-buffer
      candidates through the shared descriptor/extraction path instead of
      forcing texture baking.
- [x] Use derived-job apply validation and entity liveness checks to discard
      stale atlas, normal, and bake completions after source changes or entity
      deletion; upload/bind stale handling remains on the texture handoff path.
- [x] Keep previous valid generated outputs bound until replacements are ready
      and report failed jobs through derived-job snapshots.
- [x] Update import result/status snapshots so raw entity availability and
      downstream enrichment status are visible separately.

## Tests
- [x] Add model-scene mesh import tests proving the entity is returned/visible
      before UV, normal, bake, upload, and bind materialization completes.
- [x] Add async progression tests for missing-UV atlas generation followed by
      normal computation, normal-map bake, and albedo bake once dependencies
      are ready.
- [x] Add albedo/color scheduling tests with compatible property bindings;
      scalar/roughness/metallic source-policy scheduling is deferred beyond
      descriptor/default representation.
- [x] Add descriptor/extraction tests proving graph and point-cloud
      property-buffer candidates are represented without mesh texture-bake
      assumptions; the model-scene importer remains mesh-primitive-only.
- [x] Add stale-result tests through the shared derived-job graph for source
      edits, binding edits, cancellation, and deletion during derived jobs.
- [x] Add failure tests proving default rendering continues and previous valid
      generated outputs remain bound.

## Docs
- [x] Update `src/runtime/README.md` with progressive import materialization
      order and derived-job dependency examples.
- [x] Update asset import/runtime docs if import result semantics change.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface
      changes.

## Acceptance criteria
- [x] Imported raw geometry is available for extraction before downstream
      enrichment completes.
- [x] Missing UVs and missing normals schedule separate observable jobs instead
      of blocking import completion.
- [x] Texture bakes update descriptors only after their source property and UV
      dependencies are ready; upload/bind residency remains texture-handoff
      owned.
- [x] Graph and point-cloud presentation paths keep equal priority through
      property-buffer descriptors and do not depend on mesh-specific bakes.
- [x] Stale derived outputs cannot mutate current entity state.
- [x] The default CPU-supported CTest gate verifies the progressive import
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
