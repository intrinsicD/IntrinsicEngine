# Task Graph Architecture

IntrinsicEngine uses generic Core graph primitives for deterministic dependency
planning and callback execution. Domain meaning belongs to the consuming layer.

## Core contracts

`Extrinsic.Core.Dag.Scheduler` exposes two neutral planning surfaces:

- `DagScheduler` gathers `PendingTaskDesc` records from registered producers.
- `TaskPlanGraph` accepts records directly for callers that already own task
  discovery.

Both surfaces compile explicit dependencies and resource hazards into
`PlanTask { id, topoOrder, batch }`. Core does not assign execution lanes,
carry queue budgets, or define CPU/GPU/streaming queue domains.

`TaskKind` is an opaque, strongly typed numeric token. Core carries the token
without defining a named task taxonomy. Runtime owns the current named
tokens in `RuntimeTaskKinds`, including asset, geometry, physics, and render
work.

`Extrinsic.Core.Dag.TaskGraph` is the closure-based graph. Its
`TaskGraphExecutionMode` is deliberately semantic-neutral:

- `ExecuteCallbacks` enables `Execute()` and is the default.
- `PlanOnly` compiles deterministic metadata while rejecting whole-graph
  callback execution. Individual callback extraction through `ExecutePass()`
  and `TakePassExecute()` remains available to an owning subsystem.

`FrameGraph` is the typed Core facade over the default callback-executing task
graph.

## Submission and completion

`TaskGraph::Submit()` starts dependency-ready callbacks and returns immediately
with a copyable `TaskGraphCompletion`. Submission state and the graph
implementation are shared-owned by the completion handle and scheduled
closures, so a retained handle remains valid even when the submitting
`TaskGraph` object leaves scope. Callers must retain at least one handle until
the submission is ready; otherwise owner-thread-only work has no pump surface
and the surviving graph remains fail-closed as live rather than discarding or
running those callbacks on an arbitrary thread.

The completion contract is:

- `IsReady()` is a thread-safe poll and may be called from any thread.
- `PumpMainThreadPasses()` executes every currently ready `MainThreadOnly` or
  `AllowParallel == false` callback in deterministic priority, estimated-cost,
  and insertion order. It must run on the thread that called `Submit()` and
  returns `ThreadViolation` elsewhere.
- `Wait()` has the same owner-thread requirement. It pumps owner-thread passes,
  help-executes one scheduler task from the inject queues or worker-local
  deques, and parks on a scheduler-work progress epoch when a
  worker-backed graph has no immediately available work. The external
  worker-local scan briefly waits for contended queue critical sections before
  declaring the queues empty.
- `Execute()` is exactly the blocking compatibility form: `Submit()` followed
  by `Wait()`. There is no separate execution path and no yield-spin loop.

The `TaskGraph` control surface—setup, compile, submit, plan extraction, and
reset—requires ordinary external synchronization. Cross-thread access is
provided by `TaskGraphCompletion::IsReady()`; `Wait()` and pumping remain on
the submitting thread. A second submission observed while the first is live is
rejected, but concurrent unsynchronized control calls are not a supported
locking protocol.

When the Core task scheduler is unavailable, ready callbacks remain on the
owner queue; explicit pumping or `Wait()` therefore provides a deterministic
single-thread fallback. `Reset()` returns `InvalidState` while a submission is
live and clears the graph only after completion. `AddPass()` logs and leaves
the graph unchanged rather than mutating callback storage that workers may
read.

Worker-eligible callbacks are dispatched whenever a scheduler exists,
including a one-pass graph or a one-worker scheduler. This is an explicit
consequence of non-blocking submission: `Submit()` does not run arbitrary
worker-eligible callbacks inline. `Execute()` inherits that path; callers that
require owner-thread affinity mark the pass `MainThreadOnly` or disable
`AllowParallel`.

Each worker-backed submission records the scheduler instance that accepted
its callbacks. That scheduler must remain alive until the completion becomes
ready. `Wait()` and `PumpMainThreadPasses()` fail with `InvalidState` when an
unfinished completion observes a missing or replacement scheduler, rather
than parking against work that can no longer run.

Each submission owns a counted `CounterEvent`. The event's internal state is
shared across signal and blocking-wait operations, allowing a signal to notify
progress after publishing a count transition without racing destruction.
No-scheduler waits observe the pending count before checking the owner queue,
then park only while that value is unchanged. Worker-backed waits additionally
observe the scheduler's work-progress epoch before checking owner,
inject, and worker-local queues. Dispatch and task retirement publish that
epoch after their state changes, and the wait path rechecks it after registering
as an external waiter. This closes both completion-change and late-enqueue
queue-check-to-park windows. The final task publishes execution timing and the
idle graph state before it signals completion.

## Ownership

- Core owns identifiers, generic task metadata, dependency and hazard
  compilation, deterministic topological batches, diagnostics, and callback
  execution mechanics.
- Runtime owns task-kind names, persistent streaming state, cancellation,
  background launch policy, and main-thread apply.
- Graphics owns render-pass meaning, GPU resource states, barriers, transient
  lifetimes, and graphics queue selection.
- Runtime owns composition and phase wiring. Lower subsystems expose work
  records or snapshots rather than global scheduling policy.

## Compatibility note

The scheduler still re-exports the historical module partition spelling
`Extrinsic.Core.Dag.Scheduler:DomainGraph` to avoid a module rename. The
partition exports only the neutral `TaskPlanGraph` API; no domain metadata
remains in that surface.

## Related references

- Detailed graph-system boundaries: [task-graph-domains.md](task-graph-domains.md).
- Runtime ownership rules: [runtime.md](runtime.md).
