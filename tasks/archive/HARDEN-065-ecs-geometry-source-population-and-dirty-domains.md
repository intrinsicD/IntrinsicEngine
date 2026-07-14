# HARDEN-065 — Promote ECS geometry-source population and dirty-domain helpers

## Status

- Status: done.
- Completed: 2026-05-17.
- Commit: merge commit `4bc19484` (PR #854); implementation commits
  `d0f43176` and `e90d1429`.
- Previous slice: slice 1 landed on `main` via PR #853 / commit
  `4642f95`.
- Owner/agent: Claude on `claude/setup-agentic-workflow-uAe8i` (slice 2);
  previously Claude on `claude/setup-agentic-workflow-IY9XW` (slice 1).
- Branch: `claude/setup-agentic-workflow-uAe8i`.
- Started: 2026-05-17 (slice 1), 2026-05-17 (slice 2 on follow-up branch).
- Completed maturity: `CPUContracted` for promoted geometry-source
  population and dirty-domain helper contracts.
- Completed slice: slice 2 — population helpers + borrowed-vs-owned
  ownership decision.
- Slice 2 decision: **owned `Geometry::PropertySet` per per-domain
  component** (legacy-parity shape). The promoted `Vertices`/`Edges`/
  `Halfedges`/`Faces`/`Nodes` now own a `Geometry::PropertySet` directly
  instead of the previous non-owning `ObserverPtr<Geometry::PropertySet>`
  skeleton (which had zero promoted consumers). Rationale: smaller
  blast radius (entity is the CPU authority, no external lifetime owner
  to coordinate), deterministic ownership, and direct portability of the
  legacy `PopulateFrom*` implementations.
- Slice 2 deliverables landed via PR #854:
  - `src/ecs/Components/ECS.Component.GeometrySources.cppm` rewritten
    around owned `PropertySet` with a new `PropertyNames` namespace
    holding canonical key constants (`v:position`, `v:normal`, `e:v0`,
    `e:v1`, `h:to_vertex`, `h:next`, `h:face`, `f:halfedge`).
  - New module `Extrinsic.ECS.Components.GeometrySourcesPopulate`
    (`ECS.Component.GeometrySourcesPopulate.{cppm,cpp}`) exporting
    `PopulateFromMesh`/`PopulateFromGraph`/`PopulateFromCloud`. Ported
    from the legacy implementation; `PopulateFromGraph` also stamps
    the existing `HasGraphTopology` marker so `BuildConstView` /
    `BuildMutableView` resolve `Domain::Graph` without requiring a
    Halfedges PropertySet (graph halfedges remain internal to
    `Geometry::Graph`). Each populate call drops the entity's prior
    `GeometrySources` components and topology markers before emplacing
    the new domain, so re-population across domains (mesh→cloud,
    graph→cloud, mesh→graph, …) cannot leak stale topology into
    `BuildConstView`/`BuildMutableView` — `entt::registry::remove<T>`
    is a silent no-op on first-population entities.
  - `tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp` (twelve
    `unit;ecs` cases covering emplaced-components shape, canonical key
    presence/positions/connectivity, alive-count tracking, user-defined
    property preservation, domain detection after population, mesh-/
    graph-/cloud-specific behavior, normal-skip path, and source-object
    destruction safety).
  - Docs sync: `src/ecs/README.md`, `src/ecs/Components/README.md`,
    `docs/migration/nonlegacy-parity-matrix.md`,
    `docs/api/generated/module_inventory.md`.
- Local verification recorded before retirement:
  - `python3 tools/agents/check_task_policy.py --root . --strict` →
    243 task file(s); findings=0.
  - `python3 tools/repo/check_layering.py --root src --strict` → 742
    files scanned; no layering violations.
  - `python3 tools/repo/check_test_layout.py --root . --strict` →
    findings=0.
  - `python3 tools/docs/check_doc_links.py --root .` → 500 links
    checked; no broken relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` → 434 modules;
    `Extrinsic.ECS.Components.GeometrySourcesPopulate` registered.
  - The pinned `clang-20` / `clang-scan-deps-20` CPU gate ran in the
    PR #854 `pr-fast` workflow before merge; PR #854 landed on `main`
    as merge commit `4bc19484`.

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

- **Slice 2 (in flight on `claude/setup-agentic-workflow-uAe8i`): population helpers + ownership decision.**
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
- [x] Decide and document whether promoted `GeometrySources` are borrowed views, owned copies, or a split of owning and borrowed components. *(slice 2: owned per-domain `Geometry::PropertySet`; recorded in this file's Status block, `src/ecs/Components/README.md`, `src/ecs/README.md`, and the parity matrix.)*
- [x] Promote or replace population helpers for at least mesh, graph, and point-cloud domains without importing runtime/assets/graphics. *(slice 2: `Extrinsic.ECS.Components.GeometrySourcesPopulate` imports only `Extrinsic.ECS.Components.GeometrySources` and `Geometry.HalfedgeMesh`/`Geometry.Graph`/`Geometry.PointCloud`/`Geometry.Properties`, matching the `ecs -> {core, geometry}` contract in /AGENTS.md §2.)*
- [x] Preserve canonical property naming for positions, normals, edge connectivity, halfedge connectivity, and face halfedge references where applicable. *(slice 2: keys exposed via the new `Extrinsic.ECS.Components.GeometrySources::PropertyNames` constants and written by every populate call.)*
- [x] Add deterministic helpers for stamping position, attribute, edge-topology, face-topology, and full-GPU dirty domains. *(slice 1)*
- [x] Define who clears each dirty tag after downstream processing; runtime/graphics clearing must remain outside ECS if it touches sidecars or GPU state. *(slice 1: ECS does not clear; runtime extraction drains, mirroring the existing `DirtyTransform` pattern.)*
- [x] Record unsupported geometry domains or ownership cases as explicit follow-up tasks. *(slice 2: graph halfedges are intentionally not promoted into a `Halfedges` PropertySet — `PopulateFromGraph` stamps the existing `HasGraphTopology` marker so `BuildConstView`/`BuildMutableView` resolve `Domain::Graph` without one. Mesh/Graph/PointCloud are the three promoted domains; no other domain is in scope for HARDEN-065.)*

## Tests
- [x] Add `tests/unit/ecs/Test.ECS.GeometrySourcesPopulate.cpp` or equivalent promoted ECS tests for mesh, graph, and point-cloud population. *(slice 2: twelve `unit;ecs` cases wired through `tests/CMakeLists.txt` into `IntrinsicECSTests`.)*
- [x] Cover copied/borrowed lifetime expectations selected by this task. *(slice 2: `ECSComponentSurvivesSourceMeshDestruction` proves the ECS-owned PropertySets keep canonical positions intact after the source mesh leaves scope.)*
- [x] Cover domain detection and alive-count behavior after population. *(slice 2: `PopulateFromMeshAliveCountsMatchSourceMesh`, `PopulateFromMeshYieldsMeshDomain`, `PopulateFromGraphYieldsGraphDomain`, `PopulateFromCloudYieldsPointCloudDomain`.)*
- [x] Cover dirty-domain helper stamping for positions, attributes, topology, and full GPU-dirty cases. *(slice 1: `tests/unit/ecs/Test.ECS.GeometryDirtyDomains.cpp`.)*
- [x] Keep tests CPU-only and labeled `unit;ecs`. *(slice 1 and slice 2.)*

## Docs
- [x] Update `src/ecs/Components/README.md` with the selected geometry-source ownership and dirty-domain policy. *(slice 1 + slice 2: slice 2 closes the ownership decision and documents the promoted population helpers and canonical key constants.)*
- [x] Update `src/ecs/README.md` if the public module surface changes. *(slice 2: adds `Extrinsic.ECS.Components.GeometrySourcesPopulate` to the public surface and the directory layout.)*
- [x] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) if this removes the legacy geometry-source population retirement blocker. *(slice 2 closes the population-helpers bullet; legacy `ECS:Components.GeometrySources`/`GeometrySourcesPopulate` retirement is left to a follow-up legacy-deletion task that is out of scope for HARDEN-065.)*
- [x] Regenerate [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved. *(slice 2: regenerated; 434 modules; new `Extrinsic.ECS.Components.GeometrySourcesPopulate` row at line 83.)*

## Acceptance criteria
- [x] Promoted ECS can populate geometry-source components for mesh, graph, and point-cloud scenes without importing legacy ECS. *(slice 2: `Extrinsic.ECS.Components.GeometrySourcesPopulate` imports only promoted `Extrinsic.ECS.Components.GeometrySources` and `Geometry.*` modules; the legacy `ECS:Components.GeometrySourcesPopulate` is not imported.)*
- [x] Dirty-domain stamping is deterministic and tested for geometry mutations. *(slice 1)*
- [x] The selected ownership model is documented and does not leak runtime, asset-service, graphics, RHI, or GPU-residency state into ECS. *(slice 2: owned per-domain `Geometry::PropertySet`; recorded across the three READMEs and the parity matrix. ECS layering contract test `Test.ECS.LayeringBoundaries` continues to enforce `ecs -> {core, geometry}`.)*

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
- No active next verification step remains for HARDEN-065; the task is
  retired. No further slices remain.
