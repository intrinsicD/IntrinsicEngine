# ICP registration

Open **Mesh / Processing / ICP Registration**, **Graph / Processing / ICP
Registration**, or **PointCloud / Processing / ICP Registration**. These entries
open the same window as **View / ICP Registration**. Choose source and target
entities and their named float3 position properties. Each operand independently
supports mesh vertices, edges, halfedges and faces; graph nodes, edges and
halfedges; and point-cloud points. No conversion or compatibility property alias
is created. Deleted rows are excluded, including halfedges of deleted edges.

Point-to-plane requires finite nonzero target normals on the target position
domain. The panel obtains catalogs and readiness from runtime. Its exact
preflight is also used at command submission. Invalid bindings, missing source
transforms, identical operands and unusable normals are rejected before work or
history mutation. Run applies the final pose; editing the trajectory step
re-runs registration and applies that step through the same history owner.

## Controls and publication

`sandbox.registration`, schema `intrinsic.runtime.sandbox.registration`, version
1, stores entity IDs, canonical source/target/normal references, variant,
correspondence backend, iteration limit, distance cutoff, inlier ratio,
convergence threshold and trajectory step. The section registration is composed
by Sandbox. `ApplyEditorRegistrationConfig` validates, previews a candidate
engine config, and applies through the shared hot-config path.
`ApplyEditorConfiguredRegistrationCommand` reads that active section. Typed
commands use the same property and numerical preflight. Legacy unknown-domain
position defaults resolve to the entity's vertex/node domain; explicit domains
and property names are preserved. A nonpositive distance cutoff retains the
legacy 1e6 distance limit.

Registration publishes one undoable source-transform change. Source and target
properties and topology remain intact. Queued work snapshots both bindings,
normal binding, deletion revisions and transforms. Publication revalidates
identity and these snapshots; changed inputs discard stale results.

## Ownership and backends

| Owner | Responsibility |
| --- | --- |
| `Geometry.Registration` | Float3 query coordinates; shared double-precision CPU solve, trimming, convergence and traces |
| `Runtime.SpatialIndexCache` | Revision-aware target index; immutable CPU lease; reusable GPU batch buffers and framed readback |
| Existing editor geometry-processing command/job lane | Operand snapshots, centroid prealignment, normal conversion, backend choice, stale-result validation and history publication |
| Sandbox panel | Copied catalogs/readiness, validated config edits and typed requests |

`cpu_kdtree` is the default reference. `cpu_lbvh` acquires a shared target
snapshot. `vulkan_lbvh` uses LBVH nearest queries with a CPU solve. The initial
CPU job prepares snapshots; its main-thread readiness callback advances the
solve when a GPU batch completes. The cache records through a JobService GPU
participant and uses asynchronous transfer readbacks after producer submission.
It never starts a nested frame or blocks a worker waiting for GPU results.
Buffer capacity is reused across iterations; immutable target CPU/GPU indices
are reused across requests. In-flight batches retain their target entry through
eviction; shutdown retires work before freeing GPU resources.

The result reports requested/actual backend, CPU fallback reason and target
index reuse. Missing cache/device/job capability falls back explicitly to CPU.
A failed submitted GPU query fails the request; it cannot silently publish a
CPU result. GPU targets and query batches support at most 2^20 live rows and
finite coordinates within +/-1e18. CPU LBVH supports 2^24 target rows.

The indexed coordinate space is the entity TRS transform used by the registration
workflow. Local-space cache users remain independent of transforms. Selecting
`EntityTransform` includes the transform in freshness checks and builds the
transformed positions, preserving Euclidean distances under nonuniform scale.
Parent-hierarchy composition is not added by this integration.

## Evidence and limits

See the [method note](../../methods/geometry/registration/paper.md),
[CPU benchmark manifest](../../benchmarks/geometry/manifests/registration_spatial_smoke.yaml),
[domain/config contracts](../../tests/contract/runtime/Test.RegistrationDomains.cpp)
and [framed Vulkan test](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp).

ICP is local optimization with centroid prealignment, not global registration.
Point-to-plane has the existing conditioning and minimum-constraint limits.
Reported RMSE uses Euclidean correspondences before the current solve update.
Different tie policies or float rounding can change correspondences on exact or
near ties. Backend comparisons must check transforms and convergence as well as
nearest-index results. No default changes follow from a dispatch-only timing.

The opt-in [runtime comparison manifest](../../benchmarks/geometry/manifests/registration_runtime_spatial_smoke.yaml)
uses the framed Vulkan test with `INTRINSIC_ICP_BENCHMARK_OUTPUT` set to a JSON
output path. It times request-to-publication for KD-tree, cold/warm CPU LBVH,
cold/warm Vulkan LBVH and point-to-plane Vulkan on the same 1024 live samples.
The cold Vulkan run warms the cached target for the primary warm-run metric.
Seal raw output with `tools/benchmark/seal_benchmark_results.py` before validation.
This single-fixture, single-measurement smoke is not a performance qualification.
Display power state can dominate present pacing: the local display-off probe
measured approximately 13 s per GPU registration and exceeded the unchanged 5 s
smoke threshold, while matching the CPU transform. Keep that failed timing
qualification visible; correctness and elapsed-time gates are separate.
The framed test retains its assertions, stops on completion or a 70 s wall-clock
limit, and has a case-specific 120 s CTest timeout (`BUG-179`).
