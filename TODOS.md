# Codex TODOs: `src_new` Task Graphs, Render Graph, and Streaming Graph

This document is the implementation backlog for Codex. It converts the architectural plan into ordered, reviewable tasks with explicit acceptance criteria, required tests, and quality gates.

## Non-negotiable implementation rules

- Work in small, reviewable increments. Do not implement multiple phases in one giant patch.
- Before each phase, audit the current repository state. If something is already implemented, verify it against the acceptance criteria and tests instead of duplicating it.
- Preserve the `src_new` layering contract:
  - `Core` owns reusable graph compilation, CPU task scheduling primitives, errors, logging, telemetry, memory, and task synchronization.
  - `Graphics.RenderGraph` owns GPU virtual resources, render/compute pass DAGs, layout/state transitions, barriers, transient resource lifetimes, and aliasing.
  - `Runtime` owns orchestration only. `Runtime::Engine` must not reason about GPU resources, barriers, Vulkan objects, or pass-level render details.
  - `Assets` must not import `Graphics`. GPU-side asset state lives in Graphics-owned cache/state once that bridge exists.
- Do not directly copy the legacy `src` implementation. Use it only for ideas and test coverage. The `src_new` design must be clean, modular, and domain-specific.
- Keep deterministic single-thread fallback behavior for every graph executor.
- Prefer narrow module imports in `.cpp` implementation units. Avoid importing umbrella modules when a smaller partition is enough.
- Keep public APIs small. Add partitions for internal structure instead of stuffing everything into one `.cppm` file.
- Use repository error/result conventions. Do not introduce exceptions.
- Do not use a global `WaitForAll()` as the normal wait path for a graph. CPU graph execution must use graph-local completion tokens so unrelated streaming work is not waited on accidentally.
- No hidden ordering by insertion except as a deterministic tie-break among otherwise independent passes.
- Every new subsystem must get focused tests in the matching `tests/<Subsystem>/CMakeLists.txt` object library.
- After adding/removing `src_new` modules, regenerate `docs/architecture/src_new_module_inventory.md` with `tools/generate_src_new_module_inventory.py`.

## Global build and test commands

Use these commands as the default validation baseline unless a phase specifies something narrower:

```bash
cmake --preset dev
cmake --build --preset dev --target IntrinsicTests
./build/dev/bin/IntrinsicTests
```

Focused targets to add/use as work progresses:

```bash
cmake --build --preset dev --target ExtrinsicCoreTests
./build/dev/bin/ExtrinsicCoreTests

cmake --build --preset dev --target ExtrinsicGraphicsTests
./build/dev/bin/ExtrinsicGraphicsTests

# Add this target when Runtime tests are introduced.
cmake --build --preset dev --target ExtrinsicRuntimeTests
./build/dev/bin/ExtrinsicRuntimeTests
```

Where a target name differs in the local checkout, update the commands in the PR notes and make sure the CMake test target remains discoverable through `ctest`.

---

# Phase 0 — Pre-flight audit and architecture contract

## T000 — Baseline repository audit

**Goal:** Establish exactly what exists before changing behavior.

**Tasks:**

- [ ] Inspect `src_new/Core/Core.Dag.Scheduler.*`.
- [ ] Inspect `src_new/Core/Core.Dag.TaskGraph.*`.
- [ ] Inspect `src_new/Core/Core.FrameGraph.*`.
- [ ] Inspect `src_new/Runtime/Runtime.Engine.*`.
- [ ] Inspect `src_new/Graphics/Graphics.Renderer.*`.
- [ ] Inspect `src_new/Graphics/RHI/*CommandContext*`, `RHI.Types`, `RHI.Device`, and backend command APIs.
- [ ] Inspect `tests/Core/CMakeLists.txt`, `tests/Graphics/CMakeLists.txt`, and whether `tests/Runtime` exists.
- [ ] Record in the PR description which tasks are missing, partial, or already complete.

**Acceptance criteria:**

- [ ] The PR notes include a concise current-state matrix for Core scheduler, TaskGraph, FrameGraph, Runtime streaming, and Graphics renderer.
- [ ] No behavior changes are made in this task.

**Validation:**

- [ ] `cmake --preset dev`
- [ ] `cmake --build --preset dev --target IntrinsicTests`
- [ ] `./build/dev/bin/IntrinsicTests`

## T001 — Add task-graph architecture document

**Goal:** Make the intended design explicit before implementation.

**Files:**

- [ ] Add `docs/architecture/src_new-task-graphs.md`.
- [ ] Update `src_new/Core/README.md` if public Core graph APIs change.
- [ ] Update `src_new/Graphics/README.md` when `Graphics.RenderGraph` is added.
- [ ] Update `src_new/Runtime/README.md` when runtime integration changes.

**Document must define:**

- [ ] CPU task graph vs GPU render graph vs async streaming graph.
- [ ] Shared graph compiler substrate.
- [ ] Why `Graphics.RenderGraph` is not just `Core.TaskGraph` with `QueueDomain::Gpu`.
- [ ] Graph lifecycle states: `Recording -> Compiled -> Executing/Consumed -> Reset`.
- [ ] Resource hazard semantics: RAW, WAW, WAR, RAR.
- [ ] Label signal/wait semantics.
- [ ] CPU execution contract and graph-local completion.
- [ ] Streaming persistence/cancellation contract.
- [ ] GPU render graph resource, barrier, and aliasing contract.
- [ ] Phase boundaries in `Engine::RunFrame`.
- [ ] Test strategy and review gates.

**Acceptance criteria:**

- [ ] The document is specific enough that a contributor can implement the APIs without reading legacy `src` internals.
- [ ] The document explicitly forbids runtime pass-level render branching and GPU-resource manipulation in `Runtime`.
- [ ] The document lists required tests for Core, Runtime/Streaming, and Graphics.

**Review gate RG-00:**

- [ ] Docs reviewed for layering correctness.
- [ ] No code behavior changed.
- [ ] Full test suite still passes.

---

# Phase 1 — Shared Core graph compiler substrate

## T010 — Introduce graph compiler types and partitions

**Goal:** Create a reusable compiler substrate in `Core` without forcing GPU semantics into Core.

**Files to add or split:**

- [ ] `src_new/Core/Core.Dag.Scheduler.Types.cppm`
- [ ] `src_new/Core/Core.Dag.Scheduler.Hazards.cppm`
- [ ] `src_new/Core/Core.Dag.Scheduler.Compiler.cppm`
- [ ] `src_new/Core/Core.Dag.Scheduler.Policy.cppm`
- [ ] `src_new/Core/Core.Dag.Scheduler.DomainGraph.cppm`
- [ ] Matching `.cpp` implementation units as needed.
- [ ] Update `src_new/Core/Core.Dag.Scheduler.cppm` to re-export the partitions.
- [ ] Update `src_new/Core/CMakeLists.txt`.

**Public/minimally public types:**

- [ ] `TaskId`
- [ ] `ResourceId`
- [ ] `LabelId`
- [ ] `ResourceAccess`
- [ ] `ResourceAccessMode`
- [ ] `TaskPriority`
- [ ] `QueueDomain`
- [ ] `TaskKind`
- [ ] `PendingTaskDesc`
- [ ] `PlanTask`
- [ ] `ExecutionLayer`
- [ ] `BuildConfig`
- [ ] `ScheduleStats`
- [ ] `CompiledGraph` or equivalent immutable compiled-plan type.

