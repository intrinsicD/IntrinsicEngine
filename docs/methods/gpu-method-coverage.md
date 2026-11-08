# GPU method coverage

This page records the current state of every existing GPU or hybrid method family, and of
its CPU counterpart, against the shared run lifecycle. The lifecycle is described in the
[method template](method-template.md#runtime-execution-contract): `Runtime.StepRuns`
controls, `GpuTransactionCore` residency and Accept, and the shared execution options. A
family is migrated only when its code starts a step run. Families that do not are listed
as gaps, with their current owner.

## How to read the matrix

- **Queue.** Worker pages and `SpatialIndexCache` query batches go through `IDevice::SubmitComputeReadback`. That is the compute lane, or
  the graphics queue on hardware without a separate one. The transfer queue diagnostics
  report the lane as `ComputeLane`.
- **Transfers.** "Accept readback" means the only device-to-CPU copy of the result is the
  front readback when it is accepted.
- **Run controls.** "Panel" means the panel's `DrawGpuTransactionControls` or
  `DrawStepRunControls` row. MCP `run_control` and `run_observe` act on every step run by
  its id.
- **Frame dependency.** Worker steps submit and wait for bounded compute pages without rendering
  frames. Owner-side GPU Init and Accept callbacks, and preview-fence visibility, still need
  the frame's transfer collection (`CollectCompleted`), not just the main-thread job drain;
  when the window is minimized, GPU Accept waits until frames resume. Initial farthest-point
  orders (CPD subsamples, consolidation's initial samples) are worker pages too; the owner's
  drain still starts the work, uploads the first producer input and publishes, and consolidation's
  order reaches its solve job through one drain. The first page orders initial uploads;
  later prepared pages do not wait for unrelated graphics uploads. Hardware sharing one
  graphics queue necessarily shares its queue ordering.

## GPU and hybrid families

| Family | Logical step | Run control | Resident storage, CPU transfers | Preview | Controls, config, MCP | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Scalar analysis: kernel density, point spacing, density weights ([scalar transaction](../../src/runtime/Editor/Operations/Runtime.PointScalarTransaction.cpp)) | One-shot: one kernel dispatch | Worker-stepped `GpuTransactionCore` one-shot (ARCH-023: owner Init with a retained index view, the kernel page on the worker): ready or running start, Restart | Input positions, base view, workspace and output ring held from the first record until Accept, Discard or the run's end; a few bytes of statistics, then the Accept readback | The ring front shows the result; no intermediate copy | Sections bind the one-shot rows `auto_accept` and `start`; plugin windows edit them and draw the host row (`DrawScalarAnalysisControls`). The tools without `start` and `auto_accept` are a waiting batch; each one named replaces the section's, and a run that then waits answers at once with its id `run`. | [Scalar analysis smoke](../../tests/integration/graphics/Test.PointCloudEditingScalarAnalysisGpuSmoke.cpp) (common script, workspace kept across Restart, detach before `run_control` Accept), [scalar residency](../../tests/integration/graphics/Test.PointScalarTransactionGpuSmoke.cpp), plugin GPU smokes |
| Point normals, PCA ([normal operations](../../src/runtime/Editor/Operations/Runtime.NormalOperations.Processing.cpp)) | One-shot: completion-gated PCA pages | Worker-stepped `GpuTransactionCore` one-shot (ARCH-023: every page on the worker against a retained index view); Restart pages from row 0 | Positions, base, workspace and output ring held until the run ends; per-page statistics, then the Accept readback | None until the step ended (the vec3 ring is not observed) | `sandbox.normal_estimation` binds `auto_accept` and `start`; the panel edits them. `run_operation` waits by default; `wait: false` answers with `run`. | [Point normals smoke](../../tests/integration/graphics/Test.PointNormalsTransactionGpuSmoke.cpp) (common script), [LBVH normals](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp) |
| Mesh vertex and face normals (same owner) | One-shot: the mesh passes in one submission | As point normals | Positions, topology bundle and output ring resident; Accept readback | As point normals | As point normals | [Vertex normals smoke](../../tests/integration/graphics/Test.VertexNormalsTransactionGpuSmoke.cpp) (common script for one vertex and one face method) |
| Outliers ([outlier operation](../../src/runtime/Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cpp)) | One-shot: one kernel dispatch | Worker-stepped `GpuTransactionCore` one-shot (ARCH-023): ready or running start, Restart | Score, mask and presentation rings resident; statistics, then two Accept readbacks | Score and mask fronts show the result | `sandbox.outlier_analysis` binds `auto_accept` and `start`; the panel draws them with the section's rows. `run_operation` waits by default; `wait: false` answers with `run`. | [Outlier smoke](../../tests/integration/graphics/Test.OutlierTransactionGpuSmoke.cpp) (common script), [LBVH outliers](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp) |
| Property smoothing, explicit and implicit ([smoothing](../../src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp)) | One explicit pass, or one implicit time step over every channel (its CG chunks are not steps) | Worker-stepped `GpuTransactionCore` (ARCH-023: owner Init page, worker pages; Run batches explicit passes); Restart keeps the workspaces | Resident rings and solver state; Accept readback | Ring front | Panel row; section execution rows. `run_operation` (`property_smoothing`) waits by default; `wait: false` runs the section's start and answers with `run`. | [Smoothing smoke](../../tests/integration/graphics/Test.PropertySmoothingGpuSmoke.cpp) |
| K-Means, GPU ([GPU state](../../src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp)) | One Lloyd iteration (pages) | Worker step run | Resident assignments and centroids; bytes of metrics per page | Live preview ring at the shared cadence | Panel row; section execution rows; `run_kmeans` waits; `start_kmeans` answers with the run id | [Clustering GPU smoke](../../tests/integration/runtime/Test.ClusteringServiceGpuSmoke.cpp) |
| Resident LOP and isotropic WLOP, in place ([consolidation GPU](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp)) | One iteration (pages) | Worker step run (ARCH-023: owner Init, worker pages, owner terminal pages); the Accept-only GPU-positions transaction | Resident positions | Live preview at the shared cadence | Panel row; section execution rows; `run_point_cloud_consolidation` waits; `start_point_cloud_consolidation` answers with the run id | [Consolidation GPU parity](../../tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp) |
| Hybrid consolidation: LOP, WLOP, CLOP, EAR with Vulkan LBVH ([module](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp)) | One projection iteration; rows on the device, projection on the CPU | Worker step run (ARCH-023: rows from three query sessions its Init prepared, paged on the worker). A Vulkan initial point order runs first as a worker job of bounded pages (`SubmitInitialOrdering`, `PointSamplingGpuRun::OrderOnWorker`); the run's job depends on it (`InitialOrderJob`) | Hybrid: neighbour rows cross the device boundary each iteration (reported) | Shared cadence | As resident LOP | Consolidation GPU parity |
| ICP, Vulkan ([registration](../../src/runtime/Editor/Operations/Runtime.RegistrationOperations.Processing.cpp)) | One ICP iteration (a correspondence batch) | Step run; Accept writes the transform | Hybrid: correspondences read back each iteration | Pose preview | Panel row; section execution rows. `run_registration` waits by default; `wait: false` runs the section's start and answers with its id `run`. | [LBVH ICP](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp) |
| Coherent Point Drift, runtime ([CPD](../../src/runtime/Editor/Operations/Runtime.RegistrationOperations.CoherentPointDrift.cpp)) | One EM iteration (device E-step where supported) | Worker step run for every configuration. With a Vulkan subsample backend the start prepares the target/source orders and the first step computes them on its worker (`PointSamplingGpuRun::OrderOnWorker`) before the solver samples; a restart reuses them | Hybrid: per-iteration E-step traffic (reported as device iterations and CPU rows) | Source preview | Panel row; `run_registration` waits by default; `wait: false` starts with the section’s options and returns its run id. | Runtime CPD operations and GPU E-step/subsampling tests; [no-frame probe](../../tests/integration/graphics/Test.WorkerProducersNoFrameGpuSmoke.cpp) (Vulkan subsample orders and iterations with graphics held) |
| Keypoints, Vulkan compute ([keypoints](../../src/runtime/Editor/Operations/Runtime.PointAnalysisOperations.Keypoints.cpp)) | Paged score pass | Resident `vulkan_compute`: the scalar publication transaction's worker-stepped producer (ARCH-023: owner Init with a retained index view, every page on the worker); `vulkan_lbvh`: query sessions (the query slice) | Resident score and mask rings; Accept readback | Ring fronts | Panel row; section `auto_accept` and `start`. `run_keypoint_analysis` waits by default (runs and accepts at its end); `wait: false` runs the section's start and answers with `run`. | [LBVH keypoints](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp) |
| Keypoints, Vulkan LBVH (same owner) | One-shot analysis with bounded query pages | Shared one-shot worker run with prepared query sessions | Hybrid: rows read back | None | As above | LBVH keypoints |
| Point construction, Vulkan LBVH: Hoppe surface and kNN graph ([construction](../../src/runtime/Editor/Operations/Runtime.PointConstructionOperations.Processing.cpp)) | One-shot: preparation, every kNN query page, then CPU field or graph assembly and extraction | Shared one-shot worker run (`MeshSupport::StartOneShotCpuRun`) with one prepared `Uploads` query session: the worker writes each page's CPU-generated grid or sample queries into one fixed page and queries its active rows, no frame; a discard stops at the next page boundary or a waiting page; Accept creates the entity (one undo entry) | Hybrid: every page uploads the fixed page of 12-byte query rows and reads its kNN rows back, reported per generation as `CpuStageUploadBytes` and `CpuStageReadbackBytes` (padding included, the session's initial rows excluded) | None (the result is a new entity at Accept) | `sandbox.point_construction` binds `auto_accept` and `start`; the panel edits them and draws the host row. `run_operation` waits by default; `wait: false` answers with `run`. | [Construction GPU smoke](../../tests/integration/graphics/Test.PointConstructionGpuSmoke.cpp) (common script, reference parity, stale/cancel/refused/discard), [no-frame probe](../../tests/integration/graphics/Test.WorkerProducersNoFrameGpuSmoke.cpp), session paging contract in [`Test.QueuedEditorJobContract.cpp`](../../tests/contract/runtime/Test.QueuedEditorJobContract.cpp) |
| Farthest-point sampling, Vulkan ([sampling](../../src/runtime/Editor/Operations/Runtime.PointSamplingOperations.cpp)) | One selected sample; Run batches the allowed samples into a pair-budget chunk | Worker-stepped `GpuTransactionCore`; Restart retains workspaces | Resident distances; final readback | None | Panel row; section `auto_accept` and `start`. `run_point_sampling` waits by default (runs and accepts at its end); `wait: false` runs the section's start and answers with `run`. | [Point sampling smoke](../../tests/integration/graphics/Test.PointSamplingGpuSmoke.cpp) |
| Bilateral filter plugin, Vulkan LBVH or brute force | One filter pass | Worker step run: kNN rows paged on the worker through prepared query sessions, then the CPU update | Hybrid: k-NN rows read back each pass; moved points uploaded on the compute lane | Ring front of an in-place output | Plugin window and tool | [Bilateral plugin GPU smoke](../../tests/integration/graphics/Test.BilateralFilterPluginGpuSmoke.cpp) |
| Descriptor analysis plugin | The whole analysis (radius pages inside it) | Worker step run: radius rows paged on the worker through a prepared query session | Hybrid: rows read back | None | Plugin window and tool | [Descriptor plugin GPU smoke](../../tests/integration/graphics/Test.DescriptorAnalysisPluginGpuSmoke.cpp) |
| Progressive Poisson sampling | — | No GPU path executes: requests report `Actual = CpuReference` | — | — | Truthful backend report | [Progressive Poisson](../../tests/contract/runtime/Test.ProgressivePoissonGpuBackend.cpp) |
| Texture bake ([module](../../src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp)) | One raster bake | A frame participant, not a step run | Images owned by the graphics family | — | Bake requests | Texture bake tests |

Texture bake stays on the graphics queue for a reason in its code: it records a graphics
(raster) pipeline into images owned by the graphics family. The compute lane cannot
record that pipeline. Moving the bake would need a compute port, which does not exist.

## CPU counterparts

| Family | Current CPU form | Lifecycle |
| --- | --- | --- |
| Scalar plugins: CPU octree and CPU LBVH | One-shot worker step run of the host (`PointCloudEditingService::StartScalarCpuRun`); its holder is a plugin job on the output | Migrated: the section's `auto_accept` and `start`, the host row, Restart from the capture; Accept publishes once. Valid checks the capture and the editor session, so a detach discards the run before any Accept. [Plugin tests](../../tests/contract/runtime/Test.PointSpacingPlugin.cpp) (common script, detach then `run_control` Accept, unload, ready tool start) |
| Normals, outliers (CPU backends) | One-shot worker step run of the session's service (`MeshSupport::StartOneShotCpuRun`) | Migrated: `Apply` is the batch (`run` and `end`); the panels and `wait: false` start with the section's options. Without the step-run service only the batch runs, as one queued job. [Outlier](../../tests/contract/runtime/Test.OutlierAnalysis.cpp) and [normals](../../tests/contract/runtime/Test.NormalEstimation.cpp) common-script tests |
| Keypoints, CPU KD-tree and CPU LBVH (and the Vulkan LBVH hybrid) | One-shot worker step run of the session's service (`MeshSupport::StartOneShotCpuRun`) | Migrated: `Apply` (and `run_keypoint_analysis`) is the batch; the panel and `run_keypoint_analysis` with `wait: false` start with the section's `auto_accept` and `start` (`StartEditorKeypointAnalysisRun`) and draws the host row; a discard aborts a waiting Vulkan LBVH page at once, the result arrives at the settled end. Without the step-run service only the batch runs, as one queued job. |
| Farthest-point sampling, CPU | Worker step run with Restart | Migrated: `run_point_sampling` is the batch; the panel and `wait: false` start with the section's options |
| Property smoothing, CPU ([smoothing](../../src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp)) | Worker step run (one explicit pass or implicit time step; the variational fit is one step) with Restart | Migrated: `Apply` is the batch; the panel and `run_operation` with `wait: false` start with the section's options. No live preview: a requested `preview_every` is set to 0 and the start message says so. Without the step-run service the batch runs inline and the interactive start is refused. |
| Point construction, CPU reference and CPU LBVH | One-shot worker step run of the session's service (`MeshSupport::StartOneShotCpuRun`) | Migrated: `Apply` is the batch; the panel and `wait: false` start with the section's `auto_accept` and `start` (`StartEditorPointConstructionRun`) and Accept creates the entity. Without the step-run service only the batch runs, as one queued job. [Common-script test](../../tests/contract/runtime/Test.PointConstructionOperations.cpp) |
| K-Means, CPU | Worker step run (seeds on a worker) | Migrated |
| Bilateral plugin, CPU | Worker step run with Restart | Migrated |
| Descriptor plugin, every backend | One-shot worker step run (`StartOneShotWorkerRun`) with Restart | Migrated: its result arrives at the settled end |
| Consolidation, CPU | Worker step run; with a farthest-point initial sampling and a Vulkan `initial_backend` the initial point order first runs as a worker job of bounded device pages (`SubmitInitialOrdering`), as in the hybrid row | Migrated |
| ICP, CPU (k-d tree, LBVH) | Worker step run | Migrated |
| CPD, CPU: runtime and [CPU-only plugin](../../plugins/coherent_point_drift/CoherentPointDrift.Run.cpp) | Step run | Migrated: the plugin uses the rigid CPU reference solver; `coherent_point_drift_start` starts ready and returns its run id. It has no GPU E-step. |
| Template ([`methods/_template`](../../methods/_template/src/TemplateMethod.cpp)) | Iterative worker step run (rows and initial state built by the first worker step of a generation; no live preview, `preview_every` normalized to 0), and a one-shot method on its CPU counterpart and on the scalar GPU producer (one of its existing kernels, chosen by `Params.Method`) | Migrated; [`Test.MethodTemplate.cpp`](../../tests/contract/runtime/Test.MethodTemplate.cpp), and the GPU path in the scalar analysis smoke |

## Shared lifecycle evidence

- The core one-shot behaviours are pinned in
  [`Test.GpuTransactionLifecycleContract.cpp`](../../tests/contract/runtime/Test.GpuTransactionLifecycleContract.cpp):
  ready, Step 1, Stop during device work, Restart into a fresh ring generation, Discard
  while Accept is pending, and refused Accept while a restart waits.
- The driver's behaviours are pinned in
  [`Test.StepRuns.cpp`](../../tests/contract/runtime/Test.StepRuns.cpp), including the one-shot settled end: a
  discard or cancel while Compute is held ends nothing until the step returned and the holder finished, a refused
  cancel still frees the output, a nested settle leaves `Finished` to the outer one, and `Abandon` or a refused start
  never calls it.
- [`OneShotLifecycleScript`](../../tests/support/OneShotLifecycleScript.hpp) runs the same
  assertions on the core, on the real scalar, outlier, point-normal and mesh vertex and face normal transactions
  and the Vulkan construction hybrid, on the CPU step runs of the scalar plugins, outliers, normals and point
  construction, and on the template's one-shot (CPU, and GPU through the scalar producer).
- No row claims measured performance; the GPU rows' tests run only where the Vulkan suite
  runs.

### Verification limits

The CPU construction loop's cancellation at a page boundary has no independently held-page
test. The GPU/hybrid path has that test; its exercised removal is the run's abort signal,
not the loop's boundary check alone. The template's worker-thread test observes its Init;
it does not independently detect moving only row construction back to the owner thread.
GPU semaphore-wait and completion-visibility removal probes have not been run. Existing
known-answer tests exercise these paths, but do not replace those specific probes.

The session-upload wait and `Abandon` clearing `Valid`, `Commit` and `Prepared` have no
individual removal probe either; clearing `Restart` was independently exercised.

The initial farthest-point orders stamp their resident input slots with the compute fence before
they are released. That stamp has no removal probe: an idle pooled farthest-point workspace still
holds its last input view, which keeps the slot leased and masks the probe. A CPD ordering page
in flight at engine shutdown relies on device completion (step segments have no cancel wake-up).
A next ordering page submitted after the pre-drain idle wait is waited for by the job drain;
consolidation's ordering job is woken by the cancel itself. CPD initial ordering has no separate
Jobs-window row or stage text; its time is included in the first step.
Consolidation's ordering has no real-Vulkan no-frame check of its own; the CPD smoke covers the
shared worker transport.

### Remaining implementation differences

K-Means keeps its own stateful page recorder and submit/wait loop: a mid-run reseed orders a
new upload before recording more pages. Its shared run controls are covered by the same lifecycle
checks. New resident methods use `WorkerGpuPages` through the shared transaction core.
Initial farthest-point orders use `PointSamplingGpuRun`'s own worker pages. A runtime CPD run
with a Vulkan subsample backend holds its ordering workspace and resident input from its start to
its settled end (an interactive run included), as its Vulkan E-step workspace is held; an editor
detach ends it as stale ("inputs changed"), like every worker CPD run. Some start tools accept execution
arguments directly, while nonwaiting `run_*` tools read the same validated section settings.
[RUNTIME-355](../../tasks/backlog/runtime/RUNTIME-355-method-execution-retirement-and-consistency.md)
owns these internal simplifications, the obsolete test-only queue APIs and owner-step branches,
and the unprobed removal cases above. They do not change the shared run-control protocol.
