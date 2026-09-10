---
id: RUNTIME-230
theme: J
depends_on: [RUNTIME-229]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation; diff, CPU/GPU tests and source-bound research evidence
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-230 — Spatial queries for point-set construction

## Goal
Wire Hoppe-style reconstruction and kNN graph construction through the shared CPU/Vulkan point LBVH and existing editor/config/job infrastructure.

## Context and formulation
The operator requests continued LBVH adoption. WLOP/CLOP/EAR already landed in 9f4764b7d; do not duplicate them. The remaining directly compatible point-query consumers include Hoppe grid evaluation and kNN graph construction. Hoppe et al. 1992 (https://hhoppe.com/recon.pdf) uses nearest tangent planes, oriented through a proximity graph. Preserve the engine's point-anchored planes and weighted k+1 variant; do not claim the original centroid-plane/boundary algorithm. Poisson reconstruction (https://hhoppe.com/proj/poissonrecon/) is a distinct global formulation and stays separate.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 samples; Hoppe additionally consumes paired normals or existing CPU normal estimation. |
| Compatible entity sources | All eight canonical element domains, with live-slot mapping and named properties. |
| RuntimeModule | Existing geometry-processing commands/jobs and SpatialIndexCache. |
| Config/agent and UI | One validated construction config and editor window for both methods. |
| Publication | Explicit new mesh or graph entity; source topology/properties remain unchanged. Undo/redo owns the generated entity. |
| End-to-end tests | Canonical domains, actual GPU neighborhoods, numerical/topological oracle comparisons, stale/cancel/failure and generated-entity history/visibility contracts. |

## Spatial acceleration and right-sizing
Stable source positions lease the entity cache; CPU normal initialization must retain every live point; a filtered sample set rejects instead of silently changing the cached identity. GPU grid query chunks have no self exclusion; graph construction retains its k+1-then-self/epsilon filtering and union/mutual semantics. Normal initialization and Marching Cubes stay CPU. The CPU point-query oracle uses independent exhaustive distance/source-ID ordering, with legacy octree comparisons on non-tied fixtures. Reuse borrowed neighborhood rows, existing field/graph reduction and editor job lifetime rules. A private generated-geometry publication helper serves both real callers; no second spatial service or virtual backend hierarchy.

## Acceptance criteria
- [x] Shared field/graph supplied-neighborhood steps match independent CPU fixtures, including malformed/duplicate/tied rows and degenerate inputs.
- [x] CPU reference, CPU LBVH and actual Vulkan construction share canonical config/UI and source-domain eligibility.
- [x] Generated geometry is visible/selectable; source data is preserved, history is guarded, stale/cancel/unavailable work publishes nothing.
- [x] Bounded benchmark manifests/results, method limitations, consumer inventory and remaining-task reminders are synchronized.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox -j 6
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests -j 6
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointConstruction' -L gpu -L vulkan --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Limits
No default speedup claim. Progressive Poisson active-set/grid semantics, quality-metric global reductions, primitive/ray hierarchies and graph traversal remain separately contracted work. GPU subnormal-coordinate restrictions and BUG-180 LeakSanitizer exclusion remain explicit until separately resolved.

## Review
Scope: two point-query consumers, their shared config/publication path and focused discovery/UI wiring. Geometry consumes borrowed CSR neighborhoods; runtime owns cache leases, job dependencies and generated entities. No new service, dependency edge, renderer state or GPU pipeline is introduced. Source-domain deletion and transform watches protect publication; generated geometry/property revisions, name, transform and hierarchy protect undo. Query membership is preserved before reduction, including the fixed extra candidate and no replenishment.

| Clean-workshop row | Disposition |
| --- | --- |
| 1. Layer imports | Pass: strict layering check, no allowlist entries. |
| 2. Target links | Pass: existing targets gain sources; no link edges added. |
| 3. Exported types | Pass: geometry exports spans/properties/grid records; GPU/cache and ECS state stay in runtime. |
| 4. Renderer ownership | N/A: reuse existing authoring recipes and readback test seam. |
| 5. Typed passes | N/A: no frame-graph pass added. |
| 6. Resource dependencies | N/A: reuse the existing framed spatial-query queues. |
| 7. Maturity closure | Pass: C91 binds actual Vulkan construction/pixels and CPU contracts; no deferred runtime/config/UI/publication row. |
| 8. Exceptions | Pass: no layer exceptions introduced. Existing host/build limits remain tracked by BUG-065, BUG-177, BUG-178 and BUG-180. |

The source-documentation audit reports no errors; its 13 declaration-comment review items describe existing topology/metric and supplied-neighborhood contracts and were retained. Strict local root hygiene still rejects the pre-existing `.agents/` directory under BUG-177; it was not changed. Intermediate compilation failures from missing imports and edits after module dependency scanning, and the mesh-payload `v:point` naming defect exposed by focused tests, were corrected before final verification.

## Verification outcome
[C91 and source-bound evidence](../../ara/evidence/tables/point_construction_verification_2026-09-10.md) record 4500 CPU passes plus one expected unsanitized leak-control skip, 28 focused geometry cases, seven focused runtime cases and both actual Vulkan cases. The final Vulkan fixture matches the reference positions exactly on its bounded inputs, with equal edge arrays/face counts and visible generated geometry. Its partial-submission, stale and cancellation checks pass. The dirty schema-v2 smoke passes its declared diagnostic thresholds; no performance win is claimed.

Final verification also corrected short final GPU batch reuse, the fixture's handling of reaped terminal tokens and its explicit 180-second budget. The new menu-count assertion was updated; BUG-186 separately tracks four pre-existing appearance-test expectations from `2c9053f89`. Full initial/final logs are retained with the evidence.

## Completion
Completed 2026-09-10. Implementation commit: `27275206dd6d3d1dd6857db737f3d609ab4cb1cb`.
