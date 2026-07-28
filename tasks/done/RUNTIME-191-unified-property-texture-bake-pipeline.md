---
id: RUNTIME-191
theme: F
depends_on: [RUNTIME-190, RUNTIME-192]
maturity_target: Retired
---
# RUNTIME-191 — Unify property-to-texture baking and retire specialized paths

## Status

- Retired on 2026-07-28 at `Retired` maturity. `Runtime.TextureBakeModule`,
  `TextureBakeService`, and `Graphics.PropertyTextureBake` are now the single
  production contract, runtime service, and graphics recorder for property-to-
  texture baking. Runtime registers one bounded GPU participant; callers own
  source preparation and completed-output reconciliation.
- PR/commit: this retirement commit on `main`.
- The intake census found 12,662 lines across the generalized
  runtime/graphics bake modules and the selected-mesh, CPU mesh-attribute, and
  object-space-normal families. Production then registered two JobService GPU
  participants, exposed the object-normal queue through
  `TextureBakeProducerContext`, and retained live CPU bake calls in model-scene
  handoff plus the older selected-mesh direct command.
- Slice A completed on 2026-07-28: `Runtime.TextureBakeModule` now exports the
  canonical property/texcoord request, result/status, representation, output
  record, and diagnostics vocabulary. `TextureBakeService`, Sandbox facades
  and panels, bake snapshots, and active-world catalog tests use those types;
  consumer bindings are carried in a separate transitional snapshot rather
  than the canonical bake DTOs. The old selected-mesh direct command remains
  isolated for compatibility tests and Slice D deletion.
- Slice A verification: `cmake --build --preset ci --target IntrinsicTests
  --parallel 4` passed; the focused runtime selector covering canonical
  representation/service, Sandbox bake commands, asset-workflow lifecycle,
  object-normal lifecycle compatibility, and legacy selected-mesh
  compatibility passed 51/51. The default CPU-supported selector passed
  4,124/4,124. Strict task policy, task-state links, session-brief freshness,
  test layout, ARA claims, and source layering checks passed; documentation
  links and diff-whitespace checks also passed. Root hygiene reported only the
  existing non-fatal `.agents/` warning.
- Slice A clean-workshop review: rows 1-3 pass; rows 4-6 are not applicable
  because this slice adds no renderer member, frame pass, or recipe edge; row
  7 passes because Slices B-D remain the explicit operational/parity/retirement
  gates; row 8 passes because the transitional consumer and specialized
  producer surfaces name this active task and their Slice C/D removal points.
  No dependency edge, CMake link, module-composition order, or allowlist entry
  changed.
- Slice B completed on 2026-07-28: the canonical graphics recorder now owns
  encoded-RGBA padding/dilation, including signed-vector/normal storage, and
  the runtime registers one bounded property-texture GPU participant with
  frame-safe scratch retirement. Asset workflow, model-scene handoff, and the
  direct-mesh default policy submit through `TextureBakeService`; the
  specialized queue is no longer composed or exposed through a producer
  context. The retained specialized modules are isolated compatibility code
  awaiting Slice D deletion.
- Slice B verification: the focused canonical/property/import/runtime selector
  passed 101/101. The `ci-vulkan` ASan+UBSan
  `PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan` smoke passed and
  reads one texel beyond the source triangle to prove the generic dilation
  shader produced the padded value. The import smoke reaches canonical
  scheduling after backend promotion but remains a Slice C parity gate because
  its direct material binding still encounters the transitional bake-owned
  consumer API.
- Slice C completed on 2026-07-28: generated-output interpretation and atomic
  material/presentation reconciliation now live in `AssetWorkflowModule` and
  the Sandbox caller facades. The bake service no longer accepts consumers or
  mutates presentation/material state. Model-scene and direct-mesh import
  paths retain authored/property-buffer fallback, submit only canonical GPU
  requests, and contain no live CPU texture fallback. Direct imports use the
  existing bounded `JobService::IsReadyToApply` seam to retain one caller-owned
  retry across promoted-Vulkan cold start without blocking mesh publication or
  changing the service's fail-closed Null behavior.
- Slice C verification: the focused import/workflow/model-handoff/canonical
  texture-bake selector passed 63/63 on `ci`. The promoted-Vulkan
  `ImportedObjectSpaceNormalBakeBindsAndReadsBackExactTargetSlice` and
  `PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan` smokes both
  passed, proving exact import binding plus caller-owned rebake, rename,
  removal, and post-removal reconciliation.
