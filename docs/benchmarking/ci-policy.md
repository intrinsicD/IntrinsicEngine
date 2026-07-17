# Benchmark CI Policy

Benchmark execution is split by cost so CI remains fast while still guarding quality.

## PR-fast expectations

- Classify the exact merge-base-to-head pull-request diff before toolchain
  setup.
- Run docs/task-only structural routes without a C++ configure or build.
- Use the unsanitized Null/headless `ci-fast` preset for focused and bounded
  broad source feedback, with strict post-configure registry reconciliation.
- Preserve the full CPU, sanitizer, and Vulkan gates as separate required
  confidence signals.

### Touched-scope feedback and smoke admission

`pr-fast` publishes `ci-pr-fast-touched-scope-route`, containing the exact
changed-file records, selection reasons, fallback state, configured producer
inventory, selected CTest inventory, command closure, and per-batch timing.
Missing refs, diff failure, zero PR files, rename/delete/type ambiguity, module
interfaces, headers, CMake/toolchain/dependency inputs, and unknown paths
cannot produce an empty success; they select the bounded broad route or fail
on a malformed configured registry.

The focused lane initially excludes the candidate `IntrinsicPrSmokeTests`
cross-layer smoke. The admission budget was declared on 2026-07-17 before any
`ci-fast` hosted result was read: at least five comparable
`ubuntu-24.04`/Clang samples at one source and preset identity, no more than 5%
unique incremental Ninja command closure relative to `IntrinsicPrFastTests`,
and at most 60 seconds nearest-rank p95 for the incremental smoke build plus
exact smoke-test batch. Median, p95, cache state, selected cases, and run IDs
must be retained. Until that evidence passes, the smoke remains broad-only;
docs/task-only routes never pay it.

### Monolithic smoke ownership and budget

The required `ci-bench-smoke` pull-request workflow owns execution of the
22-result `IntrinsicBenchmarkSmoke` aggregate. It builds and runs the
`IntrinsicBenchmarks` target, then invokes strict result validation before
uploading the complete result directory. Its runner step has a two-minute
bound. The matching CTest `Run` → `Validate` fixture pair remains available
for explicit local/nightly selection, but carries the
`benchmark;geometry;graphics;physics;slow` labels. The opt-in CTest runner uses
the same 120-second slow-test limit; the CTest schema validator retains its
focused 30-second limit. Consequently, the default CPU-supported `-LE slow`
gate does not execute the same monolithic workload concurrently with thousands
of correctness tests.

`BUG-088` classified the pair from seven same-branch `ubuntu-24.04`, Clang 20,
`ci`-preset gate-timing artifacts rather than a passing retry. The measured
`IntrinsicBenchmarks` workflow execution phases (including build-tool
orchestration) were:

| Workflow run | Workflow execution phase |
| --- | ---: |
| [`29519782520`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29519782520) | 38.167 s |
| [`29521730126`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29521730126) | 27.157 s |
| [`29522214318`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29522214318) | 37.551 s |
| [`29524873009`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29524873009) | 35.097 s |
| [`29526309315`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29526309315) | 34.947 s |
| [`29531364487`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29531364487) | 37.893 s |
| [`29532745089`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29532745089) | 37.203 s |

The conventional median is 37.203 seconds and nearest-rank p95 is the observed
38.167-second maximum. Six of seven valid workflow phases took longer than 30
seconds, corroborating that duplicate execution does not belong in the fast
default lane; these aggregate phase measurements are not direct CTest-process
timings. On the same final source, required CPU
[`run 29532745081`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29532745081)
timed out the duplicate at 30.04 seconds while the dedicated benchmark lane
completed and strictly validated all 22 results. These numbers justify lane
classification only; they are not a kernel-performance comparison or speedup
claim.

## CPU source-coverage policy

The manually dispatched `ci-source-coverage` workflow owns the reproducible
Clang CPU source-coverage baseline. It is intentionally absent from pull
request and default-branch triggers until `CI-009` reviews heavy-gate lifecycle
placement. The workflow uses the unsanitized `ci-coverage-cpu` preset and
builds only `IntrinsicCpuTests`; the canonical Linux/Vulkan/Glfw identity is
explicit so CPU-side Vulkan and GLFW contracts are not silently omitted.

