# Core

`src/core` contains foundational engine modules reused by all other
engine subsystems.

## Public module surface

- `Extrinsic.Core.Config.Window`
- `Extrinsic.Core.Config.Render`
- `Extrinsic.Core.Config.Simulation`
- `Extrinsic.Core.Config.Engine`
- `Extrinsic.Core.Config.EngineLoad`
- `Extrinsic.Core.BoundedHeap`
- `Extrinsic.Core.IndexedHeap`
- `Extrinsic.Core.CallbackRegistry`
- `Extrinsic.Core.Dag.Scheduler`
- `Extrinsic.Core.Dag.TaskGraph`
- `Extrinsic.Core.Error`
- `Extrinsic.Core.FrameClock`
- `Extrinsic.Core.Filesystem.PathResolver`
- `Extrinsic.Core.Filesystem`
- `Extrinsic.Core.FrameGraph`
- `Extrinsic.Core.Hash`
- `Extrinsic.Core.HandleLease`
- `Extrinsic.Core.IOBackend`
- `Extrinsic.Core.LockFreeQueue`
- `Extrinsic.Core.Logging`
- `Extrinsic.Core.Memory`
- `Extrinsic.Core.Process`
- `Extrinsic.Core.RingBuffer`
- `Extrinsic.Core.ResourcePool`
- `Extrinsic.Core.StrongHandle`
- `Extrinsic.Core.Tasks`
- `Extrinsic.Core.Telemetry`

## Graph APIs and ownership contract

Core owns reusable graph/scheduling primitives, not domain-specific GPU policy.

- **`Extrinsic.Core.IndexedHeap`**: generic indexed binary min-heap with
  O(log n) `DecreaseKey` and `Remove`, deterministic tie-breaks on the value
  token when priorities compare equivalent, and fail-closed empty/absent-value
  operations. Geometry's Dijkstra frontier uses it as a true decrease-key heap;
  shortest-path outputs remain covered against the previous lazy
  priority-queue reference.
- **`Extrinsic.Core.Dag.Scheduler`**: shared domain-free graph compiler
  substrate (`TaskId`, `ResourceId`, opaque numeric `TaskKind` tokens, hazard
  analysis, deterministic topological layering, schedule stats, cycle
  diagnostics). `TaskPlanGraph` is the direct-submit facade and emits only task
  id, topological order, and batch metadata; queue taxonomy, budgets, and lane
  assignment belong to consumers.
- **`Extrinsic.Core.Dag.TaskGraph`**: closure-based generic task graph API
  with `AddPass`, resource/label declarations, explicit pass dependencies via
  `TaskGraphBuilder::DependsOn`, `Compile`, `BuildPlan`, nonblocking `Submit`,
  blocking `Execute`, fail-closed `Reset`, `ExecutePass`, and
  `TakePassExecute`.
  - `TaskGraphExecutionMode::ExecuteCallbacks` is the default and enables
    whole-graph `Execute()`; `PlanOnly` preserves compilation and per-pass
    callback extraction but rejects whole-graph execution.
  - Pass options (`TaskGraphPassOptions` / `FrameGraphPassOptions`) provide
    `Priority`, `EstimatedCost`, `MainThreadOnly`, `AllowParallel`, and
    `DebugCategory`.
  - `Submit()` returns a copyable `TaskGraphCompletion`; `IsReady()` is a
    thread-safe poll, while `PumpMainThreadPasses()` and `Wait()` are restricted
    to the submitting thread and return `ThreadViolation` elsewhere. Retain a
    completion handle until it is ready so owner-only work retains a pump
    surface; dropping the last handle does not migrate owner-only callbacks and
    leaves a surviving graph fail-closed as live.
  - Setup, compile, submit, plan extraction, and reset on the `TaskGraph`
    control object require external synchronization. The completion poll is
    the supported cross-thread observation surface.
  - `Execute()` is a thin `Submit()` + `Wait()` wrapper. Waiting drains ready
    owner-thread passes, help-runs one scheduler task (inject or worker-local
    steal), and parks on completion progress when idle. With no scheduler,
    owner-thread pumping is the deterministic single-thread fallback.
    Worker-eligible callbacks are dispatched even for a one-pass graph or a
    one-worker scheduler so `Submit()` remains non-blocking; explicit
    owner-thread options are the affinity contract.
  - Submission state and the graph implementation are shared-owned by the
    completion handle and dispatched closures. A retained handle can therefore
    safely outlive the `TaskGraph` object; completion signaling cannot observe
    destroyed graph-local state.
  - `Reset()` and a second `Submit()` return `InvalidState` while execution is
    live instead of relying on an assertion. `AddPass()` likewise leaves the
    graph unchanged while live, so callback storage cannot be reallocated
    beneath scheduled workers.
  - A worker-backed completion records the scheduler instance that accepted
    it. The instance must remain alive until readiness; unfinished waits and
    pumps fail closed after scheduler replacement.
  - Main-thread-only passes are queued in deterministic ready order (priority,
    then estimated cost, then insertion order) while worker-ready passes keep
    running on scheduler workers.
- **`Extrinsic.Core.FrameGraph`**: ECS-oriented facade over `TaskGraph` with
  typed read/write access declarations plus structural and commit tokens.
- **`Extrinsic.Core.FrameClock`**: reusable steady-clock helper that exposes the
  prior completed-frame duration as a non-negative clamped delta, records the
  current frame at `EndFrame()`, and supports explicit resampling after
  deliberate sleeps.

### Graph stats and diagnostics

`TaskGraph::GetScheduleStats()` / `FrameGraph::GetScheduleStats()` expose:

