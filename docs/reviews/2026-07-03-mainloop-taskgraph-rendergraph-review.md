# Main loop, task graph, and render graph review (2026-07-03)

Scope: `Engine::RunFrame()` and the runtime composition root
(`src/runtime/Runtime.Engine.*`), the core task system and DAG
(`src/core/Core.Tasks.*`, `src/core/Core.Dag.TaskGraph.*`), and the render
graph (`src/graphics/framegraph/*` plus its renderer driver). Review goals:
these systems should be general/abstract (no application-specific code),
tasks should be scheduled and processed asynchronously, and the engine
should run efficiently and non-blocking.

Findings below are the durable record; each carries the task that owns the
fix. High-severity findings were verified by direct source reads.

## What is sound (no task owed)

- The frame-loop skeleton is generic: phases are hook contracts in
  `Core.FrameLoop`, with real extension seams (`IApplication`,
  `SetImGuiEditorCallback`, `StreamingExecutor`, `DerivedJobRegistry`,
  camera/reference-scene/visualization registries, recipe overrides).
- The task scheduler is genuinely asynchronous: work-stealing pool
  (per-worker deques, LIFO pop / FIFO steal, bounded MPMC inject queue),
  128-byte SBO `LocalTask` with zero heap allocation per dispatch, idle
  workers on `atomic::wait`, no lock held while task bodies run.
- `TaskGraph::Execute` is dependency-count-driven (successors scheduled as
  their last dependency retires); topological layers are introspection only.
- The framegraph module has no ECS/runtime/app knowledge and leaks no `Vk*`
  types (verified across all 13 files); compilation is deterministic and
  fail-closed; frame-loop GPU readbacks are poll-based across frames.

## Correctness

- **R1 (high).** `TaskGraph::Execute` waits on a stack-local
  `ExecutionState` (`src/core/Core.Dag.TaskGraph.cpp:999`); a worker's final
  `state.Done.Signal()` CAS releases the waiter before `Signal` touches
  `this->m_Token` (`src/core/Core.Tasks.CounterEvent.cpp:40-45`) and while
  the worker is still inside the stack-captured `onTaskFinished` — a
  latch-destruction use-after-free race. → `BUG-055`.

## Generality / abstractness

- **R2 (high).** K-Means is hardwired into the composition root: Engine owns
  `m_KMeansGpuJobs`, exposes `SubmitKMeansGpuJob`/`ConsumeCompletedKMeansGpuJob`
  as public API, drains its transfers in maintenance, monopolizes the single
  `IRenderer::SetRuntimeFrameCommandHook` slot
  (`src/runtime/Runtime.Engine.cpp:2471-2480`), and adds a K-Means-specific
  shutdown `WaitIdle`. → `RUNTIME-143`.
- **R3 (high).** Method/app config lives in core:
  `Core::Config::EngineConfig` embeds `SandboxConfig.ProgressivePoisson`
  (`src/core/Core.Config.Engine.cppm:57-93`) and Engine hot-apply hand-compares
  its 18 fields (`src/runtime/Runtime.Engine.cpp:3271-3329`). → `CORE-009`.
- **R4 (medium).** The generic import path hardcodes a method step: every
  direct mesh import queues an object-space normal bake with fixed
  `"v:normal"`/64×64 options (`src/runtime/Runtime.Engine.cpp:1293-1373`),
  plus import-time UX policy (camera focus, auto-select, authoring defaults,
  `Runtime.Engine.cpp:3866-3873`) with no post-import extension point.
  → `RUNTIME-144` (bake re-domaining stays with `RUNTIME-129`).
- **R5 (medium).** The application lives in the wrong layer: `app/Sandbox`
  is an empty shell; the 18.5k-line `Runtime.SandboxEditorUi` (K-Means,
  Poisson, registration, remesh panels) sits in `runtime`. → `ARCH-006`.
