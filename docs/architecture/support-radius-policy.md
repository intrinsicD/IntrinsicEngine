# Support-Radius Recommendation and Workload Policy

This document is the canonical contract for selecting a default world-unit
support radius for point-set methods. It applies to the finite `vec3` property
bound to a method's position slot, independent of whether that property belongs
to points, mesh faces, edges, halfedges, or graph elements.

## Ownership and API

`Geometry.SupportRadius` owns the entity-independent analysis. Its public API is
plain parameter/result records plus `Analyze(span, optionalManualRadius, ...)`.
It imports the existing exact `Geometry.KDTree` and statistics utilities; it
does not own an ECS component, service, cache, backend interface, or method
registry. Runtime owns property binding, method-policy selection, limits, and
the explicit resolved radius passed to an executing backend.

The analysis is derived execution data, not authored geometry. It is not
published to an entity. Until a selected property has a trustworthy data
revision, runtime recomputes the bounded profile from each immutable job
snapshot. Entity identity and property name alone are not valid cache keys.

## Deterministic estimator

Automatic analysis performs the following steps:

1. Reject fewer than two samples, non-finite values, and a zero-extent point
   set with an explicit status.
2. Build one KD-tree over the complete selected property.
3. Select at most 2,048 sample indices by deterministic, evenly spread index
   positions. The estimator never randomly subsamples.
4. Query nearest neighbors once per sample, exclude the sample's own index,
   and retain distance distributions for ranks up to the requested profile
   bound. The selected rank is clamped to `point_count - 1` for small inputs.
5. Select the method policy's robust distance quantile and multiply it by the
   policy factor. The axis-aligned bounding-box diagonal is reported only as a
   scale diagnostic; it is not the recommendation when valid neighbor
   distances exist.
6. Query the recommended sphere for the same bounded sample and report support
   occupancy P50, P95, and maximum. The float KD-tree radius is rounded outward
   for broad-phase completeness, followed by the method-compatible double
   precision cutoff `distance_squared < radius_squared`.

The current LOP-family policies are deliberately small, deterministic
heuristics rather than quality claims:

| Strategies | Neighbor rank | Distance quantile | Multiplier |
| --- | ---: | ---: | ---: |
| LOP, isotropic WLOP, CLOP | 16 | P75 | 1.25 |
| anisotropic WLOP, EAR | 24 | P75 | 1.25 |

The factor puts the selected neighbor inside the compact-support cutoff rather
than exactly on its zero-weight boundary. These defaults are operational
starting points; benchmark-backed method-specific tuning can revise a row only
through a scoped method task with correctness evidence.

Manual mode preserves the positive finite world-unit radius exactly. It skips
the k-distance recommendation but still runs the same sampled occupancy and
workload checks, so a manual value is an override, not a safety bypass.

## Workload preview and failure

Runtime estimates an upper bound on the number of support queries from
strategy, input/output counts, and iteration bounds. WLOP accounts separately
for source density, L2 initialization, projected density, attraction, and
repulsion passes. CLOP additionally counts component/point evaluations for
K-means++/Lloyd mixture initialization, EM, continuous L2 initialization, and
all three attraction terms. EAR bounds every progressive insertion by all
current point pairs, a worst-case full clearance scan for every supported pair,
and the four final projection/refinement scans. Directional WLOP/EAR also count
normal preparation before refinement. Authored normals add normalization and a
worst-case input-squared local-orientation scan. Estimated normals add one KNN
query per input point plus conservative input-squared KD-tree build, candidate
evaluation, and MST seed-scan envelopes; bounded KNN sample processing and MST
adjacency visits are fixed contributions. Keeping worst-case KNN candidate work
in the fixed term ensures a sparse manual radius cannot hide quadratic normal
preparation behind low sampled support occupancy. The geometry analysis
combines support-query count with sampled P95 occupancy and adds those
non-neighborhood analytic evaluations using saturating unsigned arithmetic.
Configuration supplies two non-zero bounds:

- `max_support_neighbors` rejects a sampled support maximum above the limit;
- `max_predicted_contributions` rejects the combined predicted contribution
  count above the limit.

The preview is a deterministic guard against obviously unsafe work, not a
runtime or performance guarantee: projected samples can become denser than the
source sample. An over-budget, degenerate, build, or query status returns
`UnsafeSupportRadius` before the LOP-family method executes and publishes no
geometry result. Completion telemetry retains estimator version, source,
status, sample count, selected rank/quantile/distance, resolved radius,
bounding-box diagonal, occupancy, predicted query count, and predicted
contributions.

## Complexity and backend use

For `N` input points, `S <= 2,048` samples, and bounded rank `K <= 64`, profile
construction is `O(N log N + S(log N + K))` plus `S` exact radius queries and
`O(N)` index storage. It runs on the existing job worker rather than the frame
thread. The CPU reference and Vulkan backend consume the same resolved positive
radius and workload decision. Backends may use different acceleration
structures for their iterative queries, but they do not independently infer a
different default radius.

`Geometry.ImplicitPlaneField` keeps its hierarchy-local radius factor, and SPH
keeps its physical smoothing length. Those parameters have different semantics
and are not silently rebound to this point-set policy.
