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

Focused source routes currently build only their reconciled owner producers.
Broad routes build `IntrinsicPrFastTests` and then the candidate
`IntrinsicPrSmokeTests` cross-layer smoke; docs/task-only routes never pay
either C++ batch.

The admission budget was declared on 2026-07-17 before any `ci-fast` hosted
result was read: at least five comparable `ubuntu-24.04`/Clang samples at one
source and preset identity, no more than 5% unique incremental Ninja command
closure relative to `IntrinsicPrFastTests`, and at most 60 seconds nearest-rank
p95 for the incremental smoke build plus exact smoke-test batch. The counted
population used commit `1098922a321ba51759ab9b489bfbd8c8af05c562`,
`ci-fast`, Clang/scan-deps 20.1.2, ccache 4.9.1, an exact warm cache hit with
606 hits and zero misses/errors, 19 PR-fast producers, one smoke producer, and
3,740 + 60 exact selected cases in every run:

| Run | PR-fast commands | Incremental smoke commands | Smoke build | Smoke test | Smoke total |
| --- | ---: | ---: | ---: | ---: | ---: |
| [`29582459870`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459870) | 2,007 | 12 (0.598%) | 18.264 s | 1.143 s | 19.406 s |
| [`29582459918`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459918) | 2,007 | 12 (0.598%) | 18.252 s | 1.143 s | 19.395 s |
| [`29582459867`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459867) | 2,007 | 12 (0.598%) | 18.200 s | 1.131 s | 19.331 s |
| [`29582459970`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459970) | 2,007 | 12 (0.598%) | 17.626 s | 0.984 s | 18.610 s |
| [`29582459959`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459959) | 2,007 | 12 (0.598%) | 18.372 s | 1.123 s | 19.495 s |

The broad-route incremental smoke median is 19.395 seconds and nearest-rank
p95 is 19.495 seconds. Its 0.598% incremental closure after the complete
PR-fast aggregate and measured p95 pass the declared numerical limits, but
that result does not establish the cost after a focused owner build:

| Owner | Owner commands | Smoke increment | Owner-relative | PR-fast-relative |
| --- | ---: | ---: | ---: | ---: |
| assets | 201 | 1,200 | 597.015% | 59.791% |
| core | 189 | 1,246 | 659.259% | 62.083% |
| ecs | 1,401 | 12 | 0.857% | 0.598% |
| geometry | 707 | 856 | 121.075% | 42.651% |
| graphics | 1,189 | 348 | 29.268% | 17.339% |
| physics | 1,397 | 12 | 0.859% | 0.598% |
| platform | 155 | 1,232 | 794.839% | 61.385% |
| runtime | 1,563 | 12 | 0.768% | 0.598% |

The smoke target is 1,381 standalone commands and the monolithic
`ExtrinsicRuntime` dependency alone is 1,366. Reducing the five test
translation units to one case would still leave about 1,373 commands, so a
test-only split cannot meet universal focused admission. Splitting production
Runtime solely for CI would violate this task's right-sizing boundary. The
candidate is therefore rejected for focused routes and remains broad-only;
`CI-009` may reconsider only after product-driven target decomposition makes
the configured increment meet the existing budget. Cache-priming run
[`29580789612`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29580789612)
is not in the counted population; its cold diagnostic was 12 incremental
commands after PR-fast and 48.884 seconds.

### CI-005 final routing evidence

The final route comparison uses whole-job time for all three classes because
structural-only jobs intentionally emit no configure/build/test timing result.
For C++ routes it also reports the measured phase total. The named `CI-003`
PR-fast baseline is 1,649/1,713 seconds median/p95 for whole-job time and
1,626/1,670 seconds for measured phase total.

Every population contains five successful `workflow_dispatch` jobs at one
source identity. The docs population uses
`b5df0942ba83ab40a2bcc136949dd7be14303bc6`; all C++ steps were skipped and
the route artifact retained the two selected structural commands:

