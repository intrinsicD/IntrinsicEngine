# Method Package Template

Use `methods/_template/` as the starting point for each new method package.

## Required package files

- `README.md`: scope, backend status, and usage notes.
- `method.yaml`: machine-readable manifest.
- `paper.md`: claim summary and citation metadata.
- `tests/`: correctness and regression test notes/assets.
- `benchmarks/`: benchmark manifests and runner notes.
- `reports/`: validation/performance summaries.

## Required implementation order

1. CPU reference backend.
2. Correctness tests.
3. Benchmark harness/manifests.
4. Optimized CPU backend.
5. GPU backend (only after reference parity).

During intake, spatial-query methods also record the index reuse decision from
the [consumer inventory](../architecture/spatial-index-consumers.md) using the
[method workflow](../agent/method-workflow.md#spatial-acceleration-review).
Specify exact query semantics and missing support before assuming the shared
point LBVH can replace an existing scan, grid, tree or primitive oracle.

## Runtime execution contract

Live device previews use the shared `preview_buffers` execution option: 2 for double buffering or 3
(default) for triple buffering. Config files, UI and agent arguments validate the same range. Pass
the selected capacity at initial reservation and every later acquisition; never grow an active ring
implicitly. An occupied ring skips a preview without stalling computation. `preview_every: 0` makes
no live preview copies. CPU previews without a device ring use no additional buffers, and terminal-only
methods expose no preview-buffer option; their fixed result-ring capacities remain implementation details.
`run_observe.device_preview_buffers` reports the configured capacity of the live device-ring route, or
0 for ringless previews and `preview_every: 0`; `execution.preview_buffers` retains the requested value.
The effective capacity is not an allocated-slot count: allocation is lazy, and terminal result rings may
still exist when live preview is off. A temporarily unavailable device can prevent showing a preview.

Use the existing [job and preview contract](../architecture/engine-and-feature-modules.md#jobs-preview-and-observation-operator-2026-10-30).
The method owns its numerical state; `Runtime.StepRuns` owns its run controls. Do
not copy a scheduler, publication state machine, buffer-retirement loop or MCP
control handler into a new method.

New method plugins must also register a persistent, serializable config section through the
shared validate/apply path and add a scenario to
[`Test.StartupPluginExecution.cpp`](../../tests/integration/runtime/Test.StartupPluginExecution.cpp).
Its generated startup inventory rejects a registration without exactly one compatible
scenario; stale and duplicate entries fail too. Supply real algorithm inputs (including
normal columns, every descriptor output, or separate source/target clouds when needed).
Use `OneShotLifecycleScript` only for one-shot shapes. Iterative methods must retain their
own step semantics. The common test drives the real plugin tool and verifies no publication
at Ready or before Accept, one undo for Accept, restoration by undo, and no publication on
Discard. Set the registered plugin name explicitly in `StepRunOptions::Owner` or the shared
scalar request/run options; job labels are not identities.

Production plugin sources cannot directly call `SubmitPluginJob` or `StartPluginGpuWork`;
use `PluginStepRunJobs` or a shared typed adapter. The source-rule guard covers new plugin
sources automatically. Any temporary exception must name a current task and its exact
missing capability; CPD's persistent-config exception is owned by
[RUNTIME-361](../../tasks/backlog/runtime/RUNTIME-361-coherent-point-drift-execution-completeness.md). Actual GPU and live-preview support require their own backend tests;
a terminal-only method does not implement a fictitious live preview to satisfy the template.

Start from a working consumer whose execution shape matches the method:

| Shape | Source to adapt | Contract tests to retain |
| --- | --- | --- |
| CPU iterative plugin | [CPD run](../../plugins/coherent_point_drift/CoherentPointDrift.Run.cpp) | [StepRuns](../../tests/contract/runtime/Test.StepRuns.cpp), plus the plugin's real input/publication tests |
| Resident GPU iterative method | [Property smoothing](../../src/runtime/Editor/Operations/Runtime.MeshFieldOperations.Smoothing.cpp): worker-stepped `GpuTransactionCore`, shared [`WorkerGpuPages`](../../src/runtime/Services/Runtime.WorkerGpuPages.hpp), ring preview; [farthest-point sampling](../../src/runtime/Editor/Operations/Runtime.PointSamplingOperations.cpp) is the simpler no-preview variant | Run/Step equivalence, pause, stale Accept, cancellation and the method's GPU reference comparison |
| GPU scalar plugin | [Point spacing](../../plugins/point_spacing/PointSpacing.Run.cpp) and [scalar transaction](../../src/runtime/Editor/Operations/Runtime.PointScalarTransaction.cpp) | [Scalar analysis](../../tests/integration/graphics/Test.PointCloudEditingScalarAnalysisGpuSmoke.cpp), plus method-specific values |
| Hybrid GPU neighbour queries (rows on the device, the method's math on the CPU) | `PointCloudEditingService::PrepareGpuRows` ([`Runtime.PointCloudEditing.cppm`](../../src/runtime/Services/Runtime.PointCloudEditing.cppm)): prepared once on the main thread, paged on the run's worker. Its kNN `Width` includes the point itself, unlike the template's self-excluded query: request k + 1 for k neighbours (the bilateral run does). Iterative kNN rows with either `PointQueryStrategy` (`BruteForce` or `Lbvh`): [bilateral run](../../plugins/bilateral_filter/BilateralFilter.Run.cpp). One-shot radius rows (LBVH only): [descriptor run](../../plugins/descriptor_analysis/DescriptorAnalysis.Run.cpp) | [Bilateral plugin GPU smoke](../../tests/integration/graphics/Test.BilateralFilterPluginGpuSmoke.cpp), [descriptor plugin GPU smoke](../../tests/integration/graphics/Test.DescriptorAnalysisPluginGpuSmoke.cpp) |

The [GPU method coverage matrix](gpu-method-coverage.md) records the shared lifecycle,
CPU counterparts and necessary exceptions for every existing GPU or hybrid family.
[ARCH-023](../../tasks/done/ARCH-023-unified-gpu-method-runs.md) records the completed integration and verification.

### Executable template

[`methods/_template/src/TemplateMethod.cpp`](../../methods/_template/src/TemplateMethod.cpp)
is a small working adapter, built into `IntrinsicRuntimeContractTests` and exercised by
[`Test.MethodTemplate.cpp`](../../tests/contract/runtime/Test.MethodTemplate.cpp). Copy it,
then replace its `TemplateMath` callbacks and request. It keeps the lifecycle:

- `Init` (the start) only captures the input through `PointCloudEditingService` on the
  main thread, so a `start: ready` run computes nothing.
- `Step` is one logical iteration on a worker. The first step of each generation first
  builds the neighbour rows with an explicit `TemplateQuery` (the exhaustive
  `KNearestReference` scan or the capture's cached LBVH, with one contract) and the
  initial state, there and not on the main thread. `Run` repeats the same steps up to
  `max_iterations`.
- No live preview: the editing service displays positions only, so a scalar state copy
  would have no consumer. `preview_every` is normalized to 0, which `run_observe` reports,
  and no copy is made (as CPU smoothing does). A copy whose state can be shown adds
  `Preview`, `Show` and `Discard` as the CPD plugin does.
- `Accept` publishes once, as one undo entry, after the service's revision check (the
  generation-safe check). A capture that went stale discards the run. `Discard` publishes
  nothing. `Restart` resets the state from the same capture as a new generation.

The template also has a one-shot method (one logical step: the whole result at once) on
both backends, with one `TemplateOneShotRequest` (its `Params.K` is the one k of the CPU
rows and the GPU producer):

- `StartTemplateOneShotCpuRun` is the CPU counterpart. The copy's `TemplateOneShotCompute`
  runs on a worker over the same query rows, through
  `PointCloudEditingService::StartScalarCpuRun` (a one-shot worker step run). Its holder
  job owns the output while it runs, and Accept publishes the column with one undo entry.
  Its `finished(status, message, ownResult)` is the run's one end, always after the step
  returned, so a copy frees its step's state there and never earlier (a discard arrives
  while the worker may still compute). Without a publication it is the settled end: the
  holder already released the output, and the next run on it may start from inside
  `finished`. For a publication (applied, or refused at Accept), `finished` runs inside the
  holder's completion, so a same-output start is admitted only once `finished` returned. The Compute
  callable captures the capture together with the raw index pointer it reads, so the index
  lease cannot end before the step.
- `StartTemplateOneShotGpuRun` is the GPU path. It adds no kernel or executor: it starts
  the existing point scalar producer (`PointCloudEditingService::StartScalarAnalysis`). It
  runs one of that producer's existing kernels in `Graphics.PointScalarAnalysis`, chosen by
  `Params.Method`, not the copy's `TemplateOneShotCompute`; a copy with its own math adds
  its kernel there. For a GPU query in front of CPU math, start from the hybrid
  neighbour-query row above instead. The returned
  `ScalarAnalysis` handle owns the resident output until Accept, Discard or its
  destruction. The service owns the publication.

The GPU producer embeds `GpuTransactionCore`:

- Its input views, workspace and output ring stay held from the first record until Accept,
  Discard or the end of the run; Restart records again into them.
- Discard during a readback publishes nothing, and storage is freed only after its
  completions.
- Its step run is the same `run_control` target as the CPU run's.

`Test.MethodTemplate.cpp` runs the CPU one-shot through the common lifecycle script and
checks the GPU path's admission without a device. The scalar analysis Vulkan smoke runs the
GPU path through the same script and compares both outputs with the CPU reference.

### One-shot step runs

A one-shot is one logical step, not an exception to the lifecycle. Its device work may
contain bounded pages; point normals, for example, page their PCA rows.
`kOneShotStepRunExecution` (in `Extrinsic.Runtime.StepRunExecution`) fixes the cap at 1 and
makes no live preview copy. Its rings show the result once the step ended. `auto_accept` and
`start` remain its only execution axes; their default, for every one-shot section and
interactive start, is ready and waiting for Accept. `kOneShotBatchExecution` is the batch
(run at once, accept at the end), and `OneShotExecution` normalizes a caller's options.
`StepRunExecutionSchemaProperties(1)`, `ReadStepRunExecutionArguments(..., 1, ...)` and
`DrawStepRunExecutionOptions(..., 1, ...)` expose exactly those two rows.

In `GpuTransactionCore`:

- `Execution` is the one place the run's options live: a one-shot (`GpuTransactionKind::OneShot`) is
  normalized to one step without live preview, keeping its `Start` and automatic accept. `Start = Ready`
  records no device work until `run` or `step 1`. Run and Step 1 compute the same output.
- A Pause takes effect at the end of the step.
- A Stop during the device work keeps that step's result for Accept and suppresses a
  batch's automatic Accept.
- `Hooks.Restart` (the typed reset of one generation) makes the run restartable, even after
  its result. The core drops the device result and its ring generations, then records the
  step again from the captured inputs. The typed owner keeps the input views and
  workspaces it holds and resets only the generation's own state. The core uses a Run job
  of the same run, and the result goes into a fresh ring generation. Accept of the older
  result is refused while the restart waits; a refused restart changes nothing.
- A one-shot's step run holds its transaction until the run ends, so a start that keeps no
  handle (an agent's) still waits for Accept or Discard.
- A worker-stepped transaction (`Pages`) is sealed at once when it ends, and `Hooks.Release`
  runs once no step can run any more: at the end when none is in flight, otherwise at the step
  run's settled end (`StepRunCallbacks::Finished`). That Release also drops what the steps
  captured (the worker's workspace and index references), so a finished handle that a caller
  keeps pins no lease and the next run reuses the pooled workspace. A one-shot's `Poll` reads
  what its step wrote once `GpuTransactionStepCompleted`, so a run that rests already shows its
  result; a failed step ends through its step run.
- `GpuTransactionCurrent` is the one check every Accept passes (a panel's, `run_control`'s,
  a batch's automatic one). It includes the editor session's attachment, so a detached
  session publishes nothing.

A CPU one-shot uses `StartOneShotWorkerRun` (`Runtime.StepRuns`): one step on a worker,
one Commit, a restart computing again from the capture. The scalar plugins reach it through
`PointCloudEditingService::StartScalarCpuRun`; outliers, normals, keypoints and point
construction (whose Publish creates a new entity) through `MeshSupport::StartOneShotCpuRun`;
the descriptor plugin directly. Its end without a
publication (`Ended`) is the run's settled end (`StepRunCallbacks::Finished`): after the
step returned and the holder released the output, with the run's final observation
(`StepFailed`: the step itself failed). Free or reuse the step's state only there, never in
`Discard`; an optional `Abort` stops a waiting device page at once. A start of the same
output from the end is admitted after a cancel; from a publication's answer it may be
refused as busy until the answer returned.

Batch `Apply` entry points select Run and automatic Accept. Interactive starts read the
section's `auto_accept` and `start` rows, the same config the panels, the plugin windows and
the agent starts (`wait: false`, the `*_start` tools, a scalar tool naming either argument)
use; an agent start may name either, replacing the section's for that run only, and a `run_*`
call that waits refuses them. The run id is in every result under the one key `run`.
[`OneShotLifecycleScript`](../../tests/support/OneShotLifecycleScript.hpp) asserts this
sequence. It runs on the shared core, on the real scalar, outlier, point-normal and mesh
vertex and face normal transactions and the Vulkan construction hybrid on the device,
and on the CPU step runs of the scalar plugins, outliers, normals, point construction and the template.

Before adding the next method, make its implementation and tests answer:

- What is one **logical step**, and how is it split into bounded device submissions?
  `Run` repeats those same steps. A reusable query or reduction executes inside its
  caller's run; it does not create another interactive job.
- What is resident after `Init`, and which actual completion protects each buffer
  from reuse? A frame count alone is not proof that separate-queue work finished.
  Scratch comes from `SpatialIndexCache::MethodScratch()` leases (a workspace
  constructed from `(IDevice&, BufferManager&)` with a `ReleaseScratch()` that also
  forgets buffer-derived state); the cache applies the retirement stamp and the
  method budget, and a budget refusal fails the run with its diagnostic. Instantiate a new
  pooled kind once in the engine unit that owns its run, importing the private
  `Extrinsic.Runtime.Private.WorkspaceLease` module. The cache, plugins and tests do not
  instantiate method kinds. A plugin uses a host API or leases raw `MethodScratch()` buffers.
- Which metrics belong to each completed step? Collecting them and rendering a
  preview must not change the numerical stopping rule.
- Can the producer skip an intermediate preview when its output ring is occupied?
  Preview disabled means no intermediate copy and the same final result.
- Which captured revisions and output generation does `Accept` validate? Publish
  exactly once, as one undo step. `Discard` publishes nothing and retains storage
  until all in-flight accesses are complete.
- Which work is genuinely hybrid, how many bytes cross the device boundary, and
  what requested/actual backend and refusal reason do config, UI and MCP expose?
- Which existing device query or parallel primitive meets the method's precision,
  domain, tie-breaking and overflow requirements? Reuse that owner directly.

Exercise these behaviors through the real method binding as well as the shared
driver. A new test must fail when its mechanism is removed. Keep shader numerical
tests separate from lifecycle tests, and measure performance only under the
repository's experiment pre-flight.

## Manifest checklist

A method manifest must follow [method-manifest-schema.md](method-manifest-schema.md) and include:

- Stable ID (`geometry.*`, `rendering.*`, `physics.*`).
- Declared inputs/outputs.
- Declared backends.
- Correctness tests and benchmarks.
- Known limitations.

A method may opt into `max_iterations = 0` only when its Commit publishes the initialized state. Migrations preserve the value range of the replaced parameter unless the operator explicitly changes that capability.
