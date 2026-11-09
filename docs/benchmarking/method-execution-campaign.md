# Method execution campaign

ARCH-024's opt-in `IntrinsicMethodExecutionMeasurement` exercises resident K-Means,
its Pause/Step/Run/Stop/Accept controls, and shared GPU query sessions with brute
force and LBVH. This is authored measurement tooling, not performance evidence.
The existing clustering stall harness remains a separate workload.

The implementation is in `tests/benchmark/Test.MethodExecutionMeasurement.cpp`:
runtime composition is a test seam, not a dependency of the public-method
`benchmarks/` source tree. The fixed driver and reporter are
`tools/benchmark/method_execution_campaign.py` and `method_execution_report.py`.
They reuse the existing schema-v2 result sealer/validator and metric families.

## Frozen workload

The driver generates exactly 94 cells. K-Means has two sizes (100k/1M), five paired
seeds (11/23/37/53/71), graphics/separate queues, and previews off/double/triple.
Each process executes one warmup and three fresh logical runs, resetting accepted
outputs with Undo while retaining input identity and service caches. The cap is
20 iterations, seven clusters, Random initialization. Every repeat is compared
outside its measurement interval with the CPU reference: exact labels, completed
iterations and convergence; centroid Linf ≤1e-5; inertia absolute error
≤1e-4·max(1, reference inertia). Paired arms must also complete identical work.

Queries use 4096/32768 targets, 256 queries, k=16, pages of 16, five paired seeds,
and BF/LBVH. Duplicate coordinates and self-exclusions exercise original row IDs
and ties. Every answer is checked against `KNearestReference`, with exact IDs and
squared-distance tolerance 1e-5. One cold sweep precedes three warm sweeps. Page
timings stop only when the shared session returns Ready. Preparation call latency
is reported separately and does not imply GPU completion.

Four control cases (both K-Means sizes and queues, seed11, triple preview) measure
Pause-to-settled, Step1-to-settled, Stop-to-settled and Accept-to-publication. Pause
and Stop must be requested while a worker is active. If the fixed run finishes
before that trigger is observed, the technical check fails; the driver does not
increase the iteration cap or retry. Accept must create exactly one undo entry.

Every GPU arm renders the same reference triangle at 640×480 without VSync. The
point cloud is intentionally unrendered; recorded surface work and extracted
visualization are asserted before starting work. The triangle supplies rendering
contention; preview observations measure publication, not the renderer's consumption
of method previews. Actual queue resolution,
demotion and shared-lock diagnostics remain in raw output. Separate placement
does not establish physical hardware overlap.

## Verification and preparation

Deterministic tooling checks (no performance experiment):

```bash
python3 tests/regression/tooling/Test.MethodExecutionCampaign.py
python3 tools/benchmark/validate_benchmark_manifests.py --strict
cmake --build --preset ci-vulkan --target IntrinsicMethodExecutionMeasurement
```

Use `ci-vulkan-release` for the executable frozen into a performance campaign. The Debug
build above verifies the harness and its deterministic controls; it is not optimized timing evidence.

The measurement executable is opt-in and skips without the two driver-supplied
configuration/output environment variables. It is not an ambient benchmark gate.

Create a small explicit environment JSON containing device, driver, display,
build configuration and `exclusive_gpu`; do not dump the process environment.
Use an ignored evidence directory and absolute paths. Before a results-bearing
campaign, commit the final implementation, build that exact source, then freeze:

```bash
python3 tools/benchmark/method_execution_campaign.py prepare \
  --executable /absolute/path/to/IntrinsicMethodExecutionMeasurement \
  --environment /absolute/path/to/environment.json \
  --packet /absolute/path/to/packet.json
python3 tools/benchmark/method_execution_campaign.py technical \
  --packet /absolute/path/to/packet.json --output /absolute/path/to/technical
```

`prepare --technical-only` permits a dirty-tree development packet, which cannot
authorize a measurement run. Technical mode executes first-seed cases of every
shape and required references with the same per-process budgets. Its output is
pass/fail technical evidence, not a performance result. A clean final packet
needs its own technical receipt.

