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

- Completed and retired on 2026-07-31 at `Retired` after every production
  import route converged on one staged recipe behind the sole published
  `AssetWorkflowModule` service and the superseded public seams were deleted.
- Commit: implementation and accepted review surface through `c974242b`; this
  retirement-state commit plus the generated report provide the final exact
  task-state binding.
- The first fixed-surface high-risk review requested three revisions. The
  second independent review accepted the repaired exact revision and digest
  with no blocking findings; both dispositions remain in the append-only
  evidence record.
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
- Slice B policy ownership completed on 2026-07-31: recipe fields now drive
  authoring, direct-mesh postprocess, selection, and camera focus directly;
  the three runtime import-policy callback registries and their Sandbox
  install/uninstall lifecycle are removed.
- Slice D completed on 2026-07-31: `AssetWorkflowModule` is the sole published
  import service; the executor, recipe policies, decode/materialization, and
  texture-residency modules are private implementation file sets. The public
  `AssetImportPipeline`, IO bridges, role callback registries, compatibility
  handoffs, and their duplicate direct tests are deleted.
- The first serial ASan gate found two lifetime defects: queued asset CPU work
  could outlive `AssetLoadPipeline`, and one shutdown test borrowed loop-local
  synchronization state. Shared callback invalidation now makes queued work
  no-op after pipeline teardown without fencing unrelated scheduler work, and
  the test waits for its worker before releasing locals. A ccache-disabled
  clean rebuild reproduced the fixed surface without stale module artifacts.
- Revised-surface verification: focused import/runtime contracts passed
  101/101; the default CPU gate passed 4,013/4,013 (one expected GLFW/LSan
  skip); ASan and UBSan each passed 2,667/2,667; the queued geometry reimport
  contract passed 20 consecutive repetitions; the generated-texture Vulkan
  smoke passed 20 consecutive repetitions; and the four import/model-scene
  Vulkan smokes passed 4/4. One preceding ASan sweep and a focused repeat
  reproduced the already-tracked unrelated `BUG-123` retired-scene-save
  terminal-event race; both immutable failure receipts are retained as
  observations, and the unchanged complete selector subsequently passed. The
  broader optional Vulkan
  sweep passed 47/48 and exposed the unrelated, pre-existing stale assertion
  tracked by `BUG-124`; its full failure receipt is retained. No performance
  claim is made.
- Review revision: failed diagnostic attempts are preserved byte-for-byte as
  observation artifacts rather than filtered from a generated report; queued
  geometry reimport now carries `ExistingAsset` through ingest and reloads the
  same asset without ECS duplication; and the generated-texture Vulkan smoke
  deterministically completes Ready-event publication before its fallback
  upload while accepting a production residency request that wins the narrow
  contention window. The new queued-reimport contract and generated-texture
  Vulkan smoke each passed 20 consecutive direct diagnostic runs before the
  revised evidence gates.

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
- [x] Define typed copied results between decode, CPU materialization, ECS
      authoring, postprocess, residency, and completion; no stage may borrow
      worker/ECS/graphics ownership across boundaries.
- [x] Execute background stages through `JobService` and main-thread mutation
      through bounded, generation-revalidated apply.
- [x] Route generated property textures through `TextureBakeService` and
      geometry uploads through the common residency coordinator; result
      binding/selection/focus remains caller-owned completion policy.
- [x] Integrate normal/UV/material preparation into named materialization/
      postprocess steps and replace the role-only `AssetMeshNormals` surface.
- [x] Migrate direct mesh, point cloud, graph, model scene, standalone texture,
      editor file/drop, agent, and reference/default import workflows.
- [x] Preserve `AssetIngestStateMachine` terminal state, source diagnostics,
      generated asset identity, visible fallback, stale discard, and
      exactly-once selection/focus/completion.
- [x] Delete `Runtime.AssetGeometryIO`, `Runtime.AssetModelTextureIO`, obsolete
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

- [x] Stage-machine contracts cover success/failure/cancellation/stale result,
      malformed stage order, world replacement, and exactly-once completion.
- [x] Route matrix covers every supported payload with deterministic fake IO
      and real queued JobService execution.
- [x] Import visibility contracts prove authored entities are visible,
      selectable, correctly focused, and material/texture/property outputs use
      the new residency/bake paths.
- [x] Existing model/companion and stale-postprocess regressions are migrated
      to the recipe path before old modules are removed.
- [x] Geometry exporter tests continue to call the promoted exporter functions
      directly after the zero-production-consumer bridge export route is
      removed.
- [x] Structural tests prove no production role callback registry or deleted
      IO/handoff module remains.

## Docs

- [x] Update asset/runtime import architecture and Sandbox workflow docs with
      the stage ownership and recipe.
- [x] Regenerate module inventory and update import visibility documentation.
- [x] Refresh task indexes, session brief, and retirement records after the
      accepted fixed-surface review.

## Acceptance criteria

- [x] Every production import route executes the same explicit stage model
      through `AssetWorkflowModule`.
- [x] Each layer owns only its stage data/operation; runtime alone composes ECS,
      postprocess, bake, residency, and completion.
- [x] Real route/visibility/parity tests pass before the old IO bridges,
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
