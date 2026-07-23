---
id: RUNTIME-191
theme: F
depends_on: [RUNTIME-190]
maturity_target: Retired
---
# RUNTIME-191 — Unify property-to-texture baking and retire specialized paths

## Goal

- Make `TextureBakeModule` the single runtime owner and
  `TextureBakeService` the single public execution interface for every
  supported surface-property-to-texture bake, with one canonical request/result
  vocabulary and one GPU scheduling path. Callers prepare special source data
  before the request and process or bind special results after completion;
  normal space, color semantics, and other property meanings do not select
  parallel bake pipelines.

## Non-goals

- No implicit computation of missing source properties inside texture baking.
  Object-, world-, or tangent-space normals, curvature, ambient occlusion,
  labels, and other derived fields must already exist as named, validated
  properties before submission.
- No point-cloud or graph texture-bake promise while those domains have no
  surface/UV raster contract. The canonical interface remains extensible data,
  but this task proves mesh vertex, face, and nearest-edge properties.
- No live CPU fallback, synchronous GPU readback, or silent backend switch.
  Null and non-operational devices fail closed with actionable diagnostics.
- No change to the material/shading-model authority owned by `GRAPHICS-105`;
  this task preserves current consumer semantics while unifying how textures
  are produced and delivered.
- No new normal-generation algorithm, texture format feature, or unrelated
  renderer capability.

## Context

- Owning layer: `runtime` composes ECS property lookup, world lifetime, job
  scheduling, generated `AssetService` ownership, completion, and consumer
  binding. `graphics/renderer` owns the backend-neutral property-raster
  planning/recording implementation and shaders.
- Retired `RUNTIME-190` generalized the interactive editor/agent GPU path, but
  deliberately retained the standalone CPU compatibility baker and relocated
  the import-time object-space-normal producer unchanged. Its narrower
  acceptance therefore did not establish one engine-wide bake pipeline.
- `TextureBakeModule` currently owns both `ObjectSpaceNormalBakeService` and
  the generalized property participant, registers two GPU queue participants,
  and exposes a borrowed `RuntimeObjectSpaceNormalBakeQueue*` through
  `TextureBakeProducerContext`.
- `Runtime.AssetModelSceneHandoff` still calls
  `BakeMeshVertexColorTexture(...)` for generated albedo and can call
  `BakeMeshVertexNormalTexture(...)` when the specialized normal queue is
  absent. Thus `Runtime.MeshAttributeTextureBake` is reachable from production
  import code, not only deterministic tests.
- `Runtime.SelectedMeshTextureBake` mixes generic request/result/catalog
  records with an older selected-entity direct command, and its public records
  reuse `MeshAttributeTextureBake*` vocabulary. App/editor code consequently
  imports a CPU implementation module merely to name encoder and range enums.
- Canonical vocabulary for this task is **property**: a named geometry-domain
  value field is the bake source. “Attribute” remains renderer vocabulary
  where appropriate, but must not create a second bake contract.
- Producers of transform-derived fields own their mathematics and
  invalidation. For example, a world-space normal producer applies the normal
  matrix, publishes a named property, and advances its source generation when
  the transform changes; the bake service only validates and consumes that
  property.
- Required correctness from the specialized path remains load-bearing:
  canonical source identity, bounded scheduling, world/epoch and weak-lifetime
  validation, stale-completion rejection, cache generation, ready-frame and
  frame-safe retirement, UV/topology/property byte revalidation,
  padding/dilation, progressive fallback, and exact consumer binding. The task
  moves these invariants; it does not discard them.

## Control surfaces

- Config: no new tuning lane. Existing/default extent, encoding, range, and
  colormap inputs continue through the canonical request and validated apply
  path.
- UI: selected-entity controls construct the same canonical request used by
  every other caller; selection is caller context, not part of bake identity
  or type names.
- Agent/CLI: uses the same published `TextureBakeService` commands as the UI.
- Import/default policy: prepares required properties, then calls the same
  `TextureBakeService`; it receives no specialized queue or producer facade.

