# Curvature segmentation performance diagnostic — 2026-09-03

This is a local hotspot investigation, not claim-grade benchmark evidence.
The checkout was dirty, the ad-hoc runner payloads reported `commit: unknown`,
the health manifest explicitly says `harness_health_not_performance_or_adoption`,
and the raw producer output is legacy-shaped rather than sealed schema v2.

## Environment and scope

- Baseline source revision: `da70fb1802f8540b73e5b893df260bbe0326d054`
  with pre-existing unrelated uncommitted changes. The baseline cohort was
  collected before the optimization patch; the comparison cohort used the
  same host, compiler, Release target, fixtures, and parameters.
- Host: Intel Core i9-11900KF, 8 cores / 16 hardware threads, x86-64.
- Compiler: Clang 23.0.0.
- Debug geometry command contained `-g` and no optimization flag; Release
  contained `-O3 -DNDEBUG`.
- `perf` sampling was unavailable because `perf_event_paranoid` was 4.
  Callgrind instruction profiles and the method's built-in wall-stage timers
  were used instead.
- Both cohorts' legacy runner payloads report `commit: unknown`; the optimized
  cohort therefore remains local diagnostic evidence rather than a repository
  performance claim.

Commands exercised the existing `IntrinsicCurvaturePatchProfile` in Debug and
Release for `feature_patch_smoke` and `feature_patch_health`. The separate
`IntrinsicCurvatureSegmentationProfile` supplied a bounded warning about the
shared automatic GMM selection path.

## Matched five-run before/after comparison

Five fresh-process Release samples were collected for each cohort before and
after the exact-preserving optimization patch. Medians are reported with the
full five-run range; these measurements are useful for local engineering
decisions but are not claim-eligible benchmark rows.

| Cohort | Before median [range] (ms) | After median [range] (ms) | Local ratio |
|---|---:|---:|---:|
| 100k health | 595.708 [538.503, 795.899] | 335.340 [334.667, 337.589] | 1.78x |
| Feature smoke | 4.581 [4.494, 7.795] | 3.635 [3.563, 3.923] | 1.26x |
| Patch-quality smoke | 13.886 [13.409, 20.334] | 2.776 [2.735, 3.021] | 5.00x |
| Seed-refutation control | 8.823 [8.514, 9.322] | 2.949 [2.886, 3.148] | 2.99x |

The median 100k stage timings localize the improvement:

| Stage | Before (ms) | After (ms) | Local ratio |
|---|---:|---:|---:|
| Validation and sampling | 29.885 | 41.284 | 0.72x |
| Mixture fitting | 21.007 | 19.934 | 1.05x |
| Posterior construction | 8.782 | 8.530 | 1.03x |
| Seed selection | 114.850 | 120.742 | 0.95x |
| Simultaneous growth | 27.800 | 22.379 | 1.24x |
| Region merging | 320.089 | 83.123 | 3.85x |
| Boundary refinement | 12.547 | 1.644 | 7.63x |
| Publication and validation | 24.084 | 21.967 | 1.10x |
| Timed stage total | 586.722 | 329.427 | 1.78x |

The topology cache construction is charged to validation and sampling, which
grew 38.1%; the 100k seed stage also varied 5.1% slower. Those costs are
outweighed by the merge and refinement reductions. Median reported peak RSS
fell from 121,737,216 to 86,089,728 bytes (29.3%), but process peak RSS is not
a method-only allocation measurement.

On the feature control, bounded searches fell exactly from 20,304 to 6,768.
For diagonal A, settled visits fell from 105,508 to 76,040; diagonal B moved
from 105,544 to 76,064. The median maximum multi-scale-response stage fell
from 3.328 to 2.928 ms (1.14x). On the patch-quality control, median maximum
region-merging time fell from 4.340 to 0.439 ms (9.89x), and boundary
refinement fell from 6.953 to 0.094 ms (74.0x); validation grew from 0.566 to
0.879 ms because it now builds the reusable vertex-topology cache.

After removing elapsed-time, process-RSS, per-stage timing, and the intended
bounded-search/settled-visit counters, the before and after JSON payloads were
byte-identical after canonical key sorting. All four cohorts retained their
quality values, partition/boundary counts, deterministic-output checks, and
status. The focused curvature segmentation and patch suite also passed all 34
tests after the change.

The final verification built the optimized `ExtrinsicSandbox` target from the
new `sandbox-release` preset, passed the canonical CPU gate (4,263/4,263 tests;
one expected capability skip), and passed the grouped geometry suite under the
separate ASan and UBSan presets.

## Implemented exact-preserving changes

1. Added a `sandbox-release` configure/build preset for optimized interactive
   Sandbox runs with promoted Vulkan and without benchmark targets.
2. Cached the built-in `h:face` halfedge property beside the mesh's other
   connectivity handles, removing per-query registry/string lookup.
3. Reused one largest-radius bounded Dijkstra result per candidate side across
   all three feature scales, retaining the original strict radius filter.
4. Precomputed vertex corner/transition topology, maintained current local
   turn contributions, and used generation-stamped lazy merge-heap entries so
   only affected RAG pairs are rescored.
5. Restricted refinement work to the evolving boundary and cached exact
   Tarjan articulation sets per region, invalidating only changed regions.
6. Reused Dijkstra scratch for seed updates and replaced repeated full-face
   farthest scans with an exact lazy maximum heap using the original tie rule.

## Wall-stage samples

### 100,000-face homogeneous health fixture

