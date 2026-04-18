# sec_new Assets + Core Unified Review and Refactor Resolution Plan

Date: 2026-04-18
Scope: `src_new/Assets`, `src_new/Core`, and migration compatibility with existing `src/Core` + `src/Graphics` graph stack.

---

## 1) Mathematical & Architectural Analysis

### Theory

Model all pending work as a directed graph $G=(V,E)$ where each task packet is a node $v_i\in V$.
Dependency edges represent precedence constraints from data/resource hazards:

- RAW: write $\to$ read
- WAW: write $\to$ write
- WAR: read $\to$ write

Scheduling objective for frame-critical work:

$$
\min_{\pi} \sum_{i\in V} w_i C_i
$$

subject to topological validity:

$$
(i,j)\in E \Rightarrow C_i \le S_j
$$

and resource non-overlap constraints:

$$
\text{conflict}(i,j) \Rightarrow \neg\text{overlap}(i,j)
$$

Critical-path proxy for frame-time risk:

$$
L = \max_{p\in\mathcal P}\sum_{i\in p} c_i
$$

where $c_i$ is estimated task cost. Practical complexity target: schedule construction $O(|V|+|E|)$.

### Architecture

Use a two-level architecture:
1. **Meta-DAG planner**: global dependency correctness and schedule publication.
2. **Three specialized executors**:
   - CPU task graph
   - GPU frame graph
   - async streaming graph

A single graph is mathematically possible, but heterogeneous completion semantics (CPU immediate, GPU fence/timeline, IO latency-dominated) make one-graph execution less predictable in tail latency for production workloads.

---

## 2) Precise Issue-to-Resolution Matrix (Actionable)

Below each issue is mapped to exact refactor actions, acceptance criteria, and owner scope.

### I-01: Coarse lock topology across Assets
**Problem:** multiple lock handoffs (`PathIndex`, `Registry`, `PayloadStore`, loader map, path map, event bus) increase contention risk.

**Resolution:**
1. Introduce epoch-local packet append buffers (single writer per producer callback).
2. Replace cross-structure lock chaining with single publish commit at epoch boundary.
3. Shard mutable metadata by `AssetId.Index % N`.

**Acceptance criteria:**
- P99 lock hold time reduced by >50% under 16-thread stress.
- No lock-order inversion in TSAN stress lane.

### I-02: `AssetRegistry` encapsulation leakage
**Problem:** internal storage/mutex exposed, invariant safety weakened.

**Resolution:**
1. Make storage private only.
2. Add read-only snapshot API for debug/telemetry.
3. Keep state transitions behind validated methods (`Create/SetState/Destroy`).

**Acceptance criteria:**
- No direct external mutation paths remain.
- Invariant tests pass for generation/state lifecycle.

### I-03: `AssetLoadPipeline` semantic gap (modeled IO, synthetic completion)
**Problem:** current path can transition to `Ready` without real staged work packets.

**Resolution:**
1. Emit explicit packets: `AssetIO`, `AssetDecode`, `AssetUpload`, `Finalize`.
2. Each stage completion must be data-driven (completion packet/fence), not synthetic direct call.
3. Persist stage timestamps for every packet.

**Acceptance criteria:**
- End-to-end asset state is derivable from packet trail.
- No `Ready` transitions without prior stage completion events.

### I-04: Unsafe process-level failure (`std::exit`)
**Problem:** low-level path resolver may terminate process.

**Resolution:**
1. Replace fatal exit with `Expected<T, ErrorCode>` propagation.
2. Escalate to app-level policy at boundary (editor/runtime layer).

**Acceptance criteria:**
- No process-killing behavior in low-level core utilities.

### I-05: FileWatcher depends on scheduler liveness
**Problem:** callbacks can be dropped when scheduler is unavailable.

**Resolution:**
1. Add fallback inline dispatch path (or deferred local queue).
2. Count and expose dropped/deferred events.

**Acceptance criteria:**
- No silent drop path.
- Telemetry includes drop/defer counters.

### I-06: Payload store allocation inefficiency
**Problem:** single-value payloads stored as `vector<T>` cause avoidable churn.

**Resolution:**
1. Store payload entries in contiguous arenas/typed blocks.
2. Use singular storage for scalar payloads.
3. Return spans/views as lightweight descriptors.

**Acceptance criteria:**
- Allocation count reduced in load/reload benchmarks.
- Hot read path avoids per-item vector indirection.

### I-07: Type-erasure overhead in hot paths
**Problem:** virtual pool indirection in type pools.

**Resolution:**
1. Move to non-virtual erased dispatch table or compile-time typed pool registry.
2. Keep virtual dispatch out of packet hot path.

**Acceptance criteria:**
- Reduced instruction count on load/read microbenchmarks.

### I-08: Documentation drift
**Problem:** stale README inventories.

**Resolution:**
1. Auto-generate module inventory from CMake target lists.
2. Add CI guard checking README inventory sync.

**Acceptance criteria:**
- CI fails on inventory drift.

### I-09: Existing (`src/`) graph stack vs new (`src_new/`) DagScheduler gap
**Problem:** new `Extrinsic.Core.DagScheduler` is interface-only; existing `Core.DAGScheduler` + FrameGraph/RenderGraph already provide mature hazard/execution behavior.

**Resolution:**
1. Keep existing executors in place.
2. Implement new planner by reusing proven hazard logic from existing scheduler.
3. Route planner slices to current executors until feature parity is complete.

**Acceptance criteria:**
- No regression in cycle detection/hazard correctness during migration.

### I-10: One graph vs three graphs decision
**Problem:** architectural uncertainty.

