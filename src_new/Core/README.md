# Core

`src_new/Core` contains the engine foundation modules used by every other
`src_new` subsystem. The package is split into public module interfaces
(`.cppm`) and private implementation units (`.cpp`) that are registered in
`CMakeLists.txt`.

## Public module surface

- `Extrinsic.Core.Config.Window`
- `Extrinsic.Core.Config.Render`
- `Extrinsic.Core.Config.Engine`
- `Extrinsic.Core.CallbackRegistry`
- `Extrinsic.Core.Dag.Scheduler`
- `Extrinsic.Core.Dag.TaskGraph`
- `Extrinsic.Core.Error`
- `Extrinsic.Core.Filesystem.PathResolver`
- `Extrinsic.Core.Filesystem`
- `Extrinsic.Core.FrameGraph`
- `Extrinsic.Core.Hash`
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

> **Note:** Allocation telemetry (formerly `Memory:Telemetry`) was promoted
> to `Extrinsic.Core.Telemetry::Alloc` to avoid a circular dependency
> (`Telemetry → Tasks → Memory → Telemetry`). Allocators call
> `Core::Telemetry::Alloc::RecordAlloc()` directly.

### Task partitions

`Extrinsic.Core.Tasks` re-exports the scheduler and job API, with supporting
partitions for:

- `Extrinsic.Core.Tasks.CounterEvent`
- `Extrinsic.Core.Tasks.Internal`
- `Extrinsic.Core.Tasks.LocalTask`

## Task / FrameGraph architecture

Three scheduled work domains (from the architecture spec):

| Domain | Module | Use case |
|---|---|---|
| `QueueDomain::Cpu` | `Extrinsic.Core.FrameGraph` (wraps `TaskGraph`) | ECS system scheduling — TypeToken component deps, fiber-dispatched closures |
| `QueueDomain::Gpu` | `Extrinsic.Core.Dag.TaskGraph` directly | GPU render-pass ordering — virtual resource barriers, `BuildPlan()` drives recording |
| `QueueDomain::Streaming` | `Extrinsic.Core.Dag.TaskGraph` directly | Background IO / geometry — priority queues, `BuildPlan()` drives async dispatch |

`DomainTaskGraph` (previously in `Dag.Scheduler`) was replaced by
`Extrinsic.Core.Dag.TaskGraph::TaskGraph`, which adds closure storage
(`std::move_only_function<void()>`), TypeToken-based resource access
translation, label-based ordering, and a 3-phase `AddPass → Compile →
Execute/BuildPlan` API.

`Extrinsic.Core.FrameGraph` is the ECS-specific adapter: it wraps a CPU-domain
`TaskGraph` and layers `TypeToken<T>` component dependency declarations on top
via a `FrameGraphBuilder` helper.

## Files in this directory

### Module interfaces (`.cppm`)

```text
Core.CallbackRegistry.cppm
Core.Config.Engine.cppm
Core.Config.Render.cppm
Core.Config.Window.cppm
Core.Dag.Scheduler.cppm
Core.Dag.TaskGraph.cppm
Core.Error.cppm
Core.Filesystem.cppm
Core.Filesystem.PathResolver.cppm
Core.FrameGraph.cppm
Core.Hash.cppm
Core.HandleLease.cppm
Core.IOBackend.cppm
Core.LockFreeQueue.cppm
Core.Logging.cppm
Core.Memory.Common.cppm
Core.Memory.cppm
Core.Memory.LinearArena.cppm
Core.Memory.Polymorphic.cppm
Core.Memory.ScopeStack.cppm
Core.Process.cppm
Core.ResourcePool.cppm
Core.StrongHandle.cppm
Core.Tasks.CounterEvent.cppm
Core.Tasks.cppm
Core.Tasks.Internal.cppm
Core.Tasks.LocalTask.cppm
Core.Telemetry.cppm
```

### Private implementation units (`.cpp`)

```text
Core.Dag.Scheduler.cpp
Core.Dag.TaskGraph.cpp
Core.Filesystem.PathResolver.cpp
Core.Filesystem.cpp
Core.FrameGraph.cpp
Core.IOBackend.cpp
Core.Logging.cpp
Core.Memory.LinearArena.cpp
Core.Memory.Polymorphic.cpp
Core.Memory.ScopeStack.cpp
Core.Process.cpp
Core.Tasks.CounterEvent.cpp
Core.Tasks.Dispatch.cpp
Core.Tasks.Internal.cpp
Core.Tasks.Job.cpp
Core.Tasks.Lifecycle.cpp
Core.Tasks.LocalTask.cpp
Core.Tasks.Stats.cpp
Core.Tasks.WaitToken.cpp
Core.Tasks.Worker.cpp
Core.Telemetry.cpp
```

## Notes

- `Extrinsic.Core.Error` — shared `ErrorCode`, `Expected<T>`, `Result`.
- `Extrinsic.Core.StrongHandle` — generational handle types and hashers.
- `Extrinsic.Core.Memory` — arena- and stack-based allocators.
- `Extrinsic.Core.Tasks` — fiber scheduler (`Scheduler`) and coroutine `Job`.
- `Extrinsic.Core.Telemetry` — frame/GPU/pass timing + allocation counters.
  `Telemetry::Alloc::RecordAlloc()` is called by `Memory:LinearArena`.
- `Extrinsic.Core.Process` — safe `posix_spawnp`-based process spawning.
  Used by shader hot-reload and toolchain invocations. Never use `std::system()`.
