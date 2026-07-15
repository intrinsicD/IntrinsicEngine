# Constraints

## K01: Immediate Geometry Visibility Versus UV-Dependent Texture Binding
- **Constraint**: Meshes may render immediately with default material values, but UV-dependent texture bakes and texture bindings require valid mesh texture coordinates. Missing UVs schedule atlas generation and do not block first geometry visibility.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tasks/archive/RUNTIME-110-progressive-entity-render-data-pipeline.md], [docs/adr/0021-progressive-entity-render-data-pipeline.md]
- **From staging**: O02

## K02: Vertex-Position Dirty Surface Accounting
- **Constraint**: After RUNTIME-124, vertex-position-only mesh dirtiness may update the resident surface geometry in place through `GpuWorld::UpdateGeometryChannels`; surface deferred-retire/free accounting is expected only for full-upload triggers such as topology, vertex-count, storage-layout, or coarse GPU dirtiness. Mesh primitive edge/vertex sidecars still repack on position dirtiness.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Evidence**: [tests/contract/runtime/Test.MeshPrimitiveViewExtraction.cpp], [tasks/archive/RUNTIME-124-per-channel-partial-uploads.md], [tasks/archive/RUNTIME-126-gpu-readback-jobs-and-property-writeback.md]
- **From staging**: O06

## K03: Scheduler Reschedule Handle Ownership
- **Constraint**: A task coroutine handle published to `Scheduler::Reschedule()` is a single-use resumption token. The scheduler may resume it but must not call `done()` or `destroy()` after `resume()` returns; completed task frames self-destroy through `Job::promise_type::final_suspend()` because `await_suspend` may publish the handle to a wait token that another worker resumes and completes before the original resume call unwinds.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/core/Core.Tasks.Dispatch.cpp], [src/core/Core.Tasks.cppm], [tests/unit/core/Test.CoreTasks.cpp], [src/core/README.md], [tasks/archive/BUG-078-coretasks-counterevent-rearm-uaf.md]
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
  tasks/archive/CI-007-module-safe-persistent-ccache-pilot.md]
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
  tasks/archive/BUG-067-jobservice-completion-state-lost-update-race.md]
- **From staging**: O16

## K06: Generated-Texture Failure Cleanup Is Generation-Scoped
- **Constraint**: A post-open GPU-produced-texture failure may retire only the
  exact pending generation returned by `BeginGpuProducedTexture`. Cleanup for
  an absent, promoted, transfer-owned, or mismatched entry fails closed and
  must neither recreate a removed slot nor retire a newer replacement.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/graphics/assets/Graphics.GpuAssetCache.cpp,
  src/runtime/Runtime.ObjectSpaceNormalBakeGpuQueue.cpp,
  tests/unit/graphics/Test.GpuAssetCache.cpp,
  tests/contract/runtime/Test.ObjectSpaceNormalBakeGpuQueue.cpp,
  tasks/archive/BUG-074-object-space-normal-bake-orphaned-cache-slot-livelock.md]
- **From staging**: O17

## K07: Derived-Work Shutdown Must Quiesce Its Registry
- **Constraint**: `AsyncWorkService::ShutdownAndDrain()` must join executor
  work, drain and apply every newly ready derived result, and then cancel every
  non-terminal survivor before returning. Executor quiescence alone does not
  guarantee that readback-gated callbacks cannot resume later.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.AsyncWorkService.cpp,
  tests/contract/runtime/Test.AsyncWorkService.cpp,
  src/runtime/README.md,
  tasks/archive/BUG-076-asyncworkservice-shutdown-skips-derived-job-registry.md]
- **From staging**: O18

## K08: Runtime Module Schedules Finalize After Resolve
- **Constraint**: The runtime simulation-system schedule remains mutable
  through every module `OnResolve` callback and is finalized exactly once only
  after all register- and resolve-phase contributions are present. Boot must
  reject duplicate identities, cycles, and unprovided waits in that complete
  contribution set.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [tests/contract/runtime/Test.RuntimeModule.cpp,
  docs/architecture/runtime.md,
  docs/architecture/feature-module-playbook.md,
  tasks/archive/BUG-071-onresolve-sim-systems-bypass-finalizeforboot.md]
- **From staging**: O19

## K09: Graphics-Recorded Bake Readiness Includes Every In-Flight Frame
- **Constraint**: Under the supported fence-slot reuse model, a texture bake
  recorded into graphics frame `F` cannot become cache-ready before
  `F + FramesInFlight`. A bare CPU `F + 1` stamp may expose the texture before
  the recording frame retires.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.ObjectSpaceNormalBakeService.cpp,
  tests/contract/runtime/Test.ObjectSpaceNormalBakeGpuQueue.cpp,
  src/runtime/README.md,
  tasks/archive/BUG-073-object-space-normal-bake-read-before-gpu-write.md]
- **From staging**: O20

