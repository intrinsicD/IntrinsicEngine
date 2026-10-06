---
id: RUNTIME-320
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive integration of an existing CPU kernel; evidence is the diff, CPU contract and Sandbox integration tests, a Vulkan acceptance smoke, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# RUNTIME-320 — Convex hull of a selection, point cloud or mesh in Sandbox and agent

## Goal
- Bind the test-only `Geometry.ConvexHullBuilder` (`Build`, `BuildMesh=true`)
  into the editor as a third method of the existing point-construction family:
  the hull of the selected geometry becomes a new visible, selectable mesh.
- Origin: REVIEW-007 E7 (2026-10-06), GE04 — operator chose integration over
  deletion. Draft: Codex, read-only on `e47a1484b`; paths re-checked on `098d47df9`.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite 3D samples from a `vec3` property; at least four non-coplanar points. |
| Compatible entity sources | Any compatible `vec3` `GeometryPropertyRef` binding: mesh vertices/halfedges/edges/faces, graph nodes/halfedges/edges, point-cloud points (no vertex or name requirement); several selected entities are unioned in world space; a primitive selection on one entity (`Runtime.SelectionController` `ReadPrimitives`) restricts the samples. |
| RuntimeModule | Existing `Runtime.PointConstructionOperations`; no new `IRuntimeModule`. |
| Config/agent | `sandbox.point_construction` gains `method = convex_hull` and `distance_epsilon`; existing `preview_operation`/`run_operation` with `operation = point_construction`; result reports method, backend and output entity. |
| UI | Existing Construct-from-Points panel and Mesh/Graph/PointCloud menus; Hoppe/kNN controls shown only for their methods. |
| Publication | New mesh entity (own topology, canonical position/normal properties); sources unchanged; one undo step via `PublishEditorGeneratedEntity`. No H-Rep property. |
| End-to-end tests | Each of the eight compatible domains (incl. mesh and graph halfedges, as the existing PointConstruction tests cover), multi-entity union and primitive subset → config → preview/apply and agent → new mesh; panel test; Vulkan pixel + pick smoke. |

## Acceptance criteria
- [ ] `PointConstructionMethod::ConvexHull` with `DistanceEpsilon` round-trips through the
      codec (`Runtime.PointFeaturesConfigCodecs.cpp`, which today assumes two methods);
      non-finite or non-positive epsilon is rejected; only `CpuReference` is accepted for this method; `CpuLBVH` and `VulkanLBVH` are rejected with a reason, each covered by a test.
- [ ] Capture/compute in `Runtime.GeometryProcessingOperations.Construction.cpp` branches by
      method; the hull path needs no neighbourhood queue or normal estimation.
- [ ] Non-canonical property names, deleted slots, multi-entity world-space union and
      primitive-subset input work; preview changes nothing; UI and agent reach the same apply path.
- [ ] Empty, non-finite, collinear and coplanar input (builder `nullopt`) report a clear
      error and publish nothing.
- [ ] Undo/redo, dirty state, save/load, cancellation and stale sources are covered;
      no second or late publication.
- [ ] New smoke case `RuntimeSandboxAcceptanceGpuSmoke.ConvexHullCreatesVisibleSelectableMesh` (added to `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`) runs the
      real editor/agent command and checks visible pixels and pickability.
- [ ] `docs/architecture/point-construction.md`, `src/runtime/README.md`,
      `src/app/Sandbox/README.md` and the module inventory describe the new method.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(ConvexHull_[A-Za-z]+|PointConstructionConfig|PointConstructionOperations|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|SandboxEditorPresentation|RuntimeEngineLayering|RuntimeEnginePrivateGlue|ProcessingCompilationLocality)\.'
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.ConvexHullCreatesVisibleSelectableMesh$'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- Builder is test-only today (`tests/unit/geometry/Test_ConvexHull.cpp`); `Geometry.ConvexHull`
  stays a separate type. PK10 removed `LocalConvexHull` as its ECS consumer; do not restore it.
- Out of scope: GPU hull, culling/physics use, H-Rep property, performance claims.
- Shared files with RUNTIME-308 (Hoppe construction on the residency), RUNTIME-314 and UI-037;
  coordinate, do not absorb.