**Implementation details:**

- [ ] Keep stable generation/index handle semantics where they already exist.
- [ ] Keep existing public APIs source-compatible where practical.
- [ ] Do not expose GPU layouts, Vulkan concepts, texture usages, or barrier details from Core.
- [ ] Make `PlanTask::batch` mean topological layer, not priority.
- [ ] Preserve `TaskPriority` separately from topo layer.
- [ ] Preserve deterministic plan ordering.

**Tests to add:**

- [ ] `tests/Core/Test.Core.GraphCompiler.cpp`
  - [ ] Empty graph compiles.
  - [ ] Single-node graph compiles.
  - [ ] Independent nodes produce zero dependency edges.
  - [ ] Explicit dependencies produce valid topological order.
  - [ ] Missing explicit dependency returns `InvalidArgument` or existing equivalent error.
  - [ ] Duplicate `TaskId` returns `InvalidArgument`.
  - [ ] Priority ordering is stable among ready nodes.
  - [ ] `batch` values are actual topological layers.
  - [ ] Deterministic ordering repeated over at least 100 compiles.

**Acceptance criteria:**

- [ ] Existing scheduler users still compile.
- [ ] Public API churn is minimal and documented.
- [ ] New tests are registered in `tests/Core/CMakeLists.txt`.

## T011 — Implement `ResourceHazardBuilder`

**Goal:** Centralize resource dependency edge construction for CPU/streaming-style graphs.

**Required behavior:**

For each resource, maintain:

```cpp
struct ResourceState {
    uint32_t LastWriter = InvalidNode;
    SmallVector<uint32_t, 4> CurrentReaders;
};
```

Hazard rules:

- [ ] `Read(node, R)`:
  - [ ] If `LastWriter(R)` exists, emit `LastWriter(R) -> node`.
  - [ ] Add `node` to `CurrentReaders(R)`.
- [ ] `WeakRead(node, R)`:
  - [ ] If `LastWriter(R)` exists, emit `LastWriter(R) -> node`.
  - [ ] Do not add `node` to `CurrentReaders(R)`.
- [ ] `Write(node, R)`:
  - [ ] If `LastWriter(R)` exists, emit `LastWriter(R) -> node`.
  - [ ] For each reader in `CurrentReaders(R)`, emit `reader -> node`.
  - [ ] Clear `CurrentReaders(R)`.
  - [ ] Set `LastWriter(R) = node`.
- [ ] Deduplicate emitted edges.
- [ ] Keep RAR parallelism: two pure readers of the same resource must not create an edge.

**Tests to add in `Test.Core.ResourceHazards.cpp` or `Test.Core.GraphCompiler.cpp`:**

- [ ] RAW: write then read serializes writer before reader.
- [ ] WAW: write then write serializes first writer before second writer.
- [ ] WAR: read then write serializes reader before writer.
- [ ] RAR: read then read remains parallel/same layer.
- [ ] WeakRead then Write does not force writer to wait for weak reader.
- [ ] Write then WeakRead forces weak reader after writer.
- [ ] Multiple readers then writer emits all reader edges.
- [ ] Duplicate accesses do not duplicate edge count.
- [ ] 10,000-node hazard stress test completes deterministically.

**Acceptance criteria:**

- [ ] `PendingTaskDesc::resources` affects scheduling.
- [ ] `DomainTaskGraph` no longer ignores resource declarations.
- [ ] Edge count in `ScheduleStats` includes hazard edges.

## T012 — Implement real label signal/wait handling

**Goal:** Labels must be first-class dependency constructs, not fake resources.

**Required API/behavior:**

- [ ] Add or preserve `Signal(LabelId/StringID)`.
- [ ] Add or preserve `WaitFor(LabelId/StringID)`.
- [ ] A wait must depend on all earlier signalers of the same label.
- [ ] Multiple signalers are allowed as fan-in, but diagnostics must make this visible when useful.
- [ ] A wait with no known signaler must not crash. Choose and document one behavior:
  - [ ] either compile error; or
  - [ ] unresolved wait is ignored with a warning; or
  - [ ] wait is bound when a later signal appears.
- [ ] The chosen behavior must be covered by tests.

**Recommended behavior:**

- Wait depends on all signalers registered before the wait in recording order.
- Waiting before any signal is a compile-time `InvalidState` unless explicitly marked optional.

**Tests:**

- [ ] Signal before wait orders signaler before waiter.
- [ ] Multiple signalers before wait produce fan-in.
- [ ] Independent labels do not interfere.
- [ ] Signal after wait follows documented behavior.
- [ ] Label cycle reports `InvalidState` and includes pass names in diagnostics.

## T013 — Add cycle diagnostics with pass/resource context

**Goal:** Cycle failures must be debuggable.

**Required behavior:**

- [ ] When topological compilation fails, find at least one cycle.
- [ ] Emit pass/task names in the diagnostic.
- [ ] Include edge reason when available:
  - [ ] explicit dependency;
  - [ ] RAW/WAW/WAR resource hazard;
  - [ ] label wait/signal;
  - [ ] domain-specific reason reserved for GPU render graph.
- [ ] Return the existing error convention, typically `ErrorCode::InvalidState`.
- [ ] Keep diagnostics available in logs and optionally in `ScheduleStats`/debug info.

**Tests:**

- [ ] Explicit A -> B -> A cycle returns invalid state.
- [ ] Resource-derived cycle, if constructible through explicit plus hazard edges, includes both pass names.
- [ ] Label-derived cycle includes label name or label ID.
- [ ] Cycle diagnostic does not allocate unbounded memory on large graphs.

## T014 — Refactor `DomainTaskGraph` through shared compiler

**Goal:** Make raw `PendingTaskDesc` scheduling use the shared hazard/compiler path.

**Tasks:**

- [ ] Route explicit dependencies into compiler edges.
- [ ] Route `PendingTaskDesc::resources` into `ResourceHazardBuilder`.
- [ ] Preserve priority and queue-budget lane assignment.
- [ ] Preserve stable deterministic ready-set ordering.
- [ ] Report accurate `ScheduleStats`:
  - [ ] task count;
  - [ ] explicit edge count;
  - [ ] hazard edge count;
  - [ ] total edge count;
  - [ ] topological layer count;
  - [ ] critical path cost;
  - [ ] max ready queue width.

**Tests:**

- [ ] Existing `Test.Core.DagScheduler.cpp` still passes.
- [ ] Add raw `DomainTaskGraph` tests for resource hazards.
- [ ] Add stats tests checking edge counts and layer counts.
- [ ] Add lane assignment tests for CPU, GPU, and Streaming budgets.

## T015 — Refactor `TaskGraph` through shared compiler

**Goal:** Closure-oriented `TaskGraph` must share the same dependency engine as `DomainTaskGraph`.

**Tasks:**

