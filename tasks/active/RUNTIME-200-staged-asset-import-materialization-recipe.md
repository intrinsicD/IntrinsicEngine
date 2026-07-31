---
id: RUNTIME-200
theme: F
depends_on: [BUG-095, RUNTIME-191, RUNTIME-194, RUNTIME-197]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-RuntimeCleanup"
branch: "codex/runtime-200-staged-import-recipe"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-07-31T14:57:45Z"
maturity_target: Retired
---
# RUNTIME-200 — Staged asset-import and materialization recipe

## Status

- In progress on 2026-07-31; owner `Codex-RuntimeCleanup`, branch
  `codex/runtime-200-staged-import-recipe`.
- Slice A contract completed on 2026-07-31: the plain recipe names payload,
  authoring, postprocess, completion, request/world generation, and the seven
  ordered stages; copied stage results fail closed on malformed order, stale
  identity, invalid diagnostics, and post-terminal publication.
- Slice B decoder cleanup completed on 2026-07-31: geometry routes call their
  owning loaders directly; model/texture routes call plain decoders; the two
  callback bridge registries, their role-only runtime registration module,
  and their registry-specific tests are removed. Geometry exporter coverage
  remains at the owning geometry modules.
- Slice B queue adoption completed on 2026-07-31: editor, direct, geometry,
  model-scene, and texture requests now enter through `QueueAssetImport`; each
  queued execution carries a copied identity-bound seven-stage trace through
  success, cancellation, stale rejection, and failure publication.
- Current slice: make recipe policy own authoring/postprocess/completion, then
  internalize the remaining handoff and postprocess bodies.
- Focused recipe and queued route contracts: 31/31 passed on 2026-07-31.

## Goal

- Replace route-specific import callbacks and the monolithic model-scene
  handoff with one staged `AssetImportRecipe`:
  route → decode → CPU materialize → ECS author → postprocess → GPU residency
  → complete, then migrate every file/drop/default workflow and delete the old
  public IO bridges and handoff paths.

## Non-goals

- No universal asset format AST, new decoder framework, virtual filesystem, or
  graphics ownership in `assets`.
- No removal of `AssetIngestStateMachine`; it remains the request/terminal
  lifecycle authority.
- No synchronous import/apply, live ECS access on workers, or hidden
  import-specific scheduler.
- No asset-export framework. The bridge's export callbacks have no production
  caller; promoted geometry exporters remain ordinary lower-layer functions
  and a future real export workflow may compose them directly.

## Context

- `AssetImportPipeline`, runtime `AssetGeometryIO`/`AssetModelTextureIO`, the
  lower `Asset.GeometryIOBridge`/`Asset.ModelTextureIOBridge` callback
  registries, `AssetMeshNormals`, `AssetModelTextureHandoff`, and the large
  `AssetModelSceneHandoff` split one workflow by historical role names.
- `AssetWorkflowModule` is the durable runtime owner. Geometry/assets decoders
  remain lower-layer CPU functions; runtime composes their results into ECS,
  texture baking, residency, selection, and completion policy.
- `RUNTIME-194` supplies the work lane, `RUNTIME-197` the upload lifecycle,
  `RUNTIME-191` the single property-to-texture producer, and `BUG-095` the
  generation-safe postprocess contract.

## Right-sizing

- **Element:** the current import path is fragmented across public
  `*Pipeline`, `*Bridge`, IO, normals, and handoff surfaces, triggering the
  feature-fragmentation and pure-forwarding heuristics.
- **Simpler alternative:** keep one plain recipe/stage record family and a
  private executor inside the existing `AssetWorkflowModule`; lower layers
  retain ordinary decode/export functions and no replacement registry or
  service is introduced.
- **Blast radius:** audit all module importers and direct callers before each
  deletion; migrate production routes and their visibility/queue contracts
  first, then remove surfaces mechanically and confirm the module inventory
  plus strict layering gate.
- **Reintroduction trigger:** expose a new public import orchestration seam only
  when a present second runtime composition owner or independently replaceable
  executor exists; a new file format alone is not such a trigger.

## Slice plan

- **Slice A — recipe/stage results.** Define plain route, authoring, stage,
  diagnostic, and completion records plus fail-closed stage transitions.
