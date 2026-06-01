# RUNTIME-093 — Primitive selection refinement for meshes, graphs, and point clouds

## Status
- State: in-progress.
- Owner/agent: claude.
- Branch: `claude/intrinsicengine-agent-onboarding-X3GCq` (Slice B2).
  Slice B1 landed on `claude/intrinsicengine-agent-onboarding-KpmSE` (PR #960).
  Slice A landed on `claude/intrinsicengine-agent-onboarding-bUPlk` (PR #959).
- Maturity target: `CPUContracted` (Slice A standalone refinement core +
  Slice B1 CPU ray fallback + Slice B2 runtime frame-loop integration).
- Next verification step: Slice B2 — build `IntrinsicRuntimeContractTests` and
  run the `contract;runtime` gate filtered to `PrimitiveSelectionRefinement`,
  then the full runtime contract gate, plus layering/test-layout/doc-links/
  module-inventory checks. Slices A and B1 already verified and merged.

## Slice B2 design decision
- The readback→refinement bridge is an exported pure function
  `RefinePickReadbackResult(scene, readback)` on the task-owned
  `Extrinsic.Runtime.PrimitiveSelectionRefinement` module: it resolves the
  readback's render id to a live `entt::entity` (decode + `registry.valid()`
  version check, so a recycled/destroyed slot yields a deterministic
  `StaleEntity` rather than refining the new occupant), reads the entity
  `Transform::WorldMatrix` as `LocalToWorld`, builds the authoritative
  `GeometrySources::ConstSourceView`, and delegates to `RefinePrimitiveSelection`.
  A background (no-hit) readback resolves to no sub-primitive (`std::nullopt`).
  It mutates nothing.
- `Engine` owns the editor-facing cache: a per-`RunFrame`
  `std::optional<PrimitiveSelectionResult> m_LastRefinedPrimitive`, updated from
  each pick readback as the existing readback-drain loop consumes it (newest
  readback wins, matching the controller's latest-pick-wins coalescing; a
  background readback clears it; an empty-drain frame retains the prior value).
  Exposed read-only via `Engine::GetLastRefinedPrimitiveSelection()`.
- This is the "editor-facing selection cache" arm of the original Slice B2
  scope ("route a refined result into the `SelectionController` / editor-facing
  selection cache"). It keeps `SelectionController` graphics-free (the controller
  must not import the refinement module, which imports `Graphics.SelectionSystem`)
  and preserves the controller's whole-entity authority and the ECS tag model
  unchanged — no new ECS components, no graphics mutation.
- The cache tracks the sub-primitive under the *last pick hit*, not the
  selection set, and is keyed by render id (`EntityId`) for editor correlation
  with the controller's selection ids; `StableId` (durable id) is left 0 here.
- Nonblocking clarification (does not block this slice): if a later editor/UI
  task wants a single combined selection object, the refined-primitive datum can
  be moved onto `SelectionController` (via a controller-local plain struct to
  keep it graphics-free) and tied to selectable/selected outcomes; recorded as a
  possible follow-up rather than implemented now to keep this the smallest robust
  closing slice.

## Slice plan
- **Slice A (this slice).** Standalone `Extrinsic.Runtime.PrimitiveSelectionRefinement`
  module: the `PrimitiveSelectionResult` result type, the
  `PrimitiveRefineStatus` fail-closed taxonomy, the `PrimitiveRefineRequest`
  input (encoded GPU hint + optional local hit anchor + entity transform +
  liveness flag), and the `RefinePrimitiveSelection(view, request)` entry point.
  Implements hint-based CPU refinement against authoritative `GeometrySources`
  for all three domains — mesh (face/edge anchor + nearest vertex/edge on the
  hinted face), graph (edge → edge + nearest endpoint, point → node), and
  point cloud (point → point) — applying the entity transform so both local and
  world hit positions are reported. Pure-CPU `contract;runtime` fixture tests in
  `Test.PrimitiveSelectionRefinement.cpp`. Imports the promoted ECS
  `GeometrySources` view and the graphics `EncodedSelectionId` producer type
  only; adds no geometry/ECS imports to graphics and mutates no selection state.
  Closes `Scaffolded`. Defers the `SelectionController` integration and the
  optional CPU ray fallback for missing hints to Slice B.
- **Slice B (split into B1 + B2).** Slice B as originally scoped bundled two
  independently reviewable concerns — the pure-CPU ray fallback (a self-contained
  addition to the standalone `RefinePrimitiveSelection` entry point) and the
  `SelectionController` integration (which carries an open design question about
  how sub-primitive selection is represented in a controller that today tracks
  only whole-entity `SelectedTag`/`HoveredTag`). Per the smallest-robust-slice
  discipline they are landed as two slices.
  - **Slice B1 (this slice).** Optional CPU ray fallback for a *missing* hint
    (`SelectionPrimitiveDomain::None`): add the entity-local pick-ray inputs
    (`HasPickRay`/`RayOrigin`/`RayDirection`/`FallbackRadius`) to
    `PrimitiveRefineRequest` and resolve the nearest mesh vertex / graph node
    (reported as `Vertex`) / point-cloud point whose perpendicular distance to
    the ray (half-line clamped at the origin) is within `FallbackRadius`,
    emitting `CpuFallbackResolved` / the deterministic `CpuFallbackMiss`. A valid
    hint always wins over the ray; a missing hint with no ray stays a fail-closed
    `UnsupportedDomain`. Pure CPU, stateless, side-effect free; no graphics/ECS
    imports added, no selection state mutated. `contract;runtime` fallback
    coverage in `Test.PrimitiveSelectionRefinement.cpp`. Does not by itself close
    `CPUContracted` (the `SelectionController` integration is still owed).
  - **Slice B2.** Integrate refined results with `RUNTIME-089`: route a refined
    `PrimitiveSelectionResult` into the `SelectionController` / editor-facing
    selection cache without graphics mutation, wired into `Engine::RunFrame`.
    Closes `Scaffolded → CPUContracted`.

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
- [x] Define `PrimitiveSelectionResult` with entity ID/stable ID, domain, face/edge/vertex/point IDs, world/local hit positions, and diagnostic status. _(Slice A.)_
- [x] Implement mesh refinement: face hints anchor face selection; optional ray/local hit refinement computes nearest vertex/edge on the hinted face. _(Slice A; a `Face` payload is the GPU `gl_PrimitiveID` triangle index, mapped to a face row through the shared `MeshGeometryPacker::BuildSurfaceTriangleFaceMap` inverse so n-gon fan-triangulation resolves correctly; the missing-hint CPU ray fallback is Slice B.)_
- [x] Implement graph refinement: edge hints return edge ID and nearest endpoint/node ID; point hints return node ID. _(Slice A.)_
- [x] Implement point-cloud refinement: point hints return point ID. _(Slice A; the missing-hint nearest-point-along-ray fallback is Slice B.)_
- [x] Apply entity transforms so refinement reports both local and world hit data. _(Slice A, via `LocalToWorld`.)_
- [x] Integrate with `RUNTIME-089` so refined primitive results can update selection caches or editor state without graphics mutation. _(Slice B2: `RefinePickReadbackResult` bridges each pick readback to a `PrimitiveSelectionResult`; `Engine::RunFrame` caches it in `m_LastRefinedPrimitive` (`GetLastRefinedPrimitiveSelection()`) as the readback-drain loop runs alongside the `SelectionController` whole-entity mutation. No graphics mutation; the controller stays graphics-free.)_
- [x] Add the optional CPU ray fallback for missing hints (nearest point/vertex along the local pick ray within a configurable radius). _(Slice B1: `HasPickRay`/`RayOrigin`/`RayDirection`/`FallbackRadius` inputs + `NearestPointAlongRay`/`RefineByRayFallback`, triggered on a `None`-domain hint for mesh vertices, graph nodes, and cloud points.)_
- [x] Add diagnostics for unsupported domain, stale entity, missing geometry source, invalid primitive payload, CPU fallback used, and CPU fallback miss. _(Slice A defines the full `PrimitiveRefineStatus` taxonomy + `DebugNameForPrimitiveRefineStatus`; Slice B1 emits `CpuFallbackResolved`/`CpuFallbackMiss`.)_

## Tests
- [x] Add `contract;runtime` coverage for mesh face hint -> face result and nearest vertex/edge refinement on a triangle fixture. _(Slice A.)_
- [x] Add `contract;runtime` coverage for graph edge hint -> edge result + nearest endpoint. _(Slice A.)_
- [x] Add `contract;runtime` coverage for point-cloud point hint -> point result. _(Slice A.)_
- [x] Add CPU fallback tests for missing hints where the fallback is implemented; otherwise assert deterministic unsupported diagnostics. _(Slice A asserts the deterministic `UnsupportedDomain` diagnostics; Slice B1 adds the fallback resolve/miss/no-ray/degenerate-ray/transform cases for mesh, graph, and point cloud.)_
- [x] Add transform coverage proving local/world hit positions are consistent. _(Slice A.)_
- [x] Add stale entity and invalid payload tests. _(Slice A.)_

## Docs
- [x] Update `src/runtime/README.md` with primitive refinement ownership, result shape, and fallback policy. _(Slice A.)_
- [x] Update `docs/architecture/rendering-three-pass.md` only if the selection/refinement contract changes. _(No change in Slices A/B1/B2: graphics stays a hint/readback producer; the refinement consumer and its editor-facing cache live wholly in runtime.)_
- [x] Refresh `docs/api/generated/module_inventory.md` if new modules are added. _(Slice A: regenerated for `Extrinsic.Runtime.PrimitiveSelectionRefinement`.)_

## Acceptance criteria
- [x] Runtime can resolve entity picks into mesh face/edge/vertex, graph edge/node, and cloud point selections using authoritative CPU data. _(End-to-end via `RefinePickReadbackResult` wired into `Engine::RunFrame`.)_
- [x] Graphics remains a producer of encoded ID/readback data only; it never imports geometry or mutates selection state. _(Bridge + cache live wholly in runtime; layering check clean.)_
- [x] Invalid/stale/missing geometry states are deterministic and diagnosed. _(StaleEntity via recycling-safe `registry.valid()` check, UnsupportedDomain/MissingGeometrySource/InvalidPrimitivePayload from `RefinePrimitiveSelection`; covered by wiring tests.)_

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

