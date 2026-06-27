---
id: UI-025
theme: F
depends_on: [GEOM-043, GEOM-044]
maturity_target: CPUContracted
---
# UI-025 — Sandbox EditorUI remeshing and subdivision windows

## Goal
- Add Sandbox EditorUI method windows under `Mesh > Processing > Remesh` and `Mesh > Processing > Subdivide` that drive the `GEOM-043` remeshing options and `GEOM-044` subdivision operators on the selected mesh entity.
- Run each operator on the selected mesh entity, publish the resulting mesh to ECS `GeometrySources`, and integrate the operation with editor undo/redo, mirroring the `UI-022` menu/window/command-history convention.
- Feature-gate every control on the presence of its backing geometry kernel so an unavailable operator reports deterministic diagnostics instead of failing silently.

## Non-goals
- No geometry kernel implementation; `GEOM-043` owns remeshing and `GEOM-044` owns subdivision (Loop / Catmull-Clark / sqrt(3)).
- No GPU/RHI allocation, shader work, renderer feature work, or Vulkan-only behavior in the editor commands.
- No new persistent or generated asset; the result lives in the live `GeometrySources` of the selected entity.
- No `Runtime.Engine.cppm` public API expansion unless the existing `SandboxEditorContext` and command-history seams cannot express the workflow.
- No additional `Mesh > Processing` method windows beyond Remesh and Subdivide.
- No async/streaming execution path unless a later value-gated runtime task accepts it.