- `Extrinsic.Core.IOBackend` — abstract IO backend. `FileIOBackend` is Phase 0.
  The `PathKey` struct is an IO-layer path hash, distinct from `Assets::AssetId`.
- `Extrinsic.Core.ResourcePool` — thread-safe generational pool with deferred
  GPU-safe slot reclamation. `RetirementFrames` controls the GPU retirement lag.
- `Extrinsic.Core.Dag.TaskGraph` — general per-domain task graph for all 3
  execution domains. `TaskGraphBuilder` handles both TypeToken and ResourceId deps.
- `Extrinsic.Core.FrameGraph` — ECS system scheduler. Wraps CPU-domain TaskGraph
  with `TypeToken<T>` component dependency declarations.


`src_new/Core` contains the engine foundation modules used by every other
`src_new` subsystem. The package is split into public module interfaces
(`.cppm`) and private implementation units (`.cpp`) that are registered in
`CMakeLists.txt`.

## Public module surface

- `Extrinsic.Core.Config.Window`
- `Extrinsic.Core.Config.Render`
- `Extrinsic.Core.Config.Engine`
- `Extrinsic.Core.CallbackRegistry`
- `Extrinsic.Core.Dag.Scheduler`
- `Extrinsic.Core.Error`
- `Extrinsic.Core.Filesystem.PathResolver`
- `Extrinsic.Core.Filesystem`
- `Extrinsic.Core.Hash`
- `Extrinsic.Core.LockFreeQueue`
- `Extrinsic.Core.Logging`
- `Extrinsic.Core.Memory`
- `Extrinsic.Core.StrongHandle`
- `Extrinsic.Core.Tasks`

### Memory partitions

`Extrinsic.Core.Memory` re-exports the following partitions:

- `Extrinsic.Core.Memory:Common`
- `Extrinsic.Core.Memory:LinearArena`
- `Extrinsic.Core.Memory:ScopeStack`
- `Extrinsic.Core.Memory:Polymorphic`
- `Extrinsic.Core.Memory:Telemetry`

### Task partitions

`Extrinsic.Core.Tasks` re-exports the scheduler and job API, with supporting
partitions for:

- `Extrinsic.Core.Tasks.CounterEvent`
- `Extrinsic.Core.Tasks.Internal`
- `Extrinsic.Core.Tasks.LocalTask`

## Files in this directory

### Module interfaces (`.cppm`)

```text
Core.CallbackRegistry.cppm
Core.Config.Engine.cppm
Core.Config.Render.cppm
Core.Config.Window.cppm
Core.Dag.Scheduler.cppm
Core.Error.cppm
Core.Filesystem.cppm
Core.Filesystem.PathResolver.cppm
Core.Hash.cppm
Core.LockFreeQueue.cppm
Core.Logging.cppm
Core.Memory.Common.cppm
Core.Memory.cppm
Core.Memory.LinearArena.cppm
Core.Memory.Polymorphic.cppm
Core.Memory.ScopeStack.cppm
Core.Memory.Telemetry.cppm
Core.StrongHandle.cppm
Core.Tasks.CounterEvent.cppm
Core.Tasks.cppm
Core.Tasks.Internal.cppm
Core.Tasks.LocalTask.cppm
```

### Private implementation units (`.cpp`)

```text
Core.Dag.Scheduler.cpp
Core.Filesystem.PathResolver.cpp
Core.Filesystem.cpp
Core.Logging.cpp
Core.Memory.LinearArena.cpp
Core.Memory.Polymorphic.cpp
Core.Memory.ScopeStack.cpp
Core.Memory.Telemetry.cpp
Core.Tasks.CounterEvent.cpp
Core.Tasks.Dispatch.cpp
Core.Tasks.Internal.cpp
Core.Tasks.Job.cpp
Core.Tasks.Lifecycle.cpp
Core.Tasks.LocalTask.cpp
Core.Tasks.Stats.cpp
Core.Tasks.WaitToken.cpp
Core.Tasks.Worker.cpp
```

## Notes

- `Extrinsic.Core.Error` defines the shared `ErrorCode`, `Expected<T>`, and
  `Result` aliases used by the rest of the engine.
- `Extrinsic.Core.StrongHandle` provides generational handle types and hashers
  for resource and callback registries.
- `Extrinsic.Core.Memory` contains arena- and stack-based allocators plus
  telemetry helpers for frame-local allocation patterns.
- `Extrinsic.Core.Tasks` exposes the fiber scheduler (`Scheduler`) and
  coroutine job type (`Job`) used by higher-level runtime systems.

## Partition structure — canonical example

`Extrinsic.Core.Memory` and `Extrinsic.Core.Tasks` are the canonical examples
of the `src_new` module partition contract (see `CLAUDE.md` → "Module
partitions — internal structure contract"):

- One umbrella interface (`Core.Memory.cppm`, `Core.Tasks.cppm`) that consumers
  import. It contains only re-exports.
- One partition per internal concern, each with a single responsibility
  (`:LinearArena`, `:ScopeStack`, `:Polymorphic`, `:Telemetry`, `:Common`;
  `:Internal`, `:LocalTask`).
- Partition implementation bodies live in matching `.cpp` units attached to
  the same partition — they do not leak into the interface.
- Internal/detail partitions are imported by sibling partitions but not
  re-exported from the umbrella, so consumers cannot reach them.

Treat this shape as the default for every new `Core` module. When the shape
does not fit (very small, genuinely single-responsibility module), a single
`.cppm` / `.cpp` pair is acceptable — but default to partitions.