## K10: Abandoned Waits Reclaim Parked Continuations Outside the Wait Lock
- **Constraint**: Releasing a parked Core.Tasks wait transfers its continuation
  handle while holding the wait mutex, clears token ownership once, and
  destroys the detached coroutine frame only after unlocking. Token release or
  scheduler shutdown that merely clears the continuation leaks the frame.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/core/Core.Tasks.WaitToken.cpp,
  src/core/Core.Tasks.Lifecycle.cpp,
  tests/unit/core/Test.CoreTasks.cpp,
  src/core/README.md,
  tasks/archive/BUG-079-coretasks-abandoned-wait-continuation-leak.md]
- **From staging**: O21

## K11: Active-World Handoff Borrowers Rebind Before Prior-World Retirement
- **Constraint**: An active-world switch must rebuild asset/import scene
  borrowers during the switch maintenance pass, before the previous registry
  can retire. Keeping the old borrower through retirement creates a dangling
  scene-registry reference.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.Engine.cpp,
  tests/contract/runtime/Test.RuntimeWorldRegistry.cpp,
  docs/architecture/runtime.md,
  tasks/archive/BUG-068-asset-scene-handoff-not-rebound-on-active-world-change.md]
- **From staging**: O22

## K12: World Destruction Dominates Activation
- **Constraint**: A destroy-pending or destroy-announced world cannot become
  active. Direct activation rejects it, and maintenance revalidates any queued
  activation target as `Live` so request ordering cannot produce an
  active-and-destroying world.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [tests/contract/runtime/Test.RuntimeWorldRegistry.cpp,
  tasks/archive/BUG-075-worldregistry-activate-while-destroy-pending.md]
- **From staging**: O23

## K13: Sandbox Editor Attachments Invalidate Copied Callback Surfaces
- **Constraint**: Detaching a `SandboxEditorSession` invalidates every copied
  command/result callback through an attachment epoch and clears all
  attachment-scoped frame, cache, result, queue, and recipe state before a
  different `Engine` may attach. Stale copied surfaces fail closed instead of
  touching the prior engine or publishing into the new attachment.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.SandboxEditorFacades.cpp,
  tests/contract/runtime/Test.SandboxEditorSessionLifecycle.cpp,
  tests/integration/runtime/Test.SandboxEditorPresentation.cpp,
  tasks/done/ARCH-006-sandbox-editor-content-out-of-runtime.md]
- **From staging**: O26

## K14: Unified Disk Parameterization Fails Closed
- **Constraint**: Unified CPU disk-parameterization dispatch may report
  `Success` only when topology is connected and manifold with exactly one
  boundary loop and Euler characteristic one, numeric inputs are finite and
  valid, and shared diagnostics report usable evaluated faces. Any failed
  precondition or unusable diagnostic state returns no UV payload.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/geometry/Geometry.HalfedgeMesh.Parameterization.cpp,
  tests/unit/geometry/Test.ParameterizationDispatch.cpp,
  tests/unit/geometry/Test.HarmonicParameterization.cpp,
  tasks/done/GEOM-063-unified-cpu-parameterization-strategy-dispatch.md,
  N211, N212]
- **From staging**: O28

## K15: BFF Cone Support Requires a Seam-Aware Result
- **Constraint**: A one-UV-per-original-vertex result cannot represent BFF
  cone cuts that duplicate seam and corner degrees of freedom. Cone support
  remains excluded until a seam-aware chart/cut result contract exists; it
  must not be approximated behind the current result shape.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/geometry/Geometry.Parameterization.Bff.cppm,
  methods/geometry/boundary_first_flattening/README.md,
  tasks/done/METHOD-023-boundary-first-flattening-reference-backend.md,
  N213, N215]
- **From staging**: O31

## K16: Persistent History Cannot Own Session Cache Lifetimes
- **Constraint**: Parameterization undo/redo closures may capture only
  engine-lived scene identity and copied mutation state, never a session-owned
  view-model cache that can be destroyed before the history entry. The active
  cache is invalidated synchronously when the command is applied.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.SandboxParameterizationFacade.cpp,
  tests/contract/runtime/Test.ParameterizationFacade.cpp,
  tasks/done/RUNTIME-176-parameterization-runtime-config-integration.md,
  N214, N215]
- **From staging**: O33

## K17: Algorithm Reconstruction Preserves Deleted Storage Slots
- **Constraint**: Reconstructing geometry-source data for an algorithm must
  preserve deleted vertex tombstones and storage-aligned indexing. Converting
  deleted slots into live isolated vertices changes topology and can reject a
  disk that is valid in the source representation.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Evidence**: [src/runtime/Runtime.SandboxParameterizationFacade.cpp,
  tests/contract/runtime/Test.ParameterizationFacade.cpp,
  tasks/done/RUNTIME-176-parameterization-runtime-config-integration.md,
  N214, N215]
- **From staging**: O34
