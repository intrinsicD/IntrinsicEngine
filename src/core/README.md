# Core

`src_new/Core` contains foundational engine modules reused by all other
`src_new` subsystems.

## Public module surface

- `Extrinsic.Core.Config.Window`
- `Extrinsic.Core.Config.Render`
- `Extrinsic.Core.Config.Simulation`
- `Extrinsic.Core.Config.Engine`
- `Extrinsic.Core.CallbackRegistry`
- `Extrinsic.Core.Dag.Scheduler`
- `Extrinsic.Core.Dag.TaskGraph`
- `Extrinsic.Core.Error`
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
- `Extrinsic.Core.ResourcePool`
- `Extrinsic.Core.StrongHandle`
- `Extrinsic.Core.Tasks`
- `Extrinsic.Core.Telemetry`

## Graph APIs and ownership contract

Core owns reusable graph/scheduling primitives, not domain-specific GPU policy.

- **`Extrinsic.Core.Dag.Scheduler`**: shared graph compiler substrate (`TaskId`,
  `ResourceId`, hazard analysis, deterministic topological layering, schedule
  stats, cycle diagnostics).
- **`Extrinsic.Core.Dag.TaskGraph`**: closure-based CPU/streaming task graph API
  with `AddPass`, resource/label declarations, explicit pass dependencies via
  `TaskGraphBuilder::DependsOn`, `Compile`, `BuildPlan`, `Execute`,
  `Reset`, `ExecutePass`, and `TakePassExecute`.
  - Pass options (`TaskGraphPassOptions` / `FrameGraphPassOptions`) provide
    `Priority`, `EstimatedCost`, `MainThreadOnly`, `AllowParallel`, and
    `DebugCategory`.
  - `Execute()` uses graph-local completion (no global scheduler drain), with a
    deterministic single-thread fallback when workers are unavailable.
  - Main-thread-only passes are queued in deterministic ready order (priority,
    then estimated cost, then insertion order) while worker-ready passes keep
    running on scheduler workers.
- **`Extrinsic.Core.FrameGraph`**: ECS-oriented facade over `TaskGraph` with
  typed read/write access declarations plus structural and commit tokens.

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

## Notes

- Core intentionally does **not** expose Vulkan/resource-state/barrier semantics.
  Those remain in `Graphics.RenderGraph`.
- Runtime owns phase orchestration; Graphics owns pass-level GPU behavior.