- [ ] Preserve `AddPass(name, setup_fn, execute_fn)` API.
- [ ] Preserve builder functions for typed reads/writes.
- [ ] Preserve `ReadResource`, `WriteResource`, `WaitFor`, and `Signal`.
- [ ] Store pass metadata needed by compiler.
- [ ] `Compile()` builds immutable compiled graph/layers.
- [ ] `BuildPlan()` returns plan in compiler order.
- [ ] `ExecutePass(uint32_t)` validates compiled state and index.
- [ ] `TakePassExecute(uint32_t)` remains safe for streaming handoff until streaming executor replaces this pattern.
- [ ] `Reset()` is illegal or debug-asserted while execution token is live.
- [ ] `GetScheduleStats()` returns meaningful stats, not only task count.

**Tests:**

- [ ] Existing `Test.Core.TaskGraph.cpp` still passes.
- [ ] Closure execution order follows resource hazards.
- [ ] `BuildPlan()` batches match topological layers.
- [ ] `Reset()` clears resources, labels, stats, and pass closures.
- [ ] `TakePassExecute()` moves only the target pass closure and leaves other passes valid.
- [ ] Reusing graph across epochs does not leak labels/resources from prior epoch.

**Review gate RG-01 — Core compiler substrate:**

- [ ] `ExtrinsicCoreTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] No public Core API change is undocumented.
- [ ] No GPU-specific type has entered `Core.Dag.*`.
- [ ] `PendingTaskDesc::resources` is verified by tests to affect scheduling.
- [ ] Cycle diagnostics include names.
- [ ] Determinism test passes under repeated runs.

---

# Phase 2 — CPU `TaskGraph` and `FrameGraph` parallel execution

## T020 — Add pass options and execution metadata

**Goal:** Give CPU graph passes enough metadata for robust scheduling and diagnostics.

**API to add:**

```cpp
struct TaskPassOptions {
    TaskPriority Priority = TaskPriority::Normal;
    uint32_t EstimatedCost = 1;
    bool MainThreadOnly = false;
    bool AllowParallel = true;
    std::string_view DebugCategory = {};
};

struct FrameGraphPassOptions {
    TaskPriority Priority = TaskPriority::Normal;
    uint32_t EstimatedCost = 1;
    bool MainThreadOnly = false;
    bool AllowParallel = true;
    std::string_view DebugCategory = {};
};
```

**Tasks:**

- [ ] Add options overloads without breaking old overloads.
- [ ] Thread options into `PendingTaskDesc`/compiler metadata.
- [ ] Include options in schedule stats/debug dumps.
- [ ] Validate `EstimatedCost >= 1` or clamp to 1.

**Tests:**

- [ ] Default overload compiles and behaves as before.
- [ ] Explicit priority affects ready-node ordering.
- [ ] Estimated cost affects critical-path ordering.
- [ ] MainThreadOnly flag is preserved in compiled plan.

## T021 — Add ECS/frame structural safety declarations

**Goal:** Prevent unsafe parallel ECS structural mutations.

**FrameGraph builder additions:**

- [ ] `StructuralRead()`
- [ ] `StructuralWrite()`
- [ ] `CommitWorld()` or `CommitTick()` dependency token
- [ ] `ReadResource(ResourceId/StringID)` and `WriteResource(ResourceId/StringID)` if not already present at FrameGraph level

**Required built-in resource tokens:**

- [ ] `RegistryStructureToken`
- [ ] `SceneCommitToken`
- [ ] `RenderExtractionToken`

**Rules:**

- [ ] Entity create/destroy and component add/remove systems must declare `StructuralWrite()`.
- [ ] Systems that inspect entity/component membership declare `StructuralRead()`.
- [ ] Pure component data reads/writes use typed `Read<T>()` / `Write<T>()`.
- [ ] Extraction depends on commit token, not incidental pass order.

**Tests:**

- [ ] Two structural reads can share a layer.
- [ ] Structural write serializes after prior structural reads.
- [ ] Structural write serializes before later structural reads.
- [ ] Typed component writes do not unnecessarily serialize unrelated typed component reads.
- [ ] Commit token creates required phase boundary.

## T022 — Implement graph-local dependency-ready CPU executor

**Goal:** Replace sequential layer execution with dependency-ready dispatch through `Core.Tasks`.

**Execution model:**

- [ ] Compile graph if needed.
- [ ] Copy initial indegrees into per-execution atomic counters.
- [ ] Dispatch all root nodes that are eligible for worker execution.
- [ ] On pass completion, decrement successors.
- [ ] When a successor reaches zero, dispatch it.
- [ ] Signal a graph-local `CounterEvent` or equivalent only when this graph is complete.
- [ ] Wait on the graph-local completion primitive.
- [ ] Do not wait for unrelated global work.

**Suggested execution state:**

```cpp
struct ExecutionState {
    std::span<const uint32_t> InitialIndegrees;
    std::span<const uint32_t> Successors;
    std::unique_ptr<std::atomic_uint32_t[]> RemainingDeps;
    std::unique_ptr<std::atomic_uint8_t[]> Dispatched;
    Core::Tasks::CounterEvent Done;
};
```

**Tasks:**

- [ ] Add `TaskGraphExecutionToken` or internal execution guard.
- [ ] Add deterministic sequential fallback when scheduler unavailable or worker count is one.
- [ ] Add fail-safe if a pass closure is missing.
- [ ] Ensure closures are value-captured/moved safely and not referenced after reset.
- [ ] Prevent recursive lambda lifetime hazards.
- [ ] Add pass-level telemetry timings if existing telemetry supports it.

**Tests:**

- [ ] Independent passes run concurrently. Use atomics/barriers to prove overlap.
- [ ] Dependent passes never overlap incorrectly.
- [ ] Graph-local wait returns after graph work, while an unrelated long worker job continues running.
- [ ] Sequential fallback produces identical side-effect order for dependent passes.
- [ ] Missing closure either no-ops or returns documented error; test chosen behavior.
- [ ] Repeated execute/reset loops do not leak or use-after-free.

## T023 — Main-thread-only pass support

**Goal:** Support passes that must run on the main thread without breaking worker parallelism.

**First implementation:**

- [ ] Layer fallback: for each layer, dispatch worker-eligible passes; execute main-thread-only passes on main thread; wait for that layer before advancing.

**Better optional follow-up:**

- [ ] Main-thread ready queue: workers continue while main thread pumps ready main-thread tasks.

**Required behavior:**

- [ ] A `MainThreadOnly` pass must execute on the thread that called `Execute()`.
- [ ] Other passes in the same independent layer may run on workers if safe.
- [ ] Main-thread pass dependencies are respected.

**Tests:**

- [ ] Capture caller thread ID and assert main-thread-only pass runs there.
- [ ] Independent worker pass can overlap with main-thread pass if executor supports that mode, or document layer-conservative behavior.
- [ ] Dependent pass waits for main-thread-only predecessor.

## T024 — Route `FrameGraph::Execute()` through the new executor

**Goal:** Make `FrameGraph` a thin ECS façade over the completed CPU `TaskGraph` executor.

**Tasks:**

- [ ] Add FrameGraph pass options overloads.
- [ ] Thread typed component access through shared resource tokens.
- [ ] Implement structural tokens.
- [ ] Ensure `FrameGraph::Compile()`, `Execute()`, and `Reset()` preserve lifecycle invariants.
- [ ] Expose schedule stats via `FrameGraph`.

**Tests:**

- [ ] Existing `Test.Core.GraphInterfaces.cpp` still passes.
- [ ] Existing FrameGraph tests still pass.
- [ ] Add `tests/Core/Test.Core.FrameGraphParallel.cpp`:
  - [ ] typed read/read parallelism;
  - [ ] typed write/read serialization;
  - [ ] typed read/write serialization;
  - [ ] structural write global serialization;
  - [ ] commit/extraction barrier ordering;
  - [ ] main-thread-only pass behavior;
  - [ ] graph-local wait behavior.

## T025 — CPU graph stress and performance tests

**Goal:** Catch hidden O(N^2) or allocation regressions.

**Tests:**

- [ ] 1,000-pass mixed hazard graph compiles under a reasonable threshold.
- [ ] 10,000 independent pass graph compiles deterministically.
- [ ] Wide graph execution completes and uses more than one worker when available.
- [ ] Deep graph execution follows exact dependency chain.
- [ ] Reusing a graph for 1,000 epochs does not grow retained memory beyond expected high-water marks.

**Review gate RG-02 — CPU FrameGraph execution:**

- [ ] `ExtrinsicCoreTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] CPU graph no longer executes all passes sequentially when workers are available.
- [ ] There is a deterministic single-thread fallback.
- [ ] No global scheduler wait is used as the normal graph wait.
- [ ] Structural ECS mutation hazards are represented by explicit resources.
- [ ] Pass options and stats are documented.

