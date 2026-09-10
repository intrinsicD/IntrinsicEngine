---
id: RUNTIME-226
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T01:52:34Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-226 — FPFH descriptor analysis with shared spatial backends

## Goal
Expose the existing FPFH variant on canonical position/normal properties with CPU KD-tree, cached CPU LBVH and framed Vulkan radius queries, shared config/UI, and atomic publication of 33 float histogram columns.

## Context
The operator authorized sequential remaining spatial consumers and Framework24 ports until 07:00 Europe/Berlin on 2026-09-10. RUNTIME-225 supplies reusable neighborhood views and property guards. This slice closes the existing FPFH consumer before the remaining feature registration and Framework24 ports.

## Non-goals
No descriptor-space matching acceleration, alternative FPFH formulations, topology changes or new property value kind/service.

## Formulation and references
Preserve query-normal Darboux SPFH with three eleven-bin histograms, each normalized to 100. Compute SPFH for every live point, then self SPFH plus the inverse Euclidean-distance-weighted neighbor contribution divided by neighborhood cardinality, normalizing each final block to 100. Radius is explicit or five times positive exact nearest-live spacing. Radius neighbors are sorted by source ID before the optional lowest-ID MaxNeighbors cap. Exact required prefixes may be supplied when capped. Empty query indices mean all points; explicit query order and repeats are retained. Invalid live normals and nonfinite/unrepresentable inputs fail closed.

Reviewed [Rusu, Blodow and Beetz, ICRA 2009](https://www.cvl.iis.u-tokyo.ac.jp/class2016/2016w/papers/6.3DdataProcessing/Rusu_FPFH_ICRA2009.pdf), DOI 10.1109/ROBOT.2009.5152473, including Eq. 4. The paper's pair-source orientation chooser differs from this engine's preserved query-normal choice. [PCL implementation](https://pointclouds.org/documentation/fpfh_8hpp_source.html) uses different weighting details; no PCL interchangeability is claimed. Reviewed Szalai-Gindl/Varga (2024), DOI 10.1109/ACCESS.2024.3400591, histogram-resolution/orientation/CDF extensions; those alternatives remain excluded.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Count-matched finite float3 position and nonzero normal spans, optional query indices and complete required radius neighborhoods. |
| Compatible entity sources | All eight canonical property domains with deletion masks. |
| RuntimeModule | Existing geometry operations, SpatialIndexCache and JobService. |
| Config/agent | Persisted sandbox.descriptor_analysis schema, canonical input and 33 output bindings, radius/cap/backend/batch/capacity controls. |
| UI | FPFH Descriptor Analysis window and provenance aliases use the shared validated apply/run path and existing scalar visualization. |
| Publication | Thirty-three named same-domain float columns; one revision-checked history operation preserves deleted rows, unrelated properties and topology. |
| End-to-end tests | Analytic/reference neighborhoods; all-domain CPU/config/history/stale cases; actual Vulkan comparison and overflow/cancellation refusal. |

## Spatial acceleration review
Reuse the immutable selected-position property index. Uncapped queries require complete inclusive radius support. Capped queries may use an exact lowest-ID prefix covering min(MaxNeighbors,total hits); both CPU and Vulkan radius implementations already retain lowest source IDs. Reject overflow only when the required support cannot fit capacity. Do not substitute distance-ranked or arbitrary prefixes. Spacing, SPFH and FPFH reductions stay on CPU and are reported. Matching the 33-dimensional descriptors remains outside a 3D point LBVH.

## Right-sizing
Reuse Geometry.PointNeighborhoods, geometry feature kernels, runtime property watches, spatial cache, jobs, configuration and command history. The existing typed property catalog can store the histogram as 33 real float columns without adding a bespoke persisted vector type. All output references remain canonical and configurable; a UI name-prefix convenience creates the default bundle. No new service or backend interface.

## Slice plan
One slice: reference/span contract and tests, manifest, indexed runtime/config/UI integration, CPU and actual Vulkan verification, docs and fixed-surface independent review.

## Required changes
- [x] Add span and supplied-neighborhood descriptor computation while preserving the Cloud API.
- [x] Use the common neighborhood view without changing other consumers.
- [x] Wire all three backends and canonical property/config/UI/publication surfaces.

## Tests
- [x] Analytic histograms, capped/query-order cases, invalid normals/neighborhoods and deleted rows pass.
- [x] All-domain CPU/cache, config, history and stale/cancel tests pass.
- [x] Actual Vulkan parity and required-support overflow and dense capped-prefix checks pass.

## Docs
- [x] Update architecture, consumer inventory, method/benchmark manifests, module inventory and remaining port reminders.

## Acceptance criteria
- [x] Compatible property domains share the same method availability and publication path.
- [x] No backend silently loses required radius support or falls back to CPU queries.
- [x] Full CPU gate, focused actual Vulkan, manifest smoke and independent fixed-surface review pass with explicit variant/precision/performance limits.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.Descriptor' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Forbidden changes
No layer exceptions, silent fallback/truncation, hidden topology replacement, invented speedup or PCL interchangeability claims.

## Verification constraints
CCACHE_DISABLE=1 under BUG-178. Configure outside the sandbox for the existing vcpkg egress/cache constraint (BUG-065). Existing BUG-180 excludes GPU LSan while retaining ASan+UBSan. Count actual capability skips separately from passes.


## Plan correction

Source review of `Geometry.PointLBVH.cpp::Radius` and `lbvh_query.comp` corrected the initial distance-ranked-result assumption: radius queries retain lowest source IDs already. The implementation uses that existing contract to support dense capped descriptors without collecting discarded neighbors. Uncapped support still fails closed on overflow; a dedicated actual Vulkan cap=1 fixture covers occupancy above 1024.

## Test-harness correction
The first dense capped Vulkan test received a result but then waited for every submitted token. Successful ancestor records may already be reaped before the final result arrives, so IsComplete no longer reports them. Restrict that extra settling wait to partial-submission rejection phases; successful dependency ordering is already enforced by the job service. Retain the initial timeout receipt and run all three actual Vulkan cases again with unchanged production code and time limits.

## Completion

**Completed:** 2026-09-10
**Commit:** `2e6353df1ca9c2500bc6509ab93a71476399eef1` (implementation; retirement/review seal follows).

[Bounded verification](../../ara/evidence/tables/descriptor_verification_2026-09-10.md) and ARA C87 bind 4443 distinct CPU passes and three actual Vulkan cases. The dense cap=1 path reuses existing lowest-ID retention. Uncapped overflow and failed job chains preserve all outputs. No performance improvement or PCL interchangeability is claimed.
