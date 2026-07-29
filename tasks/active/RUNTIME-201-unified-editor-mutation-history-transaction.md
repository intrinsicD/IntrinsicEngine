---
id: RUNTIME-201
theme: F
depends_on: [RUNTIME-192, RUNTIME-193, RUNTIME-194]
maturity_target: Retired
---
# RUNTIME-201 — Unified editor mutation and history transaction

## Goal

- Route every undoable runtime entity/geometry edit through one internal
  generation-validated mutation transaction and `EditorCommandHistory`, then
  delete feature-specific history builders, `GizmoUndoStack`, and the unused
  parallel CommandBus inverse-history hook.

## Non-goals

- No database transaction framework, reflection serializer, universal command
  hierarchy, or undo of non-editor runtime lifecycle.
- No app-owned state snapshot or UI-private mutation path.
- No change to algorithm mathematics; feature owners still prepare typed
  before/after state.

## Context

- Status: in progress; owner `Codex`; branch
  `codex/runtime-201-unified-editor-mutation`; Slice A is complete. Slice B has
  migrated direct transform edits and synchronous/asynchronous ICP transform
  publication, gizmo drag commit, default/lane visualization-config edits, and
  geometry-presentation slot authoring plus synchronous/asynchronous mesh
  denoise position, mesh-curvature property, and remesh/subdivide/simplify
  topology publication, UV-regeneration topology/property publication, and
  point-cloud outlier replacement, parameterization UV publication, plus
  generic render-hint and primitive-view component edits and vertex-channel
  binding descriptors plus synchronous/asynchronous mesh, graph, and
  point-cloud vertex-normal publication plus CPU/Vulkan K-Means output
  publication. Progressive Poisson point-property publication and its
  destructive mesh-to-point-cloud conversion now capture their complete
  geometry/presentation cohorts, including queued validation and exact domain
  restoration.
  `GizmoUndoStack` and the now-unused public transform/visualization history
  adapters are deleted together with the primitive-view compatibility builder
  and the unused CommandBus inverse-history hook. The next cleanup is the final
  production mutation census.
- At task intake, `EditorCommandHistory` was already the durable editor
  undo/redo owner but also contained feature-specific builders. Geometry/method
  facades still duplicate snapshot/validate/apply/dirty/history rules; gizmo
  drag commits used the now-retired `GizmoUndoStack`, and
  `CommandBus::SetHistoryHook` /
  `RecordInverse` have tests but no production history consumer.
- The common behavior is small: capture typed before state, compute/receive
  typed after state, revalidate entity/source generation, apply atomically,
  stamp dirty generations, and record one deterministic undo/redo entry.
- Asynchronous work remains on `JobService`; only current completions may enter
  the mutation transaction.

## Right-sizing

- **Element:** the common mutation transaction shape triggers the
  interface/service and plumbing-ratio heuristics if modeled as another public
  runtime service or module.
- **Simpler alternative:** keep one include-only internal function template
  beside `EditorCommandHistory`; feature owners supply typed identity,
  generation, state, validation, atomic-apply, and dirty-stamp callbacks.
- **Blast radius:** Slice A touches only the internal helper and focused runtime
  contracts. Later feature adoption is owner-by-owner; import/include census
  and `check_layering.py` guard every slice.
- **Reintroduction trigger:** add a public abstraction only when a present
  second history backend, cross-layer caller, or independently replaceable
  transaction implementation requires one.

## Slice plan

- **Slice A — transaction helper (complete 2026-07-29).** Add an
  implementation/internal
  `ExecuteUndoableEntityMutation<TState>` shape and focused atomicity/staleness
  contracts without a new public service.