**Resolution:**
1. Keep global meta-DAG as single source of truth.
2. Retain 3 execution graphs for domain-specific runtime semantics.
3. Allow one-graph mode only for low-complexity prototypes via config.

**Acceptance criteria:**
- Production mode uses meta-DAG + domain executors.
- Prototype mode retains compatibility labels for migration.

---

## 3) Module Interface Partition (.cppm)

```cpp
// Core.DagScheduler.cppm (existing interface target shape)
module;
#include <cstdint>
#include <span>
#include <string_view>
export module Extrinsic.Core.DagScheduler;

import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;

export namespace Extrinsic::Core::Dag
{
    struct ProducerTag;
    struct TaskTag;
    struct ResourceTag;

    using ProducerId = StrongHandle<ProducerTag>;
    using TaskId = StrongHandle<TaskTag>;
    using ResourceId = StrongHandle<ResourceTag>;

    enum class QueueDomain : uint8_t { Cpu, Gpu, Streaming };
    enum class TaskPriority : uint8_t { Critical, High, Normal, Low, Background };

    struct ResourceAccess
    {
        ResourceId Resource{};
        uint8_t Mode = 0; // read/write/readwrite
    };

    struct PendingTaskDesc
    {
        TaskId Id{};
        QueueDomain Domain = QueueDomain::Cpu;
        TaskPriority Priority = TaskPriority::Normal;
        uint32_t EstimatedCost = 1;
        uint32_t CancellationGeneration = 0;
        std::span<const TaskId> DependsOn{};
        std::span<const ResourceAccess> Resources{};
    };

    using EmitPendingTaskFn = bool(*)(void* emitCtx, const PendingTaskDesc&);
    using QueryPendingTasksFn = Result(*)(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit);

    class DagScheduler
    {
    public:
        [[nodiscard]] virtual Expected<ProducerId> RegisterProducer(
            std::string_view name,
            void* producerCtx,
            QueryPendingTasksFn queryFn) = 0;

        virtual Result UnregisterProducer(ProducerId producer) = 0;
        virtual Result QueryAllPending() = 0;
        [[nodiscard]] virtual Expected<std::span<const TaskId>> BuildSchedule(uint64_t frameIndex) = 0;
        virtual void ResetEpoch() = 0;
        virtual ~DagScheduler() = default;
    };
}
```

```cpp
// Core.AssetPacketBridge.cppm (new bridge interface)
module;
#include <span>
export module Extrinsic.Core.AssetPacketBridge;

import Extrinsic.Core.Error;
import Extrinsic.Core.DagScheduler;

export namespace Extrinsic::Core::Dag
{
    struct AssetPacketBridge
    {
        virtual Result EmitAssetPackets(std::span<const PendingTaskDesc> out) = 0;
        virtual ~AssetPacketBridge() = default;
    };
}
```

---

## 4) Module Implementation Partition (.cpp)

```cpp
// Core.DagScheduler.Impl.cpp (sequence, not full code)
module Extrinsic.Core.DagScheduler.Impl;
import Extrinsic.Core.DagScheduler;

// Step 1: Query producers -> epoch-local packet store
// Step 2: Validate handles/dependencies and detect cycles
// Step 3: Build topo order + domain slices + priority ordering
// Step 4: Publish immutable schedule snapshot
// Step 5: Hand off CPU/GPU/Streaming slices to existing executors
```

Refactor rollout (strict order):
1. **Control plane first:** implement planner internals while keeping existing executors.
2. **Assets bridge:** replace direct transitions with packet emission.
3. **Parity phase:** ensure hazard/cycle correctness equals current `src` stack.
4. **Optimization phase:** sharding/SoA/lock-light migration.
5. **Default switch:** enable planner-driven mode after quality gates pass.

---

## 5) Testing & Verification

```cpp
// Core.Tests.DagScheduler.Parity.cpp
// - Compare old Core.DAGScheduler edges/layers vs new planner output on same inputs.

// Core.Tests.DagScheduler.Cycle.cpp
// - Ensure cycle detection returns InvalidState and emits diagnostics.

// Core.Tests.DagScheduler.Contention.cpp
// - 16-thread producer query and append stress, validate no dropped packets.

// Core.Tests.AssetPacketization.cpp
// - Verify AssetIO->Decode->Upload->Finalize chain and generation-safe cancellation.

// Core.Tests.MigrationCompatibility.cpp
// - New planner slices correctly route into existing FrameGraph/RenderGraph executors.
```

Mandatory gates:
- TSAN clean on scheduler + Assets packet bridge.
- Deterministic schedule hash stable across 1000 replay runs.
- P99 planner build latency under configured budget.
- No correctness regressions relative to current `src` graph behavior.

Complexities:
- Query + append: $O(N)$
- Validation + topo: $O(N+E)$
- Resource conflict indexing: expected $O(N)$
- Memory: $O(N+E+R)$

---

## 6) Telemetry

```cpp
// Required counters/timers
// Dag.Query.DurationNs
// Dag.Build.DurationNs
// Dag.TaskCount
// Dag.EdgeCount
// Dag.CriticalPathCost
// Dag.ReadyQueueDepth.P50/P95/P99
// Dag.Publish.ContentionCount
// Dag.Cancellation.DropCount
// Asset.PacketEmit.FailCount
// Asset.StageLatency.IO/Decode/Upload/Finalize
```

Operational SLOs:
- `Dag.Build.DurationNs` P99 < planner budget slice.
- `Dag.Publish.ContentionCount` near-zero under steady load.
- `Dag.ReadyQueueDepth.P99` bounded under representative workloads.
- `Asset.PacketEmit.FailCount` == 0 in normal operation.

