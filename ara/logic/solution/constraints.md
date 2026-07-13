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
- **Evidence**: [src/core/Core.Tasks.Dispatch.cpp], [src/core/Core.Tasks.cppm], [tests/unit/core/Test.CoreTasks.cpp], [src/core/README.md], [tasks/done/BUG-078-coretasks-counterevent-rearm-uaf.md]
- **From staging**: O10

## K04: Coarse module-interface cache invalidation
- **Constraint**: The retained CI ccache path disables direct and depend modes
  and hashes a deterministic digest of every repository `.cppm` through
  `CCACHE_EXTRAFILES`. Any module-interface edit therefore invalidates all
  eligible cached C++ consumers, deliberately trading reuse precision for a
  fail-closed module-safety boundary; `.cppm` compilation itself remains
  compiler pass-through.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [ara/evidence/tables/ci007_ccache_cohort.md,
  tools/ci/ccache_ci.py, .github/workflows/pr-fast.yml,
  tasks/done/CI-007-module-safe-persistent-ccache-pilot.md]
- **From staging**: O14

## K05: Completion Queue Publication Is a One-Way State Handoff
- **Constraint**: A JobService worker must store `AwaitingGate` before its
  completion record becomes visible in the queue and must perform no later
  state write. After queue publication, only `DrainCompletions` may advance the
  record to terminal `Published`, `Dropped`, or `Cancelled` state.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.JobService.cpp,
  tests/contract/runtime/Test.RuntimeJobService.cpp,
  src/runtime/README.md,
  tasks/done/BUG-067-jobservice-completion-state-lost-update-race.md]
- **From staging**: O16
