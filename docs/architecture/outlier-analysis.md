# Outlier analysis

**View → Outlier Analysis** and the Mesh, Graph, and PointCloud Processing menus
open one window. Select an entity and a canonical `vec3` property, choose
Statistical or Radius, then **Detect outliers**. Detection publishes two named
properties on the selected element domain: `outlier_mask` (`uint32`, 1 rejects,
0 keeps) and `outlier_score` (`float`). **Show mask** and **Show score** use the
shared label/scalar visualization recipes.

All eight mesh/graph/point-cloud element domains are accepted. Positions are
samples in the property's coordinate system: face centroids and edge samples
are valid inputs. Distances do not apply entity transforms. Detection preserves
cardinality, order, topology, unrelated properties and deleted output rows.
Halfedges inherit deletion from their owning edge. Live input coordinates must
be finite. Newly created output properties contain zero in deleted rows.

## Estimators and execution

Statistical analysis scores each live sample by its mean Euclidean distance to
k other samples. It requires `0 < k < live_count`. The threshold is the global
mean of those scores plus `stddev_multiplier` times their **population** standard
deviation. A finite score at the threshold is retained; larger scores are
marked. Per-sample distance accumulation uses float and global moments use
double. Coincident peers remain neighbors; only the source row is excluded.

Radius analysis scores each sample by the count of other live samples in the
inclusive radius. A count below `minimum_neighbors` is marked. A zero minimum
keeps every finite live sample. Radius must be positive and finite.

| Backend token | Neighborhood execution | Classification |
| --- | --- | --- |
| `cpu_octree` (default) | Existing geometry octree reference | CPU |
| `cpu_lbvh` | Immutable lease from the shared canonical-property spatial cache | CPU |
| `vulkan_lbvh` | Framed cache kNN or radius queries, bounded batches/readback | CPU |

GPU statistical k is 1..64; GPU input is at most 2^20 live samples. CPU LBVH is
limited to 2^24. LBVH coordinates/radius must remain within 1e18. GPU batch size
is 1..16384 (default 4096). Radius requests retain one hit but consume the full
hit count, including dense neighborhoods exceeding 1024. Counts are exact within
these input bounds; float publication represents them exactly. GPU distance
arithmetic and thresholds near floating-point boundaries remain numerically
sensitive. No speedup or automatic backend selection is claimed.

The runtime uses `SpatialIndexCache` and existing jobs; no second index service
or ECS-owned GPU buffers are introduced. GPU queries precede a dependent CPU
classification job. Publication checks the captured input, deletion and output
property revisions. Cancellation, stale inputs, unsupported configurations and
GPU failures retain previous output; an explicit Vulkan request never falls
back to CPU queries. Results report requested/actual backend, cache reuse,
query batches and separate neighborhood/CPU elapsed times.

## Removal and history

**Remove marked points** is a separate, undoable operation for topology-free
point clouds only. It consumes the most recent detection on that entity and
requires its position/deletion/mask revisions to remain current. It does not
rerun the estimator. Editing unrelated attributes or scores is allowed; editing
positions, deletion state or the mask requires detection again. The provenance
stamp is transient: after scene reload, detect again before removal.

Removal copies and compacts the entire property set in source order. Custom
properties of any supported stored type survive. Already deleted slots are
retained; this action removes only live rows marked 1. Primitive selection on
the affected point domain is cleared after cardinality changes. Undo/redo of
removal guards the complete property-set revision, preventing later unrelated
edits from being overwritten. Analysis undo/redo guards only its source and
outputs. Structural replacement can invalidate earlier property-revision-bound
history entries even after a later structural undo.

## Config and API

Section `sandbox.outlier_analysis`, schema `intrinsic.runtime.sandbox.outlier_analysis`
version 1, serializes entity, method, backend, operation, canonical positions,
mask and score references, k, multiplier, radius, minimum count and batch size.
The three references must name distinct, correctly typed properties on the same
domain (`unknown` resolves to the entity's natural vertex/node/point domain).
Output names cannot replace topology or deletion properties.

`PreviewEditorOutlierAnalysisCommand`, `ApplyEditorOutlierAnalysisConfig` and
`ApplyEditorConfiguredOutlierAnalysis` are shared by UI and agent/config callers.
`operation` is `analyze` or `remove_marked`. Preview does not build an index or
mutate properties. Legacy geometry removal functions and `ApplyEditorPointCloudOutlierRemovalCommand`
retain their CPU compatibility behavior. The new analysis/config path selects
LBVH execution; the old PointCloud menu ID opens this analysis window.

## Formulation sources and scope

The neighborhood filtering intake used Rusu et al., *Towards 3D Point cloud
based object maps for household environments* (2008),
[DOI 10.1016/j.robot.2008.08.005](https://doi.org/10.1016/j.robot.2008.08.005),
and the official PCL
[statistical filter](https://pointclouds.org/documentation/classpcl_1_1_statistical_outlier_removal.html)
and [radius filter source](https://pointclouds.org/documentation/radius__outlier__removal_8hpp_source.html).
The original paper's publisher abstract and indexed preprint were accessible;
the full preprint fetch timed out. This integration preserves the engine's
existing population-variance formulation; it does not assert bitwise PCL parity.

Density-adaptive alternatives considered were
[LOF, SIGMOD 2000](https://doi.org/10.1145/342009.335388) and
[LoOP, CIKM 2009](https://brava.dbs.ifi.lmu.de/publications/891).
They define different scores and are outside this integration. The existing
simplified LOF-like probability utility, density estimation and bilateral
filtering retain their own execution paths; GEOM-073 tracks those follow-ups.

Tests: [CPU geometry oracle](../../tests/unit/geometry/Test.PointCloudOutlierRemoval.cpp),
[runtime/config/history](../../tests/contract/runtime/Test.OutlierAnalysis.cpp),
[GPU query/publication smoke](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp),
and [window routing](../../tests/integration/runtime/Test.SandboxEditorPresentation.cpp).
The bounded [runtime benchmark](../../benchmarks/geometry/manifests/point_lbvh_outlier_runtime_smoke.yaml)
compares the same input through CPU reference and framed GPU publication; small,
display-paced smoke timings are not performance evidence for production data.

A reproducible GPU smoke/benchmark run on a Vulkan-capable host is:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
mkdir -p /tmp/outlier-benchmark
ASAN_OPTIONS=detect_leaks=0 INTRINSIC_OUTLIER_BENCHMARK_OUTPUT=/tmp/outlier-benchmark/runtime-smoke.json ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.OutlierNeighborhoodsPublishAcrossDomainsAndCountDenseSupport$' -L gpu -L vulkan --timeout 120
python3 tools/benchmark/seal_benchmark_results.py --root /tmp/outlier-benchmark --manifests-root benchmarks --run-id outlier-local-run --replace
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/outlier-benchmark --manifests-root benchmarks --strict
```

Use a distinct run ID for each invocation. This smoke follows the existing GPU
cohort's leak setting; [BUG-180](../../tasks/backlog/bugs/BUG-180-framed-icp-leak-enabled-process-retention.md)
tracks separate leak-enabled investigation. Passing this smoke is no leak-freedom claim.