- **R6 (medium).** Core task vocabulary is domain-flavored: `TaskKind`
  enumerates `AssetIO/PhysicsStep/RenderPass`
  (`src/core/Core.Dag.Scheduler.Types.cppm:32-41`); `Core.Dag.TaskGraph`
  carries GPU/streaming queue budgets, render-graph wording, and the dead
  `ResolveLane` placeholder (`src/core/Core.Dag.TaskGraph.cpp:816-833`).
  → `CORE-006`.
- **R7 (medium).** The renderer's pass set is closed and editor-flavored:
  the only render-graph producer is the fixed default recipe
  (`src/graphics/renderer/Graphics.FrameRecipe.cpp:469-637`) including
  ImGui/SelectionOutline/DebugView passes; the record path special-cases
  features via per-frame resource-name string scans
  (`src/graphics/renderer/Graphics.Renderer.cpp:2590-2663`). → `GRAPHICS-116`.
- **R8 (low).** The `F`-key focus binding is hardcoded in `RunFrame`
  (`src/runtime/Runtime.Engine.cpp:2938-2945`); folded into `RUNTIME-144`'s
  seam scope as UX policy.

## Asynchrony / blocking

- **R9 (high).** `DrainAssetImportEvents` calls the global
  `Core::Tasks::Scheduler::WaitForAll()` mid-frame
  (`src/runtime/Runtime.Engine.cpp:857-864`), reached from Phase-10 import
  applies and editor imports — one import stalls the frame on all unrelated
  background work. → `RUNTIME-140`.
- **R10 (high).** Editor method commands run heavy compute synchronously
  inside `ImGuiAdapter::EndFrame` (K-Means CPU, Poisson, denoise/remesh:
  `src/runtime/Editor/Runtime.SandboxEditorUi.cpp:4286-4308, 4789-4804`);
  the Poisson GPU drain uses `device.ReadBuffer` which performs
  `vkDeviceWaitIdle` per call
  (`src/runtime/Runtime.ProgressivePoissonGpuBackend.cpp:588-601`).
  → `RUNTIME-141` (CPU command lane); the async readback helper is owned by
  `RUNTIME-137`, Poisson GPU parity by `METHOD-014`.
- **R11 (medium).** Model-scene/texture drops and scene save/load do
  synchronous file IO + decode on the main thread
  (`src/runtime/Runtime.Engine.cpp:3645-3661, 4526-4561`); geometry drops
  already use the deferred streaming path. → `RUNTIME-142`.
- **R12 (medium).** `TaskGraph::Execute` blocks the caller with a `yield()`
  spin (`src/core/Core.Dag.TaskGraph.cpp:999-1020`), has no
  submit/poll/completion-token API, and non-CPU `QueueDomain` graphs cannot
  execute at all — so streaming work bypasses the DAG entirely
  (`Runtime.DerivedJobGraph` reimplements dependencies over
  `StreamingExecutor`). → `CORE-005`.
- **R13 (medium).** Render-graph execution is strictly single-threaded
  serial recording (`Graphics.RenderGraph.Executor.cpp:73-111`,
  `Graphics.Renderer.cpp:2789-2809, 2847+`), unintegrated with the task
  system despite per-pass layer data existing. → `GRAPHICS-119`.
- **R14 (low).** `Scheduler::Dispatch` has no priority lanes;
  `TaskGraphPassOptions::Priority` is lost for worker-eligible passes.
  `WaitForAll` help-runs only the inject queue (no stealing);
  per-dispatch `notify_one` fires even with no parked worker; one global
  `waitMutex` serializes all parks/unparks. → `CORE-007`.

## Efficiency

- **R15 (high).** Transient aliasing is virtual-only: `TransientAllocator`
  pools handles without memory placement
  (`src/graphics/framegraph/Graphics.RenderGraph.TransientAllocator.cpp`),
  and the renderer allocates one device resource per transient per
  frame-in-flight slot (`Graphics.Renderer.cpp:6126-6219`) — peak GPU memory
  is `sum(transients) × framesInFlight` with zero aliasing, while the
  lifetime-interval sweep result is discarded. → `GRAPHICS-118`.
