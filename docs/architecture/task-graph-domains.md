# Task Graph Domain Architecture (CPU Task Graph, GPU Render Graph, Async Streaming Graph)

This document defines the graph architecture contract for the canonical `src/` layout and is the implementation guide for Core/Runtime/Graphics graph work.

## 1. Three graph domains and ownership

The engine uses three graph systems with strict ownership boundaries:

1. **CPU Task Graph** (`Core`): deterministic dependency scheduling for simulation/extraction/prep jobs.
2. **GPU Render Graph** (`Graphics.RenderGraph`): pass/resource DAG for GPU execution, barriers, and transient lifetimes.
3. **Async Streaming Graph** (`Runtime` orchestration + `Assets` pipelines): persistent background jobs (IO/decode/process/upload handoff).

### Layering contract (non-negotiable)

- `Core` owns generic graph compilation substrate: nodes, edges, hazards, labels, topological layers, deterministic planning, CPU execution semantics.
- `Graphics.RenderGraph` may reuse Core substrate, but owns GPU concepts (virtual textures/buffers, usage/state transitions, barrier packets, transient aliasing).
- `Runtime::Engine` owns **phase orchestration only**. Runtime must not manipulate GPU resources, barriers, Vulkan objects, or pass-level render branching.
- `Assets` must not import `Graphics`; Graphics consumes Assets through read-only service/cache boundaries.

## 2. Why `Graphics.RenderGraph` is not `Core.TaskGraph(QueueDomain::Gpu)`

`Core.TaskGraph` models **task ordering**. A GPU render graph must additionally model **resource states over time**.

GPU compilation requires resource-aware semantics absent from generic Core scheduling:

- virtual resource declarations and imported-resource contracts,
- per-pass read/write usage declarations,
- hazard-to-barrier lowering (layout/state/access transitions),
- transient lifetime inference and alias opportunities,
- queue handoff semantics (future async compute/transfer overlaps).

Therefore, `Graphics.RenderGraph` is a domain-specific graph system that may call Core compiler utilities for topological ordering, but cannot collapse to a plain queue-domain task scheduler.

## 3. Graph lifecycle states

All graph systems follow the same conceptual lifecycle:

```text
Recording -> Compiled -> Executing/Consumed -> Reset
```

### Recording
- Add passes/tasks and declare dependencies/resources/labels.
- Mutations are legal only in this state.

### Compiled
- Validate declarations, resolve edges, topologically order into deterministic layers.
- Compilation emits immutable execution metadata for the current epoch/frame.

### Executing / Consumed
- CPU graph: execute compiled passes and signal graph-local completion.
- GPU graph: record/submit compiled pass sequence and barriers.
- Streaming graph: launch/advance persistent jobs until completion/cancel.

### Reset
- Clear per-epoch/per-frame transient declarations.
- Handle generations prevent stale references.
- Reset is illegal while an execution token for the same graph is still live.

## 4. Resource hazard semantics (shared conceptual model)

For a resource $R$ and tasks/passes $A, B$:

- **RAW** (Read-After-Write): $A:W(R), B:R(R)$ => edge $A \to B$.
- **WAW** (Write-After-Write): $A:W(R), B:W(R)$ => edge $A \to B$.
- **WAR** (Write-After-Read): $A:R(R), B:W(R)$ => edge $A \to B$.
- **RAR** (Read-After-Read): $A:R(R), B:R(R)$ => no required edge.

Complexity target for hazard edge construction is $O(N + E_h)$ where $N$ is pass/task count and $E_h$ is emitted hazard edge count.

## 5. Label signal/wait semantics

Labels are explicit ordering tokens independent from data resources.

- `Signal(label)` publishes a synchronization point.
- `WaitFor(label)` depends on all signalers of `label` recorded before the waiter.
- Wait with no prior signaler should return compile-time invalid-state unless explicitly optional in API.
- Label edges participate in cycle detection and diagnostics.

## 6. CPU execution contract and graph-local completion

CPU graph execution requirements:

- deterministic topological-layer ordering,
- parallel execution only among same-layer independent tasks,
- graph-local completion token/event per execute call,
- waiting on graph completion must not block unrelated global work,
- deterministic single-thread fallback when workers are unavailable.

## 7. Streaming persistence and cancellation contract

Streaming graph semantics differ from frame-local CPU/GPU graphs:

- tasks can remain pending/running across frame boundaries,
- stable handles use generation checks to suppress stale completion publication,
- cancellation is explicit and race-safe (pending/running states),
- main-thread apply callbacks are routed through a bounded handoff queue,
- shutdown must deterministically drain/cancel before scheduler teardown.

## 8. GPU render-graph contract: resources, barriers, aliasing

### Resources
- resource handles are generation-validated,
- imported resources are external lifetime; transient resources are graph-managed,
- imported initial/final state contracts are explicit.

### Barriers
For each pass use, compiler resolves previous state to next required state and emits barrier packets immediately before the consuming pass executes.
Imported resources append a final packet with `PassIndex = the original declared pass count` (the compiler's end-of-graph sentinel); those final-state transitions are emitted after the last pass so present/layout handoff cannot overtake the work that writes the resource.
Graphics lowers the resulting packets through `RHI::ICommandContext::SubmitBarriers` where the coarse API is available, preserving deterministic ordering while keeping barrier semantics out of `Core`.

### Transient lifetime and aliasing
For virtual resource $v_i$, define live interval $I_i = [f_i, l_i]$ from first/last use pass indices.
Alias is legal only when intervals do not overlap:

$$
I_i \cap I_j = \varnothing
$$

plus descriptor compatibility constraints.

Target complexity:
- lifetime inference: $O(P + U)$ for passes $P$ and uses $U$,
- naive alias search: $O(V^2)$ for virtual resources $V$ (acceptable initially),
- optimized binning/interval packing can reduce practical cost later.

## 9. Runtime frame-phase boundaries (`Engine::RunFrame`)

Canonical high-level phase boundaries:

1. Platform events / resize handling.
2. Fixed-step simulation (CPU task graph jobs).
3. Variable tick.
4. Immutable render input snapshot.
5. Renderer begin frame.
6. Render world extraction (non-blocking streaming reads).
7. Render prep (CPU graph jobs).
8. Render execution (GPU render graph compile/record/submit).
9. End frame + present.
10. Maintenance: transfer retirement + streaming completions/apply/pump.
11. Frame clock finalize.

`Runtime` owns phase sequencing; `Graphics` owns pass-level implementation details.

## 10. Test strategy and quality gates

### Core graph tests
- compiler correctness (topological order, determinism, cycle diagnostics),
- hazard semantics (RAW/WAW/WAR/RAR and weak-read variants),
- label signal/wait behavior,
- execution semantics (graph-local completion, fallback mode, main-thread-only options).

### Runtime/Streaming tests
- persistence across frames,
- cancellation and generation mismatch suppression,
- shutdown ordering and no post-shutdown publication,
- maintenance-phase integration behavior.

### Graphics render-graph tests
- pass/resource declaration validation,
- dependency compilation from resource usage,
- pass culling and lifetime inference,
- barrier generation from usage transitions,
- imported resource contracts and present-path validation,
- null backend execution path observability.

## 11. Review gates

Every phase touching graph behavior must pass:

1. **Layering review:** no upward dependency violations.
2. **Determinism review:** stable output ordering under repeated compiles.
3. **Execution safety review:** no global scheduler wait as normal graph wait path.
4. **Test review:** focused subsystem tests + aggregate test target pass.
