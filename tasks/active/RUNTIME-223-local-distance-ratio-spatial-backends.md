---
id: RUNTIME-223
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-09T23:08:50Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-223 — Local distance ratio with shared spatial backends

## Goal
Expose the existing local-distance-ratio calculation through the outlier analysis workflow, using reference octree, cached CPU LBVH and framed Vulkan LBVH neighborhoods.

## Non-goals
- Full LOF, LoOP, and Framework24 covariance probability are different formulations; their tokens are not introduced by this slice.
- No new runtime service, component or scheduler; no implicit topology edits.

## Context
The operator authorized sequential LBVH consumers and Framework24 ports until 07:00 Europe/Berlin on 2026-09-10. Existing normal/outlier/density/spacing work is the known verified starting checkpoint. This slice extends the current outlier operation instead of copying its capture, publication, history and config wiring.

## Formulation
Preserve `EstimateOutlierProbability`: query min(n,max(k,2)+1) candidates ordered by squared distance and source ID, then remove self. The score is the mean retained distance divided by the mean of neighboring mean distances; denominator <=1e-12 produces zero. Coincident peers remain. Flag score strictly greater than the finite nonnegative threshold. Fewer than two samples, malformed neighborhoods, nonfinite input or unrepresentable float results fail closed. Avoid k+1 overflow.

Reviewed [LOF (Breunig et al., 2000)](https://sigmodrecord.org/2000/06/07/lof-identifying-density-based-local-outliers/) and [LoOP (Kriegel et al., 2009)](https://doi.org/10.1145/1645953.1646195). LOF uses reachability density; LoOP normalizes probabilistic distance. This existing heuristic implements neither complete formulation and is not a calibrated probability. Framework24's `bcg_point_cloud_vertex_outlier_probability.cpp` instead averages Mahalanobis Gaussian weights; that separate port follows this slice.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 span plus optional packed candidate rows. |
| Compatible entity sources | All eight canonical property domains, excluding deleted rows. |
| RuntimeModule | Existing geometry processing operation, SpatialIndexCache and JobService. |
| Config/agent | Existing sandbox.outlier_analysis section, local_distance_ratio method and score_threshold. |
| UI | Existing Outlier Analysis window and provenance aliases, same validated apply path. |
| Publication | Named same-domain score and mask, revision-checked undo/redo; explicit Remove Marked keeps its existing topology-free gate. |
| End-to-end tests | Analytic geometry, all-domain CPU/cache and real framed Vulkan comparisons, stale/cancel history coverage. |

## Spatial acceleration review
Immutable selected-property positions reuse SpatialIndexCache; GPU source slots remap to compact IDs. This method queries without self exclusion to preserve candidate-boundary ties, unlike statistical outlier detection. Vulkan retains at most 64 candidates (k<=63); no silent truncation or fallback. Existing count/coordinate/batch limits apply. Descriptor-space, graph-distance and moving-position work are outside this query contract.

## Right-sizing
Reuse the current outlier config, UI, operation result, mask/score history and jobs. Reuse the geometry neighbor validator with supplied rows, with the CPU reference retaining its owned candidate cache for the two-pass reduction. A third selectable method is a present variant axis; no new abstraction family is needed.

## Slice plan
One slice: reference span and supplied rows, analytic tests, benchmark manifest, CPU/GPU runtime integration, docs and fixed-surface review.

## Required changes
- [x] Add span and supplied-neighbor overloads preserving the cloud wrapper.
- [x] Add local_distance_ratio and validated score_threshold to existing control surfaces.
- [x] Reuse cached/framed queries with correct no-exclusion candidate semantics.

## Tests
- [x] Analytic, tie, overflow, invalid-input and wrapper tests pass.
- [x] All-domain publication, cache, history and current-source protection pass.
- [x] Actual Vulkan comparison and manifest-backed diagnostic smoke pass.

## Docs
- [x] Update outlier architecture, spatial consumers, method records and module inventory.

## Acceptance criteria
- [x] All three backends produce scores and masks through the existing outlier workflow.
- [x] Existing statistical/radius methods remain unchanged and full CPU gate passes.
- [ ] Fixed-surface independent review and completion evidence are recorded.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.LocalDistanceRatioPublishesAcrossDomains$' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Forbidden changes
- No dependency or policy exceptions, unrelated refactors, or new backend tokens without executing their reference comparisons.
- No claims of Framework24 covariance equivalence, full LOF/LoOP or speedup.

## Verification constraints
BUG-178 requires CCACHE_DISABLE=1. BUG-180 owns the existing GPU LeakSanitizer exclusion; ASan and UBSan stay enabled. Record actual capability skips separately from passes.