| Run | Job | Job time |
| --- | ---: | ---: |
| [`29585138136`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138136) | `87900157817` | 8 s |
| [`29585138198`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138198) | `87900151256` | 9 s |
| [`29585138297`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138297) | `87900162595` | 10 s |
| [`29585138413`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138413) | `87900153522` | 10 s |
| [`29585138671`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138671) | `87900155652` | 9 s |

The focused population uses
`eaff576a806bb4a01739547836bbccd09bfd5304`, two reconciled geometry
producers, 707 Ninja commands, 1,596 exact cases, and the same compatible warm
cache in every run (198 hits, one expected miss for the changed implementation
unit, zero errors):

| Run | Job | Phase total | Job time |
| --- | ---: | ---: | ---: |
| [`29585138771`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138771) | `87900153207` | 160.272 s | 221 s |
| [`29585138636`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138636) | `87900153873` | 149.092 s | 186 s |
| [`29585138766`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138766) | `87900157946` | 149.457 s | 194 s |
| [`29585138770`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138770) | `87900156931` | 170.219 s | 217 s |
| [`29585138767`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29585138767) | `87900152871` | 166.081 s | 218 s |

The broad fail-closed population is the same five-run
`1098922a321ba51759ab9b489bfbd8c8af05c562` smoke-budget cohort. Every run
selected 20 producers, 2,019 unique commands, 3,800 exact cases, and an exact
warm cache with 606 hits and zero misses/errors:

| Run | Job | Phase total | Job time |
| --- | ---: | ---: | ---: |
| [`29582459870`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459870) | `87891214200` | 663.938 s | 714 s |
| [`29582459918`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459918) | `87891214737` | 615.927 s | 653 s |
| [`29582459867`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459867) | `87891217187` | 617.695 s | 667 s |
| [`29582459970`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459970) | `87891220710` | 596.108 s | 684 s |
| [`29582459959`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29582459959) | `87891216269` | 653.089 s | 703 s |

| Route | Job median / p95 | Job reduction vs CI-003 | Phase median / p95 | Phase reduction vs CI-003 |
| --- | ---: | ---: | ---: | ---: |
| Docs-only | 9 / 10 s | 99.45% / 99.42% | N/A | N/A |
| Focused geometry | 217 / 221 s | 86.84% / 87.10% | 160.272 / 170.219 s | 90.14% / 89.81% |
| Broad fail-closed | 684 / 714 s | 58.52% / 58.32% | 617.695 / 663.938 s | 62.01% / 60.24% |

These are feedback-latency comparisons, not an isolated causal claim. The
`CI-003` population was cold, sanitized, and used the old selector, while the
new C++ populations are warm, unsanitized, and route-specific. The comparison
therefore measures the delivered policy as a whole; it does not attribute the
reduction to ccache, sanitizer removal, or touched-scope selection separately.

### CI-011 measured slow-cohort evidence

The claim-grade timing comparison uses two manual `ci-linux-clang` profiles on
the same `ubuntu-24.04` hosted-runner protocol, with five samples per cohort,
four-way CTest parallelism, serial PRE_TEST discovery, restored CTest cost data,
and per-sample load diagnostics. Baseline
[`run 29600380925`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29600380925)
used the pre-split test population at `e3fa9187`; candidate
[`run 29600381191`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29600381191)
used the calibrated population at `9830559c`.

| Cohort | Cases | Baseline median / p95 | Candidate median / p95 | Median / p95 reduction | Candidate results over five samples |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full fast CPU | 4,062 | 31.016747 / 31.160935 s | 24.830993 / 24.894463 s | 19.943% / 20.110% | 20,275 pass, 35 skip, 0 fail/error |
| PR-fast | 3,740 | 29.233065 / 29.557700 s | 20.297884 / 20.880414 s | 30.565% / 29.357% | 18,690 pass, 10 skip, 0 fail/error |
| Ordinary-slow timing cohort | 8 | N/A | 4.051059 / 4.083242 s | N/A | 40 pass, 0 skip/fail/error |

The parity comparator proved exact eight-for-eight heavy-case removals and
fast-sentinel additions in both fast populations, with no other case or label
drift. The manual ordinary-slow timing report contains exactly the eight moved
cases, all passing in every sample. The unchanged fast counts therefore do not
mean the stress cases still run in fast gates: the retained sentinels replace
them, and the named ordinary-slow lane owns the originals. The exact mappings
and per-case classification evidence are documented in
[`tests/README.md`](../../tests/README.md).