- Slice D deleted the isolated selected-mesh, mesh-attribute, and object-space-
  normal runtime/graphics modules, their specialized shaders, obsolete tests,
  and every CMake entry. The four canonical contract cases moved to
  `Test.TextureBakeModule.cpp`; no compatibility alias or production import
  remains. Runtime/graphics architecture docs and the generated module
  inventory now describe the sole surviving path.
- The full CPU gate exposed that `AssetWorkflowModule` had accidentally made
  the app-composed `TextureBakeService` mandatory. Resolution now uses an
  optional service lookup, minimal asset-workflow composition is pinned by the
  fallback-bootstrap contract, and composed Null devices remain wired but
  correctly report the GPU-only service unavailable.
- Slice D verification: both canonical `ci` and ASan+UBSan `ci-vulkan`
  `IntrinsicTests` builds passed. The focused runtime/import/workflow/
  structural selector passed 30/30; the default CPU-supported gate selected
  4,042 cases with zero failures and six expected display/LSan skips. The two
  exact promoted-Vulkan import and caller-reconciliation readback smokes passed
  2/2 on an NVIDIA GeForce RTX 4090 with driver 580.159.04. Strict layering,
  test-layout, task-policy/state, docs, ARA, clean-workshop, inventory, and
  diff-hygiene checks passed; the generated inventory contains 372 modules.
- The complete gate also exposed an unrelated timing race in
  `RuntimeSceneLifecycle.RetiredQueuedSceneSavePublishesTerminalEvent`: two
  full-gate failures and a focused ninth-pass/tenth-fail run are recorded as
  `BUG-123`. No test, label, timeout, or assertion was weakened; the required
  full selector subsequently completed with zero failures.
- Slice D clean-workshop review: rows 1-3 pass; rows 4-6 are not applicable
  because no renderer member/subsystem, frame pass, or recipe dependency was
  added; row 7 passes at `Retired` with CPU plus exact operational-Vulkan
  evidence; row 8 passes because no compatibility marker, allowlist entry, or
  specialized source surface remains.

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
- No material, visualization, presentation, or other consumer mutation inside
  texture baking. `GRAPHICS-105` and the relevant caller own how a completed
  output is interpreted and bound.
- No new normal-generation algorithm, texture format feature, or unrelated
  renderer capability.

## Context

- Owning layer: `runtime` composes canonical property resolution, world
  lifetime, job scheduling, generated `AssetService` ownership, completion,
  and output-asset lifecycle. `graphics/renderer` owns the backend-neutral
  property-raster planning/recording implementation and shaders. Callers own
  all material/presentation/visualization result processing and binding.
- Retired `RUNTIME-190` generalized the interactive editor/agent GPU path, but
  deliberately retained the standalone CPU compatibility baker and relocated
  the import-time object-space-normal producer unchanged. Its narrower
  acceptance therefore did not establish one engine-wide bake pipeline.
- At intake, `TextureBakeModule` owned both
  `ObjectSpaceNormalBakeService` and the generalized property participant,
  registered two GPU queue participants, and exposed a borrowed
  `RuntimeObjectSpaceNormalBakeQueue*` through
  `TextureBakeProducerContext`.
- At intake, `Runtime.AssetModelSceneHandoff` called
  `BakeMeshVertexColorTexture(...)` for generated albedo and could call
  `BakeMeshVertexNormalTexture(...)` when the specialized normal queue was
  absent. Thus `Runtime.MeshAttributeTextureBake` was reachable from
  production import code, not only deterministic tests.
- At intake, `Runtime.SelectedMeshTextureBake` mixed generic
  request/result/catalog records with an older selected-entity direct command,
  and its public records reused `MeshAttributeTextureBake*` vocabulary.
  App/editor code consequently imported a CPU implementation module merely to
  name encoder and range enums.
- Canonical vocabulary for this task is **property**: a
  `RUNTIME-192` `GeometryPropertyRef` is the bake source. “Attribute” remains
  renderer vocabulary where appropriate, but must not create a second bake
  contract.
- Producers of transform-derived fields own their mathematics and
  invalidation. For example, a world-space normal producer applies the normal
  matrix, publishes a named property, and advances its source generation when
  the transform changes; the bake service only validates and consumes that
  property.
