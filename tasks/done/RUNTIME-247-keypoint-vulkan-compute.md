---
id: RUNTIME-247
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive implementation; source, CPU-reference comparisons, Vulkan readbacks and declared benchmark runs carry evidence.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, method.engine-integration, runtime.spatial-query-locality, repo.source-documentation]
---
# RUNTIME-247 — Full Vulkan keypoint computation

## Goal
Add a selectable Vulkan compute implementation of the existing centroid-PCA
keypoint detector. Preserve the CPU reference and CPU methods using cached
CPU/GPU neighborhoods. The user explicitly requests this backend after the
BUG-194 active-job diagnosis.

## Plan and reuse
1. Keep the RUNTIME-225 formulation and CPU reference. GPU stages: nearest-other
   spacing, double reduction, per-point centroid covariance/eigenvalue scoring,
   then read-only-score nonmaximum suppression. Original source IDs break ties.
2. Reuse Graphics.PointLBVH and its shader ABI. Add a bounded method workspace
   in graphics, with no ECS/runtime dependency. Require optional shader FP64;
   unsupported devices refuse this backend without changing engine eligibility.
3. Extend SpatialIndexCache with a bounded GPU recording/readback callback over
   its retained index. The existing frame participant owns submission, safe
   readback and shutdown. No new runtime module, job registry or method switch
   in the cache. Existing publication/history owns final CPU properties.
4. Add vulkan_compute alongside the current hybrid vulkan_lbvh option. UI
   chooses CPU/Vulkan, with CPU acceleration selected separately. Batch size
   bounds dispatch size; capacity rejects excessive support, never truncates.
5. Compare scalar values and masks with the reference on all domains, explicit
   and automatic radii, deleted rows, ties, degeneracy and failures. Exercise
   cancellation/staleness, unsupported devices, visualization and undo/redo.
6. Extend the declared benchmark coverage and run dragon diagnostics. Review,
   fix findings and reconcile native, sanitizer and actual Vulkan checks.

The current shared float reduction cannot preserve the reference's double
spacing sum, and there is no GPU PCA implementation to reuse. Those kernels
belong to the method workspace. Resource lifetime and the RHI boundary justify
that owner; no factory or generic algorithm framework is needed.

## Formulation intake
Retain the engine's centroid covariance variant documented in
[keypoint analysis](../../docs/architecture/keypoint-analysis.md), including its
positive-spacing and isotropic-eigenvalue rules. RUNTIME-225 reviewed Zhong
(2009), DOI 10.1109/ICCVW.2009.5457637 and the Open3D/PCL variants. The Open3D
primary documentation was checked again; the DOI endpoint was unavailable in
this session. This is a backend port, not a new full-ISS or Gaussian-saliency
implementation. Numerical differences from GPU reduction/traversal order must
be measured against the CPU reference, including threshold/tie cases. The
numerical corrections discovered during those comparisons are recorded below
and apply to both backends; no tolerance was widened to hide a disagreement.

## Engine integration
| Surface | Decision |
| --- | --- |
| Least-structured input | Finite float3 position property plus existing live-slot mapping. |
| Compatible entity sources | All eight canonical domains; no conversion or topology replacement. |
| RuntimeModule | Existing SpatialIndexCache, JobService and point-analysis operations. |
| Config/agent | Existing validated keypoint section, explicit new backend token. |
| UI | Entity chooser; CPU/Vulkan backend; separate CPU acceleration and applicable GPU controls. |
| Publication | Atomic named float saliency/uint32 mask with revision/history guards. |
| End-to-end tests | Reference comparisons, actual GPU execution, all-domain publication and failure coverage. |
| Deferred | No integration surface intentionally deferred. |

## Acceptance criteria
- [x] All computation stages execute on Vulkan with final-result-only readback.
- [x] CPU reference comparisons and complete-support/numerical guards pass.
- [x] Config/UI, requested/actual identity, all-domain publication and history work.
- [x] Declared benchmark and dragon diagnostics record quality and timing honestly.
- [x] Review, native CPU, relevant sanitizer/GPU and structural checks pass.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests IntrinsicShaderOutputs
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.Keypoint' --timeout 120
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Review and numerical decisions
Claude reviewed a fixed source packet, followed by two smaller correction
reviews. The initial plan request was blocked by a temporary approval-service
usage failure; later source reviews went through the same approval process.
Review packets and verdicts are under `/tmp/intrinsic-keypoint-vulkan-20260914/`.

- Exact planar mask comparisons exposed analytic eigenvalue roundoff. Both
  keypoint reducers normalize the two smaller eigenvalues at
  `64 * double-epsilon * largest` before ratio tests and suppression. A CPU
  regression requires zero planar scores and lowest-ID ties with gamma32=0.
