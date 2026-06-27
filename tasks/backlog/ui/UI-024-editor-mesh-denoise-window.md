---
id: UI-024
theme: F
depends_on: [GEOM-042]
maturity_target: CPUContracted
---
# UI-024 — Sandbox EditorUI mesh denoising window

## Goal
- Add a Sandbox EditorUI method window under `Mesh > Processing > Denoise` that runs the `GEOM-042` mesh denoiser on the selected mesh entity and publishes the updated positions to canonical `v:position` data.
- Follow the `UI-022` editor pattern: domain menu > `Processing` submenu > method window with a single action button, publication to canonical geometry data, and undo/redo through the editor command history.

## Non-goals
- No geometry kernel implementation; `GEOM-042` owns the mesh denoising algorithm. The runtime only composes ECS `GeometrySources`, the `GEOM-042` kernel, and the editor command/history seam.
- No GPU/RHI allocation, renderer feature work, shader work, or compute path; CPU publication plus the existing deferred dirty-tag contract only.
- No new persistent or generated asset; the command mutates live entity `GeometrySources` `v:position` in place via the command history.
- No async/streaming execution path unless a later value-gated runtime task accepts it.
- No `Runtime.Engine.cppm` public API expansion unless the existing `SandboxEditorContext` and command-history seams cannot express the workflow.
- No additional method leaves under edges, halfedges, faces, graph, or point-cloud domains in this slice.

