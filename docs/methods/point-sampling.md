# Point sampling backend contract

The standalone sampler accepts count-matched vec3 properties on mesh vertices,
graph nodes and point clouds. It samples live rows in world space and publishes
rank/selection properties atomically, or creates a sampled point-cloud entity.
All configuration surfaces use `Runtime.PointSamplingOperations`.

The CPU backend is `cpu_reference`. Exact weighted farthest-point sampling also
selects `gpu_vulkan_compute`; requested backend, actual backend and fallback
reason remain explicit. Unsupported method/device requests keep their documented
CPU fallback. Residency acquisition and compute submission refusals fail without
publishing. The GPU checks a bounded prefix against the CPU reference before
publication; a parity mismatch reports the reason and uses the CPU order.

GPU preview: no; commit via the existing terminal undoable rank/mask publication.
There is no intermediate Accept/Discard state. Cancelled or stale jobs retain
existing outputs. The GPU kernel reads canonical residency positions and weights,
gathers live rows, applies the captured transform and converts positions to double
on the device. CPU copies are retained for the existing reference-prefix check,
not uploaded as kernel inputs. Float subnormals are loaded using integer bits and
converted through double; world positions round to float before distance work,
matching the existing CPU path. The CPU reference and its expectations are unchanged.

Each chunk evaluates at most the workspace's declared point-pair budget. Only the
terminal chunk returns the order and clearances; earlier chunks signal completion
with zero bytes. IO diagnostics expose `GpuInputUploadBytes`, `GpuInputCacheHits`
and `CpuStageReadbackBytes`. A second run at unchanged input revisions reuses the
canonical slots. Row-map and world-matrix metadata are written separately at start.

The registered `RUNTIME290VulkanPointSampling.FarthestPointMatchesTheCpuAcrossChunksAndThroughTheEditor`
smoke compares the complete order and clearances with the CPU reference (tolerance
zero), records `max_clearance_delta`, compares published rank/mask fields on weighted,
transformed inputs with deleted rows, and checks zero input uploads on repeat runs.
Its direct run spans multiple submissions and requires empty intermediate payloads.
Mock-device `ResidentPointSampling.*` tests cover residency reuse, metadata-only
writes, terminal publication/undo, stale watches, cancellation and admission refusal.

The remaining LOP, k-means and keypoint residency migrations are owned by
[RUNTIME-300](../../tasks/done/RUNTIME-300-fully-gpu-methods-on-residency-inputs-and-rings.md).

The runtime leases sampling workspaces from the spatial cache. Completed workspaces
retain scratch capacity and their pipeline across requests; `Begin` resets the
order and per-run parameters, uploads current row/transform metadata, and unbinds
retained row/weight buffers when those inputs are absent. Larger requests grow
only undersized buffers. A new request cannot reuse a workspace still held by a
GPU submission.