- **Slice B — workflow adoption.** Migrate direct geometry, model scene,
  texture, drop/file, and default-policy routes; split the handoff
  implementation by actual ownership behind `AssetWorkflowModule`.
- **Slice C — parity.** Prove visibility, selectability, material/texture
  behavior, stale cancellation, and queued responsiveness for every route.
- **Slice D — cleanup.** Delete old role callback registries, public IO bridge
  modules, monolithic handoff surface, compatibility aliases, and duplicate
  tests only after the real workflows use the recipe.

## Required changes

- [x] Define one plain `AssetImportRecipe` with explicit payload route,
      required stages, `ImportAuthoringRecipe`, postprocess policy, and stable
      request/world generations.
- [ ] Define typed copied results between decode, CPU materialization, ECS
      authoring, postprocess, residency, and completion; no stage may borrow
      worker/ECS/graphics ownership across boundaries.
- [ ] Execute background stages through `JobService` and main-thread mutation
      through bounded, generation-revalidated apply.
- [ ] Route generated property textures through `TextureBakeService` and
      geometry uploads through the common residency coordinator; result
      binding/selection/focus remains caller-owned completion policy.
- [ ] Integrate normal/UV/material preparation into named materialization/
      postprocess steps and replace the role-only `AssetMeshNormals` surface.
- [ ] Migrate direct mesh, point cloud, graph, model scene, standalone texture,
      editor file/drop, agent, and reference/default import workflows.
- [ ] Preserve `AssetIngestStateMachine` terminal state, source diagnostics,
      generated asset identity, visible fallback, stale discard, and
      exactly-once selection/focus/completion.
- [ ] Delete `Runtime.AssetGeometryIO`, `Runtime.AssetModelTextureIO`, obsolete
      role callback registries including `Asset.GeometryIOBridge` and
      `Asset.ModelTextureIOBridge`, the public `AssetImportPipeline`,
      `AssetModelTextureHandoff` and monolithic `AssetModelSceneHandoff`
      surfaces, and duplicate path-specific orchestration after parity; keep
      the recipe executor private to `AssetWorkflowModule`, the assets-owned
      route and pure external-path resolver, and lower-layer decoders as
      ordinary functions.
- [x] Remove the test-only bridge export dispatch while preserving promoted
      geometry exporter correctness tests at their owning geometry modules; do
      not create an unused replacement export service.

## Tests

- [ ] Stage-machine contracts cover success/failure/cancellation/stale result,
      malformed stage order, world replacement, and exactly-once completion.
- [ ] Route matrix covers every supported payload with deterministic fake IO
      and real queued JobService execution.
- [ ] Import visibility contracts prove authored entities are visible,
      selectable, correctly focused, and material/texture/property outputs use
      the new residency/bake paths.
- [ ] Existing model/companion and stale-postprocess regressions are migrated
      to the recipe path before old modules are removed.
- [ ] Geometry exporter tests continue to call the promoted exporter functions
      directly after the zero-production-consumer bridge export route is
      removed.
- [ ] Structural tests prove no production role callback registry or deleted
      IO/handoff module remains.

## Docs

- [ ] Update asset/runtime import architecture and Sandbox workflow docs with
      the stage ownership and recipe.
- [ ] Regenerate module inventory and update import visibility documentation.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Every production import route executes the same explicit stage model
      through `AssetWorkflowModule`.
- [ ] Each layer owns only its stage data/operation; runtime alone composes ECS,
      postprocess, bake, residency, and completion.
- [ ] Real route/visibility/parity tests pass before the old IO bridges,
      callback registries, and handoff orchestration are deleted.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AssetImport|AssetWorkflow|ModelScene|ImportVisibility' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'Import|ModelScene' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Another import service/registry per format or a live lower-layer
  `AssetService`/ECS/graphics dependency.
- Deleting a route before its visibility/selectability and failure contracts
  run through the recipe.
- Preserving the old handoff as a permanent compatibility facade.

## Maturity

- Target: `Retired`; CPU stage/route contracts, operational real-workflow
  visibility, and parity precede the final mechanical deletion slice.
