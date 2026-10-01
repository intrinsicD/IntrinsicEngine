# Locally optimal projection backend contract

`PointCloudConsolidationStrategy::Lop` uses the canonical
`Geometry.PointCloud.Consolidation` CPU reference. The runtime adapter lives in
`src/runtime/Modules/PointCloudConsolidation/`. WLOP, CLOP and EAR retain their
existing producers and publication behavior.

## Resident Vulkan execution

LOP resolves the selected float3 property through `ResolveGpuPropertyInput` and
retains its `GpuPropertyView`. The kernels read stride-12 rows directly. The
private packed position upload is not used by LOP. For downsampling, the CPU
reference's selected row IDs are uploaded as metadata; coordinates still come
from the resident property. Support-radius analysis and initial sample selection
remain CPU preparation stages. The preparation snapshot releases its CPU position
and initial-position copies after resident GPU admission; admission fallback
still retains the positions it needs.

`SpatialIndexCache::QueueGpuCompute` owns submission and completion. Source-grid
construction, initialization pages, projected-grid construction, projection pages
and diagnostic reduction run as separate chunks. The single-invocation iteration
finalize closes the iteration's last projection page, and a preview copy is
recorded at the start of the following grid or reduction chunk; neither costs its
own chunk. The next chunk uses immediate completion. Grid scatter measures the maximum 27-cell
candidate count over occupied query cells (source plus projected candidates for
projection). A dispatch uses `max(1, 2^18 / maximum_candidates)` rows; several
dispatches share a submission of at most `max(1, 2^24 / maximum_candidates)` rows.
The shaders fail closed if a single row exceeds the submission candidate budget.
Dense clouds therefore page without a point-count admission cap. Reduction pages
contain at most 4096 entries. Grid planning and each iteration's finalize read
64-byte diagnostics; other intermediate chunks request completion only. Finalize
stops on the CPU reference's maximum-displacement convergence criterion.
Grid/resource and support-workload admission retain their existing limits.
Pipelines and the private scratch buffers persist across runs: a completed run
returns its owned buffers per role, and the next run (LOP or isotropic WLOP)
reuses each one whose capacity covers its plan, replacing only outgrown buffers.
The resident input view is never retained. Buffers of a failed recording stay
retired until device-idle shutdown and are not reused.

For same-cardinality in-place positions, runtime begins a positions run before
recording. At each `gpu_preview_interval` boundary (default 5), and at termination,
a device copy writes the completed float3 result into a leased ring slot. The
slot becomes the front only after completion. Intermediate ring pressure drops
a preview; terminal pressure waits for a slot. The renderer's existing GpuWorld
position observer consumes the front. CPU positions remain unchanged.
An observation's iteration count advances at finalization, before the copy's
completion, which publishes with the next grid or reduction chunk. Consumers checking a preview boundary must also wait for its preview
publication count: before the first publication, residency `Front()` can return
the canonical input even though the run already owns a ring.

Stop completes the current iteration and retains a terminal front with
`NotConverged` geometry status; Accept preserves that status and reports
`InvalidState` rather than a success error code. The panel
then offers Accept/Discard; `EditorGpuPositionRunCurrent` disables stale Accept
with its reason. Accept uses the positions run API's readback, guarded undoable
publication and revision binding. Discard and attachment loss release the run
without publishing CPU positions. Batch and `run_point_cloud_consolidation`
agent requests auto-accept. A full result event is delivered once, after terminal
publication or failure. Busy requests and resident input-acquisition refusals
are reported without CPU fallback. Missing residency/scene, retired resources, plan failure and missing GPU
state retain CPU-reference fallback.

Named-output and cardinality-changing LOP requests retain their existing terminal
property/replacement publication, with resident inputs and completion paging;
they do not create a same-cardinality positions run. Downsampling remains covered
by the existing Vulkan LOP parity fixture.

## Diagnostics and numerical scope

Results, the panel summary and agent output expose requested/actual backend,
fallback reason, input upload bytes, resident hits, CPU-stage upload/readback bytes,
submission count and preview count. Production GPU completion does not run the
CPU reference or report parity measurements.
Private state, unit weights, displacement initialization and any
sample row map are counted as CPU-stage uploads. The diagnostic readback is counted
as CPU-stage readback; the positions run API owns Accept's property readback.
Terminal replacement readback is also included in the non-ring path's readback
counter. A discarded run preserves the input revision, so a repeat run hits the
resident input. Accept binds the front to the new revision, also avoiding another
input upload.

The resident and frozen-reference Vulkan tests compare RMS and maximum point
displacement against the CPU reference and record `lop_parity_rms` and
`lop_parity_linf` in gtest XML. The frozen-reference test also records CPU/GPU
iteration counts and maximum displacement, and asserts matching iteration counts
and convergence. The resident smoke also reads the non-terminal
preview front at its configured boundary and the restored renderer positions
after Discard.

Backend identity: requested/actual `VulkanCompute`, implementation id
`gpu_vulkan_compute`; the CPU reference (`cpu_reference`) stays canonical.
The test tolerances are RMS 5e-4 and Linf 2e-3 (float grid/projection arithmetic).
Grid scatter assigns per-cell indices using atomic cursor increments; candidate
order within a cell therefore depends on GPU scheduling. Initialization and
projection sum floats in that order, whereas the CPU reference uses double
accumulators. Paging partitions query rows without splitting a row's sum or
changing the 27-cell stencil and radius predicate. Changing scheduling can change
floating-point results across iterations; the backend does not promise bitwise
repeatability. Final reduction pages retain ascending row order and only compute
the average-displacement diagnostic, not positions or convergence.

GPU arithmetic retains the float grid/projection formulation and its existing
parity tolerances. No subnormal-input refusal is added. Numerical behavior on devices that flush float
subnormals still requires actual Vulkan verification; mock tests do not evaluate
shader arithmetic.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite count-matched float3 position property. |
| Compatible entity sources | Compatible mesh, graph and point-cloud element domains. |
| RuntimeModule | `PointCloudConsolidation`. |
| Config/agent | Validated `sandbox.point_cloud_consolidation` codec; `gpu_preview_interval`; agent operation selects entity/domain/property and auto-accepts. |
| UI | Existing consolidation panels; Stop, stale-aware Accept, Discard and IO diagnostics. |
| Publication | GPU preview: yes; commit via positions run API for same-cardinality in-place positions. Named outputs and cardinality changes use existing terminal publication. |
| End-to-end tests | `ResidentLop` mock contracts and `PointCloudConsolidationGpuParity.ResidentLopPreviewDiscardAcceptParityAndRepeatInputIo` and `ResidentLopMultipleProjectionAndReducePagesMatchCpu`. |

The preview-boundary and paging assertions require execution on a Vulkan host;
mock contracts verify recording/publication order but do not execute shaders.
See [property coherence](../architecture/property-coherence.md) and
[RUNTIME-300](../../tasks/done/RUNTIME-300-fully-gpu-methods-on-residency-inputs-and-rings.md).

Normal-refinement rounds constrain the iteration limit only for EAR and anisotropic
WLOP. LOP, isotropic WLOP and CLOP permit shorter runs while preserving that unused
setting through config serialization.
