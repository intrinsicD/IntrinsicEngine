---
id: RUNTIME-220
theme: J
depends_on: [GEOM-077]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive implementation; diff, tests and bounded CPU/Vulkan evidence.
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-220 — Kernel density with shared spatial backends

## Goal
- Continue the user's accepted LBVH consumer sequence with local Gaussian kernel density, following normal estimation and outlier analysis.

## Formulation and scope
- Preserve the existing local Gaussian average, k+1-then-self-filter candidate policy (k floored to two), and global nearest-other spacing bandwidth heuristic. No full-sample KDE, new estimator, Gaussian radius cutoff, topology edit or backend-default change.
- [Silverman 1986, §§2.4–2.5](https://ned.ipac.caltech.edu/level5/March02/Silverman/Silver_contents.html) distinguishes kernel and nearest-neighbor estimators. The inherited engine hybrid uses a normalized 3D Gaussian averaged over selected neighbors and a univariate rule applied to nearest-other distances, with mean-spacing and absolute floors. Its name does not imply an optimal multivariate bandwidth or a normalized probability distribution over space.
- Reviewed [SciPy's multivariate bandwidth formulation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.gaussian_kde.html) and [Gramacki & Gramacki's FFT bandwidth selection](https://arxiv.org/abs/1511.07482). Covariance bandwidth matrices, plug-in/cross-validation selection and FFT approximations are separate estimators, excluded here.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 span, optionally supplied nearest candidate IDs. |
| Compatible entity sources | All eight canonical element domains, with deleted rows excluded. |
| RuntimeModule | Existing geometry-processing owner, SpatialIndexCache and JobService; no additional service. |
| Config/agent | Validated sandbox.kernel_density section: canonical input/output, backend, k, bandwidth and batch size. |
| UI | Kernel Density window and mesh/graph/point-cloud menu aliases use the shared validated config path. |
| Publication | One named same-domain float density; preserve deleted rows, unrelated properties and topology. Revision-checked undo/redo and async publication. |
| End-to-end tests | Analytical geometry oracle, CPU/cache comparisons, eight-domain publication/config/visualization, actual Vulkan readback, reuse/stale/cancel/limit tests. |

## Spatial acceleration review
- Property-space 3D Euclidean kNN over stable source samples. Query min(n,max(k,2)+1) candidates with no exclusion; remove self during evaluation. Coincident distinct samples remain eligible. Nearest-other distance is the minimum retained nonself distance, so the same query supplies bandwidth and density support.
- Cached immutable snapshots own source mapping. CPU octree remains default/reference; CPU LBVH and Vulkan query the shared cache. Original-ID ties and compact mapping are preserved. Source/deletion changes invalidate output; output edits block stale publication.
- Vulkan candidate capacity 64 (k<=63), live count <=2^20, positions <=1e18. No unsupported backend silently falls back. GPU queries/readback feed CPU bandwidth and Gaussian evaluation.
- Right sizing: one config record and one existing module implementation unit. Plain supplied candidate spans cross the geometry boundary. Existing job dependency is necessary for framed GPU-to-CPU execution; no new universal neighborhood interface or runtime module lifecycle.

## Acceptance criteria
- [x] CPU span/supplied-neighbor reference and analytical tests preserve the inherited estimator.
- [x] CPU LBVH and Vulkan cache consumers publish through common config/jobs/history on all domains.
- [x] UI discovery and scalar visualization expose the same operation.
- [x] Full CPU, actual Vulkan, benchmark diagnostics and structural checks pass; limitations and remaining consumers documented.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointLBVHGpuSmoke.KernelDensity' -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Review and completion
- Implemented and locally verified. Keep this note active pending a commit/publication reference; no commit or push in this slice.
- Clang 23 `ci` built IntrinsicTests and ExtrinsicSandbox. All 38 focused tests pass. Full CPU selector: 4399 selected, 4393 passes and six sandbox capability skips; the five native-window cases pass outside the sandbox, leaving 4398 distinct passes and one expected unsanitized leak-control skip.
- Actual `ci-vulkan` ASan+UBSan test on RTX 3050 driver 590.48.01 passed in 44.76 s with zero density delta (1e-5 absolute bound), eight domains, cold/warm reuse, manual bandwidth at k=63, dense duplicates and stale/cancel/history checks. Existing GPU leak exclusion remains owned by BUG-180.
- The dirty schema-v2 benchmark validates: warm framed total 9876.23 ms versus CPU reference 118.69 ms on this tiny fixture. CPU remains the default; no speedup claim.
- Scope/layering/tests/docs sweep passed. Existing runtime cache/jobs own state and GPU lifetimes; geometry sees value spans only. The config module exposes the present backend/control axis; no new service, scheduler, renderer pass or dependency exception.
- Strict task policy, layer imports/links, test layout, doc links, method/benchmark manifests and skill mirrors pass; inventory regenerated. The clean-workshop scorecard and limitations are in the [verification record](../../ara/evidence/tables/density_vulkan_verification_2026-09-09.md). C82 binds the bounded result.