---

# Phase 3 — Persistent async streaming executor

## T030 — Decide streaming executor home and module shape

**Goal:** Place streaming execution in the right layer.

**Preferred module:**

- [ ] `src_new/Runtime/Runtime.StreamingExecutor.cppm`
- [ ] `src_new/Runtime/Runtime.StreamingExecutor.cpp`

**Alternative if asset-specific:**

- [ ] `src_new/Assets/Asset.StreamingExecutor.*`

**Decision rule:**

- [ ] Put generic continuous executor mechanics in `Runtime`.
- [ ] Put asset-specific decode/load pipeline construction in `Assets`.
- [ ] Do not put asset-specific state machines inside `Core`.

**Acceptance criteria:**

- [ ] The module depends only on allowed lower layers.
- [ ] The location is documented in the relevant README.

## T031 — Implement persistent streaming task table

**Goal:** Streaming work must survive across frames and not be destroyed by frame-local graph reset.

**Types:**

```cpp
enum class StreamingTaskState {
    Pending,
    Ready,
    Running,
    WaitingForMainThreadApply,
    WaitingForGpuUpload,
    Complete,
    Failed,
    Cancelled
};
```

```cpp
struct StreamingTaskDesc {
    std::string Name;
    TaskKind Kind;
    TaskPriority Priority;
    uint32_t EstimatedCost;
    uint64_t CancellationGeneration;
    std::span<const StreamingTaskHandle> DependsOn;
    std::move_only_function<StreamingResult()> Execute;
    std::move_only_function<void(StreamingResult&&)> ApplyOnMainThread;
};
```

**API:**

```cpp
class StreamingExecutor {
public:
    StreamingTaskHandle Submit(StreamingTaskDesc desc);
    void Cancel(StreamingTaskHandle handle);
    void PumpBackground(uint32_t maxLaunches);
    void DrainCompletions();
    void ApplyMainThreadResults();
    void ShutdownAndDrain();
};
```

**Tasks:**

- [ ] Implement stable handles with generation validation.
- [ ] Implement persistent storage with state transitions.
- [ ] Implement dependency counters.
- [ ] Implement priority-ready queues.
- [ ] Implement cancellation generation checks.
- [ ] Implement worker completion queue.
- [ ] Implement main-thread apply queue.
- [ ] Implement shutdown drain/cancel semantics.

**Tests:**

- [ ] Submitted task remains pending across frames until pumped.
- [ ] Dependency chain spans frames.
- [ ] Higher-priority ready task launches before lower-priority ready task.
- [ ] Cancelling a pending task prevents execution.
- [ ] Cancelling a running task suppresses stale apply.
- [ ] Generation mismatch prevents stale result publication.
- [ ] `ApplyOnMainThread` runs on caller thread of `ApplyMainThreadResults()`.
- [ ] Shutdown drains or cancels all running work deterministically.

## T032 — Replace frame-local streaming graph tick

**Goal:** Remove the pattern of compiling streaming work, moving closures, resetting graph, and relying on detached worker execution.

**Tasks:**

- [ ] Locate current `Engine::TickStreamingGraph()` or equivalent.
- [ ] Replace frame-local streaming graph execution with `StreamingExecutor` calls.
- [ ] Phase 10 should call:
  - [ ] collect completed transfers;
  - [ ] drain streaming completions;
  - [ ] apply main-thread results;
  - [ ] tick asset service/state machines;
  - [ ] pump a bounded number of background launches.
- [ ] Keep old `GetStreamingGraph()` only as a temporary compatibility shim if required, and mark it deprecated.
- [ ] Ensure shutdown calls `StreamingExecutor::ShutdownAndDrain()` before `Core.Tasks::Scheduler::Shutdown()`.

**Tests:**

- [ ] Add or create `tests/Runtime/Test.Runtime.StreamingExecutor.cpp`.
- [ ] Runtime maintenance phase applies completed streaming result once.
- [ ] Streaming job cannot publish after engine shutdown begins.
- [ ] Long streaming job does not block render extraction.
- [ ] Frame-local reset cannot invalidate running streaming closures.

## T033 — Add asset/GPU upload handoff hooks without overreaching

**Goal:** Prepare streaming executor for asset decode and GPU upload without implementing unrelated asset features.

**Tasks:**

- [ ] Define a result type that can represent CPU payload ready, failed load, or upload request.
- [ ] Ensure worker thread does not create/destroy GPU resources directly.
- [ ] Provide main-thread handoff API that a future `GpuAssetCache` can consume.
- [ ] Do not invent a second transfer queue or staging manager.
- [ ] If `GpuAssetCache` already exists, wire non-blocking request/result behavior through it; otherwise add TODO stubs and tests for executor behavior only.

**Tests:**

- [ ] Worker result enqueues main-thread upload request callback.
- [ ] Upload request callback can be skipped safely if task was cancelled.
- [ ] Failed worker result moves task to `Failed` and does not call upload callback.

**Review gate RG-03 — Streaming executor:**

- [ ] Runtime or Assets focused tests pass.
- [ ] `ExtrinsicCoreTests` still passes.
- [ ] `IntrinsicTests` passes.
- [ ] Streaming jobs persist across frames.
- [ ] Cancellation suppresses stale apply.
- [ ] Shutdown order is deterministic.
- [ ] Worker code cannot mutate ECS or GPU resources directly through the executor API.

---

# Phase 4 — `Graphics.RenderGraph` scaffold

## T040 — Add `Graphics.RenderGraph` module and CMake wiring

**Goal:** Create a GPU-specific render graph module in `Graphics`, not `Core`.

**Files to add:**

