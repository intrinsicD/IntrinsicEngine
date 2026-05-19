# RUNTIME-093 — Primitive selection refinement for meshes, graphs, and point clouds

## Goal
- Implement runtime-owned CPU refinement that converts graphics primitive ID hints and pick rays into authoritative mesh face/edge/vertex, graph edge/node, and point-cloud point selection results using promoted `GeometrySources` data.

## Non-goals
- No graphics ID-pass/readback implementation (`GRAPHICS-074`).
- No runtime selection mutation policy (`RUNTIME-089`) beyond returning refined results to that controller.
- No transform-gizmo hit testing (`RUNTIME-084`).
- No new geometry algorithms unless a small local query helper is required and belongs in `geometry`.
- No graphics-side live geometry or ECS imports.

## Context
- Owner/layer: `runtime` for selection policy/refinement; `geometry` may own reusable CPU spatial/query helpers if needed.
- Graphics `EncodedSelectionId` provides a domain hint (`Entity`, `Face`, `Edge`, `Point`) plus a 28-bit payload. Runtime must treat it as a hint and refine against authoritative CPU geometry.
- `GeometrySources` owns mesh/graph/cloud positions and topology; runtime is the only layer allowed to bridge those CPU sources with graphics pick results.
- This task makes primitive-level sandbox selection useful for vertices, edges, faces, graph nodes/edges, and cloud points.

## Required changes
- [ ] Define `PrimitiveSelectionResult` with entity ID/stable ID, domain, face/edge/vertex/point IDs, world/local hit positions, and diagnostic status.
- [ ] Implement mesh refinement: face hints anchor face selection; optional ray/local hit refinement computes nearest vertex/edge on the hinted face; missing hints fall back to CPU ray/geometry query when supported.
- [ ] Implement graph refinement: edge hints return edge ID and nearest endpoint/node ID; point hints return node ID.
- [ ] Implement point-cloud refinement: point hints return point ID; missing hints may fall back to nearest point along pick ray within configurable radius.
- [ ] Apply entity transforms so refinement reports both local and world hit data.
- [ ] Integrate with `RUNTIME-089` so refined primitive results can update selection caches or editor state without graphics mutation.
- [ ] Add diagnostics for unsupported domain, stale entity, missing geometry source, invalid primitive payload, CPU fallback used, and CPU fallback miss.

## Tests
- [ ] Add `contract;runtime` coverage for mesh face hint -> face result and nearest vertex/edge refinement on a triangle fixture.
- [ ] Add `contract;runtime` coverage for graph edge hint -> edge result + nearest endpoint.
- [ ] Add `contract;runtime` coverage for point-cloud point hint -> point result.
- [ ] Add CPU fallback tests for missing hints where the fallback is implemented; otherwise assert deterministic unsupported diagnostics.
- [ ] Add transform coverage proving local/world hit positions are consistent.
- [ ] Add stale entity and invalid payload tests.

## Docs
- [ ] Update `src/runtime/README.md` with primitive refinement ownership, result shape, and fallback policy.
- [ ] Update `docs/architecture/rendering-three-pass.md` only if the selection/refinement contract changes.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [ ] Runtime can resolve entity picks into mesh face/edge/vertex, graph edge/node, and cloud point selections using authoritative CPU data.
- [ ] Graphics remains a producer of encoded ID/readback data only; it never imports geometry or mutates selection state.
- [ ] Invalid/stale/missing geometry states are deterministic and diagnosed.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -L 'contract;runtime|unit;geometry' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Importing `GeometrySources` or geometry query code into `src/graphics/*`.
- Mutating ECS selection state from graphics.
- Implementing editor widgets or gizmo interaction in this refinement task.
- Treating GPU primitive hints as authoritative when CPU geometry invalidates them.

## Maturity
- Target: `CPUContracted` primitive refinement across mesh, graph, and point-cloud fixtures.
- `Operational` interactive proof is owned by `RUNTIME-089`, `GRAPHICS-074`, and final sandbox acceptance.

