---
id: HARDEN-083
theme: F
depends_on: [HARDEN-065, GEOM-012]
maturity_target: CPUContracted
---
# HARDEN-083 — Geometry source availability and provenance contract

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: `GeometrySources` now exposes a CPU source-availability contract
  that separates exact active domain, source provenance, and available
  vertex/node, edge, halfedge, and face data. Topology markers can explain
  provenance but do not advertise missing source property sets.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests
  IntrinsicECSTests` succeeded, and focused CPU/null coverage for
  `GeometrySources`, geometry availability, packers, extraction, progressive
  data, and `SandboxEditorUi` passed 205/205 tests.

## Goal
- Define the canonical ECS-layer way to ask which CPU geometry source data an entity actually provides, while keeping the underlying geometric provenance (`Mesh`, `Graph`, `PointCloud`, `Unknown`) separate from source capabilities.

## Non-goals
- No runtime render-lane routing in this task.
- No GPU residency, `GpuSceneSlot`, RHI, or graphics handle exposure from ECS.
- No geometry algorithm implementation or container-domain-view changes; `GEOM-012` already owns mesh/graph/cloud container views.
- No broad replacement of `GeometrySources::ConstSourceView` or scene serialization formats.

## Context
- Owning subsystem/layer: `ecs` (`ecs -> core`, with existing explicit geometry `PropertySet` types only).
- Existing CPU source truth lives in `src/ecs/Components/ECS.Component.GeometrySources.cppm`: `Vertices`, `Edges`, `Halfedges`, `Faces`, `Nodes`, topology markers, `ConstSourceView`, `MutableSourceView`, and `DetectDomain`.
- Current `DetectDomain(...)` is an exact mutually exclusive classification. That is useful for provenance and validation, but it is the wrong primitive for capability questions such as "can this mesh be treated as a point set?" or "does this entity have halfedge properties?".
- `GEOM-012` retired geometry-container domain views; this task is the ECS entity-source counterpart for promoted `GeometrySources` data.
- Runtime/UI/graphics follow-ups must consume this contract instead of duplicating source-presence checks in editor UI, progressive render-data helpers, or extraction code.

## Required changes
- [x] Audit all exported `GeometrySources` source/view/domain helpers and classify each current use as provenance, exact-domain validation, or source capability.
- [x] Add a neutral source-availability contract next to `ConstSourceView` / `MutableSourceView` that reports available CPU source data independently of `ActiveDomain`.
- [x] Define provenance rules separately from source capabilities: mesh provenance comes from mesh topology/faces, graph provenance from graph topology/nodes, and point-cloud provenance from vertex-only source data; partial or mixed entities remain diagnosable.
- [x] Report point-source availability from `Vertices` and `Nodes` explicitly so mesh vertices, graph nodes, and point-cloud points can all satisfy point-set consumers without collapsing their provenance.
- [x] Report edge, halfedge, and face source availability only when the corresponding promoted source data is present; topology markers may explain provenance but must not pretend a missing `Halfedges` or `Faces` property source exists.
- [x] Preserve the current exact-domain behavior for existing callers unless the task explicitly migrates a caller and updates its tests.
- [x] Add stable debug names/string helpers for the new availability/provenance values if needed by diagnostics.

## Tests
- [x] Add `unit;ecs` coverage for mesh entities proving provenance is mesh while point, edge, halfedge, and face source capabilities are independently reported.
- [x] Add `unit;ecs` coverage for graph entities populated by `PopulateFromGraph`, proving nodes and edges are available, graph provenance is retained, and halfedge-property capability is absent unless a `Halfedges` component exists.
- [x] Add `unit;ecs` coverage for point-cloud entities proving vertex point-source availability and point-cloud provenance.
- [x] Add `unit;ecs` coverage for partial/mixed entities, including a face-bearing or mesh-marked entity without halfedge properties, proving consumers can early-out by missing capability while provenance remains inspectable.
- [x] Preserve existing `GeometrySources` population/domain tests.

## Docs
- [x] Update `src/ecs/README.md` and `src/ecs/Components/README.md` to document the difference between exact active domain, provenance, and CPU source availability.
- [x] Update `tasks/backlog/ecs/README.md` if this task changes ECS backlog state or follow-up ownership.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening this task.
- [x] Regenerate `docs/api/generated/module_inventory.md` if any ECS module surface changes.

## Acceptance criteria
- [x] ECS has one documented CPU source-availability contract for promoted `GeometrySources` entities.
- [x] `ActiveDomain` / exact-domain classification is no longer the only way to answer whether an entity has vertices, nodes, edges, halfedges, or faces.
- [x] Mesh-as-point-set and mesh-as-edge-set availability is representable without changing the entity's provenance to point cloud or graph.
- [x] Graph-as-point-set availability is representable without inventing a `Vertices` source for graph nodes.
- [x] Missing halfedge/face sources are visible as missing capabilities, not hidden behind topology markers.
- [x] No `src/ecs` code imports runtime, graphics, platform, app, RHI, or GPU ownership types.

## Verification
```bash
cmake --build --preset ci --target IntrinsicECSTests
ctest --test-dir build/ci --output-on-failure -R 'GeometrySources|ECS' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding runtime, graphics, RHI, platform, app, asset-service, or GPU handle dependencies to `src/ecs`.
- Making topology markers stand in for missing CPU property sources.
- Replacing `GEOM-012` geometry container views with ECS-specific algorithm adapters.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
- This task closes the ECS CPU availability contract. Runtime render-lane availability is owned by `RUNTIME-117`; GPU availability is owned by `RUNTIME-119`.