- [ ] `src_new/Graphics/Graphics.RenderGraph.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Resources.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Pass.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Compiler.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Barriers.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.TransientAllocator.cppm`
- [ ] `src_new/Graphics/Graphics.RenderGraph.Executor.cppm`
- [ ] Matching `.cpp` implementation units.
- [ ] Update `src_new/Graphics/CMakeLists.txt`.
- [ ] Update `src_new/Graphics/README.md`.
- [ ] Add tests to `tests/Graphics/CMakeLists.txt`.

**Layering:**

- [ ] May import `Extrinsic.Core` graph compiler/scheduler types as needed.
- [ ] May import `Extrinsic.RHI.*`.
- [ ] May import `Extrinsic.Graphics.RenderWorld` and `GpuWorld` read-only interfaces.
- [ ] Must not import `ECS`.
- [ ] Must not expose Vulkan backend types in public RenderGraph API.

**Acceptance criteria:**

- [ ] Empty render graph compiles and resets.
- [ ] Module inventory regeneration succeeds.

## T041 — Implement virtual resource model

**Goal:** Support imported and transient textures/buffers.

**Public handles:**

- [ ] `TextureRef`
- [ ] `BufferRef`

**Resource APIs:**

```cpp
TextureRef ImportBackbuffer(std::string_view name, RHI::TextureHandle handle);
TextureRef ImportTexture(std::string_view name, RHI::TextureHandle handle, TextureState initial);
BufferRef  ImportBuffer(std::string_view name, RHI::BufferHandle handle, BufferState initial);

TextureRef CreateTexture(std::string_view name, const TextureDesc& desc);
BufferRef  CreateBuffer(std::string_view name, const BufferDesc& desc);
```

**Tasks:**

- [ ] Implement stable resource handles with generation.
- [ ] Track imported vs transient resources.
- [ ] Track initial/final states for imported resources.
- [ ] Store texture/buffer descriptors.
- [ ] Validate resource ref generation on every access.
- [ ] Keep debug names for diagnostics.

**Tests:**

- [ ] Import backbuffer returns valid texture ref.
- [ ] Imported texture cannot be alias-allocated.
- [ ] Transient texture lifetime is empty before use.
- [ ] Invalid generation fails validation.
- [ ] Duplicate debug names are allowed or rejected according to documented behavior.

## T042 — Implement render pass builder and access declarations

**Goal:** Passes declare all resource usage through a builder.

**API sketch:**

```cpp
class RenderGraphBuilder {
public:
    TextureRef Read(TextureRef, TextureUsage usage);
    TextureRef Write(TextureRef, TextureUsage usage);
    BufferRef  Read(BufferRef, BufferUsage usage);
    BufferRef  Write(BufferRef, BufferUsage usage);

    void SetQueue(RenderQueue queue);
    void SetRenderPass(const RHI::RenderPassDesc& desc);
    void SideEffect();
};
```

**Usage enums:**

- [ ] `TextureUsage::ColorAttachmentRead`
- [ ] `TextureUsage::ColorAttachmentWrite`
- [ ] `TextureUsage::DepthRead`
- [ ] `TextureUsage::DepthWrite`
- [ ] `TextureUsage::ShaderRead`
- [ ] `TextureUsage::ShaderWrite`
- [ ] `TextureUsage::TransferSrc`
- [ ] `TextureUsage::TransferDst`
- [ ] `TextureUsage::Present`
- [ ] `BufferUsage::IndirectRead`
- [ ] `BufferUsage::IndexRead`
- [ ] `BufferUsage::VertexRead` if needed
- [ ] `BufferUsage::ShaderRead`
- [ ] `BufferUsage::ShaderWrite`
- [ ] `BufferUsage::TransferSrc`
- [ ] `BufferUsage::TransferDst`
- [ ] `BufferUsage::HostReadback`

**Tests:**

- [ ] Pass with declared read compiles.
- [ ] Pass with declared write compiles.
- [ ] Pass cannot use invalid resource ref.
- [ ] Pass cannot write an imported read-only/present-only resource unless imported as writable.
- [ ] Side-effect pass is not culled.

## T043 — Implement RenderGraph compile validation and pass DAG

**Goal:** Compile render passes into valid topological order.

**Compile phases:**

- [ ] Validate all resource refs.
- [ ] Validate pass render attachment declarations match declared writes.
- [ ] Validate present pass targets imported backbuffer.
- [ ] Build producer/consumer dependency edges:
  - [ ] write -> read;
  - [ ] write -> write;
  - [ ] read -> write;
  - [ ] explicit pass dependency, if API supports it;
  - [ ] queue handoff placeholder edge, even if only one queue exists initially.
- [ ] Topologically sort passes using shared compiler substrate or a Graphics-specific wrapper around it.
- [ ] Detect cycles with pass/resource names.
- [ ] Build immutable compiled plan.

**Tests:**

- [ ] Two independent passes can remain same layer or adjacent deterministic order.
- [ ] Write color then shader-read creates dependency.
- [ ] Read then write creates dependency.
- [ ] Write then write creates dependency.
- [ ] Invalid present target fails validation.
- [ ] Missing resource declaration fails if execution tries to resolve it.
- [ ] Cycle reports pass names.

## T044 — Implement basic lifetime analysis and pass culling

**Goal:** Know first/last use and remove unused passes safely.

**Tasks:**

- [ ] Compute first use and last use for every virtual resource.
- [ ] Mark imported resources as external lifetime.
- [ ] Mark side-effect passes as roots for reachability.
- [ ] Mark present/backbuffer final use as side-effect root.
- [ ] Cull passes whose outputs are never consumed and which have no side effects.
- [ ] Do not cull passes that write imported resources.

**Tests:**

- [ ] Unused transient-producing pass is culled.
- [ ] Side-effect pass remains.
- [ ] Present chain keeps all producer passes.
- [ ] Imported-resource writer remains.
- [ ] Lifetime first/last pass indices are correct.

## T045 — Implement coarse barrier packet generation

**Goal:** Generate resource transitions from declared usage.

**Tasks:**

- [ ] Map `TextureUsage` to abstract texture state/layout/access.
- [ ] Map `BufferUsage` to abstract buffer access/stage.
- [ ] For every resource use transition, emit barrier packet before consuming pass.
- [ ] Collapse redundant no-op transitions.
- [ ] Support imported initial state.
- [ ] Support imported final/present state.
- [ ] Implement conversion to current RHI coarse `TextureBarrier` / `BufferBarrier` calls where possible.
- [ ] Keep exact Sync2-style barrier API as a later phase if RHI does not yet expose it.

**Tests:**

- [ ] Undefined/imported -> color attachment write barrier.
- [ ] Color attachment write -> shader read barrier.
- [ ] Depth write -> depth read/shader read barrier.
- [ ] Compute shader write -> indirect read barrier.
- [ ] Transfer dst -> shader read barrier.
- [ ] Color/shader read -> present barrier for backbuffer.
- [ ] Redundant read -> read transition emits no barrier.

## T046 — Implement RenderGraph execution context and null backend behavior

**Goal:** Make graph execution callable before full Vulkan backend support.

**API sketch:**

```cpp
class RenderGraphContext {
public:
    RHI::ICommandContext& Commands();
    RHI::IDevice& Device();
    const RenderWorld& World();

    RHI::TextureHandle Resolve(TextureRef);
    RHI::BufferHandle Resolve(BufferRef);
};
```

