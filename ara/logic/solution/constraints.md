# Constraints

## K01: Immediate Geometry Visibility Versus UV-Dependent Texture Binding
- **Constraint**: Meshes may render immediately with default material values, but UV-dependent texture bakes and texture bindings require valid mesh texture coordinates. Missing UVs schedule atlas generation and do not block first geometry visibility.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/done/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O02

## K02: Vertex-Position Dirty Surface Accounting
- **Constraint**: After RUNTIME-124, vertex-position-only mesh dirtiness may update the resident surface geometry in place through `GpuWorld::UpdateGeometryChannels`; surface deferred-retire/free accounting is expected only for full-upload triggers such as topology, vertex-count, storage-layout, or coarse GPU dirtiness. Mesh primitive edge/vertex sidecars still repack on position dirtiness.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tests/contract/runtime/Test.MeshPrimitiveViewExtraction.cpp], [tasks/done/RUNTIME-124-per-channel-partial-uploads.md], [tasks/done/RUNTIME-126-gpu-readback-jobs-and-property-writeback.md]
- **From staging**: O06

## K03: Scheduler Reschedule Handle Ownership
- **Constraint**: A task coroutine handle published to `Scheduler::Reschedule()` is a single-use resumption token. The scheduler may resume it but must not call `done()` or `destroy()` after `resume()` returns; completed task frames self-destroy through `Job::promise_type::final_suspend()` because `await_suspend` may publish the handle to a wait token that another worker resumes and completes before the original resume call unwinds.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/core/Core.Tasks.Dispatch.cpp], [src/core/Core.Tasks.cppm], [tests/unit/core/Test.CoreTasks.cpp], [src/core/README.md], [tasks/active/BUG-078-coretasks-counterevent-rearm-uaf.md]
- **From staging**: O10