The fixture has 100,000 faces, 50,451 vertices, 150,450 edges, zero supplied
curvature and feature evidence, fixed GMM count 1, 415 automatic seeds, 414
accepted merges, and one final region. Curvature estimation and feature
detection are outside this timed path.

- Debug total: 4,505.948 ms.
- Observed Release totals: 533.746 ms, 559.549 ms, and an earlier 716.910 ms.
  The manifest has zero warmups and only two measured executions, so the range
  is retained instead of selecting one speedup claim.
- Diagnostic Debug/Release ratio over these samples: approximately 6.3x–8.4x.
- Most recent Release second-execution stage total: 526.650 ms.

| Release stage | ms | Share of stage total |
|---|---:|---:|
| Region merging | 282.795 | 53.7% |
| Seed selection | 105.781 | 20.1% |
| Validation and sampling | 28.790 | 5.5% |
| Simultaneous growth | 23.856 | 4.5% |
| Publication and validation | 22.884 | 4.3% |
| Mixture fitting | 19.684 | 3.7% |
| Boundary refinement | 11.693 | 2.2% |
| Posterior construction | 8.325 | 1.6% |
| Unattributed partition/energy setup | 23.031 | 4.4% |

The result stored 1,554,397 diagnostic/output entries. Observed process peak
working-set values varied with process history and are not treated as a
method-only allocation measurement.

### 2,304-face paired-transition controls

- Feature detection: 4.524 ms maximum paired variant. The multi-scale response
  was 3.323 ms (73.5%). It executed 20,304 bounded Dijkstra searches and
  settled 105,508 face visits for 3,384 interior candidate edges. Curvature
  was supplied, so its timing was zero.
- Patch solve: 13.138 ms maximum paired variant. Boundary refinement was
  6.445 ms (49.1%) and region merging was 4.041 ms (30.8%). This control used
  fixed GMM count 1 and a 48-face boundary-adjacent test seed override, not the
  user-facing automatic seed path. Its zero-VI/boundary quality gate passed.

### Automatic GMM warning, not a valid comparison

On the separate 10,000-face sphere profile, fixed count 1 spent 2.075 ms in
GMM fitting and 11.316 ms total, including 5.906 ms curvature estimation. The
automatic 1-through-4 probe spent 127.763 ms in GMM fitting and 139.338 ms
total, but failed its quality gate with 0.4336 misclassified-face fraction.
This run is retained only to show that fitting every candidate count can
dominate; it is not evidence that fixed count is a valid replacement. The
current runtime configuration permits automatic counts 1 through 12.

## Instruction-profile localization

The Release 100k Callgrind run retired 13.159 billion instructions.

- `PropertyRegistry::Find` handled 22,342,735 name lookups and accounted for
  2.972 billion inclusive instructions (22.58%). `_Hash_bytes` alone accounted
  for 1.475 billion (11.21%). The dominant source is `Mesh::Face()`, which
  resolves the built-in `h:face` property by string name for every query.
- The Debug paired-smoke profile retired 7.070 billion instructions.
  `MergeBoundaryCredit` accounted for approximately 1.523 billion (21.54%),
  principally repeated `VertexTurnContribution` topology walks, transition
  searches, and corner-angle recomputation.
- `RegionRemainsConnectedWithoutFace` accounted for approximately 585.2
  million instructions (8.28%) across 565 per-candidate connectivity searches.
- `ApplyMerge` itself was only about 0.24% of that profile, so replacing region
  containers before eliminating repeated energy/connectivity work is unlikely
  to be the best first optimization.

## Exact-preserving optimization hypotheses

1. Cache `h:face` as a built-in `HalfedgeProperty`, matching the other mesh
   connectivity handles, so `Face()` becomes direct indexed access.
2. For each candidate edge side, run bounded Dijkstra once to the largest of
   the three radii and reuse the resulting distances for all radii. Shortest
   paths and the blocked transition are unchanged, so this removes two-thirds
   of the searches without changing the reference formulation.
3. Precompute corner angles and edge-to-transition indices; maintain local
   per-vertex boundary-turn state. Rescore only RAG pairs dependent on vertices
   changed by a merge, using generation-stamped entries in a minimum heap.
4. Replace the full face scan used to pick each farthest seed with an exact
   lazy maximum heap over current distances, and reuse Dijkstra scratch.
5. Restrict refinement to boundary faces and replace a fresh tree-based BFS for
   every candidate with reusable generation storage plus exact region
   articulation information that is refreshed only for changed regions.

Parallel GMM candidates and per-edge feature responses, revision-keyed reuse of
curvature/feature evidence, coarse-to-fine solving, and local sculpt updates are
later options. They require an explicit optimized-backend/parity campaign or a
method-contract change rather than being silently substituted into the CPU
reference.

## Evidence limitations and next gate

All 56 benchmark manifests passed strict validation. The ad-hoc raw 100k JSON
failed strict result validation because it lacked schema-v2 run/attempt IDs,
manifest/config binding, source state, threshold disposition, and claim
eligibility. The repository already retains separate sealed, clean-source,
non-claim-eligible METHOD-039 control rows; those explicitly make no performance
claim.

Before a speedup is asserted, add a Release, schema-v2, end-to-end benchmark on
a representative user mesh that times curvature estimation, computed feature
evidence, automatic GMM selection, automatic seeds, patch solve, publication,
and total editor latency. Retain the CPU reference output as the quality oracle
and compare region labels/boundaries, energies, diagnostics, memory, and runtime
under identical resolved parameters.