Coverage evidence has a narrower meaning than correctness evidence:

| Evidence | What it proves | What it does not prove |
| --- | --- | --- |
| Exact test inventory | The intended executable/case/label set was selected. | That an assertion is meaningful or passed. |
| Covered line/function/region/branch sets | Instrumented execution reached those mapped production locations. | That the reached behavior was checked correctly. |
| Assertions and contract tests | The encoded invariant held for the exercised inputs. | That an untested backend or path operated. |
| Non-skipped backend evidence | The named capability path executed on the recorded backend. | General source completeness or performance. |

Consequently, a global percentage is diagnostic only. It is not a permanent
threshold and equal or higher percentages do not establish no-loss parity.
For a test-only refactor, the comparison first requires identical production
source, production CMake/preset/dependency inputs, normalized production
compile commands, compiler and LLVM tools, preset/backend identity, exclusion
policy, and execution mode. It then fails when any previously covered
production region or either outcome of a covered branch disappears. New
coverage is allowed. Test executable names and their target-keyed working
directory diagnostic may change during a test-only split. A separate identity
keyed by stable test case names still requires every case to run from the same
normalized working directory, and the comparator also requires the same
selector, common CTest environment, profile mode, and all other execution
identity fields. Changed-production-line coverage is emitted separately as
informational diff evidence; threshold policy waits for repeated baselines
under `CI-009`.

### Included and excluded sources

The engine-owned production roots are C++ source and headers under `src/` and
`methods/`. The deterministic exclusion policy removes tests, benchmarks,
configured build/generated files, checked-in assets, vcpkg and other external
or third-party code, and compiler/runtime sources. LLVM's full JSON export is
retained beside the normalized report so filtering and summary calculations
remain auditable.

Every executable in the generated `IntrinsicCpuTests.txt` inventory must
exist, match the CPU selection derived from `RegisteredTestTargets.tsv`, emit
an instrumented profile, and appear in the one `llvm-cov` object set.
GoogleTest producers run once with the exact enabled-case filter derived from
CTest, and their machine-readable XML must name that exact executed set;
manual producers run through CTest separately. This coverage-reporting-only
batching reduces thousands of process/profile shards without changing the
authoritative case-isolated correctness gate. Profile paths include the target
plus `%m-%p`, and all execution shards merge with
`llvm-profdata --failure-mode=any`; missing or corrupt inputs fail nonzero.
CTest discovery uses a separate collision-safe profile namespace retained for
diagnostics and never merged into execution evidence.

### Artifacts and reproduction

`coverage.json` records schema version, production source/build-input and
compile-command digests, compiler/tool versions, preset/build/backend identity,
the exclusion policy, profile diagnostics, per-file summaries, and normalized
covered line, function, region, and true/false branch-outcome keys. The
artifact also retains the exact case/label/executable inventory in
`test-inventory.json`, per-target GoogleTest XML, raw `llvm-cov` JSON, raw
profiles, the merged profile, per-executable logs, and failure diagnostics. The
named hosted run and gate-timing artifact identify the commit. Absolute
source/build paths are normalized out of comparable identities.

Local reproduction is:

```bash
cmake --preset ci-coverage-cpu --fresh
cmake --build --preset ci-coverage-cpu --target IntrinsicCpuTests
python3 tools/ci/run_source_coverage.py \
  --build-dir build/ci-coverage-cpu \
  --output build/ci-coverage-cpu/coverage \
  --preset ci-coverage-cpu \
  --diff-base HEAD^
python3 tools/ci/compare_source_coverage.py \
  --baseline <baseline>/coverage.json \
  --candidate build/ci-coverage-cpu/coverage/coverage.json \
  --test-only-refactor
```

The output directory must be absent or empty so stale raw profiles cannot enter
a new result. A claim-grade full baseline names the hosted workflow run and
artifact rather than checking the potentially large profile set into Git.

## Nightly/deep expectations

- Run deeper correctness/performance suites.
- Include broader datasets and optional GPU workloads.
- Publish benchmark artifacts for trend tracking.

## Gate latency telemetry

