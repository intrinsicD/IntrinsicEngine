# src_new Assets/Core Issue Closure Matrix (2026-04-18 Rev B)

This matrix tracks closure status for I-01..I-10 from the review follow-up.

| ID | Status | Closure Notes |
|---|---|---|
| I-01 Coarse lock topology across Assets | ✅ | Added shard-by-index strategy in `AssetPathIndex` (32 shards) and `AssetPayloadStore` (32 shards), reducing global lock contention domains. |
| I-02 Registry encapsulation leakage | ✅ | Already closed in earlier pass. |
| I-03 AssetLoadPipeline semantic gap | ✅ | Added fence-driven completion bridge (`ArmGpuFence`, `CompleteGpuFence`) and persistent stage trails for completed assets. |
| I-04 unsafe std::exit in path resolver | ✅ | Already closed in earlier pass. |
| I-05 FileWatcher scheduler liveness coupling | ✅ | Already closed in earlier pass. |
| I-06 Payload store allocation inefficiency | ✅ | Removed `std::vector<T>{value}` single-item storage; payload now stores typed object in shared payload cell with size metadata. |
| I-07 Type-erasure overhead in hot path | ✅ | Simplified `Asset.TypePool` to pure type-key provider; removed virtual-dispatch erase/type-pool path from payload hot path. |
| I-08 Documentation drift automation | ✅ | Added module inventory generator script + generated inventory doc + CMake drift-check targets. |
| I-09 Old graph vs new DagScheduler parity gap | ✅ | Added concrete `DagScheduler` implementation partition with producer registration, query snapshot, topological planner, lane assignment, and stats. |
| I-10 One graph vs three graphs decision | ✅ | Removed mode-switch complexity and added explicit dedicated interfaces for the three graphs: `CpuTaskGraph`, `GpuFrameGraph`, and `AsyncStreamingGraph`. |

## Remaining caveats

- Full build/test execution is still environment-blocked by missing `libxrandr` during CMake configure.
- Runtime integration points for the new DAG scheduler and fence bridge still need broader engine wiring in higher-level systems.
