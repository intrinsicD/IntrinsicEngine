---
id: UI-039
theme: I
depends_on: [RUNTIME-206, UI-035]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "feature/ui-039-lop-multi-domain-discovery"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-04T18:59:59Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# UI-039 — Semantic point-set slot binding and LOP discovery

## Status

- Completed on 2026-08-04 at `Operational`. Stable Mesh, Graph, and PointCloud
  Processing entries open the same consolidation panel path and shared state.
  The panel binds canonical typed Position, optional Normal, and output slots
  from the selected mesh, graph, or point-cloud property catalog, while runtime
  remains the source of publication readiness and exact disabled diagnostics.
- Commit: pending final evidence binding.
- The implementation, review-request repair, fresh CPU verification,
  retirement, and accepted fixed-surface review seal are recorded by repository
  history and `tasks/evidence/UI-039/`.

## Goal

- Establish the typed method-slot binding pattern for point-set UI and make the
  LOP/WLOP/CLOP/EAR panel operational for compatible properties on every
  resolved Mesh, Graph, and PointCloud element domain through `RUNTIME-206`.
- Record and enforce the storage rule that public/persisted geometry vector
  properties use float `glm::vec*`; higher precision is local to internal
  computations rather than exposed as `glm::dvec*` properties.

## Non-goals

- No consolidation kernel, runtime service, dynamic universal method schema,
  converter entity, or topology-editing implementation.
- No duplicated per-domain config or panel logic.
- No automatic property-name alias such as copying `f:centroid` into
  `v:position`; method slots bind the original property directly.

## Context

- `UI-035` supplies the initial PointCloud-only panel. The LOP-family papers
  operate on point sets; `RUNTIME-206` separates that input contract from
  topology-safe result publication and already accepts full
  `GeometryPropertyRef` records.
- Reuse the literature audit recorded by `RUNTIME-206` when naming strategies,
  normal requirements, and count-changing restrictions.
- The three provenance menus are stable discovery locations backed by one
  point-set panel/state implementation. They do not constrain eligibility: a
  selected mesh, graph, or point cloud remains eligible whenever compatible
  property slots resolve.
- `Geometry::MeshUtils::FaceCentroid` may compute in double precision, but its
  published `f:centroid` property must be `glm::vec3` so public storage follows
  the repository numeric policy and participates in typed slot discovery.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Named Position/optional Normal slots bound to selected finite `vec3` properties; property name and container provenance are not eligibility filters. |
| Compatible entity sources | Mesh vertex/edge/halfedge/face, graph node/edge/halfedge, and point-cloud point properties. |
| RuntimeModule | Consume `RUNTIME-206` availability/config/submit/result records. |
| Config/agent | Panel edits the shared validated consolidation config and submits the same typed property-slot refs available to non-UI callers. |
| UI | Register stable Mesh, Graph, and PointCloud Processing entries backed by one panel/state implementation, and populate Position, optional Normal, and output slots from the selected entity's canonical property catalog. |
| Publication | Explain same-cardinality in-place publication and disable count-changing mesh/graph requests from runtime readiness. |
| End-to-end tests | Semantic menu registration, slot readiness, config/run routing, diagnostics, and same-domain publication for all physical property domains, including published mesh face centroids. |

## Required changes

- [x] Add the public-float-vector and typed semantic-slot rules to the canonical
      method workflow and geometry API policy; regenerate their skill mirrors.
- [x] Change existing published mesh vector quantities (`f:area_vector`,
      `f:centroid`, and `f:scalar_gradient`) from `glm::dvec3` to `glm::vec3`
      while retaining double precision inside their computations.
- [x] Register the shared consolidation panel under Mesh, Graph, and PointCloud
      Processing, remove selected-provenance gating, and expose Position,
      optional Normal, and output selectors from the selected entity's runtime
      property catalog.
- [x] Use runtime-provided source capability and publication readiness; for
      mesh/graph selections disable invalid target-count requests with the
      exact shared diagnostic rather than hiding the method.
- [x] Preserve one validated config/apply/submit path and render strategy,
      convergence, normal, backend, history, and stale-result diagnostics.
- [x] Keep visualization and selection on the originating entity/domain.

## Tests

- [x] Assert all three stable menu entries register and render real ImGui
      windows while sharing one panel/state and accepting PointCloud, Graph,
      and Mesh selections without an exact-provenance diagnostic.
- [x] Exercise successful same-cardinality runs from the three provenance
      families and each physical property-domain family, including the
      canonical `f:centroid`, plus disabled count-changing mesh/graph requests.
- [x] Prove published mesh vector quantities are `glm::vec3` properties while
      their direct calculations remain double precision.
- [x] Verify the app performs no direct ECS mutation or conversion and displays
      runtime failure/staleness diagnostics.

## Docs

- [x] Update Sandbox/runtime method UI documentation with semantic-menu slot
      binding and the source/publication matrix.
- [x] Synchronize the canonical method workflow into repository skill mirrors.

## Acceptance criteria

- [x] Loading a mesh, graph, or point cloud exposes a matching stable
      LOP-family menu entry; every entry opens the shared panel and exposes all
      compatible property domains without conversion.
- [x] Every domain drives the same runtime config and typed operation.
- [x] UI cannot request topology-destructive publication.
- [x] No point-set method requires its Position slot to be named
      `v:position`, and public geometry vector properties use float storage.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloudConsolidation|MethodPanel|MeshQuantities' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/sync_skills.py --check
```

Recorded results: the focused semantic-slot, consolidation, and mesh-quantity
selector passed `50/50`; the repaired three-menu headless presentation selector
passed `3/3`; the complete CPU-supported selector selected `4,079`, passed
`4,078`, failed zero, and skipped its policy-defined GLFW/LSan case.
Strict task, documentation, skill-mirror, method-manifest, module-inventory,
layering, test-layout, root-hygiene, clean-workshop, ARA, and workflow-evidence
checks close the task. Sanitizer and GPU/Vulkan execution are not claimed:
UI-039 changes no ownership/allocator/backend lifetime, and `METHOD-020` owns
the LOP-family Vulkan backend and parity work.

## Forbidden changes

- No PointCloud-provenance gate, `VertexProperty` filter, property-name alias,
  converter, app-owned readiness/config, direct property mutation, dynamic
  universal method schema, or separate panel implementation per domain.

## Maturity

- Reached: `Operational` for CPU/Null semantic property discovery, validated
  runtime routing, same-domain publication readiness, and float public vector
  property storage.
- Workflow status: technical acceptance, driver self-review, durable handoff,
  and independent high-risk fixed-surface review are recorded under
  `tasks/evidence/UI-039/`.