**Tasks:**

- [ ] Allocate/resolve imported resources.
- [ ] For first scaffold, transient resources may be logical-only or allocated through existing texture/buffer managers if available.
- [ ] Execute passes in compiled order.
- [ ] Emit barrier calls before each pass.
- [ ] Null backend should record/observe calls enough for tests without requiring a real GPU.
- [ ] Failed compile prevents execute.

**Tests:**

- [ ] Execute empty graph succeeds.
- [ ] Execute simple present chain in mock/null backend records expected pass order.
- [ ] Barrier packets are visible to tests.
- [ ] Resource resolution fails for invalid ref.

**Review gate RG-04 — RenderGraph scaffold:**

- [ ] `ExtrinsicGraphicsTests` passes.
- [ ] `ExtrinsicCoreTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] `Graphics.RenderGraph` has no ECS dependency.
- [ ] Core still has no GPU resource/barrier semantics.
- [ ] RenderGraph compile order comes from declared resources, not hardcoded pass order.
- [ ] Barriers are generated from usage transitions.

---

# Phase 5 — Renderer integration with RenderGraph

## T050 — Renderer owns and resets a RenderGraph per frame

**Goal:** Move GPU pass ordering into `Graphics.RenderGraph` inside the renderer.

**Tasks:**

- [ ] Add `RenderGraph` member to renderer implementation.
- [ ] In `ExecuteFrame`, reset graph, import frame/backbuffer resources, add passes, compile, execute.
- [ ] Keep `Runtime::Engine` unaware of pass-level details.
- [ ] Renderer `BeginFrame`/`EndFrame` lifecycle remains unchanged unless documented.
- [ ] Resize invalidates/recreates graph-owned transient resources as needed.

**Tests:**

- [ ] Null renderer/frame path compiles and executes graph.
- [ ] Resize causes transient resource invalidation without stale handle use.
- [ ] Runtime smoke test confirms `RunFrame` does not inspect graph resources.

## T051 — Register initial fixed pass sequence through graph builders

**Goal:** Convert fixed-order render code into graph pass registration.

**Passes to register, feature-gated where appropriate:**

- [ ] Compute prologue / scene update / culling.
- [ ] Picking pass, conditional on pending pick.
- [ ] Optional depth prepass.
- [ ] G-buffer pass.
- [ ] Deferred lighting pass.
- [ ] Forward surface pass.
- [ ] Forward line pass.
- [ ] Forward point pass.
- [ ] Overlay surface/debug pass if present.
- [ ] Bloom pass.
- [ ] Tone-map pass.
- [ ] FXAA/SMAA pass.
- [ ] Selection outline pass.
- [ ] Debug view pass.
- [ ] ImGui pass.
- [ ] Present pass.

**Rules:**

- [ ] Every pass declares all texture/buffer reads/writes.
- [ ] No pass relies on registration order for required correctness.
- [ ] Registration order is only a stable tie-break.
- [ ] Fixed sequence may remain as builder call order, but dependencies/barriers must come from resource declarations.

**Tests:**

- [ ] Expected pass names appear in compiled graph when features enabled.
- [ ] Conditional picking pass absent when no pick request.
- [ ] G-buffer outputs feed deferred lighting.
- [ ] Deferred lighting feeds postprocess.
- [ ] Postprocess feeds present.
- [ ] Selection outline reads entity ID/depth resources.
- [ ] Pass culling removes disabled/unused optional passes.

## T052 — Import persistent GPU world resources into RenderGraph

**Goal:** Persistent buffers/textures from `GpuWorld` and systems must enter graph as imported resources.

**Tasks:**

- [ ] Import scene/instance/entity/material/light buffers as read resources.
- [ ] Import indirect draw buffers as read/write as appropriate.
- [ ] Import shadow atlas if present.
- [ ] Import selection readback buffers if present.
- [ ] Ensure imported resource states are specified.
- [ ] Ensure graph does not own imported lifetimes.

**Tests:**

- [ ] Imported buffers are not destroyed by graph reset.
- [ ] Imported buffers cannot alias transient buffers.
- [ ] Culling write -> draw indirect read barrier is generated.
- [ ] Lighting pass reads light buffer.

## T053 — Renderer integration telemetry and diagnostics

**Goal:** Make render-graph failures obvious and measurable.

**Tasks:**

- [ ] Log compile failures with pass/resource names.
- [ ] Expose per-frame graph stats:
  - [ ] pass count;
  - [ ] culled pass count;
  - [ ] resource count;
  - [ ] barrier count;
  - [ ] transient memory estimate;
  - [ ] compile time;
  - [ ] execute/record time.
- [ ] Add debug dump function for compiled render graph.

**Tests:**

- [ ] Compile failure log contains failing pass name.
- [ ] Stats are nonzero for nonempty graph.
- [ ] Debug dump contains pass order and resources.

**Review gate RG-05 — Renderer integration:**

- [ ] `ExtrinsicGraphicsTests` passes.
- [ ] `IntrinsicTests` passes.
- [ ] Renderer `ExecuteFrame` builds and runs a `Graphics.RenderGraph`.
- [ ] Runtime does not know render pass names or GPU resources.
- [ ] Null/headless path remains testable.
- [ ] Pass order is test-verified by graph output, not only by code inspection.

---

# Phase 6 — Precise GPU barriers, transient resources, aliasing

## T060 — Extend RHI barrier API if needed

**Goal:** Move from coarse barriers to explicit Sync2-style barrier packets where backend supports it.

**Tasks:**

- [ ] Define backend-agnostic barrier packet structs in RHI or Graphics/RHI-facing layer:
  - [ ] texture/image barrier;
  - [ ] buffer barrier;
  - [ ] memory barrier if needed.
- [ ] Include source/destination stages, access masks, old/new layouts, queue ownership if supported.
- [ ] Add command-context method to submit a batch of barriers.
- [ ] Keep fallback mapping to existing coarse API for null/unsupported backends.
- [ ] Implement Vulkan backend mapping.

**Tests:**

- [ ] RHI unit tests verify packet fields survive to mock command context.
- [ ] Vulkan mapping unit tests if backend test hooks exist.
- [ ] Existing coarse barrier tests still pass.

## T061 — Implement transient resource allocation

**Goal:** Allocate render graph-created transient textures/buffers from graph lifetime analysis.

**Tasks:**

- [ ] Build transient resource allocation requests from descriptors and first/last use.
- [ ] Allocate actual RHI resources before execution.
- [ ] Free/return transient resources after frame or keep in a reusable cache.
- [ ] Handle resize by invalidating incompatible cached resources.
- [ ] Never allocate imported resources.
- [ ] Add memory estimate/statistics.

**Tests:**

- [ ] Transient resources are allocated before first use.
- [ ] Transient resources are released/recycled after graph reset/frame completion.
- [ ] Resize invalidates resources with old extent.
- [ ] Imported resources are never allocated or freed by transient allocator.

## T062 — Implement aliasing of compatible transient resources

**Goal:** Reuse memory for non-overlapping transient resources.

**Rules:**

- [ ] Resources may alias only when lifetimes do not overlap.
- [ ] Descriptors must be compatible.
- [ ] Imported resources never alias.
- [ ] Aliasing boundaries must emit required discard/transition barriers.
- [ ] Aliasing must be optional behind a config flag for debugging.

**Tests:**

- [ ] Non-overlapping compatible textures alias.
- [ ] Overlapping compatible textures do not alias.
- [ ] Non-compatible descriptors do not alias.
- [ ] Imported resource does not alias.
- [ ] Alias on/off produces same logical pass output order and barrier correctness.

## T063 — Vulkan validation and GPU integration tests

**Goal:** Confirm render graph barriers and lifetimes are valid under real backend validation.

**Tests:**

- [ ] G-buffer -> lighting -> postprocess -> present frame passes Vulkan validation.
- [ ] Depth write -> shader read transition passes validation.
- [ ] Compute write -> indirect draw read transition passes validation.
- [ ] Swapchain present transition passes validation.
- [ ] Transient aliasing enabled passes validation.
- [ ] Tests are skipped gracefully when Vulkan validation backend is unavailable.

**Review gate RG-06 — Production GPU graph mechanics:**

- [ ] Graphics tests pass in null backend.
- [ ] Vulkan validation tests pass or skip cleanly in unsupported environments.
- [ ] Barriers are generated from declared usages.
- [ ] Aliasing is correct and can be disabled.
- [ ] Resize behavior is tested.
- [ ] No pass emits ad-hoc barriers outside RenderGraph except documented backend internals.

---

# Phase 7 — Runtime phase integration and render-prep graph

## T070 — Keep `Engine::RunFrame` broad-phase-only

**Goal:** Preserve the intended frame loop shape.

**Tasks:**

- [ ] Verify `RunFrame` consists only of broad phases:
  - [ ] platform/event handling;
  - [ ] fixed-step simulation;
  - [ ] variable tick;
  - [ ] render input/snapshot;
  - [ ] renderer begin;
  - [ ] render extraction;
  - [ ] render prepare;
  - [ ] render execute;
  - [ ] end/present;
  - [ ] maintenance/streaming rendezvous;
  - [ ] clock end.
- [ ] Remove or avoid any pass-level renderer logic from Runtime.
- [ ] Ensure streaming rendezvous happens in maintenance.
- [ ] Ensure GPU transfers are collected before asset state flips to ready.
- [ ] Update Runtime README if phase ordering changes.

**Tests:**

- [ ] Runtime frame-loop test with fakes verifies phase order.
- [ ] Resize path still drains/resizes/acknowledges in order.
- [ ] Minimized window path does not tick sim/render.
- [ ] Maintenance runs after present/end-frame.

## T071 — Convert render-prep work to CPU task graph where useful

**Goal:** Use CPU graph for render-prep jobs without prematurely parallelizing unsafe work.

**Candidate jobs:**

- [ ] frustum cull prep;
- [ ] draw packet sort;
- [ ] LOD selection;
- [ ] staging upload preparation;
- [ ] CPU-side visibility list building;
- [ ] debug draw packet freezing.

**Tasks:**

- [ ] Add a render-prep `TaskGraph` owned by renderer or frame context.
- [ ] Render-prep graph reads immutable `RenderWorld`.
- [ ] Render-prep graph writes renderer-owned transient prep packets only.
- [ ] Execute and await render-prep graph before GPU RenderGraph execution.
- [ ] Keep render-prep graph optional/sequential for small scenes.

**Tests:**

- [ ] Render-prep job cannot run before extraction has produced `RenderWorld`.
- [ ] GPU graph cannot execute before render-prep graph completes.
- [ ] Independent render-prep jobs can run in parallel.
- [ ] Render-prep graph does not mutate ECS.

**Review gate RG-07 — Runtime integration:**

- [ ] Runtime tests pass.
- [ ] Core tests pass.
- [ ] Graphics tests pass.
- [ ] Full `IntrinsicTests` pass.
- [ ] `RunFrame` remains broad-phase-only.
- [ ] Streaming, CPU graph, and GPU graph each enter the frame at the documented phase.

---

# Phase 8 — Final docs, module inventory, and cutover audit

## T080 — Regenerate module inventory and sync READMEs

**Tasks:**

- [ ] Run `python3 tools/generate_src_new_module_inventory.py`.
- [ ] Update `docs/architecture/src_new_module_inventory.md`.
- [ ] Update `src_new/Core/README.md` with new graph partitions and public surface.
- [ ] Update `src_new/Graphics/README.md` with `Graphics.RenderGraph` public surface.
- [ ] Update `src_new/Runtime/README.md` with streaming executor/frame phases.
- [ ] Update top-level `TODO.md` if milestones are completed or replaced.

**Acceptance criteria:**

- [ ] Inventory reflects every new `.cppm` module/partition.
- [ ] README public module surfaces match CMake files.
- [ ] Architecture docs and implementation names match.

## T081 — Add post-merge audit checklist result

**Goal:** Make architecture-touching changes safe to merge.

**Audit items:**

- [ ] No new dependency cycle between `Core`, `Assets`, `ECS`, `Graphics`, `Runtime`, `Platform`.
- [ ] `Graphics` does not import `ECS` for RenderGraph.
- [ ] `Runtime` does not inspect GPU resources/barriers.
- [ ] `Core` does not expose GPU layout/barrier semantics.
- [ ] Streaming worker closures cannot mutate ECS/GPU directly.
- [ ] CPU graph uses graph-local wait.
- [ ] Single-thread fallback works.
- [ ] Null/headless paths still pass.
- [ ] Cycle diagnostics include pass/task names.
- [ ] Build/test commands are listed in PR notes.

## T082 — Final end-to-end acceptance tests

**Required test runs:**

- [ ] `cmake --preset dev`
- [ ] `cmake --build --preset dev --target IntrinsicTests`
- [ ] `./build/dev/bin/IntrinsicTests`
- [ ] `./build/dev/bin/ExtrinsicCoreTests`
- [ ] `./build/dev/bin/ExtrinsicGraphicsTests`
- [ ] `./build/dev/bin/ExtrinsicRuntimeTests` if Runtime tests were added.
- [ ] Focused graph stress tests.
- [ ] Vulkan validation graph test if backend/environment supports it.
- [ ] Compile hotspot tool if module partition changes caused suspicious build regressions:

```bash
python3 ./tools/compile_hotspots.py \
  --build-dir build \
  --top 40 \
  --json-out build/compile_hotspots_report.json \
  --baseline-json tools/compile_hotspot_baseline.json
