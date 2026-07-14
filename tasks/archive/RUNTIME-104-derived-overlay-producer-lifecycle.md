---
id: RUNTIME-104
theme: F
depends_on: []
---
# RUNTIME-104 — Derived overlay producer lifecycle

## Goal
- Decide whether persistent derived overlays are necessary for current promoted workflows, and implement only the runtime-owned producer lifecycle that cannot be represented by existing transient debug or visualization packet paths.

## Non-goals
- No graphics-owned ECS mutation or live ECS reads.
- No immediate GPU upload from runtime/editor overlay creation.
- No legacy `Graphics.OverlayEntityFactory` import.
- No arbitrary property-buffer residency; `GRAPHICS-084` owns that backend-facing work.
- No persistent overlay API if current workflows are adequately served by existing transient/debug/visualization packet lanes.

## Context
- Owner/layer: `runtime/editor/app` produce overlay intent and lifecycle; graphics consumes immutable packets and reports diagnostics.
- Legacy `Graphics.OverlayEntityFactory` created child mesh/point-cloud/graph entities, attached hierarchy, seeded dirty state, assigned pick IDs, and destroyed overlays. The new architecture must keep authoritative geometry and mutation above graphics and route rendering through snapshots/packets.
- Reuse `Runtime.RenderExtraction`, `Runtime.MeshPrimitiveViewPacker`, `Runtime.VisualizationAdapters`, `Runtime.SpatialDebugAdapters`, `SelectionController`, `StableEntityLookup`, `Graphics.VisualizationPackets`, `Graphics.TransientDebugUploadHelper`, and `Graphics.VisualizationOverlayUploadHelper`.

## Value gate
- Current state: promoted runtime and graphics already have transient debug packets, visualization adapters, primitive views, selection controller, and overlay upload helpers.
- Improvement: persistent overlays are retained only when they improve current selection/visualization workflows without graphics mutating ECS or storing RHI handles in components.
- Scope decision: first compare existing packet lanes against the workflow need. If they are sufficient, retire the legacy overlay factory behavior instead of adding a new producer API.

## Required changes
- [x] Compare current transient debug, visualization, primitive-view, and selection lanes against each requested persistent overlay class.
- [x] Define the no-new-API decision for mesh, graph, point, line, triangle, and vector-field derived overlays: no runtime overlay descriptors or stable keys are added because current workflows are represented by ordinary `GeometrySources`, primitive-view sidecars, transient debug packets, or existing visualization packets.
- [x] Add no lifecycle APIs because no persistent derived overlay class is retained for current workflows; future value-gated work must open a new task before adding create/update/destroy APIs.
- [x] Route current overlay-like rendering through existing extraction packet lanes or geometry residency sidecars without storing graphics handles in ECS.
- [x] Preserve vector-field parent/child destruction invariants by documenting that the promoted vector-field packet path creates no child ECS entities; future child-overlay work must restore same-frame parent closure before extraction.
- [x] Coordinate with `GRAPHICS-085` by updating its scope to consume the classification for backend packet proof and selection/outline behavior.

## Tests
- [x] No create/update/destroy contract tests were added because no persistent overlay lifecycle API is retained; existing `GeometrySources`, primitive-view, selection, and scene-lifecycle tests remain the contracts for represented workflows.
- [x] Add extraction coverage proving vector-field overlay packets reach `RuntimeRenderSnapshotBatch`/`RenderWorld` without creating legacy-style child ECS entities or graphics reading live ECS.
- [x] Vector-field child cleanup is satisfied for current workflows by the no-child-entity packet path and documented as the required gate for any future child-overlay task.

## Docs
- [x] Update `src/runtime/README.md`, `docs/migration/nonlegacy-parity-matrix.md`, and vector-field overlay lifecycle docs for the no-new-persistent-overlay decision.
- [x] Update `tasks/backlog/runtime/README.md` and `tasks/backlog/ui/README.md` where editor producers consume the seam.
- [x] No module inventory regeneration is required because no public module surface changed.

## Acceptance criteria
- [x] Each legacy derived overlay class is retained with a runtime-owned need, represented through an existing packet lane, or explicitly retired.
- [x] No retained persistent derived overlays require runtime-owned APIs in current workflows; future persistent overlays require a new value-gated task.
- [x] Selection eligibility and stable IDs for represented selectable workflows stay on ordinary renderables, primitive-view sidecars, `SelectionController`, and `StableEntityLookup`; packet-only visualization overlays are explicitly visual-only.
- [x] Legacy overlay factory behavior has a promoted owner split with no graphics-side ECS mutation.

## Status
- Completed 2026-06-11 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Classification: mesh/graph/point child overlays are represented by ordinary promoted `GeometrySources` entities when users import or author data; mesh edge/vertex overlays use runtime-owned primitive-view sidecars; transient line/point/triangle debug overlays stay on transient debug packets; vector-field and isoline overlays stay on data-only visualization packets. No persistent derived-overlay creation/update/destroy API is retained for current workflows.
- Backend packet command-shape proof is retired by `GRAPHICS-085`; selected visualization property-buffer residency remains `GRAPHICS-084`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Overlay|Visualization|Selection|Extraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
bash -lc "set -o pipefail; ctest --test-dir build/ci --output-on-failure -R 'Overlay|Visualization|Selection|Extraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 | tee /tmp/runtime-104-focused-ctest.log"
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
tools/ci/run_clean_workshop_review.sh . --strict
```

Result: configure succeeded with Clang 23; `IntrinsicTests` built; focused
CTest passed 302/302; layering, test layout, doc links, task policy,
task-state links, session-brief freshness, docs-sync diff checks, and the
clean-workshop automated scorecard rows passed.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing graphics renderer internals into overlay producer APIs.
- Storing live graphics/RHI handles in ECS.

## Maturity
- Target: `CPUContracted` for runtime overlay lifecycle; backend packet proof is retired by `GRAPHICS-085`.

## Slice plan
- **Slice A — decision gate.** Compare existing transient debug, visualization, primitive-view, and selection lanes against each legacy overlay class and classify every class as retained (with the runtime-owned need), representable by an existing lane, or retired. The classification is the input `GRAPHICS-085` scopes against.
- **Slice B — descriptors and lifecycle APIs.** For retained classes only: overlay descriptors, stable keys, create/update/destroy APIs with parent closure, transform inheritance, selection eligibility, and dirty-domain stamps, with `contract;runtime` tests. Preserves the default CPU gate.
- **Slice C — extraction wiring.** Route retained overlays through extraction packet lanes into `RuntimeRenderSnapshotBatch`, preserve the vector-field parent/child destruction invariants, and land the regression tests. Backend consumption proof stays with `GRAPHICS-085`.