Scheduled ownership was verified separately by manual nightly-deep
[`run 29603101707`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29603101707)
at `ad264dca`. Its retained `nightly-deep-reports` artifact contained one
`cpu-slow.junit.xml` execution with exactly eight passes and no skips,
failures, errors, duplicates, or unrelated cases
(`sha256:5b402222068439b7297f32cd2b6e2632b3e7e2c789392e280894f446c0f0faac`).

One additional measured outlier,
`RuntimeAssetImportFormatCoverage.AssetImportPipelineAccessorExposesQueueAndEventState`,
waited two seconds before the engine could pump queued work. That harness defect
was removed rather than classified `slow`; the existing blocked-worker contract
remains in `SlowQueuedTextureReadDoesNotBlockRunFrame`.

The `CI-003` and `CI-005` populations remain historical context only. They used
different sanitizer, cache, selector, invocation, and case populations, so the
matched reduction above is not calculated against either and no isolated
per-change speedup is inferred from them.

### CI-008 grouped CTest and worker-budget evidence

Matched hosted run
[`29625346673`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29625346673)
at `886495e78f64b47262db76ecf7cb996d74b06686`, artifact `8424178333`
(`sha256:344f0f8d8960791407e16c2bb6b3f0d704e255d482e6853dac3e459c64b60dc2`),
configured individual and grouped plans from the same source identity. The
workflow built the CPU product once, materialized the grouped binary view with
hardlinks, and failed closed unless all 28 inventoried producer binaries shared
the corresponding inode. Cross-plan selection retained 4,062 logical records
across 28 producers: 4,061 GoogleTest cases across 27 source-backed producers
plus one manual process-level CTest.
`Test.GroupedCTestParity.py registration` proved exact parity for the 4,061
GoogleTest identities and replacement-only preservation of the manual CTest
record. Exactly 1,351 cases from five audited producers changed representation;
physical CPU CTest records fell from 4,062 to 2,716, a reduction of 1,346
process entries. No unaffected producer changed representation.

| CTest budget | Individual median / p95 (s) | Grouped median / p95 (s) | Median / p95 reduction (%) |
| --- | ---: | ---: | ---: |
| `--parallel 1` | 72.278999 / 73.071192 | 62.085994 / 62.533167 | 14.102 / 14.422 |
| `--parallel 2` | 39.256532 / 39.445179 | 37.201451 / 37.313669 | 5.235 / 5.404 |
| `--parallel 4` | 27.715948 / 27.847886 | 29.489894 / 29.551698 | -6.400 / -6.118 |

All 15 alternating matched pairs reported exact logical status parity:
4,055 passed and six disabled, with no failed, skipped, or error cases. The
grouped physical CTest reports retained 2,709 passed records, including the five
wrapper processes, and seven skipped/disabled records; no physical record
failed or errored. The required full CPU workflow therefore retains
`--parallel 4`, the fastest absolute grouped-plan median and p95. This is not a
claim that grouping improves the four-slot plan: grouping improved the matched
one- and two-slot results but regressed the four-slot A/B result by 6.400% at
median and 6.118% at p95.
ASan and UBSan use the same replacement-only representation but remain explicit
at `--parallel 1`; no sanitizer concurrency speedup is claimed. The 41 cases
that deliberately request more than one scheduler worker reserve peak runnable
capacity: 22 use `PROCESSORS=3` and 19 use `PROCESSORS=4`.

Earlier run
[`29622055604`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29622055604)
exposed the individually registered RuntimeWorldRegistry harness race tracked
and retired as `BUG-113`. Because that correction changed executed test code,
none of the earlier run's partial timings enter the table above.