```

## T083 — Final cutover criteria

Mark complete only when all are true:

- [ ] `FrameGraph::Execute()` uses dependency-ready graph execution when workers are available.
- [ ] CPU graph has deterministic single-thread fallback.
- [ ] `DagScheduler`, `DomainTaskGraph`, and `TaskGraph` share compiler/hazard logic or have an explicitly documented reason not to.
- [ ] `PendingTaskDesc::resources` affects scheduling.
- [ ] Cycle diagnostics include names and edge reasons.
- [ ] Streaming tasks are persistent across frames.
- [ ] Streaming cancellation prevents stale result publication.
- [ ] Streaming shutdown is deterministic and happens before task scheduler shutdown.
- [ ] `Renderer::ExecuteFrame()` builds and executes a real `Graphics.RenderGraph`.
- [ ] GPU barriers are generated from declared pass resource usage.
- [ ] Render passes declare resources through graph builders.
- [ ] Runtime owns only broad phase orchestration.
- [ ] Tests cover Core graph compile, CPU execution, streaming executor, render graph compile, render graph barriers, and renderer integration.
- [ ] Docs and module inventory are synchronized.

---

# Suggested PR breakdown

## PR 1 — Documentation and audit only

- T000
- T001
- RG-00

## PR 2 — Core graph compiler substrate

- T010
- T011
- T012
- T013
- T014
- T015
- RG-01

## PR 3 — CPU FrameGraph parallel executor

- T020
- T021
- T022
- T023
- T024
- T025
- RG-02

## PR 4 — Persistent streaming executor

- T030
- T031
- T032
- T033
- RG-03

## PR 5 — RenderGraph scaffold

- T040
- T041
- T042
- T043
- T044
- T045
- T046
- RG-04

## PR 6 — Renderer integration

- T050
- T051
- T052
- T053
- RG-05

## PR 7 — Precise barriers and transient resources

- T060
- T061
- T062
- T063
- RG-06

## PR 8 — Runtime integration and render-prep graph

- T070
- T071
- RG-07

## PR 9 — Final docs, inventory, and cutover audit

- T080
- T081
- T082
- T083

---

# Codex working prompt

Use this prompt when starting each PR-sized chunk:

```text
You are implementing the next task-graph milestone in IntrinsicEngine `src_new`.

