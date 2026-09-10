---
id: RUNTIME-227
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T02:54:01Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-227 — Compact density weights with shared spatial backends

## Goal
Expose the existing compact-support density-weight kernels through canonical point properties, CPU KD-tree, cached CPU LBVH and framed Vulkan radius support, shared config/UI and one undoable float output.

## Context
The operator authorized sequential remaining spatial consumers and Framework24 ports until 07:00 Europe/Berlin on 2026-09-10. This is the next consumer after RUNTIME-226. Read-only numerical review found a reproducible tiny-radius false exclusion in the existing kernel KD-tree broad phase; this slice corrects it while preserving the kernel formula.

## Formulation and references
Preserve Geometry.PointCloud.Kernels: direct weight 1+sum of nonself compact radial contributions, or its reciprocal. Gaussian uses sigma=h/4; ThetaLop uses exp(-16*r²/h²); WendlandC2 uses (1-r/h)^4*(1+4r/h). Strict double-distance r<h, double accumulation in source-ID order, float output, duplicate contributions retained, isolated weight 1.

Reviewed [Huang et al. 2009 WLOP](https://www.cs.sfu.ca/~haoz/pubs/siga09_consolidater.pdf), DOI 10.1145/1618452.1618522, and [Preiner et al. 2014 CLOP](https://www.cg.tuwien.ac.at/research/publications/2014/preiner2014clop/preiner2014clop-paper_small.pdf), DOI 10.1145/2601097.2601172. Preserve the existing discrete density correction; continuous mixture attraction and later incomplete-gamma kernel variants are separate formulations. Existing paper intakes in methods/geometry/locally_optimal_projection and continuous_lop supply lineage and extension boundaries.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 position span, positive double support radius, existing kernel/mode enums; optional complete candidate neighborhoods. |
| Compatible entity sources | All eight canonical geometry element domains with deletion masks; one live sample is valid. |
| RuntimeModule | Existing geometry operations, SpatialIndexCache, JobService and history. |
| Config/agent | Persisted sandbox.density_weights with canonical position/output refs, support radius, kernel, mode, backend and GPU batch/capacity. |
| UI | Compact Density Weights window, provenance aliases and existing scalar visualization use validated config/apply/run. |
| Publication | One named same-domain float property, revision checked, preserving deleted rows, unrelated properties and topology. |
| End-to-end tests | Exhaustive analytic/tiny-radius oracle, all-domain config/CPU/cache/history/stale checks and actual Vulkan support/failure tests. |

## Spatial acceleration review
The index belongs to the immutable selected position property and is reused from the spatial cache. Gather complete radius candidates without an estimator cap. Broaden float queries with sqrt(h²*(1+8*float epsilon)+8*float min_normal), computed in double and rounded outward; existing double kernel Weight determines strict support. CPU saturates the query radius before float conversion at floatmax, preserving extreme h acceptance. LBVH rejects an expanded radius above its coordinate limit. The Vulkan path additionally rejects nonzero subnormal coordinate components with explicit diagnostics; normal/zero coordinates keep the existing shader AABB clamp within its contract. Query overflow fails without publication. The numerical argument is bounded to existing float subtraction/square/add expressions, not whole-device denormal preservation.

A concrete regression uses v=bit_cast<float>(0x1a01460f), points 0 and (v,v,v), h=4.6766236639043417e-23. Exact distance is inside h, but the old float broad phase excludes it. The corrected CPU and actual GPU paths must match exhaustive double weights, including an internal-node cluster variant. Existing GEOM-062 extreme-radius checks remain required.

## Right-sizing
Reuse the current geometry Weight/reduction kernels, PointNeighborhoods, canonical property watches, cache, jobs, config and history. Introduce one private plain radius-row state plus free advancement function to consolidate existing keypoint and descriptor GPU pagination and serve density weights; these are present callers with identical source-ID remapping and complete/prefix accounting. Callers retain numerical membership and job/revision ownership. No new service, generic backend interface or runtime lifecycle module.

The consolidation consumers reuse the conservative helper. Anisotropic attraction now rejects exact double-distance shell candidates before rounded-offset weighting, and repulsion skips zero radial contributions before derivative evaluation. Three regression fixtures cover supplied-candidate agreement, extremely small support, and anisotropic boundary exclusion.

## Non-goals
No substitution of the projection methods' existing validated GPU grid driver, no moving-sample projection integration, no continuous-Gaussian attraction change, no general shared-LBVH subnormal repair, no silent fallback/truncation, no speedup claim.

## Slice plan
One slice: CPU reference correction and supplied-neighborhood reducer, correctness tests and manifest; shared radius pagination plus runtime/config/UI; actual GPU and combined verification, docs and independent fixed-surface review.

## Required changes
- [ ] Add conservative query radius and supplied complete-neighborhood density reduction using the existing kernel formula.
- [ ] Reuse radius pagination across keypoints, descriptors and density weights.
- [ ] Wire all three backends, canonical config/UI, publication/history and explicit numerical limits.

## Tests
- [ ] Analytic, duplicate/isolated, strict-boundary/shell, malformed neighborhoods, tiny internal-node regression and legacy extreme-radius tests pass.
- [ ] All-domain CPU/cache/config/history/stale/cancel tests pass.
- [ ] Actual Vulkan agrees with the reference and rejects overflow/subnormal coordinates/stale/cancelled/partially submitted work without modifying outputs.
- [ ] Existing keypoint and descriptor Vulkan cases remain passing after pagination reuse.

## Docs
- [ ] Synchronize architecture, spatial-consumer inventory, method/benchmark manifests, module inventory and remaining projection/Framework24 port reminders.

## Acceptance criteria
- [ ] All compatible property domains share the same method availability and publication path.
- [ ] No accepted backend loses strict-support contributors or silently truncates candidate neighborhoods.
- [ ] Full CPU, focused actual Vulkan, manifest smoke and independent fixed-surface review pass with explicit precision/performance limits.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.(DensityWeight|Keypoint|Descriptor)' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Forbidden changes
No layer exceptions, silent fallback/truncation, topology replacement or unsupported performance/whole-device claims.

## Verification constraints
CCACHE_DISABLE=1 under BUG-178. Existing vcpkg sandbox constraint BUG-065 requires native configure. Existing BUG-180 excludes GPU LeakSanitizer while retaining ASan+UBSan. Count capability skips separately from passes.