Matched source-coverage run
[`29620815336`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29620815336)
at `4ab702451dc0aaf0a3c16dc98c891cb39499f504`, artifact
`8422537827`
(`sha256:cad43ca733d69d8747f521eea1e95bcd91f05a50ca41fcc8368e7082530cc145`),
reused one instrumented product build for both registration plans. Exact
comparison reported zero gained or lost covered production lines, regions, and
branch arms. Later changes added CTest scheduling metadata, selection
diagnostics, workflow updates, and an explicit completion step in the
individually registered RuntimeWorldRegistry harness. That fixture is outside
the grouped cohort; none of those changes alter instrumented product source or
grouped product execution.

The `CI-003` 3,594-case, 217.53-second full-CPU phase remains historical
context only: its source population, sanitizer topology, and invocation differ.
The same-SHA matched A/B population above is the decision evidence, so no
causal reduction is calculated against `CI-003`.

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
builds only `IntrinsicCpuCoverageTests`. That aggregate combines the default
fast CPU cohort with ordinary slow correctness while excluding benchmark, SLO,
GPU/Vulkan, and quarantined ownership. The canonical Linux/Vulkan/Glfw identity
is explicit so CPU-side Vulkan and GLFW contracts are not silently omitted.

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
keyed by stable test case names still requires every common case to run from
the same normalized working directory, and the comparator also requires the
same selector, common CTest environment, profile mode, and all other execution
identity fields.

The stricter `--test-cohort-transition` mode binds each report to its sibling
test inventory. It permits only the fast sentinels declared by the supplied
transition manifest, requires every moved heavy case in both complete CPU
coverage populations, and permits its labels to change only by adding `slow`.
This prevents a fast-only coverage run from treating routed-out stress cases as
covered by smaller replacements. Changed-production-line coverage is emitted
separately as informational diff evidence; threshold policy waits for repeated
baselines under `CI-009`.

### Clang 20 counter determinism

Coverage normalization rejects negative or saturated exported execution
counters. An `INT64_MAX` value is not treated as covered, uncovered, or an
approved coordinate: LLVM's JSON exporter can clamp an unsigned count derived
from an invalid mapping expression to that value, so accepting it would make
the evidence ambiguous.

Atomic baseline run `29609841968` and candidate run `29609841892` exposed two
production-source manifestations while correctly failing closed. Clang 20
mapped the second use of one renderer `hasNextLevel` condition to a subtraction
that evaluated negative before unsigned conversion and JSON clamping.
Separately, `Scheduler::WaitForAll` reloaded its in-flight count after a failed
pop, making one fallback branch depend on whether a worker completed in that
window. `BUG-112` removed the redundant reload and computes the renderer's
correlated role/offset pair through one control-flow site. No coverage schema,
coordinate allowlist, or test-only production hook was added.

The final serialized-producer pair passed at identical production identity:
baseline
[`29613834782`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29613834782)
(`dc7d09f3`, artifact `8420178739`) and candidate
[`29613834772`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29613834772)
(`ad751bf7`, artifact `8420345114`). Both raw exports are free of saturated
counters. Their shared production, build-input, and compile-command digests are
`b754df9f393e16b4a0384236b1eb06fd9187a18cf8c9703c0ef14bb8a0d0e553`,
`4c24e3f6e30449d04f45257e9a21dbd3494b69a3b317c94f3b448fa00491c619`,
and `6fc85cd42aef613364a666bb180d4d8f1981e74f81bd94575a7ec5a755873622`;
the declared cohort comparison reported zero lost regions and branch arms.

### Included and excluded sources

The engine-owned production roots are C++ source and headers under `src/` and
`methods/`. The deterministic exclusion policy removes tests, benchmarks,
configured build/generated files, checked-in assets, vcpkg and other external
or third-party code, and compiler/runtime sources. LLVM's full JSON export is
retained beside the normalized report so filtering and summary calculations
remain auditable.

Every executable in the generated `IntrinsicCpuCoverageTests.txt` inventory
must exist, match the complete CPU correctness selection derived from
`RegisteredTestTargets.tsv`, emit an instrumented profile, and appear in the
one `llvm-cov` object set.
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
cmake --build --preset ci-coverage-cpu --target IntrinsicCpuCoverageTests
python3 tools/ci/run_source_coverage.py \
  --build-dir build/ci-coverage-cpu \
  --output build/ci-coverage-cpu/coverage \
  --preset ci-coverage-cpu \
  --cohort cpu-coverage \
  --diff-base HEAD^