- Required correctness from the specialized path remains load-bearing:
  canonical source identity, bounded scheduling, world/epoch and weak-lifetime
  validation, stale-completion rejection, cache generation, ready-frame and
  frame-safe retirement, UV/topology/property byte revalidation,
  padding/dilation, output publication, and cleanup. Caller integration tests
  separately preserve progressive fallback and exact consumer binding; those
  semantics do not move into the baker.

## Control surfaces

- Config: no new tuning lane. Existing/default extent, storage encoding,
  numeric range, and padding inputs continue through the canonical request and
  validated apply path. Colormap/material meaning remains caller-owned
  processing.
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
  ownership, and output catalog operations private in its implementation
  units. Express storage encoding, range, raster mapping, and padding as
  request data; do not accept consumers or semantic normal/color space.
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
  property-reference/UV-raster/storage-encoding/range request, result, snapshot,
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
- **Slice C — parity and workflow result migration (`ParityProven`).** Prove
  interactive and import-time normal/color outputs, progressive fallback,
  generated-asset identity, caller-owned material/presentation binding, world
  replacement, stale completion, and shutdown behavior through the canonical
  path. Remove every live CPU bake call and decide whether the minimal CPU
  oracle is deleted or moved to test-only support.
- **Slice D — legacy deletion (`Retired`).** In a separate mechanical cleanup
  commit, delete the superseded selected-mesh, mesh-attribute CPU production,
  object-space-normal runtime/graphics modules and shaders, obsolete tests and
  CMake entries, then refresh documentation and generated inventories. No
  compatibility re-export remains.

## Required changes

- [x] Export one plain `PropertyTextureBakeRequest` /
      `PropertyTextureBakeResult` family from `Runtime.TextureBakeModule`,
      covering source entity/world, one canonical `GeometryPropertyRef`, UV /
      topology raster mapping, expected source/property generations, storage
      encoding and numeric range,
      extent/padding, output description, and stable output identity. The
      result reports operation/output `AssetId`, generation, readiness, and
      diagnostics; neither request nor result owns a consumer list, normal
      space, material channel, visualization mode, or presentation binding.
- [x] Replace `SelectedMeshTextureBake*` and
      `MeshAttributeTextureBake*` vocabulary in the service, Sandbox facade,
      UI, agent surface, catalogs, diagnostics, and tests with the canonical
      property vocabulary.
- [x] Keep source preparation outside the bake interface. Import and editor
      flows must create or transform the required named property first and
      submit only after domain/count/type/finite-value and UV validation can
      succeed. Transform-derived producers must advance source
      generation/dirty identity so an old world-space result cannot survive a
      transform change.
- [x] Extend the general graphics property-raster plan/recorder with the
      currently load-bearing padding/dilation and signed-vector storage
      encoding behavior,
      without normal-specific public plans, shaders, descriptor contracts, or
      Vulkan types crossing the graphics/runtime interface.
- [x] Consolidate scheduling, canonical content identity, GPU residency
      validation, cache generations, ready-frame publication, stale rejection,
      frame-safe retirement, generated-asset ownership, and output catalog
      lifecycle into the one `TextureBakeModule` implementation and one
      JobService GPU participant.
- [x] Change `AssetWorkflowModule` and `AssetModelSceneHandoff` to call the
      published `TextureBakeService` after property materialization. Preserve
      visible property-buffer/authored-texture fallback while asynchronous
      output is pending and preserve exact world/epoch/entity lifetime checks.
      Once a result is ready, the asset/material/presentation owner binds or
      postprocesses it through its own typed operation.
- [x] Remove `TextureBakeProducerContext`'s specialized queue exposure; no
      production interface may publish or accept
      `RuntimeObjectSpaceNormalBakeQueue*`.
- [x] Remove live calls to `BakeMeshAttributeTexture(...)`,
      `BakeMeshVertexNormalTexture(...)`, and
      `BakeMeshVertexColorTexture(...)`. If a CPU oracle remains useful, move
      the minimum pure raster reference into test support with no production
      module import, asset mutation, fallback selection, or duplicate public
      DTOs.
- [x] Delete the production modules
      `Runtime.SelectedMeshTextureBake`,
      `Runtime.MeshAttributeTextureBake`,
      `Runtime.ObjectSpaceNormalBake{Queue,Service,Binding,Submission}`, and
      `Graphics.ObjectSpaceNormalTextureBake` after parity evidence exists;
      remove their implementation units, specialized shaders, CMake entries,
      compatibility wrappers, and production imports.
