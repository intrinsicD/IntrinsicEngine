# ICP registration

Open **ICP Registration** from **Processing / Reconstruction & Registration** in
the Mesh, Graph or PointCloud menu. These entries open the same window as
**View / Processing / Reconstruction & Registration / ICP Registration**. Choose source and target
entities and their named float3 position properties. Each operand independently
supports mesh vertices, edges, halfedges and faces; graph nodes, edges and
halfedges; and point-cloud points. No conversion or compatibility property alias
is created. Deleted rows are excluded, including halfedges of deleted edges.

Point-to-plane requires finite nonzero target normals on the target position
domain. The panel obtains catalogs and readiness from runtime. Its exact
preflight is also used at command submission. Invalid bindings, missing source
transforms, identical operands and unusable normals are rejected before work or
history mutation. Run ICP starts a run with the section's execution options
(by default it computes at once and accepts its final pose at the end); editing
the trajectory step re-runs registration and applies that step through the same
history owner. While a run is live the window shows the shared step-run row
(Run, Pause, Step n, Stop, Restart, Accept, Discard), the moving-source preview
and the plots of the run's metric log.

## Controls and publication

`sandbox.registration`, schema `intrinsic.runtime.sandbox.registration`, version
1, stores entity IDs, canonical source/target/normal references, variant,
correspondence backend, the shared step-run execution options
(`max_iterations` 1..100000, default 50; `preview_every`; `preview_interval_ms`;
`auto_accept`, default `end`; `start`, default `run`), distance cutoff, inlier
ratio, convergence threshold and trajectory step. The section registration is composed
by Sandbox. `ApplyEditorRegistrationConfig` validates, previews a candidate
engine config, and applies through the shared hot-config path.
`ApplyEditorConfiguredRegistrationCommand` reads that active section. Typed
commands use the same property and numerical preflight. Legacy unknown-domain
position defaults resolve to the entity's vertex/node domain; explicit domains
and property names are preserved. A nonpositive distance cutoff retains the
legacy 1e6 distance limit.

Every queued run is a step run of the session's `StepRunsService` (ARCH-023):
one step is one `AdvanceICP` iteration, Run repeats the same steps, and
`run_control`/`run_observe` address it by the run id (`run`) its result reports.
Metrics per step are `rmse`, `inliers`, seconds since the first step and the
step's seconds; a preview is the 4x4 pose alone (the pre-aligned source stays
with the panel's view). Nothing is published before the run's one Accept,
which writes one undoable source-transform change: the trajectory step's pose
(clamped to the iterations done) composed with the pre-alignment. Source and
target properties and topology remain intact. A run snapshots both bindings,
the normal binding, deletion revisions, entity identity and transforms; every
frame its revision watches, entities, world and transforms are checked (a
change discards the run), and Accept revalidates values and row maps in full
(an unversioned change refuses it). A solve that ends without iterating (fewer
than three correspondences) ends the run as converged. Without a job lane the
run computes and accepts at once, which supports only `start=run` with
`auto_accept=end`; with a job lane but no step-run service it is refused.
`run_registration` runs the section at once and accepts at its end whatever
its `start` and `auto_accept`; with `wait: false` (ICP only) it runs the
section's own `start` and `auto_accept` and answers at once with the run's
id (`run`), which `run_control` and `run_observe` drive. A ready CPD run starts
through the CPD plugin's `coherent_point_drift_start`.

## Ownership and backends

| Owner | Responsibility |
| --- | --- |
| `Geometry.Registration` | Float3 query coordinates; shared double-precision CPU solve, trimming, convergence and traces |
| `Runtime.SpatialIndexCache` | Revision-aware target index; immutable CPU lease; reusable GPU batch buffers and compute-lane readback |
| `Runtime.RegistrationOperations` | Operand snapshots, centroid prealignment, normal conversion, backend choice, the step run (worker steps for every backend; Vulkan through a prepared worker query session), stale-result validation and history publication |
| Sandbox panel | Copied catalogs/readiness, validated config edits and typed requests |

`cpu_kdtree` is the default reference. `cpu_lbvh` acquires a shared target
snapshot. `vulkan_lbvh` uses LBVH nearest queries with a CPU solve;
`vulkan_brute_force` takes the same Vulkan path and admission with the
exhaustive query strategy, which builds no target hierarchy
([spatial indices](spatial-indices.md#gpu-query-strategies)). Either Vulkan
request falls back to `cpu_lbvh` with a diagnostic when Vulkan is unavailable,
and a brute-force request also when sources x targets x 2 exceeds the shared
2^24 engineering work limit (checked before the device);
results report the requested and actual backend. CPU backends step on a
worker (the KD-tree is built by the first step). A Vulkan run steps on a worker
too (ARCH-023): its start prepares a `SpatialQuerySession` on the main thread
(resident target, its hierarchy for LBVH, every buffer and pipeline), and each step
submits one correspondence page on the method compute lane, waits for it there in
bounded slices (a discard gives the wait up and releases the page) and solves the
iteration, with no frame, pump or main-thread callback; a pause, Stop or Discard
takes effect at an iteration boundary. A float64 device writes the pose into the
page's own command buffer; otherwise the step uploads the transformed queries by a
mapped write into the session's staging, copied on the compute lane.
Buffer capacity is prepared once and reused across iterations and restarts;
a finished run's buffers return to the method scratch cache once its last page completed;
immutable target CPU/GPU indices are reused across requests. On float64-capable
devices the prealigned source is uploaded once, then each iteration writes a
128-byte double-precision pose in its page's command buffer and transforms the query
coordinates on the device. The resulting float3 coordinates feed the existing LBVH or
brute-force query; the start's preparation submission records the target's missing
gather and build. Devices without shader float64 retain the CPU query-transform path
(its rows reach the device through the session's staging and a compute-lane copy).
Correspondence readback and the double-precision CPU solve remain hybrid. A session
retains its target entry through eviction until its last page completed; shutdown
retires work before freeing GPU resources.

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