python3 tools/ci/compare_source_coverage.py \
  --baseline <baseline>/coverage.json \
  --candidate build/ci-coverage-cpu/coverage/coverage.json \
  --test-cohort-transition tools/ci/slow_test_cohort.json
```

The output directory must be absent or empty so stale raw profiles cannot enter
a new result. A claim-grade full baseline names the hosted workflow run and
artifact rather than checking the potentially large profile set into Git.

## Sanitizer and performance build identities

The required CPU and capability gates use explicit sanitizer identities. Each
preset expands `${presetName}` into both its build directory and vcpkg installed
directory, so sanitizer variants do not share project objects, C++ module BMIs,
or installed package trees:

| Preset | Resolved sanitizer | Build directory | vcpkg installed directory | Required owner |
| --- | --- | --- | --- | --- |
| `ci` | `none` | `build/ci` | `external/vcpkg-installed/ci` | Full CPU correctness plus scheduled SLO diagnostics, benchmark, and nightly CPU work |
| `ci-asan` | `asan` | `build/ci-asan` | `external/vcpkg-installed/ci-asan` | Exact CPU cohort under AddressSanitizer |
| `ci-ubsan` | `ubsan` | `build/ci-ubsan` | `external/vcpkg-installed/ci-ubsan` | Exact CPU cohort under UndefinedBehaviorSanitizer |
| `ci-vulkan` | `asan-ubsan` | `build/ci-vulkan` | `external/vcpkg-installed/ci-vulkan` | Promoted Vulkan capability and process-level LeakSanitizer contracts |

`INTRINSIC_ENABLE_SANITIZERS` remains a compatibility switch for raw CMake
callers. Named presets additionally set `INTRINSIC_SANITIZER_MODE` to `none`,
`address`, `undefined`, or `address,undefined`; configure rejects disagreement
between the two inputs. The resolved cache identity is the telemetry authority.

The dedicated ASan and UBSan jobs build `IntrinsicCpuTests` and use only the
canonical exclusion predicate:

```bash
cmake --preset <sanitizer-preset> --fresh \
  -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset <sanitizer-preset> --target IntrinsicCpuTests
ctest --test-dir build/<sanitizer-preset> --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' \
  --no-tests=error --timeout 60 --parallel 1
```

They do not add a positive `-L` filter, because doing so can silently omit a
valid regression-only producer. `CI-008` retained replacement-only grouped
registration for the audited pure cohort, while sanitizer CTest execution stays
explicitly serial at `--parallel 1`; no sanitizer concurrency speedup is
claimed. The variants have distinct defect ownership rather than overlapping
combined CPU compiles: ASan owns address/lifetime/leak findings and UBSan owns
undefined behavior findings. `ci-vulkan` is the sole retained combined
ASan+UBSan preset among required CI gates because
`ExtrinsicSandbox.VulkanShutdownLsanContract` requires address instrumentation
and a LeakSanitizer negative control on the promoted Vulkan path. Developer
presets retain their existing local combined instrumentation semantics.

Nightly CPU correctness, SLO, benchmark smoke, and performance diagnostics
configure the unsanitized `ci` preset once and stay in `build/ci`. The nightly
job does not reconfigure that tree with raw sanitizer flags; required sanitizer
coverage remains in the isolated presets above. `CI-009` owns any later
optimized Release performance identity.

After configuring and building `IntrinsicCpuTests` in all three CPU trees,
capture and compare the normalized producer/case selections with:

```bash
python3 tools/ci/cpu_test_selection.py capture \
  --build-dir build/ci --preset ci --expected-sanitizer none \
  --output build/ci/cpu-test-selection.json
python3 tools/ci/cpu_test_selection.py capture \
  --build-dir build/ci-asan --preset ci-asan --expected-sanitizer asan \
  --output build/ci-asan/cpu-test-selection.json