## Context
- Status: backlog. Blocked on `GEOM-042` (mesh denoiser kernel). The window is feature-gated/disabled until `GEOM-042` lands; the menu leaf and command surface must compile and present deterministic "denoiser unavailable" diagnostics while the dependency is unresolved.
- Owning subsystem/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.cppm` and `src/runtime/Editor/Runtime.SandboxEditorUi.cpp`. Runtime composes ECS `GeometrySources` + the geometry denoiser + editor command/history; geometry owns the algorithm and never depends on runtime.
- Mirrors `UI-022` (vertex-normal recompute windows): the domain menu exposes a `Processing` submenu, a method window owns the action button label (`Denoise`), the command discovers the selected mesh entity, calls a geometry-owned kernel, publishes to a single canonical property, stamps the deferred dirty tag, and records an undoable entry in `EditorCommandHistory`.
- Selection and discovery reuse `SandboxEditorContext` and the existing eligible-mesh discovery seam advertised by `SandboxEditorGeometryProcessingAlgorithm`; this task adds a denoise method advertisement for mesh vertex discovery rather than a new UI-owned discovery path.
- Canonical target property is `Extrinsic::ECS::Components::GeometrySources::PropertyNames::kPosition` (`v:position`) on `GeometrySources::Vertices`. The denoiser reads current `v:position` and writes back count-matched, finite positions.
- GPU synchronization follows the promoted deferred dirty-tag contract used by `UI-022`: the editor command publishes CPU `v:position`, stamps `DirtyVertexAttributes`, and mesh extraction/main-loop residency repacks and uploads on the next extraction opportunity. The UI command must not call renderer/RHI upload APIs or launch a GPU task; reserve `GpuDirty` for broad fallback cases.
- `src/runtime/Runtime.Engine.cppm` is the composition root and already exposes editor command history; prefer keeping the denoise command in the Sandbox EditorUI command surface.

## Slice plan
- [ ] Slice 1 — Feature-gated leaf and window: add the `Mesh > Processing > Denoise` menu leaf, the window state/model, and parameter controls; the `Denoise` action reports deterministic "denoiser unavailable" diagnostics while `GEOM-042` is unresolved.
- [ ] Slice 2 — Live command on `GEOM-042`: wire the runtime command DTO/result to the landed `GEOM-042` kernel, publish `v:position`, stamp `DirtyVertexAttributes`, and record an undoable history entry.

## Required changes
- [ ] Extend the processing menu model in `src/runtime/Editor/Runtime.SandboxEditorUi.cppm` so the mesh domain exposes a `Denoise` method leaf under `Mesh > Processing`, with no new leaves under other domains or elements in this slice.
- [ ] Add a `SandboxEditorGeometryProcessingAlgorithm` enumerator (e.g. `MeshDenoise`) and its `DebugNameForSandboxEditorGeometryProcessingAlgorithm(...)` mapping in `src/runtime/Editor/Runtime.SandboxEditorUi.cppm`/`.cpp`, advertising mesh vertex discovery from `GeometrySources`.
- [ ] Add Sandbox EditorUI state/model data for the denoise window in `src/runtime/Editor/Runtime.SandboxEditorUi.cppm`: selected mesh entity, parameter settings, last result/diagnostics, and a `GEOM-042`-availability flag that disables the `Denoise` action when the kernel is absent.
- [ ] Add denoise parameter settings: `iterations`, `sigma_spatial`, `sigma_range`, and stage selection, validated and clamped to the ranges the `GEOM-042` DTO accepts; the canonical output target is fixed to `v:position`.
- [ ] Add a runtime-owned mesh denoise command DTO/result and an `ApplySandboxEditorMeshDenoiseCommand(...)` helper in `src/runtime/Editor/Runtime.SandboxEditorUi.cpp` that validates the selected entity/domain, calls the `GEOM-042` denoiser, and publishes count-matched, finite positions as `v:position` on `GeometrySources::Vertices`.
- [ ] Fail-closed when no eligible mesh entity is selected, when mesh `GeometrySources` are not writable, when `v:position` is absent, or when `GEOM-042` is unavailable: return a deterministic command status/diagnostic and mutate no properties.
- [ ] Stamp `DirtyVertexAttributes` after successful CPU publication so mesh extraction performs a deferred reupload; do not call renderer/RHI APIs and do not stamp `GpuDirty` from the UI command.
- [ ] Record the operation through `EditorCommandHistory` with a specific denoise command label so the position write is undoable/redoable.
- [ ] Surface denoise result diagnostics (status, written vertex count, iteration count, skipped deleted slots, non-finite/degenerate counts) in the window without requiring graphics/Vulkan availability.

## Tests
- [ ] Extend `tests/contract/runtime/Test.SandboxEditorUi.cpp` so menu contract coverage proves `Denoise` appears under `Mesh > Processing` for this slice and nowhere else.
- [ ] Add a runtime/editor contract test proving the command discovers an eligible mesh entity from `GeometrySources`, invokes the denoiser, and writes finite, count-matched `v:position` values back to the mesh vertex property set.
- [ ] Add a contract test proving the denoise command is undoable/redoable: after undo the original `v:position` values are restored exactly, and redo reapplies the denoised positions.
- [ ] Add gating tests: with no eligible mesh entity selected, and with the `GEOM-042` kernel unavailable, the command returns a deterministic status/diagnostic and mutates no `GeometrySources` properties.
- [ ] Add a test proving `DirtyVertexAttributes` (and editor dirty state via `EditorCommandHistory`) is updated after a successful denoise, and that the command issues no direct renderer/RHI calls.
- [ ] Keep existing Sandbox EditorUI menu/processing/capability contract tests passing.

## Docs
- [ ] Update `src/runtime/README.md` with the mesh denoise editor workflow, the geometry/runtime ownership split (`GEOM-042` owns the kernel), and the deferred dirty-tag GPU synchronization contract.
- [ ] Update [`tasks/backlog/ui/README.md`](README.md) and this task if scope changes before promotion.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if runtime module surfaces change.

## Acceptance criteria
- [ ] The mesh denoise UI path is discoverable as `Mesh > Processing > Denoise` and opens a window exposing `iterations`, `sigma_spatial`, `sigma_range`, and stage selection plus a single `Denoise` action.
- [ ] The denoise command calls the `GEOM-042` geometry-owned kernel and publishes `v:position` without any UI/runtime-owned denoising algorithm.
- [ ] The command is undoable/redoable through `EditorCommandHistory`: undo restores original positions exactly and redo reapplies denoised positions.
- [ ] With no eligible mesh entity or with `GEOM-042` unavailable, the command/window reports deterministic statuses/diagnostics and mutates no unrelated or canonical properties.
- [ ] Successful runs stamp `DirtyVertexAttributes` and rely on deferred extraction rather than direct renderer/RHI calls.
- [ ] Focused runtime contract tests and structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|EditorCommandHistory|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Implementing the mesh denoising kernel in UI/runtime instead of consuming `GEOM-042`; the runtime only composes the kernel.
- Reaching from geometry into runtime/ECS/UI to satisfy this workflow; `src/geometry/*` must not depend on assets/runtime/graphics/rhi/ecs/app.
- Adding renderer/RHI dependencies, Vulkan-only behavior, GPU/compute, or shader work to the editor command.
- Introducing a new persistent/generated asset or an async/streaming execution path.
- Expanding `Runtime.Engine.cppm` public API when the existing `SandboxEditorContext`/command-history seams suffice.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted`; the endpoint is a CPU/null-safe editor command plus a UI menu/command contract test.
- No `Operational` Vulkan/GPU follow-up is owed beyond the editor contract unless a later task wires a real interactive smoke or backend-specific visual proof.

- Closure: no `Operational` follow-up is owed for this task.