- Collinear and repeated-root cases also exposed invalid generic PCA frames.
  `Geometry.PCA` reuses the existing Eigen symmetric solver when adjacent roots
  coalesce or cross-product/orthogonalization cannot produce a reliable basis.
  It returns the solver's complete right-handed frame. Distinct, well-conditioned
  roots and the reference isotropic branch retain their existing path. Numerical
  solver failure is nonfinite/invalid and the keypoint operation rejects it.
- Existing batch readbacks now validate byte counts and assert the neighbor ABI.
  Full compute already validated its one final readback. Both paths keep their
  resource leases through completion and shutdown.
- Fresh LBVH builds already end with the required write/read barrier; the new
  workspace documents shader-readable inputs and no overlapping reuse. No
  additional synchronization framework was introduced.
- The covariance second pass culls at the salient radius. Full compute reports
  all dispatches, and traversal overflow has its own diagnostic bit.
- Preserve the explicit isotropic floor and valid zero-score candidate rules.
  CPU and GPU both compare float-published eigenvalues widened to double. Shared
  config validation already rejects invalid batch/capacity bounds; duplicate
  preflight and hidden CPU fallback were rejected.
- Retain the bounded 64-lane double spacing reduction. No measurement identifies
  it as the end-to-end bottleneck, so another reduction framework is unwarranted.
- The full CPU gate caught a compilation-locality regression. Remove the unused
  device callback argument and forward-declare the already globally attached
  command-context type. CPU spatial consumers do not import Device/CommandContext.
  The UI test now selects both hybrid acceleration and the full Vulkan backend.

## Review sweep
Scope is the full keypoint backend and correctness fixes required by its
comparisons/review. No layout moves, new services or compatibility paths.
The cache owns index lifetime and readback; graphics owns method buffers/shaders;
runtime owns property capture, validation, config and atomic publication/history.
The two new source files form one implementation/interface owner. Existing
property, job, backend and visualization mechanisms are reused.

Clean-workshop rows: 1 layer imports **pass**, 2 CMake edges **pass**, 3 exported
ownership **pass**, 4 renderer owner **pass**, 5 new frame-pass IDs **n/a**, 6
new recipe dependencies **n/a**, 7 maturity closure **pass** (bounded Operational),
8 temporary exceptions **pass** (none). The existing GPU frame participant is
reused; no pass ordering or renderer composition was added.

## Verification incidents
- The GPU harness initially asserted operational readiness before the engine's
  first validated frame; corrected to use the existing cold-start sequence.
- Compiler feedback caught test callback constness and an over-broad test edit;
  both were corrected without changing gates.
- BUG-188 again affected sandbox sanitizer discovery. Actual device runs and
  sanitizer discovery use host access; general Vulkan retains ASan+UBSan and
  the repository's existing leak policy (BUG-180).
- BUG-195 records the full host disk and truncated result files. About 5 GiB of
  inactive, rebuildable diagnostic objects/libraries/binaries were removed.
  Source, datasets and historical config/logs were retained. Incomplete output
  is superseded by rerun evidence, not counted as success.

## Completion
Completed 2026-09-14 at **Operational**, bounded by the fixtures and supported
input envelope in [C93](../../ara/logic/claims.md#c93-full-vulkan-keypoint-computation-executes-the-bounded-reference-fixtures).
Commit reference: the enclosing RUNTIME-247 retirement commit; exact changed
source hashes and command receipts are retained in the
[verification record](../../ara/evidence/diagnostics/keypoint_compute_2026-09-14/record.json).

[Verification table](../../ara/evidence/tables/keypoint_compute_verification_2026-09-14.md):
4,622 distinct native CPU passes with one expected unsanitized leak-control skip;
1,450 geometry and 37 affected runtime cases per isolated sanitizer; seven actual
Vulkan cases, no skips. The developer sandbox and shaders rebuild successfully.
Shared backend UI and spatial compilation-locality checks
pass. Structural checks and method/benchmark schemas pass. Full isolated engine
sanitizer suites were not rerun.

The declared smoke is local dirty-source evidence, not eligible for a repeatable
performance claim. The dragon probe compared 151,486 OBJ positions through the
runtime point domain and was removed after recording exact masks/scores. All
stages currently share one GPU frame; bounded dispatch size does not guarantee
bounded frame time or mid-dispatch cancellation. BUG-194 and BUG-193 retain the
separate desktop display/pacing investigations. BUG-195 owns verification disk
headroom. No integration surface is deferred within this backend task.