## Backends

- Backend axis: the promoted runtime path is GPU-only through the existing
  backend-neutral `Graphics.PropertyTextureBake` recorder and operational RHI
  device. A CPU reference may remain only in test support and is not a runtime
  backend, public fallback, or source of production request types.
- Do not add `ITextureBakeBackend`, a factory, or backend registry unless a
  second live backend with the same semantic and lifetime contract is delivered
  in this task.

## Right-sizing decision

- **Elements:** `Runtime.SelectedMeshTextureBake`,
  `Runtime.MeshAttributeTextureBake`,
  `Runtime.ObjectSpaceNormalBake{Queue,Service,Binding,Submission}`, and
  `Graphics.ObjectSpaceNormalTextureBake` trigger the role-fragmentation and
  parallel-pipeline heuristics. Much of their interface is scheduling,
  forwarding, or normal-specific duplication around one raster capability.
- **Simpler alternative:** deepen the existing `TextureBakeModule` and
  `Graphics.PropertyTextureBake` seams. Export plain canonical DTOs from
  `Runtime.TextureBakeModule`; keep lifecycle, queues, completion, asset
  ownership, and bindings private in its implementation units. Express
  encoding, padding, and consumer semantics as request data rather than
  services.
- **Blast radius:** runtime module/interface and CMake wiring, asset workflow
  and model-scene handoff, Sandbox editor facades/panels, renderer property
  bake recorder/shaders, generated-asset/presentation tests, Vulkan smokes,
  runtime/graphics architecture docs, and the generated module inventory.
  Source search plus the strict layering gate must confirm the complete set.
- **Reintroduction trigger:** split out a backend adapter only when a second
  live backend shares the canonical contract; split out an independently owned
  producer only when it has a demonstrably different lifetime owner. A new
  encoding or source property meaning alone never justifies another
  Service→Queue→Binding→Submission family.

## Slice plan

- **Slice A — canonical contract (`CPUContracted`).** Move the generic
  property/domain/value/encoding/range/normal-space request, result, snapshot,
  and diagnostic vocabulary onto `Runtime.TextureBakeModule`; migrate
  editor/agent/import call sites away from `SelectedMesh*` and
  `MeshAttributeTextureBake*` public types without changing execution
  semantics. Any transitional alias remains owned by this active task and must
  be removed before retirement.
- **Slice B — one operational producer (`Operational`).** Generalize
  `Graphics.PropertyTextureBake` and the module-private participant to preserve
  the specialized path's identity, padding/dilation, residency, completion,
  and retirement guarantees. Materialize import source properties first, route
  import/default policy through `TextureBakeService`, remove the raw
  object-normal queue borrow, and register exactly one texture-bake GPU
  participant.
- **Slice C — parity and consumer migration (`ParityProven`).** Prove
  interactive and import-time normal/color outputs, progressive fallback,
  generated-asset identity, multi-consumer binding, world replacement, stale
  completion, and shutdown behavior through the canonical path. Remove every
  live CPU bake call and decide whether the minimal CPU oracle is deleted or
  moved to test-only support.
- **Slice D — legacy deletion (`Retired`).** In a separate mechanical cleanup
  commit, delete the superseded selected-mesh, mesh-attribute CPU production,
  object-space-normal runtime/graphics modules and shaders, obsolete tests and
  CMake entries, then refresh documentation and generated inventories. No
  compatibility re-export remains.

## Required changes

- [ ] Export one plain `PropertyTextureBakeRequest` /
      `PropertyTextureBakeResult` family from `Runtime.TextureBakeModule`,
      covering source entity/world, property name/domain/value kind, UV
      property, encoding/storage/range/normal-space metadata, extent/padding,
      output identity, and zero or more compatible consumers.
- [ ] Replace `SelectedMeshTextureBake*` and
      `MeshAttributeTextureBake*` vocabulary in the service, Sandbox facade,
      UI, agent surface, catalogs, diagnostics, and tests with the canonical
      property vocabulary.
