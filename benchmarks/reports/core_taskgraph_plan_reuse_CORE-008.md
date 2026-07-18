# CORE-008 compiled TaskGraph plan-reuse result

This report records a matched-host, order-balanced comparison for:

- `core.taskgraph_plan_reuse.ecs3.smoke`
- `core.taskgraph_plan_reuse.renderprep9.smoke`

It supports CORE-008's exact-structure replay decision. It is not a
cross-machine, end-to-end frame-time, render-graph, or general scheduling
claim.

## Provenance and procedure

- Baseline production and frozen-harness revision:
  `e8df606f794dc8a77aa03e962fa8f834705effb0`.
- Candidate production revision:
  `8a50b725eea65a680b04f48e1d01a9ce81f1be33`.
- Test-only verification revision:
  `28ffc72e41a1b5aa7e225f472a4667b3eca87848`. It changes only
  `Test.RenderPrepPipeline.cpp`; no production or benchmark-harness source
  differs from the candidate revision.
- The benchmark declaration, implementation, two manifests, and monolithic
  runner path are byte-identical between baseline and candidate.
- Preserved baseline executable SHA-256:
  `15e536b748d8b99679fe51329e42dc7bdd9b2a6f205ce97170ea4b28a49f539a`.
- Preserved candidate executable SHA-256:
  `5fd26220e157100d777e6ecbef3df72e52554fab61402ebe51e5fa5644b0b498`.
- Build: local optimized `ci-release`, unsanitized Release, Clang 23.0.0.
- Host: Linux 6.14.0-37-generic x86_64; 11th Gen Intel Core i9-11900KF,
  8 cores / 16 threads.
- Backend: `cpu_optimized`; TaskGraph execution mode: `PlanOnly`.
- Datasets:
  `builtin.synthetic_taskgraph.ecs_bundle_3.v1` and
  `builtin.synthetic_taskgraph.render_prep_9.v1`.
- Timed scope: pass registration, `Compile()`, and `ResetForReplay()`.
- Sampling inside each result: three warmup batches followed by nine measured
  batches of 512 epochs; `runtime_ms` is the median per-epoch runtime.
- Comparison population: five baseline/candidate pairs collected sequentially
  from the preserved executables. Order alternated
  B/C, C/B, B/C, C/B, B/C.
- Each executable invocation emitted 25 result JSON files. All ten capture
  directories passed strict validation: 250 of 250 payloads.

The durable pre-change payloads are
[`core_taskgraph_plan_reuse_ecs3_smoke_e8df606f.json`](../baselines/core_taskgraph_plan_reuse_ecs3_smoke_e8df606f.json)
and
[`core_taskgraph_plan_reuse_renderprep9_smoke_e8df606f.json`](../baselines/core_taskgraph_plan_reuse_renderprep9_smoke_e8df606f.json).
They are median representatives from an earlier independent five-run baseline
population. The comparison below uses the later order-balanced population, so
its baseline medians differ slightly.

## Result

Ranges are the minimum and maximum of five run-level results. The paired effect
is the median of the five per-pair candidate/baseline ratios, with its pairwise
range.

| Workload | Measurement | Baseline median [range] | Candidate median [range] | Paired effect, candidate / baseline |
| --- | --- | ---: | ---: | ---: |
| ECS-like, 3 passes | Runtime (ms/epoch) | 0.001017 [0.001014-0.001031] | 0.000158 [0.000156-0.000173] | 0.154376x [0.153249-0.168288] |
| ECS-like, 3 passes | Throughput (epochs/s) | 983,180 [969,758-986,047] | 6,338,673 [5,780,804-6,392,249] | 6.480003x [5.942519-6.536348] |
| Render-prep-like, 9 passes | Runtime (ms/epoch) | 0.002576 [0.002521-0.002651] | 0.000388 [0.000382-0.000392] | 0.150621x [0.145228-0.155494] |
| Render-prep-like, 9 passes | Throughput (epochs/s) | 388,174 [377,282-396,652] | 2,579,267 [2,553,196-2,617,627] | 6.644610x [6.436866-6.885315] |

Under these local conditions, the median paired registration/compile/replay
runtime was 84.562% shorter for the three-pass shape and 84.938% shorter for
the nine-pass shape. Every pair moved in the same direction. The corresponding
throughput ratios are the mathematical inverse of runtime and therefore the
same timing signal, not independent corroboration.

The five runtime observations were:

| Pair | Order | ECS baseline / candidate (ms) | ECS C/B | Render-prep baseline / candidate (ms) | Render-prep C/B |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | B/C | 0.001017 / 0.000157 | 0.154376x | 0.002576 / 0.000388 | 0.150621x |
| 2 | C/B | 0.001028 / 0.000173 | 0.168288x | 0.002651 / 0.000385 | 0.145228x |
| 3 | B/C | 0.001031 / 0.000158 | 0.153249x | 0.002573 / 0.000388 | 0.150797x |
| 4 | C/B | 0.001014 / 0.000159 | 0.156805x | 0.002595 / 0.000382 | 0.147206x |
| 5 | B/C | 0.001015 / 0.000156 | 0.153695x | 0.002521 / 0.000392 | 0.155494x |

More importantly, every capture recorded the intended work transition:

| Shape | Compile calls | Baseline builds / reuses | Candidate builds / reuses | Plan-order checksum | Callback-rebind checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| ECS-like, 3 passes | 6,146 | 6,146 / 0 | 1 / 6,145 | 15204780315719745675 | 11373157322135443726 |
| Render-prep-like, 9 passes | 6,146 | 6,146 / 0 | 1 / 6,145 | 15299564801597488601 | 9452821555824480147 |