- **R16 (high).** Both graphs are rebuilt/recompiled from scratch although
  topology rarely changes: the ECS `FrameGraph` re-registers the same three
  system passes and recompiles per fixed tick
  (`src/runtime/Runtime.Engine.cpp:546-584`); the renderer does
  `Reset → BuildDefaultFrameRecipe → Compile` every frame
  (`Graphics.Renderer.cpp:2180, 2388, 2406`). → `CORE-008` (CPU DAG plan
  cache + adoption) and `GRAPHICS-117` (render-graph compile cache).
- **R17 (medium).** A multi-KB render-graph debug dump string is built
  unconditionally every frame (`Graphics.Renderer.cpp:2497`).
  → `GRAPHICS-117`.
- **R18 (medium).** Framegraph compiler/executor churn: ~35 vectors + name
  string copies per compile, per-edge `std::string` even when empty,
  O(passes×packets) barrier emission rescans, `thread_local` compile
  validation state, `ColorAttachmentRead` treated as a write state,
  duplicated format-size table (BC formats overestimated). → `GRAPHICS-120`.
- **R19 (medium).** Runtime frame-path steady-state waste: full
  `StableEntityLookup::Rebuild` per frame despite incremental APIs
  (`src/runtime/Runtime.Engine.cpp:3022`), `StreamingExecutor` task records
  never reclaimed with O(all-tasks-ever) ready scans
  (`Runtime.StreamingExecutor.cpp:96-124`), unconditional
  `FlushPreRenderTransformState` re-sweeps, per-frame `liveRenderableKeys`
  set allocation (`Runtime.RenderExtraction.cpp:2305`), full geometry payload
  copies during import applies. → `RUNTIME-145` (editor-context rebuild cost
  stays with `RUNTIME-138`).

## Task map

| Task | Owns | Severity |
| --- | --- | --- |
| `BUG-055` | TaskGraph/CounterEvent latch-destruction race (R1) | high |
| `RUNTIME-140` | Remove global `WaitForAll` from import apply path (R9) | high |
| `RUNTIME-141` | Async editor method-command lane (R10) | high |
| `RUNTIME-143` | Multi-subscriber frame hook + K-Means out of Engine (R2) | high |
| `GRAPHICS-118` | Real placed transient aliasing (R15) | high |
| `CORE-005` | Non-blocking TaskGraph submit/completion API (R12) | medium |
| `CORE-006` | Domain-free core task vocabulary (R6) | medium |
| `CORE-007` | Scheduler priority/wait/wake hardening (R14) | low–medium |
| `CORE-008` | Compiled DAG plan reuse across ticks (R16 CPU half) | medium |
| `CORE-009` | App-owned config sections out of core (R3) | medium |
| `RUNTIME-142` | Async model-scene/texture/scene-file IO (R11) | medium |
| `RUNTIME-144` | Post-import processor + import UX seam (R4, R8) | medium |
| `RUNTIME-145` | Runtime frame-path efficiency polish (R19) | medium |
| `ARCH-006` | Sandbox editor content out of runtime (R5) | medium |
| `GRAPHICS-116` | Pass contribution seam + typed record-path lookups (R7) | medium |
| `GRAPHICS-117` | Render-graph compile cache + gated debug dump (R16/R17) | medium |
| `GRAPHICS-119` | Parallel pass command recording (R13) | medium |
| `GRAPHICS-120` | Framegraph compiler/executor efficiency polish (R18) | low–medium |

Ownership boundaries respected: `RUNTIME-129` keeps the normal-bake GPU
re-domaining, `RUNTIME-137` the async readback helper, `RUNTIME-138` the
selected-entity editor cache, `METHOD-014` Poisson GPU parity.