- [ ] Keep source preparation outside the bake interface. Import and editor
      flows must create or transform the required named property first and
      submit only after domain/count/type/finite-value and UV validation can
      succeed. Transform-derived producers must advance source
      generation/dirty identity so an old world-space result cannot survive a
      transform change.
- [ ] Extend the general graphics property-raster plan/recorder with the
      currently load-bearing padding/dilation and normal-encoding behavior,
      without normal-specific public plans, shaders, descriptor contracts, or
      Vulkan types crossing the graphics/runtime interface.
- [ ] Consolidate scheduling, canonical content identity, GPU residency
      validation, cache generations, ready-frame publication, stale rejection,
      frame-safe retirement, generated-asset ownership, and consumer binding
      into the one `TextureBakeModule` implementation and one JobService GPU
      participant.
- [ ] Change `AssetWorkflowModule` and `AssetModelSceneHandoff` to call the
      published `TextureBakeService` after property materialization. Preserve
      visible property-buffer/authored-texture fallback while asynchronous
      output is pending and preserve exact world/epoch/entity lifetime checks.
- [ ] Remove `TextureBakeProducerContext`'s specialized queue exposure; no
      production interface may publish or accept
      `RuntimeObjectSpaceNormalBakeQueue*`.
- [ ] Remove live calls to `BakeMeshAttributeTexture(...)`,
      `BakeMeshVertexNormalTexture(...)`, and
      `BakeMeshVertexColorTexture(...)`. If a CPU oracle remains useful, move
      the minimum pure raster reference into test support with no production
      module import, asset mutation, fallback selection, or duplicate public
      DTOs.
- [ ] Delete the production modules
      `Runtime.SelectedMeshTextureBake`,
      `Runtime.MeshAttributeTextureBake`,
      `Runtime.ObjectSpaceNormalBake{Queue,Service,Binding,Submission}`, and
      `Graphics.ObjectSpaceNormalTextureBake` after parity evidence exists;
      remove their implementation units, specialized shaders, CMake entries,
      compatibility wrappers, and production imports.
- [ ] Preserve one generated output's deterministic rebake/rename/remove
      identity and atomic multi-consumer behavior without introducing a second
      asset registry, texture manager, event bus, queue facade, or binding
      family.
- [ ] Separate semantic migration commits from the final mechanical deletion
      commit so review can distinguish behavior changes from removals.

## Tests

- [ ] CPU/headless contract tests cover canonical request validation for
      vertex, face, and nearest-edge domains; scalar, label, vector, color, and
      normal encodings; missing/invalid properties, UVs, topology, counts,
      finite values, ranges, extents, padding, and consumers.
- [ ] CPU/headless contracts prove Null/non-operational devices fail closed and
      neither import nor editor/agent routes execute a CPU fallback.
- [ ] Import contracts prove required properties are materialized before
      submission and that no object/world/tangent normal or other derived
      property is synthesized by the bake service.
- [ ] A transform-derived normal contract proves that changing the transform
      and republishing the named property invalidates the old source identity,
      while an unchanged property/generation remains deterministic.
- [ ] Runtime lifecycle tests cover bounded scheduling, supersession, exact
      content/cache generation, stale completion rejection, world/document
      replacement, weak target lifetime, generated-asset cleanup, and shutdown
      with the single participant.
- [ ] Consumer tests cover pending fallback, exact normal/albedo/visualization
      binding, multi-consumer updates, rebake, rename, remove, and preservation
      of unrelated material channels.
- [ ] Opt-in `gpu;vulkan` readback smokes prove representative editor/agent and
      import/default-policy normal and color properties use
      `Graphics.PropertyTextureBake`, including padding/dilation, with no
      specialized participant or shader.
- [ ] Structural regression coverage or strict source scans prove production
      source and app code contain no imports, calls, or compatibility aliases
      for the retired specialized modules.

## Docs