- [x] Preserve one generated output's deterministic rebake/rename/remove
      identity. Migrate existing multi-consumer workflows to caller-owned
      result processing that applies the completed `AssetId` atomically while
      preserving unrelated channels; remove `SetConsumers`,
      `TextureBakeConsumerUpdateRequest`, and equivalent bake-owned binding
      APIs rather than replacing them.
- [x] Separate semantic migration commits from the final mechanical deletion
      commit so review can distinguish behavior changes from removals.

## Tests

- [x] CPU/headless contract tests cover canonical request validation for
      vertex, face, and nearest-edge domains; scalar, label, vector, color, and
      storage encodings; missing/invalid properties, UVs, topology, counts,
      finite values, ranges, extents, and padding. Assert that consumer or
      normal-space semantics cannot be supplied to the bake request.
- [x] CPU/headless contracts prove Null/non-operational devices fail closed and
      neither import nor editor/agent routes execute a CPU fallback.
- [x] Import contracts prove required properties are materialized before
      submission and that no object/world/tangent normal or other derived
      property is synthesized by the bake service.
- [x] A transform-derived normal contract proves that changing the transform
      and republishing the named property invalidates the old source identity,
      while an unchanged property/generation remains deterministic.
- [x] Runtime lifecycle tests cover bounded scheduling, supersession, exact
      content/cache generation, stale completion rejection, world/document
      replacement, weak target lifetime, generated-asset cleanup, and shutdown
      with the single participant.
- [x] Caller integration tests cover pending fallback, exact
      normal/albedo/visualization result processing, atomic multi-consumer
      updates outside the baker, rebake, rename, remove, and preservation of
      unrelated material channels.
- [x] Opt-in `gpu;vulkan` readback smokes prove representative editor/agent and
      import/default-policy normal and color properties use
      `Graphics.PropertyTextureBake`, including padding/dilation, with no
      specialized participant or shader.
- [x] Structural regression coverage or strict source scans prove production
      source and app code contain no imports, calls, or compatibility aliases
      for the retired specialized modules.

## Docs

- [x] Update `src/runtime/README.md` to document one property-to-texture module,
      one public service, caller-owned preparation/result processing, and the
      absence of specialized/CPU live routes.
- [x] Update `src/graphics/renderer/README.md` and the relevant
      `docs/architecture/*` pages for the one generic property-raster recorder
      and ownership split.
- [x] Update legacy/migration documentation and every link that names the
      deleted module surfaces.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module
      deletions and regenerate `tasks/SESSION-BRIEF.md` on every task lifecycle
      change.

## Acceptance criteria

- [x] Every live editor, agent/CLI, import, and default-policy
      property-to-texture request enters the same `TextureBakeService` method
      using the same request/result types.
- [x] Runtime registers exactly one property-texture GPU participant and
      exposes no specialized normal queue, service, binding, submission, or
      producer context.
- [x] Normal, color, scalar, label, and vector bakes differ only through
      prepared source properties and storage/output data; none selects a
      parallel end-to-end path or embeds consumer semantics in the baker.
- [x] Import-time missing-texture policy first materializes the named source
      property and then uses the canonical asynchronous GPU path, retaining
      visible fallback until exact completion.
- [x] No production target imports or exports
      `Runtime.SelectedMeshTextureBake`,
      `Runtime.MeshAttributeTextureBake`,
      `Runtime.ObjectSpaceNormalBake*`, or
      `Graphics.ObjectSpaceNormalTextureBake`; no compatibility re-export or
      duplicate bake vocabulary remains.
- [x] A retained CPU oracle, if any, exists only in test support and cannot be
      selected by production code.
- [x] Existing generated-asset, cache/residency, stale-result, padding/dilation,
      and shutdown guarantees have parity evidence through the canonical path;
      progressive/material/visualization integration has separate caller-owned
      result-processing parity.
- [x] The final implementation passes the right-sizing deletion test: removing
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
- Encoding object/world/tangent space, material channels, visualization modes,
  or consumer lists into the bake request/result instead of preparing the
  source property and processing the output in the caller.
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

- Achieved: `Retired`. Slice A established the CPU-contracted canonical
  vocabulary; Slice B supplied operational promoted-Vulkan evidence; Slice C
  proved workflow parity and caller-owned reconciliation; Slice D deleted all
  superseded modules, shaders, CMake entries, direct calls, compatibility
  aliases, and duplicate public vocabulary.
- Null/headless composition remains fail-closed, while the canonical GPU path
  is operational on the exercised Vulkan-capable host.
