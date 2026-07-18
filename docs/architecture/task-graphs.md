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
