---
id: RUNTIME-221
theme: J
depends_on: [GEOM-077]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive implementation; diff and bounded CPU/Vulkan tests.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-221 — Point spacing and radius estimation with shared spatial backends

## Goal
Continue the accepted LBVH consumer sequence with point statistics and splat-radius estimation.

## Formulation
Preserve the existing heuristic: query min(n,max(k,1)+1) candidates, remove self, and multiply mean retained distance by a nonnegative scale. Distinct coincident peers remain. Report nearest-other spacing separately from mean-k radius. Both use selected property-coordinate units. CPU octree remains the default.

Reviewed [Surface Splatting (Zwicker et al., 2001)](https://vcg.seas.harvard.edu/publications/20010101-surface-splatting) and [High-Quality Surface Splatting (Botsch et al., 2005)](https://graphics.rwth-aachen.de/media/papers/splatting1.pdf). These define filtered splat footprints and projection; the existing isotropic mean-distance heuristic is not their complete reconstruction/filtering method and cannot guarantee hole-free coverage. EWA filters, elliptical footprints and normal alignment remain separate rendering work.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 spans; optional supplied nearest candidate IDs. |
| Compatible entity sources | All eight canonical domains, excluding deleted rows. |
| RuntimeModule | Existing geometry-processing owner, SpatialIndexCache and JobService. |
| Config/agent | Validated sandbox.point_spacing section: canonical positions/radii, k, scale, backend and GPU batch size. |
| UI | Point Spacing and Radii window and mesh/graph/cloud aliases; shared validated apply path. |
| Publication | Named same-domain float radii and aggregate nearest spacing/bounds/centroid; preserve unrelated properties, topology and deleted output rows. Revision-checked undo/redo. |
| Rendering | Scalar-color visualization of the radius property. Model-space radius upload/projection remains RUNTIME-222; current renderer expects pixel sizes. |
| End-to-end tests | Independent analytical geometry cases, eight-domain CPU/cache/config/history tests, actual Vulkan readback and bounded benchmark. |

## Spatial acceleration review
Stable property-space Euclidean samples use the shared immutable cache. CPU IDs are compact; GPU IDs map back to original slots. Query without exclusion to preserve the original k+1 policy. Statistics span/supplied-row overloads preserve deterministic sampled-row stride; runtime computes full spacing statistics from the radius neighborhoods. Vulkan k<=63, count<=2^20, batch<=16384; CPU LBVH count<=2^24; LBVH coordinate bound 1e18. No silent fallback. Invalid/nonfinite or unrepresentable float calculations reject publication.

## Right-sizing
A plain config record and one operation implementation reuse existing jobs, history and cache. The asynchronous boundary is required by framed GPU readback and stale-source protection. No new service, scheduler, component or rendering pass. CPU reference streams one neighborhood at a time; supplied rows are scoped value input for real CPU/GPU callers.

## Acceptance criteria
- [x] Span/reference and supplied-neighbor statistics/radii preserve formula, ties and sample policy.
- [x] CPU LBVH and Vulkan neighborhoods connect to config, runtime, publication and UI on all domains.
- [x] Analytical, lifecycle and real GPU comparisons pass; documentation and consumer reminders are synchronized.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy$' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Review and completion
Implemented and locally verified; keep active pending commit/publication reference.
Clang 23 ci builds IntrinsicTests and ExtrinsicSandbox. All 38 focused checks pass;
full CPU selection has 4403 passes and six capability skips, with all five native
window cases passing on follow-up: 4408 distinct passes and one expected
unsanitized leak-control skip. Actual ci-vulkan ASan+UBSan execution passes in
45.38 s with zero radius/spacing error (1e-5 bound), cache/history/stale/cancel
checks and all eight domains. BUG-180 owns the existing GPU leak exclusion.

The dirty schema-v2 smoke validates. Warm framed GPU wall time is 9903.50 ms
versus 95.18 ms CPU reference on the tiny fixture; CPU remains the default.
C83 and the [verification record](../../ara/evidence/tables/spacing_vulkan_verification_2026-09-10.md)
bind the numerical scope. No renderer-size or speedup claim is made.

Scope/layering/tests/docs and clean-workshop review pass. Public surfaces remain
value records/spans, cache/jobs own runtime state, and no dependency exception,
service, component or renderer pass is added. Strict task/layering/test-layout,
method/benchmark manifests, skill mirrors and doc links pass. Source audit has
zero objective errors; existing broad-interface advisories remain. Root metadata
is the pre-existing BUG-177 finding; ccache remains disabled under BUG-178.
RUNTIME-222 explicitly owns model-space radius rendering. No commit or push.
