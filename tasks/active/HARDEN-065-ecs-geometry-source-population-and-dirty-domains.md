# HARDEN-065 — Promote ECS geometry-source population and dirty-domain helpers

## Status

- Status: in-progress (slice 1 in flight; slice 2 deferred).
- Owner/agent: Claude on `claude/setup-agentic-workflow-IY9XW`.
- Branch: `claude/setup-agentic-workflow-IY9XW`.
- Started: 2026-05-17.
- Current slice: slice 1 — geometry dirty-domain stamping helpers.
- Next verification step: run the default CPU gate against
  `IntrinsicECSTests` + `IntrinsicEcsContractTests`; regenerate the
  module inventory; promote slice 2 (population helpers + ownership
  decision) on a follow-up branch once slice 1 lands.

## Slice plan

- **Slice 1 (this slice): geometry dirty-domain stamping helpers.**
  - Extend `src/ecs/Components/ECS.Component.DirtyTags.cppm` with
    five inline `Mark*Dirty(entt::registry&, entt::entity)` helpers —
    `MarkVertexPositionsDirty`, `MarkVertexAttributesDirty`,
    `MarkEdgeTopologyDirty`, `MarkFaceTopologyDirty`, and
    `MarkGpuDirty` — that `emplace_or_replace` the corresponding tag.
    Idempotent and CPU-only; no graphics/runtime/asset/RHI imports.
  - Document the clearing-side ownership: ECS does not clear these
    tags. Runtime extraction (or any equivalent downstream consumer)
    drains and removes them, matching the existing
    `DirtyTags::DirtyTransform` drain in
    `Runtime.RenderExtraction.cpp:450-454`.
  - Add focused `unit;ecs` tests in
    `tests/unit/ecs/Test.ECS.GeometryDirtyDomains.cpp` covering each
    helper, idempotency, and independence across the five dirty
    domains. Wire through `tests/CMakeLists.txt`.
  - Update `src/ecs/Components/README.md` and
    `docs/migration/nonlegacy-parity-matrix.md` with the new
    stamping-side contract and the slice-2 deferral of population +
    ownership decision.
  - Regenerate `docs/api/generated/module_inventory.md` (no module
    surface change expected — helpers live inside the existing
    `Extrinsic.ECS.Component.DirtyTags` module — but rerun the
    generator to confirm).

- **Slice 2 (deferred): population helpers + ownership decision.**
  - Decide and document whether promoted `GeometrySources` are
    borrowed views, owned copies, or a split of owning and borrowed
    components. The legacy `ECS:Components.GeometrySources` uses
    owned `Geometry::PropertySet` per-component; the current
    promoted form uses non-owning `ObserverPtr<Geometry::PropertySet>`.
  - Promote or replace `PopulateFromMesh`, `PopulateFromGraph`, and
    `PopulateFromCloud` for the chosen ownership model, preserving
    canonical property naming (`v:position`, `v:normal`, `e:v0`,
    `e:v1`, `h:to_vertex`, `h:next`, `h:face`, `f:halfedge`).
  - Add focused `unit;ecs` tests in
    `tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp` covering
    mesh, graph, and point-cloud population, copied/borrowed
    lifetime expectations, domain detection, and alive-count
    behavior after population.
  - Update `src/ecs/Components/README.md`, `src/ecs/README.md`, and
    the parity matrix again with the ownership decision.

## Goal
- Define and implement the promoted ECS contract for populating `GeometrySources` from mesh/graph/point-cloud data and for stamping geometry dirty-domain tags deterministically.

## Non-goals
- No graphics/RHI upload implementation.
- No asset file import/export ownership changes.
- No live `AssetService`, renderer, runtime sidecar, or GPU handle storage in ECS components.
- No broad rewrite of geometry containers.