Constraints:
- Do not copy legacy `src`; use it only for inspiration.
- Preserve `src_new` layering: Core graph primitives, Graphics GPU render graph, Runtime orchestration only.
- Add focused tests before or alongside implementation.
- Keep deterministic fallback behavior.
- Do not move GPU barrier/layout concepts into Core.
- Do not use global scheduler wait for normal graph execution.
- Update CMake, README, and module inventory when modules change.
- Stop at the review gate for this PR and ensure all required tests pass.

Implement the next unchecked TODOs from `codex_task_graph_todos.md` and include the exact validation commands and results in the PR notes.
```

# User-provided custom instructions

### System Prompt

This prompt configures the AI to act as the ultimate authority on both **Geometry Processing Research** and **High-Performance Engine Architecture**.

# ROLE
You are the **Senior Principal Graphics Architect & Distinguished Scientist in Geometry Processing**.
*   **Academic Background:** You hold Ph.D.s in Computer Science and Mathematics, specializing in **Discrete Differential Geometry**, Topology, and Numerical Optimization.
*   **Industry Experience:** You have 20+ years of experience bridging the gap between academic research and AAA game engine architecture (Unreal/Decima) or HPC (CUDA/Scientific Vis).
*   **Superpower:** You do not write "academic code" (slow, pointer-heavy). You translate rigorous mathematical theories into **Data-Oriented, GPU-Driven, Lock-Free C++23**.

# CONTEXT & GOAL
You are designing and implementing a **"Next-Gen Research & Rendering Engine."**
*   **Purpose:** A platform for real-time geometry processing, path tracing, and physics simulation.
*   **Performance Target:** < 2ms CPU Frame Time.
*   **Philosophy:** **"Rigorous Theory, Metal Performance."** Every algorithm must be mathematically sound (robust to degenerate inputs) and computationally optimal (cache-friendly, SIMD/GPU-ready).

## CORE ARCHITECTURE: The 3-Fold Hybrid Task System
1.  **CPU Task Graph (Fiber-Based):** Lock-free work-stealing for gameplay/physics.
2.  **GPU Frame Graph (Transient DAG):** Manages Virtual Resources, aliasing, and Async Compute (Vulkan 1.3 Sync2).
3.  **Async Streaming Graph:** Background priority queues for asset IO and heavy geometric processing (e.g., mesh simplification, remeshing).

# GUIDELINES

## 1. Mathematical & Algorithmic Standards
*   **Formalism:** When introducing geometric algorithms, use **LaTeX** (`$...$` or `$$...$$`) to define the formulation precisely (e.g., minimizing energies, spectral decomposition).
*   **Robustness:** Explicitly handle degenerate cases (zero-area triangles, non-manifold edges). Prefer numerical stability over naive implementations.
*   **Analysis:** Briefly state the Time Complexity ($O(n)$) and Space Complexity of your proposed solutions.

## 2. Engineering & Data-Oriented Design (DOD)
*   **Memory Layout:**
    *   **Struct-of-Arrays (SoA):** Mandatory for hot data (positions, velocities).
    *   **Allocators:** Use `LinearAllocator` (Stack) for per-frame data. No raw `new`/`delete` or `std::shared_ptr` in hot loops.
    *   **Handle-Based Ownership:** Use generational indices (`StrongHandle<T>`) instead of pointers.
*   **GPU-Driven Rendering:**
    *   **Bindless by Default:** Descriptor Indexing.
    *   **Buffer Device Address (BDA):** Raw pointers in shaders.
    *   **Indirect Execution:** CPU prepares packets; GPU drives execution (Mesh Shaders/Compute).

## 3. Coding Standards (Modern C++ & Modules)
*   **Standard:** **C++23**.
    *   Use **Explicit Object Parameters** ("Deducing `this`").
    *   Use **Monadic Operations** (`.and_then`, `.transform`) on `std::expected`.
    *   Use `std::span` and Ranges views over raw pointer arithmetic.
*   **Modules Strategy:**
    *   **Logical Units:** One named module per library (`Core`, `Geometry`).
    *   **Partitions:** `.cppm` for Interface (`export module Core:Math;`), `.cpp` for Implementation (`module Core:Math.Impl;`).
    *   **Headers:** Global Module Fragment (`module;`) only.

# WORKFLOW
1.  **Theoretical Analysis:** Define the problem mathematically. What is the geometric invariant? What is the energy to minimize? (Use LaTeX).
2.  **Architecture Check:** Which Graph handles this? (CPU vs. Compute Shader).
3.  **Data Design:** Define memory layout (SoA vs AoS) for cache coherency.
4.  **Interface (.cppm):** Minimal exports using C++23 features.
5.  **Implementation (.cpp):** SIMD-friendly, branchless logic.
6.  **Verification:** GTest + Telemetry marker.

# OUTPUT FORMAT
Provide code in Markdown blocks. Use the following structure:

**1. Mathematical & Architectural Analysis**
*   *Theory:* $$ E(u) = \int_S |\nabla u|^2 dA $$ (Explain the math/geometry).
*   *Implementation:* "We will solve this using a Parallel Jacobi iteration on the Compute Queue..."

**2. Module Interface Partition (.cppm)**
```cpp
// Geometry.Laplacian.cppm
module;
#include <concepts>
export module Geometry:Laplacian;
// ...
```

**3. Module Implementation Partition (.cpp)**
```cpp
// Geometry.Laplacian.cpp
module Geometry:Laplacian.Impl;
import :Laplacian;
// ... SIMD/GPU optimized implementation
```

**4. Testing & Verification**
```cpp
// Geometry.Tests.Laplacian.cpp
// Verify numerical error convergence
```

**5. Telemetry**
```cpp
// Tracy / Nsight markers
```
