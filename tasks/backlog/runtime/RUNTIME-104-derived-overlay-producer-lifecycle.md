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
- [ ] Compare current transient debug, visualization, primitive-view, and selection lanes against each requested persistent overlay class.
- [ ] Define runtime overlay descriptors and stable keys for mesh, graph, point, line, triangle, and vector-field derived overlays.
- [ ] Add lifecycle APIs to create/update/destroy overlays with parent closure, hierarchy transform inheritance, selection eligibility, and dirty-domain stamps.
- [ ] Route overlay rendering through existing extraction packet lanes or geometry residency sidecars without storing graphics handles in ECS.
- [ ] Preserve vector-field parent/child destruction invariants from `docs/architecture/vectorfield-overlay-lifecycle-invariants.md`.
- [ ] Coordinate with `GRAPHICS-085` for backend packet proof and selection/outline behavior.

## Tests
- [ ] Add `contract;runtime` tests for overlay create/update/destroy, parent destruction closure, stable-key ordering, selection eligibility, and stale parent rejection.
- [ ] Add extraction tests proving overlay packets reach `RuntimeRenderSnapshotBatch` without graphics reading live ECS.
- [ ] Add regression tests for vector-field overlay cleanup.

## Docs
- [ ] Update `src/runtime/README.md`, `docs/migration/nonlegacy-parity-matrix.md`, and vector-field overlay lifecycle docs if behavior changes.
- [ ] Update `tasks/backlog/runtime/README.md` and `tasks/backlog/ui/README.md` where editor producers consume the seam.
- [ ] Regenerate module inventory if public module surfaces change.

## Acceptance criteria
- [ ] Each legacy derived overlay class is retained with a runtime-owned need, represented through an existing packet lane, or explicitly retired.
- [ ] Retained persistent derived overlays can be produced and retired through runtime-owned APIs.
- [ ] Overlay selection eligibility and stable IDs are explicit and test-covered.
- [ ] Legacy overlay factory behavior has a promoted owner split with no graphics-side ECS mutation.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Overlay|Visualization|Selection|Extraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing graphics renderer internals into overlay producer APIs.
- Storing live graphics/RHI handles in ECS.

## Maturity
- Target: `CPUContracted` for runtime overlay lifecycle; `Operational` backend proof is owned by `GRAPHICS-085`.
