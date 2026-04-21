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
