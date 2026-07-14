# Core scheduler smoke benchmark

`IntrinsicSchedulerBenchmarkSmoke` is the PR-fast, synthetic CPU benchmark for
the generic Core task scheduler. Its stable ID is
`core.scheduler_hardening.smoke`.

The workload reports median dispatch-and-drain throughput, external steal
latency from a worker-local deque, and a priority-inversion probe. The priority
probe queues low work before high work behind a blocked single worker; its
`quality_error_l2` is the number of low-priority callbacks occupying the first
high-priority-sized execution window. Zero is the required scheduler contract.

Warmup and measured populations are declared in the manifest. The smoke uses
only built-in synthetic work, writes schema-valid result JSON, and is suitable
for the default CPU PR gate. Steal queue-contention telemetry is also the
measurement used to decide whether the optional Chase–Lev deque rewrite has
evidence supporting its added complexity.
