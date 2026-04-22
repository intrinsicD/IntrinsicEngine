# Core
`src_new/Core` contains the engine foundation modules used by every other
`src_new` subsystem. The package is split into public module interfaces
(`.cppm`) and private implementation units (`.cpp`) that are registered in
`CMakeLists.txt`.
## Public module surface
- `Extrinsic.Core.Config.Window`
- `Extrinsic.Core.Config.Render`
- `Extrinsic.Core.Config.Simulation`  — `SimulationConfig::WorkerThreadCount` for `Tasks::Scheduler`
- `Extrinsic.Core.Config.Engine`
- `Extrinsic.Core.CallbackRegistry`
- `Extrinsic.Core.Dag.Scheduler`
- `Extrinsic.Core.Dag.TaskGraph`      — `ExecutePass(uint32_t)` + `TakePassExecute(uint32_t)` for Streaming/GPU dispatch
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
### Memory partitions
`Extrinsic.Core.Memory` re-exports the following partitions:
- `Extrinsic.Core.Memory:Common`
- `Extrinsic.Core.Memory:LinearArena`
- `Extrinsic.Core.Memory:ScopeStack`
- `Extrinsic.Core.Memory:Polymorphic`
### Task partitions
`Extrinsic.Core.Tasks` re-exports the scheduler and job API, with supporting
partitions for:
- `Extrinsic.Core.Tasks.CounterEvent`
- `Extrinsic.Core.Tasks.Internal`
- `Extrinsic.Core.Tasks.LocalTask`
## Task / FrameGraph architecture
Three scheduled work domains:
| Domain | Module | Engine owner | Use case |
|---|---|---|---|
| `QueueDomain::Cpu` | `Extrinsic.Core.FrameGraph` | `Engine::m_FrameGraph` | ECS system scheduling per sim-tick |
| `QueueDomain::Gpu` | `Extrinsic.Core.Dag.TaskGraph` | `IRenderer` internally | GPU render-pass ordering + Sync2 barriers |
| `QueueDomain::Streaming` | `Extrinsic.Core.Dag.TaskGraph` | `Engine::m_StreamingGraph` | Async asset IO + geometry processing |
`Engine::RunFrame` drives:
- **CPU graph**: `OnSimTick` → `AddPass` → `Compile` → `Execute` → `Reset` (per tick)
- **GPU graph**: `BeginFrame` → `ExecuteFrame` → `EndFrame` (per frame, inside `IRenderer`)
- **Streaming graph**: `TickStreamingGraph()` → `Compile` → `BuildPlan` → `TakePassExecute` → `Scheduler::Dispatch` → `Reset` (Phase 10)
`TakePassExecute(passIndex)` moves a pass closure out of the graph before
`Reset()` so the worker-thread capture outlives the graph's pass storage.
## Files in this directory
### Module interfaces (`.cppm`)
Core.CallbackRegistry.cppm, Core.Config.Engine.cppm, Core.Config.Render.cppm,
Core.Config.Simulation.cppm, Core.Config.Window.cppm, Core.Dag.Scheduler.cppm,
Core.Dag.TaskGraph.cppm, Core.Error.cppm, Core.Filesystem.cppm,
Core.Filesystem.PathResolver.cppm, Core.FrameGraph.cppm, Core.Hash.cppm,
Core.HandleLease.cppm, Core.IOBackend.cppm, Core.LockFreeQueue.cppm,
Core.Logging.cppm, Core.Memory.Common.cppm, Core.Memory.cppm,
Core.Memory.LinearArena.cppm, Core.Memory.Polymorphic.cppm,
Core.Memory.ScopeStack.cppm, Core.Process.cppm, Core.ResourcePool.cppm,
Core.StrongHandle.cppm, Core.Tasks.CounterEvent.cppm, Core.Tasks.cppm,
Core.Tasks.Internal.cppm, Core.Tasks.LocalTask.cppm, Core.Telemetry.cppm
## Notes
- `Extrinsic.Core.HandleLease` — RAII ref-counted handle wrapper. `Lease<H,M>`
  defers the `LeasableManager` concept check to `Reset()` (deferred
  `static_assert`) so the alias can appear inside the manager's own class body
  while it is still an incomplete type.