- task/edge counts split by explicit vs hazard edges;
- topological layer count and max ready-queue depth;
- critical-path cost estimate;
- compile/execute timing; and
- last diagnostic text (including cycle details).

### Scheduler partition exports

`Extrinsic.Core.Dag.Scheduler` re-exports:

- `Extrinsic.Core.Dag.Scheduler:Types`
- `Extrinsic.Core.Dag.Scheduler:Hazards`
- `Extrinsic.Core.Dag.Scheduler:Policy`
- `Extrinsic.Core.Dag.Scheduler:Compiler`
- `Extrinsic.Core.Dag.Scheduler:DomainGraph`

The `:DomainGraph` partition spelling is retained for module compatibility,
but it exports the domain-free `TaskPlanGraph` API.

### Memory partition exports

`Extrinsic.Core.Memory` re-exports:

- `Extrinsic.Core.Memory:Common`
- `Extrinsic.Core.Memory:LinearArena`
- `Extrinsic.Core.Memory:ScopeStack`
- `Extrinsic.Core.Memory:Polymorphic`

### Task partition exports

`Extrinsic.Core.Tasks` re-exports:

- `Extrinsic.Core.Tasks.CounterEvent`
- `Extrinsic.Core.Tasks.Internal`
- `Extrinsic.Core.Tasks.LocalTask`

`CounterEvent` shared-owns its count and scheduler wait token internally.
`Signal()` retains that state while it publishes and notifies each count
transition, so `WaitForProgress()` can park a blocking thread without a
signal-versus-destruction race. Blocking callers capture `PendingCount()`
before checking their work queues and wait only while that value is unchanged,
closing the queue-check-to-park lost-wake window. Coroutine waiters keep the
existing wait-token park/unpark contract, and destroying the public event
releases the token after the last in-progress signal or blocking wait retires.
Wait tokens include a scheduler-instance identity, so destroying an event
retained across scheduler shutdown cannot release a colliding slot in a later
scheduler instance.

`Scheduler::TryRunOne()` is the neutral external-help seam used by graph
completion waits. It executes at most one task on the caller, checking inject
work and then worker-local deques; it does not impose graph or runtime domain
policy.

Task coroutine handles published to the scheduler are single-use resumption
tokens. `Scheduler::Reschedule()` resumes a handle but must not inspect
`done()` or destroy the frame after `resume()` returns, because the coroutine
may park, another worker may unpark it, and the frame may complete before the
original resume call unwinds. Completed task frames self-destroy through the
task promise's non-suspending final suspend.

Parked continuations are cancelled, not resumed, when their wait token is
released. The wait registry transfers their single-use handles under its mutex,
then destroys the coroutine frames after unlocking; this prevents frame
destructors from running inside registry synchronization. Scheduler shutdown
first joins all workers, transfers every continuation still parked in the wait
registry, and destroys those frames before releasing the scheduler context.
Signal/unpark and cancellation therefore compete for the same registry-owned
token, so exactly one path can resume or destroy each frame.

## Engine config fields

`Extrinsic.Core.Config.Engine` exports `EngineConfig`, the value type runtime
consumes at composition time. Per-field ownership is:

- `Render`, `Simulation`, `Window` — see the per-partition modules.
- `Window.Backend` — defaults to `WindowBackend::Configured`, preserving the
  platform backend selected by CMake. Tests can set `WindowBackend::Null` to
  force the deterministic headless platform window without importing platform
  modules into core config.
- `ReferenceScene { Enabled, Selector }` — opt-in seam consumed by
  `Runtime::Engine::Initialize()` to populate a deterministic reference
  renderable through the runtime-owned `Extrinsic.Runtime.ReferenceScene`
  registry. `EngineConfig{}` defaults to `Enabled = false` so existing
  CPU/null tests observe zero renderable candidates;
  `Runtime::CreateReferenceEngineConfig()` flips it to
  `true`/`ReferenceSceneSelector::Triangle`. The provider implementation
  lives in `runtime`; `core` only carries the value-type enum
  `ReferenceSceneSelector` so this header stays free of runtime/graphics
  imports.

`Extrinsic.Core.Config.EngineLoad` is the adjacent parse/serialize lane for
that value type. It exports the versioned JSON schema id
`intrinsic.core.engine-config`, side-effect-free `PreviewEngineConfig(...)`,
`LoadEngineConfigFile(...)`, `SerializeEngineConfig(...)`, and typed
diagnostics. The loader starts from caller-provided defaults, ignores invalid or
unknown fields with `FallbackApplied` diagnostics, and keeps the value-type
`EngineConfig` module free of IO imports. The schema and boot-only field
partition are documented in
[`docs/architecture/engine-config.md`](../../docs/architecture/engine-config.md).

## Notes

- Core intentionally does **not** expose Vulkan/resource-state/barrier semantics.
  Those remain in `Graphics.RenderGraph`.
- Runtime owns phase orchestration; Graphics owns pass-level GPU behavior.
- Core intentionally does **not** promote the legacy global
  `Core.Commands`, `Core.FeatureRegistry`, or `Core.SystemFeatureCatalog`
  model. `CORE-002` retired that catalog shape: command history and dirty state
  are runtime/editor responsibilities, while render passes, ECS systems,
  panels, geometry operators, shader reload, and GPU-memory policy stay with
  their owning layers.
- `Extrinsic.Core.BoundedHeap`, `Extrinsic.Core.RingBuffer`, and
  `Extrinsic.Core.Telemetry` are the retained dependency-free utility and
  instrumentation seams used by promoted consumers.