Compile-heavy workflows emit one result for
`ci.gate-latency.github-ubuntu-24.04.v1`. The result measures configure, build,
and test/execution phases and reports their sum as `total_time_ms`; it does not
include runner queue time, dependency installation, unrelated structural
checks, or artifact upload.

The result uses the canonical benchmark JSON schema with
`backend: external_baseline`. Gate identity, preset, compiler, sanitizer,
runner image, cache state, selected test count, Ninja command-edge count, and
cache diagnostics remain explicit. The five configured graph-identity fields
(`EXTRINSIC_PLATFORM`, `EXTRINSIC_BACKEND`, requested and selected platform
backend, and the headless flag) are read from the gate's `CMakeCache.txt`;
missing or incomplete explicit cache input fails aggregation closed.
Unavailable counters are represented by zero plus a matching
`*_available: false` diagnostic rather than being silently invented.

Cold and warm-cache samples are reported separately. Performance claims require
at least five comparable samples and cite median and p95 plus the exact commit,
runner image, preset, sanitizer, and gate selector.

### Warm-configure failure guard

`time_command.py --max-warm-seconds` is a hard failure only when
`actions/cache` reports an exact vcpkg binary-cache hit. `BUG-081` calibrated
the guard from contemporary exact-hit GitHub Actions runs collected on
2026-07-09 through 2026-07-16. Every population used `ubuntu-24.04`, Clang 20,
the workflow's declared preset, and an exact cache identity; samples span image
versions `20260705.232.1` and `20260714.240.1`. Median uses the conventional
midpoint rule and p95 uses nearest rank, so p95 is the observed maximum for
these small populations.

| Guarded context | Exact-hit samples (run: seconds) | Median | p95 |
| --- | --- | ---: | ---: |
| `pr-fast` | [29326012528](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326012528): 6.882; [29310409627](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29310409627): 7.749; [29306897219](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29306897219): 6.082; [29290382724](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290382724): 15.464; [29211278659](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29211278659): 6.515; [29211167225](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29211167225): 22.002 | 7.316 s | 22.002 s |
| `ci-linux-clang` | [29505287087](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29505287087): 9.959; [29501697671](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29501697671): 12.952; [29491998461](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29491998461): 9.509; [29488777640](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29488777640): 10.741; [29488509696](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29488509696): 5.922; [29485440146](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29485440146): 18.261; [29482204431](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29482204431): 11.490; [29482105867](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29482105867): 9.426; [29333921619](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29333921619): 6.504; [29326805774](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326805774): 8.656; [29326012533](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326012533): 8.775; [29311863286](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29311863286): 11.253; [29310409591](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29310409591): 16.558; [29306897210](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29306897210): 10.784; [29290396424](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290396424): 6.117; [29290382868](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290382868): 10.894 | 10.350 s | 18.261 s |
| `ci-sanitizers` ASan | [29519782498](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29519782498): 30.368; [29326012509](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326012509): 15.137; [29310409592](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29310409592): 13.916; [29306897170](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29306897170): 11.365; [29290382723](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290382723): 12.115; [29204545802](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29204545802): 10.013 | 13.016 s | 30.368 s |
| `ci-sanitizers` UBSan | [29326012509](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326012509): 10.598; [29310409592](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29310409592): 11.399; [29306897170](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29306897170): 10.077; [29290382723](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290382723): 8.710; [29204545802](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29204545802): 8.758 | 10.077 s | 11.399 s |
| `ci-vulkan` | [29326012577](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326012577): 5.208; [29310409655](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29310409655): 6.198; [29306897280](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29306897280): 14.553; [29290382751](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290382751): 9.333; [29280699135](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29280699135): 10.406; [29278614647](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29278614647): 5.943; [29277091536](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29277091536): 15.074; [29275795415](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29275795415): 10.871; [29204545763](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29204545763): 10.656 | 10.406 s | 15.074 s |
| `ci-bench-smoke` | [29519782520](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29519782520): 11.251; [29326012574](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29326012574): 10.211; [29325473494](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29325473494): 12.525; [29310409612](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29310409612): 13.450; [29306897190](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29306897190): 7.508; [29290382853](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29290382853): 10.294; [29204545807](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29204545807): 11.795; [29082107982](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29082107982): 14.019; [29079330843](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29079330843): 9.363 | 11.251 s | 14.019 s |
| `nightly-deep` CPU | [29475153703](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29475153703): 7.492; [29392444882](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29392444882): 14.153; [29309516631](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29309516631): 8.492; [29227934714](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29227934714): 11.781; [29182066778](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29182066778): 19.032; [29141930320](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29141930320): 10.315; [28998474622](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/28998474622): 24.265 | 11.781 s | 24.265 s |

