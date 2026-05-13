# HARDEN-065 — Promote ECS geometry-source population and dirty-domain helpers

## Goal
- Define and implement the promoted ECS contract for populating `GeometrySources` from mesh/graph/point-cloud data and for stamping geometry dirty-domain tags deterministically.

## Non-goals
- No graphics/RHI upload implementation.
- No asset file import/export ownership changes.
- No live `AssetService`, renderer, runtime sidecar, or GPU handle storage in ECS components.
- No broad rewrite of geometry containers.

## Context
- Owner/layer: `ecs`, with allowed dependencies on `core` and explicitly justified CPU-only `geometry` types.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- Promoted `Extrinsic.ECS.Components.GeometrySources` currently provides borrowed `ObserverPtr<Geometry::PropertySet>` source views and domain/count helpers.
- Legacy `ECS:Components.GeometrySourcesPopulate` provides `PopulateFromMesh`, `PopulateFromGraph`, and `PopulateFromCloud`, but that behavior is not promoted.
- `DirtyTags::{GpuDirty,DirtyVertexPositions,DirtyVertexAttributes,DirtyEdgeTopology,DirtyFaceTopology}` exist, but no promoted mutation helpers consistently stamp them.

## Required changes
- [ ] Decide and document whether promoted `GeometrySources` are borrowed views, owned copies, or a split of owning and borrowed components.
- [ ] Promote or replace population helpers for at least mesh, graph, and point-cloud domains without importing runtime/assets/graphics.
- [ ] Preserve canonical property naming for positions, normals, edge connectivity, halfedge connectivity, and face halfedge references where applicable.
- [ ] Add deterministic helpers for stamping position, attribute, edge-topology, face-topology, and full-GPU dirty domains.
- [ ] Define who clears each dirty tag after downstream processing; runtime/graphics clearing must remain outside ECS if it touches sidecars or GPU state.
- [ ] Record unsupported geometry domains or ownership cases as explicit follow-up tasks.

## Tests
- [ ] Add `tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp` or equivalent promoted ECS tests for mesh, graph, and point-cloud population.
- [ ] Cover copied/borrowed lifetime expectations selected by this task.
- [ ] Cover domain detection and alive-count behavior after population.
- [ ] Cover dirty-domain helper stamping for positions, attributes, topology, and full GPU-dirty cases.
- [ ] Keep tests CPU-only and labeled `unit;ecs`.

## Docs
- [ ] Update `src/ecs/Components/README.md` with the selected geometry-source ownership and dirty-domain policy.
- [ ] Update `src/ecs/README.md` if the public module surface changes.
- [ ] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if this removes the legacy geometry-source population retirement blocker.
- [ ] Regenerate [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved.

## Acceptance criteria
- [ ] Promoted ECS can populate geometry-source components for mesh, graph, and point-cloud scenes without importing legacy ECS.
- [ ] Dirty-domain stamping is deterministic and tested for geometry mutations.
- [ ] The selected ownership model is documented and does not leak runtime, asset-service, graphics, RHI, or GPU-residency state into ECS.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding live asset services, graphics/RHI handles, renderer residency handles, or runtime sidecars to canonical ECS components.
- Moving geometry IO or asset import/export ownership into ECS.
- Mixing this semantic promotion with legacy file deletion.
- Introducing physics solver or rigid-body behavior.
