---
id: RUNTIME-321
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive integration of an existing CPU kernel; evidence is the diff, CPU contract and Sandbox integration tests, a Vulkan acceptance smoke, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality, runtime.processing-compilation-locality]
---
# RUNTIME-321 — Implicit plane field remeshing and Octree node properties in Sandbox and agent

## Goal
- Bind the test-only `Geometry.ImplicitPlaneField` (`BuildPlaneField` → `ExtractMesh`) into the
  editor: a selected surface mesh is converted to an adaptive signed plane field and re-extracted
  as a new mesh.
- Give the Octree node properties (`Octree::AddNodeProperty`/`GetNodeProperty`, used only by
  the plane field) a real consumer: an optional field-sample point cloud publishes the active
  leaves' closest point, normal, signed distance, max plane error, support radius, flags and
  source primitive as point properties.
- Origin: REVIEW-007 E7 (2026-10-06), GE05. Draft: Codex, read-only on `e47a1484b`;
  paths re-checked on `098d47df9`.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Surface mesh with face connectivity and vertex positions; positions alone do not satisfy the kernel. |
| Compatible entity sources | Any ECS mesh source with those topology/property contracts, independent of import origin or property names; graphs and point clouds are rejected with the shared readiness reason. |
| RuntimeModule | New `Runtime.ImplicitPlaneFieldOperations` on the existing editor processing commands/JobService; no new lifecycle service. |
| Config/agent | New section `sandbox.implicit_plane_field` (BuildParams, grid resolution/budget, `publish_field_samples`); `preview_operation`/`run_operation` with `operation = implicit_plane_field`. |
| UI | Mesh-processing panel: source, BuildParams, grid budget, run diagnostics (node/active/ambiguous counts, max depth). |
| Publication | New mesh entity with canonical position/normal properties and one undo step; optional second point-cloud entity carrying the node properties. Node properties are never written onto source vertices. |
| End-to-end tests | Mesh → config/panel/agent → field → extraction → visible, selectable mesh (and sample cloud); incompatible sources rejected with identical readiness. |

## Acceptance criteria
- [ ] Config validates finite values, `MinDepth <= MaxDepth` and a node/grid budget that bounds the
      adaptive build itself (`MaxGridVertices` alone does not cap tree nodes); round-trips.
- [ ] Preview is side-effect free; UI, config and agent share validation, parameters and apply.
- [ ] `MaxDepthReached`, missing field values, empty extraction, invalid topology and budget overflow
      are reported; no partial mesh.
- [ ] Field-sample cloud carries the seven node properties with correct node binding; its values
      match `PlaneField::ClosestPoint/Normal/SignedDistance/...` for the same leaves.
- [ ] Mesh/transform/property change, deletion, detach and cancellation prevent stale publication;
      the source mesh and its properties are unchanged.
- [ ] New outputs are visible, selectable, undoable and saveable;
      `RuntimeSandboxAcceptanceGpuSmoke.ImplicitPlaneFieldCreatesVisibleSelectableMesh` checks pixels.
- [ ] New test suites `ImplicitPlaneFieldOperations`, `ImplicitPlaneFieldConfig` are added to `IntrinsicRuntimeContractTests`, and the new smoke case `RuntimeSandboxAcceptanceGpuSmoke.ImplicitPlaneFieldCreatesVisibleSelectableMesh` to `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`; they do not exist yet, and `--no-tests=error` cannot detect their absence while other selectors match.
- [ ] Prepared-frame and compilation-locality checks include the new producer without relaxed
      allowlists; `docs/architecture/geometry.md`, `spatial-index-consumers.md`,
      `agent-control-lane.md`, runtime/Sandbox READMEs and the module inventory are updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(ImplicitPlaneField|Octree|ImplicitPlaneFieldOperations|ImplicitPlaneFieldConfig|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|SandboxEditorPresentation|SandboxEditorSessionLifecycle|RuntimeEngineLayering|RuntimeEnginePrivateGlue|ProcessingCompilationLocality|EditorCompilationLocality)\.'
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.ImplicitPlaneFieldCreatesVisibleSelectableMesh$'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- Reuse mesh capture (`Runtime.GeometryProcessingOperations.MeshSources.hpp`) and generated-entity
  publication (`Runtime.EditorGeneratedEntity.hpp`); point construction is a publication model only.
- The twelve unrelated test files importing `Geometry.ImplicitPlaneField` (REVIEW-007 GE05) stay
  untouched here.
- Out of scope: point-cloud reconstruction kernel, persistent ECS octree, SparseGrid/GPU port,
  support-radius semantics. METHOD-032 forbids generalizing `Geometry.Octree` for its corner
  lattice; METHOD-003/027/033 are not absorbed. Coordinate shared files with UI-037 and RUNTIME-314.
