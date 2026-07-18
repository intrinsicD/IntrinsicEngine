# Core Benchmarks

Core benchmark manifests and deterministic synthetic workloads live here. The
CORE-007 scheduler smoke uses the stable ID
`core.scheduler_hardening.smoke` and runs inside the monolithic
`IntrinsicBenchmarkSmoke` executable owned by the optimized `ci-release` lane.

The workload reports median dispatch-and-drain throughput, a saturated
TaskGraph priority-inversion probe, and isolated wait-token registry
throughput for one external thread and eight contending threads. The priority
probe registers 64 Background passes before 16 Critical passes behind one
blocked worker. `quality_error_l2` is the number of Background callbacks in
the first 16 execution slots; zero is the required contract. A pre-priority
baseline therefore emits a schema-valid `failed` payload and makes the
monolithic runner return non-zero after writing every result.

The wait-token probe prewarms eight registry slots, excludes thread creation
from its timed population, and reports aggregate contended throughput plus
scaling efficiency in diagnostics. It is measurement evidence for the
measure-first sharding decision, not an independent performance claim.

The baseline public scheduler stats do not expose worker-notification counts,
so this runner does not infer wake behavior from unrelated counters. CORE-007
uses candidate telemetry and deterministic contract tests for conditional-wake
evidence while keeping the before/after benchmark executable identical.

CORE-008 adds two stable workloads under method
`core.taskgraph_plan_reuse`: `core.taskgraph_plan_reuse.ecs3.smoke` models the
three-pass ECS system bundle, and
`core.taskgraph_plan_reuse.renderprep9.smoke` models the nine-pass render-prep
pipeline. Each records three warmup batches and nine measured batches of 512
registration → compile → replay-reset epochs. `runtime_ms` is the median
milliseconds per graph epoch and throughput is graph epochs per second.
Plan-order and fresh-callback checks run outside the timed window and require
zero `quality_error_l2`. Plan-build/reuse counters distinguish the
full-rebuild baseline from the candidate without changing the workload.

Run and validate the complete optimized smoke population with:

```bash
cmake --preset ci-release
cmake --build --preset ci-release --target IntrinsicBenchmarkSmoke
GIT_COMMIT=<commit> build/ci-release/bin/IntrinsicBenchmarkSmoke /tmp/intrinsic-benchmark-smoke
python3 tools/benchmark/validate_benchmark_results.py \
  --root /tmp/intrinsic-benchmark-smoke --strict
```

The expected exit code is `0` for the current candidate. Exit code `2` applies
only when reproducing the preserved CORE-007 pre-priority baseline binary;
validate that emitted directory even though its priority probe intentionally
fails.
