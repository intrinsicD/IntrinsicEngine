---
id: RUNTIME-225
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T00:50:40Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-225 — ISS keypoint analysis with shared spatial backends

## Goal
Expose the existing ISS-style centroid-PCA keypoint detector on canonical position properties, with reference KD-tree, cached CPU LBVH and framed Vulkan radius neighborhoods, shared config/UI and atomic saliency/mask publication.

## Context
The operator authorized sequential remaining spatial consumers, then Framework24 ports, until 07:00 Europe/Berlin on 2026-09-10. RUNTIME-224 and BUG-183 provide the verified cache and dependency lifecycle. Framework24 scalar Gaussian saliency is a separate method and remains on the port list.

## Non-goals
No complete ISS descriptor, density-compensated reference frame, FPFH descriptor matching, topology changes or new spatial service.

## Formulation and references
Preserve the existing detector variant: centroid covariance over self plus every nonself point in the inclusive salient radius, descending eigenvalues, positive lambda1/lambda2 and inclusive Gamma21/Gamma32 ratio gates. Score is lambda3. Radius NMS keeps no lower score and breaks equal-score ties by lowest source ID. Auto radii use 6 and 4 times exact nearest-live spacing. Preserve the existing positive-spacing requirement even for manual radii; reject invalid/nonfinite parameters and unrepresentable results. Compact live finite Cloud rows before spacing/querying, fixing the legacy eight-raw-neighbor cutoff in deleted data.

Reviewed Zhong (2009), DOI 10.1109/ICCVW.2009.5457637, including the [author-uploaded original](https://www.researchgate.net/publication/224135303_Intrinsic_shape_signatures_A_shape_descriptor_for_3D_object_recognition): density-compensated query-centered scatter and voxel resolution differ from this engine variant. Later primary implementations include [Open3D centroid covariance](https://www.open3d.org/docs/latest/tutorial/geometry/iss_keypoint_detector.html) and [PCL query-centered scatter](https://pointclouds.org/documentation/iss__3d_8hpp_source.html). No complete-paper equivalence is claimed.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 position span and optional complete radius rows. |
| Compatible entity sources | All eight canonical property domains with their deletion masks. |
| RuntimeModule | Existing geometry operations, SpatialIndexCache and JobService. |
| Config/agent | Persisted sandbox.keypoint_analysis schema, typed input/output bindings, radii/gates/minimum support and backend/batch/capacity controls. |
| UI | ISS Keypoint Analysis window and provenance aliases use the same validated apply/run path; scalar saliency visualization uses the existing recipe path. |
| Publication | Named same-domain float saliency and uint32 keypoint mask; atomic revision-checked history, preserve unrelated/deleted rows and topology. |
| End-to-end tests | Reference analytic tests; all-domain CPU/cache/config/history/stale/cancel checks; actual Vulkan comparison and full-support overflow refusal. |

## Spatial acceleration review
Reuse an immutable selected-property index. Query complete support at max(salient radius, NMS radius), then filter the rows per stage. Sort source IDs before covariance/NMS. Vulkan radius overflow must reject the operation rather than truncate the covariance or suppression neighborhood. Auto-scale and all covariance/NMS work stay on CPU and are reported. Descriptor matching remains outside a 3D position metric.

## Right-sizing
Extract only the existing offsets/indices neighborhood view into Geometry.SpatialQueries and preserve the normals alias: normals and keypoints are present callers. Keep point-feature math in its existing module. Reuse canonical property resolution, cached queries, jobs, config, history and visualization; do not introduce a service or backend interface. Private property-watch/domain helpers replace duplicate code in three present runtime consumers.

## Slice plan
One slice: reference/span contract and tests, manifest, indexed runtime/config/UI integration, CPU and actual Vulkan verification, docs and fixed-surface independent review.

## Required changes
- [x] Add span and supplied-neighborhood keypoint analysis while preserving the Cloud API.
- [x] Use the common neighborhood view without changing normal-estimation semantics.
- [x] Wire all three backends and canonical property/config/UI/publication surfaces.

## Tests
- [x] Analytic saliency/NMS, ties, invalid neighborhoods and deleted-spacing regression pass.
- [x] All-domain CPU/cache, config, history and stale/cancel tests pass.
- [x] Actual Vulkan parity and complete-support overflow checks pass.

## Docs
- [x] Update architecture, consumer inventory, method/benchmark manifests, module inventory and remaining port reminders.

## Acceptance criteria
- [x] Compatible property domains share the same method availability and publication path.
- [x] No backend silently truncates complete radius support or falls back to CPU queries.
- [x] Full CPU gate, focused actual Vulkan, manifest smoke and independent fixed-surface review pass with explicit variant/precision/performance limits.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.Keypoint' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Forbidden changes
No layer exceptions, silent fallback/truncation, hidden topology replacement, invented speedup or complete-paper/Framework24-saliency claims.

## Verification constraints
CCACHE_DISABLE=1 under BUG-178. Configure outside the sandbox for the existing vcpkg egress/cache constraint (BUG-065); the initial combined configure approval timed out, and the standalone retry succeeded. Existing BUG-180 excludes GPU LSan while retaining ASan+UBSan. Count actual capability skips separately from passes.

## Completion

**Completed:** 2026-09-10
**Commit:** `aa59947a42e5db293d053e0381487300f6e66f32` (implementation; retirement/review seal follows).

[Bounded verification](../../ara/evidence/tables/keypoint_verification_2026-09-10.md) and ARA C86 bind the CPU and actual Vulkan outcome. Full CPU has 4434 distinct passes after native follow-up and one expected unsanitized leak-control skip. Both Vulkan cases pass without skips. Historical menu-array compilation and task-field validation errors have passing replacement gates. The reviewed submission-failure race was corrected and exercised before closure. No performance improvement is claimed.