- **Slice B — feature adoption (in progress).** Direct transform edits,
  synchronous/asynchronous ICP transform publication, and coalesced gizmo drag
  commit use the internal transaction as of 2026-07-29. Entity-default and
  lane-targeted visualization-config plus geometry-presentation slot edits now
  use the same transaction. Mesh denoise and curvature are the first
  geometry-property owners migrated; remesh, subdivide, and simplify share the
  migrated mesh-topology owner, while UV regeneration supplies its semantic
  source snapshot and full-GPU dirty policy to the same mechanics. Point-cloud
  outlier removal supplies its full point-property/deleted-slot snapshot and
  replacement dirty policy. Parameterization UV publication supplies its exact
  semantic triangle topology, positions, and current-UV snapshot. Render-hint
  and primitive-view edits supply the complete optional render-component
  cohort. Vertex-channel binding edits supply the complete optional binding
  descriptor and selected-channel dirty policy. The three vertex-normal owners
  supply exact non-output source-property and optional current-normal snapshots
  for both immediate and queued publication. Clustering captures input points
  plus its exact optional label/color/scalar output cohort and borrows document
  history only when that optional module is composed. Progressive Poisson
  captures its four optional point outputs plus visualization state, or the
  complete mesh/point-cloud geometry-source and presentation cohort for its
  destructive conversion. Audit the remaining import postprocess commits.
- **Slice C — cleanup (in progress).** `GizmoUndoStack` was deleted with gizmo
  adoption. The unused public transform and visualization adapter DTOs/builders
  were deleted after their owners adopted the transaction; render-hint adoption
  also deleted the primitive-view history builder. The CommandBus
  inverse-history API and its compatibility tests are deleted. Delete any
  remaining duplicate apply paths found by the final production mutation
  census.

## Required changes

- [x] Define one internal mutation helper taking stable entity/world identity,
      expected generations, typed before/after state, pure validation, atomic
      apply, dirty stamping, and deterministic undo/redo callbacks.
- [ ] Fail closed without mutation/history on stale entity, source/property/
      presentation generation, validation failure, cancelled work, or failed
      apply.
- [ ] Keep feature-specific state capture and restoration code with the owning
      feature; transform, visualization/presentation, mesh denoise, and mesh
      curvature plus mesh topology replacement, point-cloud replacement, and
      parameterization, render hints, and vertex-channel bindings satisfy this
      now; mesh/graph/point-cloud normal properties share another owner-local
      typed state, clustering owns its typed output cohort, and Progressive
      Poisson owns its point-output/domain-conversion cohort. Remaining import
      paths still need classification or migration.
- [x] Route gizmo drag commit and property transform edits through the same
      history owner and preserve drag coalescing semantics. The production
      census found no separate keyboard-only transform mutation path.
- [ ] Migrate geometry/property, presentation/visualization, registration,
      clustering/method, parameterization, import postprocess, and destructive
      domain-conversion mutations.
- [ ] Delete `GizmoUndoStack`, feature-specific builders from
      `EditorCommandHistory`, and `CommandBus::SetHistoryHook` /
      `RecordInverse` after production adoption and parity. Gizmo plus the
      transform/visualization/primitive-view builders and CommandBus hook are
      complete.

## Tests

- [x] Generic contracts cover success, failed validation, stale generation,
      apply failure, exact one-entry commit, deterministic undo/redo, and no
      partial mutation.
- [ ] Feature matrix covers transform/gizmo, topology/property,
      presentation/material, async method completion, import enrichment, and
      domain conversion through the same helper. Transform/gizmo,
      presentation, and sync/async denoise/curvature property plus mesh
      topology/UV-regeneration, point-cloud replacement, and parameterization
      plus render-hint, vertex-channel, and sync/async vertex-normal coverage is
      complete. CPU clustering completion has exact history and stale-output
      coverage; GPU completion rejoins that same commit function. Progressive
      Poisson covers exact point-output restoration and full
      mesh-to-point-cloud domain/presentation undo/redo with stale-state
      rejection.
- [x] Gizmo drag coalescing and exact transform restoration remain covered
      through `EditorCommandHistory`.
- [ ] Structural tests prove no second undo stack, CommandBus history hook, or
      feature-owned parallel history path remains.

## Docs

- [ ] Update editor/runtime command-history docs with transaction ownership,
      stale completion, and feature-specific state responsibilities.
- [ ] Regenerate module inventory if public surfaces are removed.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Every undoable production edit commits exactly once through
      `EditorCommandHistory` using the same generation-validated transaction
      shape.
- [ ] UI and agent callers submit the same typed operation; neither owns
      snapshots or bypasses runtime validation.