Attach actual deterministic test evidence to the receipt's `contract_checks`
mapping for `device_completion_not_submission`, `preview_depth_2_and_3` and
`minimized_init_preview_accept`. Each entry contains `command`, `exit_code: 0`,
`source_revision`, `output` (absolute log path), and `output_sha256`. These tests
must exercise held device completion, at least four live preview steps/reuse for
both capacities, and minimized/no-render completion. A string asserting success
is insufficient. The harness itself checks poisoned/empty outputs and a tagged
5ms owner delay against the same evaluators and frame observations.

Full experiment pre-flight requires two fresh independent reviews of the final
packet digest: one Claude and one Codex, neither author. The review JSON contains
`reviews`, with entries `provider` (`claude`/`codex`), distinct `session`,
`author: false`, `verdict: PASS`, `packet_sha256`, `transcript` (absolute path), and
`transcript_sha256`. This receipt records review evidence; implementation review
does not substitute for experiment review. The driver checks exact bindings and
refuses missing/stale receipts:

```bash
python3 tools/benchmark/method_execution_campaign.py run \
  --packet /absolute/path/to/packet.json \
  --reviews /absolute/path/to/reviews.json \
  --technical-receipt /absolute/path/to/technical/technical_receipt.json \
  --output /absolute/path/to/fresh-campaign
```

The process budgets are 60×20s K-Means, 20×10s queries, 4×20s controls, 10×20s CPU
references, and 60s reporting: 1740s plus 60s margin. The global limit is 1800s;
timeouts kill the owned process group, never unrelated workloads. Every process
rechecks frozen source/config/executable bindings. There are no adaptive retries.
A technical error stops the remaining plan; missing cells remain missing. The
operator must keep the explicitly confirmed exclusive GPU environment intact;
this driver does not monitor or kill other GPU users.

## Outputs and interpretation

Raw per-cell JSON, commands, resolved configs and logs remain outside `results/`.
The reporter reads digest-bound raw data, renders `report.html`, and seals up to
three schema-v2 workload summaries. No summary is synthesized for a workload with
no observations. Each passed cell must contain precisely warmup0 and measured1/2/3
(K-Means/queries), or measured0 (controls); each query sweep must cover all sixteen
pages and 256 completed queries. Missing or duplicate runs/pages are rejected.
Partial cell coverage fails its frozen sample-count threshold. Each invocation
gets a new execution UUID in `outcomes.json`, separate from packet identity;
re-rendering preserves its canonical run/attempt IDs. Emitted failed raw files
are also hash-bound. Their available curves appear prominently as rejected
observations and never enter passing summaries.
Outputs remain `claim_eligible: false` until the required post-run referee audit.

Frame curves include instrumentation and known-delay frames, labeled by phase.
All-frame percentiles include warmup, the injected delay and Undo overhead; they
are not steady-state latency. The delay probe must appear in exactly its tagged
frame and containing `VariableTick` interval. An earlier slow frame cannot satisfy it.
Step metric trajectories, page trajectories, control events, before/after point
samples, latency, quality and missing/failed cells are retained. Logical throughput
is N×completed iterations/time (K-Means) or completed queries/time, not GPU
instruction throughput. End-to-end/request/Accept latencies are separate raw
fields; controls include their deliberate pauses in elapsed run time.

`memory_peak_bytes` means readback-ring staging high-water only. Resident, index
and scratch active/pending/idle byte series remain separate because accounting
can overlap; they do not establish total GPU heap use. Upload/readback fields
measure engine-accounted traffic, not hardware bus bytes. Preview copies and
shown-step transitions are observations, not exact preview drop counts.

Five paired seeds are independent units; the three within-process repeats are
noise estimates. Queue comparisons hold preview settings fixed; preview comparisons
hold the queue fixed. A performance conclusion needs paired distributions and an
effect beyond within-seed repeat spread and observation resolution. No significant
speedup, hardware overlap, or plugin-wide performance claim follows merely from
successful execution. These algorithms have no training losses to invent.