- [ ] Update `src/runtime/README.md` to document one property-to-texture module,
      one public service, caller-owned preparation/result processing, and the
      absence of specialized/CPU live routes.
- [ ] Update `src/graphics/renderer/README.md` and the relevant
      `docs/architecture/*` pages for the one generic property-raster recorder
      and ownership split.
- [ ] Update legacy/migration documentation and every link that names the
      deleted module surfaces.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module
      deletions and regenerate `tasks/SESSION-BRIEF.md` on every task lifecycle
      change.

## Acceptance criteria

- [ ] Every live editor, agent/CLI, import, and default-policy
      property-to-texture request enters the same `TextureBakeService` method
      using the same request/result types.
- [ ] Runtime registers exactly one property-texture GPU participant and
      exposes no specialized normal queue, service, binding, submission, or
      producer context.
- [ ] Normal, color, scalar, label, and vector bakes differ only through
      prepared source properties, request data, and result consumers; none
      selects a parallel end-to-end path.
- [ ] Import-time missing-texture policy first materializes the named source
      property and then uses the canonical asynchronous GPU path, retaining
      visible fallback until exact completion.
- [ ] No production target imports or exports
      `Runtime.SelectedMeshTextureBake`,
      `Runtime.MeshAttributeTextureBake`,
      `Runtime.ObjectSpaceNormalBake*`, or
      `Graphics.ObjectSpaceNormalTextureBake`; no compatibility re-export or
      duplicate bake vocabulary remains.
- [ ] A retained CPU oracle, if any, exists only in test support and cannot be
      selected by production code.
- [ ] Existing generated-asset, cache/residency, stale-result, padding/dilation,
      progressive presentation, and shutdown guarantees have parity evidence
      through the canonical path.
- [ ] The final implementation passes the right-sizing deletion test: removing
      any remaining bake module would redistribute necessary complexity across
      multiple callers or cross a real runtime/graphics seam.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'TextureBake|AssetModelSceneHandoff|AssetWorkflowModule' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -L 'gpu' -L 'vulkan' -R 'PropertyTextureBake|TextureBakeModule' --timeout 120

python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode \
  --base-ref origin/main --strict
tools/ci/run_clean_workshop_review.sh . --strict

! rg -n \
  'Extrinsic\\.Runtime\\.(SelectedMeshTextureBake|MeshAttributeTextureBake|ObjectSpaceNormalBake)|Extrinsic\\.Graphics\\.ObjectSpaceNormalTextureBake|RuntimeObjectSpaceNormalBakeQueue' \
  src tests/CMakeLists.txt
```

## Forbidden changes

- Replacing the retired modules with the same specialized pipeline under new
  names.
- Adding a new `Service`/`Queue`/`Binding`/`Submission` chain, forwarding
  facade, backend registry, or texture manager.
- Computing a missing property implicitly inside `TextureBakeService`.
- Retaining a live CPU fallback or letting importer/editor availability choose
  different semantic bake paths.
- Keeping compatibility aliases, re-exports, specialized shaders, or raw queue
  borrows when the task retires.
- Weakening world/epoch, content identity, cache-generation, ready-frame,
  stale-completion, residency, or shutdown guarantees in the name of
  simplification.
- Mixing the final mechanical module deletion with semantic migration or
  unrelated `GRAPHICS-105` material-authority work.

## Maturity

- Target: `Retired`. The canonical interface remains `CPUContracted` on
  Null/headless hosts and must be `Operational` on a Vulkan-capable host before
  legacy deletion.
- `Operational` owned by `RUNTIME-191`; Slice B must cite an actually executed
  Vulkan-capable integration/readback run before Slice C may claim parity.
- Slice A reaches `CPUContracted`; Slice B reaches `Operational`; Slice C
  reaches `ParityProven`; Slice D reaches `Retired`.
- The task must remain open until the old production modules, shaders, CMake
  entries, direct calls, compatibility aliases, and public vocabulary are
  deleted. No lower-maturity closure or separate retirement follow-up is
  accepted.