- [ ] `GizmoUndoStack`, CommandBus inverse history, and specialized history
      builders are deleted after behavior parity.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|Gizmo|Undo|Redo|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^EditorCommandHistory\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Transform/registration adoption verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests IntrinsicRuntimeIntegrationTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.(Registration|TransformEdit)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`
- `ctest --test-dir build/ci --output-on-failure -R 'TransformEdit|InspectorTransform|UiTransform' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`
- `ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|Gizmo|Undo|Redo|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Gizmo/history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^EditorCommandHistory\.|^GizmoInteraction\.|^GizmoFrameService\.|RuntimeEngineLayering\.SceneInteractionModuleOwnsGizmoFrameServiceOutOfEngine' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`
- `ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|Gizmo|Undo|Redo|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `rg -n 'GizmoUndoStack|GizmoTransformEdit|UndoStack\(' src/runtime --glob '*.{cpp,cppm,hpp}'`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
- `tools/ci/run_clean_workshop_review.sh . --strict`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Visualization-history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.VisualizationConfig|^EditorCommandHistory\.(PrimitiveViewAdapterIsReversible|UndoableEntityMutation)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`
- `ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|Gizmo|Undo|Redo|Mutation|VisualizationConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `rg -n '\b(EditorTransformEditCommand|MakeTransformEditCommand|EditorVisualizationConfigCommand|MakeVisualizationConfigCommand|MakeVisualizationConfigTargetCommand)\b' . --glob '!build/**' --glob '!tasks/archive/**' --glob '!tasks/done/**'`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
- `tools/ci/run_clean_workshop_review.sh . --strict`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Geometry-presentation history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.GeometryPresentation(SlotCommandsUseCommandHistory|HistoryRejectsInterveningGenerationWithoutMutation)$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`
- `ctest --test-dir build/ci --output-on-failure -R 'GeometryPresentation|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Mesh-denoise property-history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.MeshDenoise' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `ctest --test-dir build/ci --output-on-failure -R 'MeshDenoise|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Mesh-curvature property-history convergence verification completed on
2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.MeshCurvature' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `ctest --test-dir build/ci --output-on-failure -R 'MeshCurvature|MeshDenoise|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Mesh-topology replacement history convergence verification completed on
2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.Mesh(Remesh|Subdivide|Simplify)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `ctest --test-dir build/ci --output-on-failure -R 'Mesh(Remesh|Subdivide|Simplify)|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

UV-regeneration history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.(UvRegeneration|Mesh(Remesh|Subdivide|Simplify))' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'UvRegeneration|Mesh(Remesh|Subdivide|Simplify)|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Point-cloud replacement history convergence verification completed on
2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.PointCloudOutlierRemoval' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'PointCloudOutlierRemoval|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Parameterization UV history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^ParameterizationFacade\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'ParameterizationFacade|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

Render-hint history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.(RenderHint|PrimitiveView)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'RenderHint|PrimitiveView|EditorCommandHistory|Mutation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180`
- `rg -n 'EditorPrimitiveViewSettingsCommand|MakePrimitiveViewSettingsCommand' . --glob '!build/**' --glob '!tasks/archive/**' --glob '!tasks/done/**'`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --head-ref HEAD --strict`
- `python3 tools/repo/check_root_hygiene.py --root .`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/check_task_state_links.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`

CommandBus inverse-history cleanup verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^RuntimeCommandBus\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `rg -n 'RecordInverse|SetHistoryHook|CommandHistoryRecord|CommandHistoryHook' . --glob '!build/**' --glob '!tasks/archive/**' --glob '!tasks/done/**' --glob '!ara/**'`

Vertex-channel binding history convergence verification completed on
2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.VertexChannelBinding' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

Vertex-normal property history convergence verification completed on
2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\..*VertexNormals' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`

Clustering output history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^ClusteringModule\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120`

Progressive Poisson history convergence verification completed on 2026-07-29:

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.ProgressivePoisson' --timeout 60`

## Forbidden changes

- A second history service/stack, app-owned snapshot, or generic serialized
  object graph.
- Applying an async result before generation validation.
- Deleting specialized paths before their exact undo/redo parity tests use the
  common transaction.

## Maturity

- Target: `Retired`; common atomicity contracts and all real workflow
  migrations must pass before the parallel history mechanisms are removed.
- Slice A establishes the internal transaction contract. Direct transform/ICP
  and gizmo production adoption is complete, including removal of the parallel
  gizmo stack; the remaining feature migrations and history-path removals stay
  open.