python3 tools/ci/cpu_test_selection.py capture \
  --build-dir build/ci-ubsan --preset ci-ubsan --expected-sanitizer ubsan \
  --output build/ci-ubsan/cpu-test-selection.json
python3 tools/ci/cpu_test_selection.py compare \
  --report build/ci/cpu-test-selection.json \
  --report build/ci-asan/cpu-test-selection.json \
  --report build/ci-ubsan/cpu-test-selection.json \
  --require-sanitizer none --require-sanitizer asan \
  --require-sanitizer ubsan \
  --output build/cpu-test-selection-parity.json
```

The capture validates the resolved sanitizer mode, compatibility boolean,
capability flags, aggregate producers, labels, and exact path-free CTest case
inventory. Comparison fails if any variant selects a different normalized
cohort. On pull requests, and on manual `ci-linux-clang` runs that retain the
default sanitizer input, that workflow invokes the reusable sanitizer jobs
after starting the unsanitized job, downloads all three selection artifacts,
and requires the same comparison before the workflow can pass. The sanitizer
workflow is reusable rather than independently pull-request-triggered, so
enforcing parity does not duplicate the unsanitized build or test pass.

Manual evidence or diagnosis may run only the unsanitized full CPU job without
rebuilding unchanged sanitizer variants:

```bash
gh workflow run ci-linux-clang.yml --ref <ref> -f run_sanitizers=false
```

The input defaults to `true`; pull requests always run ASan, UBSan, and parity,
and default-branch pushes retain their unsanitized-only topology.

Required timing/selection uploads fail closed. `BUG-111` recorded one hosted
intermediary HTTP 403 after ASan tests and result validation had passed; the
next artifact in the same job succeeded, and a specific-job rerun at the same
SHA finalized both outputs. For the same failure signature, use
`gh run rerun --job <failed-job-id>` rather than rerunning the whole workflow
or sanitizer matrix. A failed artifact attempt does not enter timing evidence.

The full CPU workflow measures exactly the canonical selector; at CI-006
implementation commit `a7ae8e7f` it contained 4,062 cases. The FrameGraph
nanosecond SLO diagnostic runs once in the explicit unsanitized nightly lane,
not again in the pull-request/default-branch debug-correctness job. Nightly's
broad CPU selector excludes `slow`, the SLO lane matches only the exact `slo`
label, and the result-producing benchmark lane owns the two benchmark-smoke
fixture cases.
Before the topology split, the direct full-CPU invocation used a combined
sanitizer tree and therefore skipped its own performance assertions.
Activating the same debug SLO on `ubuntu-24.04` produced 3-5x threshold
excursions in otherwise passing runs `29589810886` and `29589810898`; all
4,062 correctness cases passed in both. No threshold was raised. The scheduled
Debug diagnostic is explicitly non-blocking under `CI-009` and uploads JUnit
when thresholds fail; this is a tracked warning-mode check, not a claim-grade
performance result. `CI-009` owns the optimized Release identity, calibration,
and promotion to a blocking SLO lifecycle.

### CI-006 isolated-topology baseline

`CI-006` retained five passing hosted samples for each isolated CPU identity.
ASan and UBSan used runs
[29589810741](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29589810741),
[29589810850](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29589810850),
[29589810886](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29589810886),
[29589810898](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29589810898),
and
[29589811057](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29589811057)
at implementation commit `a7ae8e7f`. The ASan sample in run `29589810886`
comes from successful same-SHA attempt 2, job `87928232616`; the failed
artifact-finalization attempt recorded by `BUG-111` is excluded. Unsanitized
samples used runs
[29592744115](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29592744115),
[29592761257](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29592761257),
[29592761260](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29592761260),
[29592761292](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29592761292),
and
[29592761299](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29592761299)
at `44da41e3`. The intervening commits changed workflows, policy docs, task
records, and Python workflow regressions only; they did not change CMake,
presets, C++, CTest registration, or selected CPU-test sources.

Every one of the 15 selection reports resolved 26 producers and 4,062 cases to
digest
`07c9f615629327c0502cd4aa73c411de41693b88ccc0ea80dbabb624cb6cf08b`.
The real three-report comparison passed with schema
`intrinsic.cpu-test-selection-parity/v1`.

| Identity | Configure median / p95 (s) | Build median / p95 (s) | Test median / p95 (s) | Total median / p95 (s) |
| --- | ---: | ---: | ---: | ---: |
| `none` | 34.722 / 37.876 | 1707.296 / 1746.903 | 32.292 / 33.472 | 1771.333 / 1816.077 |
| `asan` | 10.855 / 14.579 | 1698.595 / 1748.666 | 523.328 / 526.892 | 2232.778 / 2288.766 |
| `ubsan` | 10.546 / 13.459 | 1539.424 / 1730.877 | 178.098 / 183.850 | 1733.820 / 1923.281 |

This table is the first isolated identical-selector baseline. It supports
topology identity and current-cost claims only. It does not isolate a causal
performance effect; the later same-SHA grouped-registration A/B evidence is
recorded in the CI-008 section above.

The retained `CI-003` baseline predates this topology. Its `ci`,
`ci-vulkan`, and benchmark populations record the historical
`combined-project-default` identity, while its dedicated sanitizer jobs used
different selectors and configuration overrides. Those immutable records remain
valid evidence for the old policy but are not identical-selector measurements
of the new `none|asan|ubsan|asan-ubsan` topology. Do not attribute a timing
difference to sanitizer isolation alone or rewrite the historical identities.

## Nightly/deep expectations

- Run deeper correctness/performance suites.
- Include broader datasets and optional GPU workloads.
- Publish benchmark artifacts for trend tracking.
- Keep CPU performance and benchmark work in the unsanitized `build/ci` tree;
  sanitizer and promoted-Vulkan variants remain isolated in their named trees.

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
As described in the sanitizer topology above, these samples retain their
historical combined/default and ad hoc variant identities; they are not
identical-selector measurements of the current isolated presets.

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

## Compile-hotspot evidence

`tools/analysis/compile_hotspots.py` reports physical compiler invocations from
the latest record for each `.o` or `.pcm` output in a Ninja log. Outputs are
grouped only when their start time, end time, and Ninja command hash match.
This counts a module command that emits both a BMI and an object once, while
retaining separately executed BMI, object, or implementation phases. Each
group also has a cross-run `edge_id`, computed from its resolved source, edge
kind, and sorted output list; run-local timestamps and command hashes remain
diagnostics and are not part of that stable baseline key.

Source ownership comes from the configured `compile_commands.json`, not an
output-name guess. The declared repository roots are `src/`, `tests/`,
`methods/`, and `benchmarks/`. Generated build sources, repository dependency
roots (`external/` and `third_party/`), and sources outside the declared roots
are classified separately. Missing output mappings and mappings with more than
one source remain in `resolution_issues` with candidate diagnostics; they are
never silently omitted from the full edge inventory or admitted to a
baseline. The printed top-N is the global repository-owned ranking and has no
per-root quota.

The Linux correctness workflow enforces the hotspot baseline only after its
CPU correctness suite reports. Nightly likewise runs its fast and scheduled
slow correctness cohorts before the report, so a hotspot failure cannot
suppress already-built correctness feedback.

Refresh a target set only from at least five clean, comparable builds at one
commit, preset, compiler/scanner, runner image, cache state, and producer
selection. Retain each full JSON report, rank normalized repository-owned
edges across the cohort, and record the chosen offenders and threshold
derivation with the hosted run IDs. Do not mix incremental logs, select a
convenient retry, preserve stale targets by fiat, or impose per-root quotas.
For the one source-completeness diagnostic, build the compile-only
`IntrinsicTests` aggregate. It covers registered test, method, and benchmark
producers without invoking the `IntrinsicBenchmarks` custom target, whose build
also executes the benchmark runner.
The checked-in Octree budget is the historical BUG-004 threshold migrated to
the strict identity schema; it is not a BUILD-004 five-sample refresh.
[`RUNTIME-166`](../../tasks/backlog/runtime/RUNTIME-166-slim-render-extraction-module.md)
is a current source-owner consumer of this evidence. Compile optimization
belongs to the affected layer task rather than to the analysis tool.

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