All twenty task-graph result payloads reported zero quality error and zero
failures. Baseline and candidate plan-order and callback-rebind checksums match
for each shape. Thus the candidate built once, reused every subsequent
accepted compile, preserved dependency order, and executed callbacks rebound
for the current validation epoch.

## Engineering decisions

- Replay is explicit through `ResetForReplay()`; destructive `Reset()` retains
  its prior cold-rebuild semantics.
- A plan is reused only after exact ordered comparison of pass names and count,
  compilation-affecting options, resource declaration origin/identity/mode,
  explicit dependencies and reasons, and raw wait/signal labels. Callback
  identity is excluded and current callbacks are rebound on every epoch.
- Structural mismatch or compile failure clears plan usability and cannot fall
  back to stale topology or callbacks.
- The implementation retains two bounded pass-node banks and one successful
  topology. It does not introduce a global name interner, hash-only validity
  decision, or pooled execution state whose lifetime could overlap an escaped
  completion handle.
- Successful compile paths avoid eager edge-reason metadata, and submission
  avoids transient ready-list vectors and captured callback trampolines. No
  direct allocation count was collected, so this report makes no
  allocation-rate claim.
- The fixed-step runtime and persistent renderer-prep owner use the replay
  lifecycle. Runtime shape changes recompile transparently, while stable
  shapes expose one build followed by reuse through public counters.

## Cold-path SLO

The existing optimized 2,000-node destructive-reset Architecture SLO remained
green at the test-only verification revision:

| Metric | Observed | Budget |
| --- | ---: | ---: |
| Cold/rebuild compile p99 | 126,887 ns | 350,000 ns |
| Execute p95 | 793,500 ns | 8,333,334 ns |

This test calls destructive `Reset()` and re-registers the graph each
iteration, so replay caching cannot hide a cold-compilation regression.

## Correctness and validation

CORE-008 adds deterministic coverage for:

- exact replay with fresh callbacks and preserved dependency order;
- invalidation on pass count/name, options, resource origin/identity/mode,
  dependencies, reasons, and labels;
- fail-closed recovery after a changed replay fails;
- valid empty compiled graphs;
- completed-handle lifetime across a later replay submission;
- two real Null-runtime fixed ticks with one build and subsequent reuse;
- app/module shape transitions that rebuild; and
- persistent renderer-prep reuse, current inputs, sequential fallback, forced
  failure recovery, and owner-thread callback execution with scheduler workers.

The branch through `28ffc72e` passed the default CPU-supported gate with
4,096/4,096 selected tests and one expected GLFW/LSan lifecycle skip. Fresh,
serial grouped sanitizer gates passed 2,750/2,750 under ASan in 226.93 seconds
and 2,750/2,750 under UBSan in 90.16 seconds; the UBSan run skipped the one
ASan-only GLFW lifecycle case. All 29 benchmark manifests passed validation,
and all ten paired capture directories passed strict validation for their
complete 25-result sets.

## Limitations

- This is one local host and five short paired captures, not a cross-runner
  confidence interval or release-wide performance characterization.
- The workloads are synthetic linear chains with three and nine passes.
  Dynamic real-world graphs may have different equality-check and rebuild
  costs.
- The timed region excludes pass execution. Callback freshness and topology
  correctness are validated separately outside the timed batches.
- The measurements establish reduced registration/compile/replay overhead and
  avoided topology builds, not end-to-end simulation or rendering frame time.
- Candidate payloads remain temporary artifacts because checking a second
  result under the same stable `benchmark_id` into baseline discovery would be
  ambiguous. The executable hashes and summarized population preserve the
  comparison's session provenance.
- No Vulkan path, render-graph cache, memory footprint, or allocation-rate
  result is claimed.

## Reproduction

At clean checkouts of the baseline and candidate production revisions, build
and preserve each executable once:

```bash
cmake --preset ci-release --fresh
cmake --build --preset ci-release \
  --target IntrinsicBenchmarkSmoke IntrinsicBenchmarkTests
LABEL=baseline-e8df606f # use candidate-8a50b725 at the candidate revision
cp build/ci-release/bin/IntrinsicBenchmarkSmoke \
  "/tmp/IntrinsicBenchmarkSmoke-core008-$LABEL"
sha256sum "/tmp/IntrinsicBenchmarkSmoke-core008-$LABEL"
```

Collect five pairs, alternating which preserved executable runs first:

```bash
BASELINE=/tmp/IntrinsicBenchmarkSmoke-core008-baseline-e8df606f
CANDIDATE=/tmp/IntrinsicBenchmarkSmoke-core008-candidate-8a50b725
OUT=/tmp/core008-paired-8a50b725-v2

GIT_COMMIT=e8df606f794dc8a77aa03e962fa8f834705effb0 \
  "$BASELINE" "$OUT/pair-1/baseline"
GIT_COMMIT=8a50b725eea65a680b04f48e1d01a9ce81f1be33 \
  "$CANDIDATE" "$OUT/pair-1/candidate"

# Repeat as C/B, B/C, C/B, B/C for pairs 2 through 5.

for capture in "$OUT"/pair-*/*; do
  python3 tools/benchmark/validate_benchmark_results.py \
    --root "$capture" --strict
done
```

Run the correctness and cold-path gates:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

ctest --test-dir build/ci-release --output-on-failure \
  -R '^ArchitectureSLO\.FrameGraphP95P99BudgetsAt2000Nodes$' \
  --timeout 120
```