Direct job-log comparison retained the exact configured/restored primary cache
identity. Three keys appear in the table:

- `Linux-vcpkg-5473e109440db896a38daaa7aa8eefd0d12331c980f6c350c190cb108ad20c1b`
  covers every sample except the runs enumerated below.
- `Linux-vcpkg-51c6cfee9c817d807dcfa0537b51e86c806aa8f10aaa7d35037f0d43dd873785`
  covers `pr-fast` runs `29211278659` and `29211167225`, both sanitizer
  contexts in run `29204545802`, `ci-vulkan` run `29204545763`,
  `ci-bench-smoke` runs `29204545807`, `29082107982`, and `29079330843`,
  and nightly runs `29141930320`, `29182066778`, and `29227934714`.
- `Linux-vcpkg-018e2147eaa8a1e2b807e186151e000be7ed0ad931c1822a99e99b9a6813cfa9`
  covers nightly run `28998474622`.

There are no restore-key-only samples in the retained population. Runner agent
version was `2.335.1` throughout. Image `20260714.240.1` covers ASan run
`29519782498`, benchmark-smoke run `29519782520`, and Linux runs `29505287087`,
`29501697671`, `29488777640`, `29488509696`, `29485440146`, and `29482204431`;
all remaining samples use image `20260705.232.1`.

The fleet-wide rule is `ceil-to-5-seconds(1.25 × max context p95)`. The maximum
observed p95 is ASan's `30.368 s`; adding a 25% margin yields `37.960 s`, which
rounds to a `40 s` hard limit. All seven workflow call sites use that one value.
The optional self-hosted nightly GPU job has not produced a comparable five-sample
population, so its otherwise identical call site conservatively inherits the
fleet maximum instead of an unevidenced lower threshold.

The newer hosted image weakly correlated with higher `ci-linux-clang` timing
(new-image median `11.116 s`, old-image median `9.468 s`), but ranges overlap
and the new image also produced a `5.922 s` sample. The evidence does not
distinguish shared-runner variance, image/context interaction, or source/config
interaction as the cause of the `30.368 s` ASan tail sample. The calibration
retains that observed tail without making a causal claim.
`tests/regression/tooling/Test.CiTiming.py` pins all seven call sites to the
calibrated finite value and separately proves that an exact cache hit above the
configured limit still exits non-zero.

The CI-003 pre-optimization baseline is
[`benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json`](../../benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json).
It contains five GitHub Actions API samples for each required gate and both
sanitizer matrix legs. Median and p95 use the nearest-rank method; with five
samples p95 is the observed maximum. All baseline compiles are cold because
ccache was absent, while the vcpkg binary-package cache was warm. The `pr-fast`
ccache policy persists only ccache's external content store, reports hit/miss,
cache-size, error, and availability diagnostics, and treats both exact and
compatible-prefix restores as warm samples. Each immutable saved key includes
the configured compiler/scanner, pinned ccache, preset, sanitizer identity,
toolchain/dependency-input hash, and commit SHA. Ccache 4.9.1 passes `.cppm`
compiles through; cacheable C++ consumers run with direct and depend modes off
and hash a deterministic digest of every repository module interface through
`CCACHE_EXTRAFILES`. No warm-compile population existed in the CI-003 baseline,
and the baseline records that absence rather than combining cache states.

Each CI-003 baseline population uses the same five pull-request commits. The
retained run/job IDs were checked against all 30 jobs and 25 workflow runs
through the authenticated GitHub API; workflow identity, SHA, event,
conclusion, runner image, phase/job durations, and vcpkg cache-step completion
matched the artifact. Compare future cohorts by median/p95 rather than a single
run because the sanitizer samples retain substantial cold-build variance.

