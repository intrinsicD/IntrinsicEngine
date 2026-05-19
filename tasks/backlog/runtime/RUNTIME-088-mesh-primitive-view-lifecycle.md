# RUNTIME-088 — Mesh primitive view lifecycle for vertices and edges

## Goal
- Add runtime-owned lifecycle support that derives optional retained edge and vertex render views from a mesh entity so sandbox users can render/select mesh edges and vertices in addition to filled faces.

## Non-goals
- No base mesh surface residency (`RUNTIME-085`) implementation in this task.
- No graph or point-cloud residency (`RUNTIME-086`, `RUNTIME-087`).
- No graphics pass command work; retained line/point pass command routing is `GRAPHICS-071` and already done.
- No editor UI panel for toggling views; this task exposes runtime/editor-facing control seams only.
- No ECS storage of graphics/RHI handles.

## Context
- Owner/layer: `runtime`; it may read ECS mesh `GeometrySources` and attach CPU render-hint components or maintain sidecars, but graphics never reads live ECS.
- Upstream: `RUNTIME-085` provides surface mesh residency; `GRAPHICS-071` records line/point command bodies; `GRAPHICS-074` and `RUNTIME-093` consume primitive IDs for selection.
- Architecture docs describe mesh helper lines/points as views over authoritative mesh geometry; this task makes that explicit and testable for the promoted path.

## Required changes
- [ ] Define a runtime control surface for mesh primitive views (for example `MeshPrimitiveViewSettings` in runtime/editor state) that can enable edge view, vertex view, or both for a mesh entity without moving policy into graphics.
- [ ] Decide whether edge/vertex views are represented as child ECS entities with `RenderLines` / `RenderPoints` and shared authoring identity, or as additional runtime sidecars attached to the parent renderable; document the trade-off and implement the selected model.
- [ ] For edge views, derive line-list indices from mesh `GeometrySources::Edges` or halfedge/faces when explicit edges are absent; bind through the graph/line residency path or a mesh-specific shared geometry view as appropriate.
- [ ] For vertex views, derive point geometry from mesh vertices and bind through point residency while preserving the parent mesh stable identity and primitive-domain payload mapping.
- [ ] Keep view lifetime synchronized with the parent mesh: create/update on enable, reupload on relevant dirty domains, and release on disable or parent destruction.
- [ ] Add diagnostics for created/destroyed views, invalid topology, missing source geometry, and unsupported sharing cases.

## Tests
- [ ] Add `contract;runtime` coverage for enabling edge view on a triangle mesh: extraction submits a line renderable with deterministic edge count.
- [ ] Add `contract;runtime` coverage for enabling vertex view on a mesh: extraction submits a point renderable with deterministic point count.
- [ ] Add dirty-domain coverage: vertex-position changes update both surface and vertex/edge view bounds; topology changes update edge view indices.
- [ ] Add disable/destruction cleanup coverage proving view sidecars/entities release their geometry.
- [ ] Add invalid topology tests for missing edges/halfedges and out-of-range endpoints.

## Docs
- [ ] Update `src/runtime/README.md` with the selected mesh primitive view model.
- [ ] Update `docs/architecture/rendering-three-pass.md` if the view model changes primitive-lane or selection-domain wording.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [ ] Runtime can expose mesh faces, edges, and vertices as separate renderable lanes while preserving a single authoritative mesh data source.
- [ ] Mesh edge/vertex render views participate in retained line/point rendering and later selection without graphics owning live mesh topology.
- [ ] View enable/disable, dirty updates, and cleanup are deterministic and tested.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract;runtime|contract;graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding mesh topology traversal or view creation to `src/graphics/*`.
- Storing graphics-owned handles in ECS components.
- Implementing editor UI panels or input policy in this runtime lifecycle task.
- Duplicating full mesh CPU data when a shared view or sidecar can preserve ownership cleanly.

## Maturity
- Target: `CPUContracted` for mesh primitive view lifecycle.
- `Operational` visual proof is owned by the final sandbox acceptance task after selection and present wiring land.