## Context
- Status: backlog.
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`; runtime composes the geometry operators while geometry owns the algorithms. Runtime depends on geometry; geometry never depends on runtime/ECS/UI.
- This task mirrors `tasks/backlog/ui/UI-022-sandbox-editor-vertex-normal-recompute.md`: a domain menu (`Mesh`) carries a `Processing` submenu whose leaves open method windows; the opened window owns its action-button label, not the menu leaf.
- The selected mesh entity is discovered through the same selection/`GeometrySources` seams used by `UI-022`. Eligibility requires a live selected mesh entity with a writable halfedge-mesh `GeometrySources` provenance; ineligible selections show deterministic diagnostics rather than hidden failure. Reuse `Extrinsic::Runtime::GeometryAvailability` (`GeometryElementDomain::MeshVertex` / `MeshFace`) to confirm a mesh source before enabling the action.
- Subdivision Catmull-Clark and Loop already exist; sqrt(3) subdivision and the new remeshing options (uniform/adaptive, sizing laws, project-to-surface) are introduced by `GEOM-043`/`GEOM-044`. Each window control must be feature-gated on the availability of the specific kernel it drives.
- Both operators replace the mesh topology/geometry wholesale: the command must publish the new mesh into the selected entity's `GeometrySources` (replacing vertex/edge/halfedge/face element sets and their canonical properties) and snapshot enough state for undo to restore the prior mesh.
- GPU synchronization follows the promoted dirty-tag contract used by `UI-022`: the editor command publishes the CPU mesh into `GeometrySources`, stamps the existing dirty tags that mesh residency/extraction already consumes for a topology replacement, and lets render extraction repack/upload on the next extraction opportunity. The UI command must not call renderer/RHI upload APIs directly or launch a GPU update task. If mesh extraction does not currently treat a full topology replacement as a re-upload trigger, extend that consumer-side dirty set rather than authoring a UI-owned broad `GpuDirty` tag.
- `src/runtime/Runtime.Engine.cppm` is the composition root and already exposes `EditorCommandHistory`. Prefer keeping the remesh/subdivide commands in the Sandbox EditorUI command surface unless implementation proves a broader runtime command API is needed.

## Slice plan
- [ ] Slice A — Remesh window driving `GEOM-043`: uniform/adaptive selection, target edge length, iterations, project-to-surface toggle (Slice A controls), plus a `Remesh` action that publishes the result and records undo.
- [ ] Slice B — Sizing-law selection for the Remesh window (mean-curvature vs error-bounded, `GEOM-043` Slice B), feature-gated on the sizing-law kernel.
- [ ] Slice C — Subdivide window driving `GEOM-044`: operator selection Loop / Catmull-Clark / sqrt(3), feature-edge-preservation toggle for Loop, iteration count, plus a `Subdivide` action that publishes the result and records undo.

## Required changes
- [ ] Extend the `Mesh > Processing` menu model in `src/runtime/Editor/Runtime.SandboxEditorUi.cppm`/`.cpp` to expose `Remesh` and `Subdivide` method leaves that open their respective windows; no other new `Processing` methods are added.
- [ ] Add Sandbox EditorUI state/model data for the Remesh window: selected entity, method selection (uniform/adaptive), target edge length, iteration count, project-to-surface toggle, sizing-law selection (mean-curvature vs error-bounded), last result, and fail-closed diagnostics.
- [ ] Add Sandbox EditorUI state/model data for the Subdivide window: selected entity, operator selection (Loop / Catmull-Clark / sqrt(3)), Loop feature-edge-preservation toggle, iteration count, last result, and fail-closed diagnostics.
- [ ] Feature-gate each control on the presence of its backing kernel: disable adaptive/sizing-law/project-to-surface controls when the `GEOM-043` capability is absent and disable an operator entry (notably sqrt(3) and Loop feature-edge preservation) when the `GEOM-044` capability is absent, with a deterministic "kernel unavailable" diagnostic instead of a hidden no-op.
- [ ] Add a runtime-owned remesh command DTO/result and `ApplySandboxEditorMeshRemeshCommand(...)` helper in `Runtime.SandboxEditorUi.*` that validates the selected entity/domain, invokes the selected `GEOM-043` operator with the window parameters, and replaces the mesh in the entity's `GeometrySources`.
- [ ] Add a runtime-owned subdivide command DTO/result and `ApplySandboxEditorMeshSubdivideCommand(...)` helper that validates the selected entity/domain, invokes the selected `GEOM-044` operator (with iteration count and Loop feature-edge preservation) and replaces the mesh in the entity's `GeometrySources`.
- [ ] Stamp the existing dirty state that mesh residency/extraction consumes for a topology replacement after a successful publication, reserving any broad `GpuDirty` fallback for cases the UI command does not own; do not call renderer/RHI directly.
- [ ] Record each operation through `EditorCommandHistory` with specific, distinct command labels (remesh vs subdivide) so the prior mesh is restored on undo and reapplied on redo.
- [ ] Surface remesh/subdivide result diagnostics (status, input/output vertex and face counts, iteration count, fail-closed reason) in the windows without requiring graphics/Vulkan availability.

## Tests
- [ ] Extend `tests/contract/runtime/Test.SandboxEditorUi.cpp` so menu contract coverage proves `Remesh` and `Subdivide` appear under `Mesh > Processing` for this slice and no other new method leaves are added.
- [ ] Add a remesh command test proving the command discovers an eligible selected mesh entity, invokes the selected `GEOM-043` operator with the window parameters (uniform/adaptive, target edge length, iterations, project-to-surface, sizing law), and replaces the mesh in `GeometrySources`.
- [ ] Add a subdivide command test proving the command discovers an eligible selected mesh entity, invokes the selected `GEOM-044` operator (Loop / Catmull-Clark / sqrt(3), iteration count, Loop feature-edge preservation), and replaces the mesh in `GeometrySources`.
- [ ] Add undo/redo tests proving a successful remesh and a successful subdivide each restore the prior mesh on undo and reapply the result on redo through `EditorCommandHistory`.
- [ ] Add gating tests proving that when a selected operator's kernel is unavailable the control is disabled/feature-gated and the command returns a deterministic "kernel unavailable" status without mutating `GeometrySources`.
- [ ] Add selected-entity/domain validation tests proving non-mesh or empty selections fail closed with deterministic diagnostics and no property mutation.
- [ ] Add a dirty-state test proving the topology-replacement dirty tags and editor dirty state are stamped after a successful publish, and that the command relies on deferred extraction rather than direct renderer/RHI calls.

## Docs
- [ ] Update `src/runtime/README.md` with the remesh/subdivide editor workflow, the geometry/runtime ownership split, the kernel feature-gating behavior, and the deferred dirty-tag GPU synchronization contract.
- [ ] Update [`tasks/backlog/ui/README.md`](README.md) and this task if scope changes before promotion.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if runtime module surfaces change.

## Acceptance criteria
- [ ] The Remesh window is discoverable as `Mesh > Processing > Remesh` and the Subdivide window as `Mesh > Processing > Subdivide`.
- [ ] The Remesh command calls the `GEOM-043` operator with uniform/adaptive selection, target edge length, iterations, project-to-surface, and sizing-law selection, and replaces the selected entity's mesh in `GeometrySources` without UI-owned remeshing algorithms.
- [ ] The Subdivide command calls the `GEOM-044` operator with Loop / Catmull-Clark / sqrt(3) selection, Loop feature-edge preservation, and iteration count, and replaces the selected entity's mesh in `GeometrySources` without UI-owned subdivision algorithms.
- [ ] A successful remesh and a successful subdivide are each undoable and redoable through `EditorCommandHistory`.
- [ ] When a selected operator's kernel is unavailable the corresponding control is feature-gated and the command returns a deterministic status without mutating `GeometrySources`.
- [ ] Failure and ineligible-selection cases report deterministic command statuses/diagnostics and do not mutate unrelated properties.
- [ ] Focused runtime contract tests and structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing the remeshing or subdivision geometry kernels in UI/runtime instead of consuming `GEOM-043`/`GEOM-044`.
- Reaching from geometry into runtime/ECS/UI, or otherwise introducing a geometry -> runtime dependency, to satisfy this workflow.
- Adding renderer/RHI/Vulkan dependencies or GPU-only behavior to the editor commands.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted`; the endpoint is CPU/null-safe editor commands plus UI contract coverage.
- No `Operational` Vulkan/GPU follow-up is owed unless a later task requires backend-specific visual proof.

- Closure: no `Operational` follow-up is owed for this task.