## Context
- Owner/layer: `ecs`, with allowed dependencies on `core` and explicitly justified CPU-only `geometry` types.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- Promoted `Extrinsic.ECS.Components.GeometrySources` currently provides borrowed `ObserverPtr<Geometry::PropertySet>` source views and domain/count helpers.
- Legacy `ECS:Components.GeometrySourcesPopulate` provides `PopulateFromMesh`, `PopulateFromGraph`, and `PopulateFromCloud`, but that behavior is not promoted.
- `DirtyTags::{GpuDirty,DirtyVertexPositions,DirtyVertexAttributes,DirtyEdgeTopology,DirtyFaceTopology}` exist as POD tag types in `Extrinsic.ECS.Component.DirtyTags`, but no promoted mutation helpers consistently stamp them. The existing `Extrinsic.ECS.System.RenderSync` already establishes the producer/consumer pattern for `DirtyTags::DirtyTransform` (stamped by `RenderSync::OnUpdate`, drained by `Runtime.RenderExtraction`); slice 1 mirrors that pattern for geometry domains by offering producer-side helpers in ECS and leaving the drain side to runtime.

## Required changes
- [ ] Decide and document whether promoted `GeometrySources` are borrowed views, owned copies, or a split of owning and borrowed components. *(slice 2)*
- [ ] Promote or replace population helpers for at least mesh, graph, and point-cloud domains without importing runtime/assets/graphics. *(slice 2)*
- [ ] Preserve canonical property naming for positions, normals, edge connectivity, halfedge connectivity, and face halfedge references where applicable. *(slice 2)*
- [x] Add deterministic helpers for stamping position, attribute, edge-topology, face-topology, and full-GPU dirty domains. *(slice 1)*
- [x] Define who clears each dirty tag after downstream processing; runtime/graphics clearing must remain outside ECS if it touches sidecars or GPU state. *(slice 1: ECS does not clear; runtime extraction drains, mirroring the existing `DirtyTransform` pattern.)*
- [ ] Record unsupported geometry domains or ownership cases as explicit follow-up tasks. *(slice 2)*

## Tests
- [ ] Add `tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp` or equivalent promoted ECS tests for mesh, graph, and point-cloud population. *(slice 2)*
- [ ] Cover copied/borrowed lifetime expectations selected by this task. *(slice 2)*
- [ ] Cover domain detection and alive-count behavior after population. *(slice 2)*
- [x] Cover dirty-domain helper stamping for positions, attributes, topology, and full GPU-dirty cases. *(slice 1: `tests/unit/ecs/Test.ECS.GeometryDirtyDomains.cpp`.)*
- [x] Keep tests CPU-only and labeled `unit;ecs`. *(slice 1.)*

## Docs
- [x] Update `src/ecs/Components/README.md` with the selected geometry-source ownership and dirty-domain policy. *(slice 1 records the dirty-domain stamping policy; slice 2 will add the ownership decision.)*
- [ ] Update `src/ecs/README.md` if the public module surface changes. *(slice 1: no module surface change.)*
- [x] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) if this removes the legacy geometry-source population retirement blocker. *(slice 1 partially resolves the GPU-sync dirty-tag emission policy bullet; slice 2 will close the population-helpers bullet.)*
- [x] Regenerate [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved. *(slice 1: rerun to confirm no surface change.)*

## Acceptance criteria
- [ ] Promoted ECS can populate geometry-source components for mesh, graph, and point-cloud scenes without importing legacy ECS. *(slice 2)*
- [x] Dirty-domain stamping is deterministic and tested for geometry mutations. *(slice 1)*
- [ ] The selected ownership model is documented and does not leak runtime, asset-service, graphics, RHI, or GPU-residency state into ECS. *(slice 2)*

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding live asset services, graphics/RHI handles, renderer residency handles, or runtime sidecars to canonical ECS components.
- Moving geometry IO or asset import/export ownership into ECS.
- Mixing this semantic promotion with legacy file deletion.
- Introducing physics solver or rigid-body behavior.

## Next verification step
- Run the default CPU gate against `IntrinsicECSTests` + `IntrinsicEcsContractTests`; regenerate the module inventory; once slice 1 lands, open slice 2 for the population helpers and the borrowed-vs-owned ownership decision on a follow-up branch.