Before the measured build, `pr-fast` runs a hermetic named-module invalidation
probe with the compiler and `clang-scan-deps` selected by the configured `ci`
preset. The probe uses its own ccache config and content store, keeps the module
implementation and importer byte-for-byte and mtime-stable, and requires an
empty-cache miss, unchanged-source hits, misses after an interface-only layout
change, Ninja importer invalidation, and output parity with a clean no-ccache
build. Its JSON is uploaded as `ci-ccache-module-invalidation-pr-fast`; probe
time and probe-cache statistics are not included in the measured gate result.

The aggregate uses the distinct benchmark ID
`ci.gate-latency.github-ubuntu-24.04.v1.aggregate-baseline` and links to the
per-run `ci.gate-latency.github-ubuntu-24.04.v1` profile through
`diagnostics.source_benchmark_id`. It reports population/sample counts and
grouped cold-population statistics; it does not impersonate the direct
configure/build/test/total metrics emitted by one gate invocation.

The sampled Vulkan test phase predates the completed Xvfb split and failed
before frame-pacing capture, so its test/total timing is diagnostic only. Use
its configure/build baseline until five post-BUG-064 test samples accumulate.

The result artifact is uploaded as `ci-gate-timing-<gate>` (with the sanitizer
name appended for matrix legs) and contains one canonical `result.json`.
Configure, build, and test/execution phase inputs remain job-local and are not
uploaded. If a measured phase fails or never starts, aggregation still emits an
`error`/`failed` result with phase diagnostics and fails closed.

### CI-007 retained ccache evidence

`CI-007` retained the ccache policy in `pr-fast` only after a contemporary
five-sample cold/warm comparison on commit `5394e51b`. Every sample used
`ubuntu-24.04`, Clang 20, the `ci` preset with project-default ASan/UBSan,
3,549 selected tests, 1,955 Ninja command edges, and an exact vcpkg cache hit.
Every gate and hermetic invalidation probe passed with zero ccache errors.

| Population | Runs | Ccache result per run | Build median / p95 | Total median / p95 |
| --- | --- | --- | ---: | ---: |
| Cold | [29113978973](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29113978973), [29115473396](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29115473396), [29117419922](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29117419922), [29119188858](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29119188858), [29127596925](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29127596925) | 0 hits / 575 misses | 1,406.634 s / 1,510.259 s | 1,634.116 s / 1,753.939 s |
| Warm | [29208730070](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29208730070), [29209234114](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29209234114), [29209736454](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29209736454), [29210235661](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29210235661), [29210606175](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29210606175) | 575 hits / 0 misses | 609.476 s / 630.449 s | 856.085 s / 877.683 s |

The warm population reduced build median by 56.7% and build p95 by 58.3%; total
median and p95 fell by 47.6% and 50.0%. The retained policy is therefore the
bounded `pr-fast` store described above. No other gate consumes it, and any
future expansion requires its own comparable correctness and timing evidence.

The separate repository-interface sample used evidence-only commit `4befbe1e`,
which added one static exported API revision to `Extrinsic.Core.Error` without
changing object layout or importer sources. Hosted run
[29211278659](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29211278659)
restored the compatible prior store, then reported 0 hits, 575 misses, and zero
errors before passing all 3,549 selected tests. The matching clean local
no-ccache source state passed the complete 3,617-test CPU gate. The temporary
API marker was removed by `fd97d4d1`; it was never merged into the production
source surface.

## Stale-run cancellation

Compile-heavy pull-request workflows use this workflow-level concurrency key:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true
```

A newer commit cancels only an older run of the same workflow for the same pull
request. Different pull requests and default-branch runs have distinct groups.
Manual/push events fall back to the full Git ref. The `ci-linux-clang`
default-branch trigger disables cancellation explicitly so every merged commit
retains its full confidence result and timing artifact.

## Failure policy

- Schema validation failures are hard failures.
- Missing benchmark result roots are reported as blocked prerequisite failures;
  run the benchmark producer target before treating validation as actionable.
- Smoke threshold regressions fail PR-fast once enabled in strict mode.
- Heavy-suite regressions are triaged with explicit follow-up tasks.
