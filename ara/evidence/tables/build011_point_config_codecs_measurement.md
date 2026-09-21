# Point config codec compilation — bounded matched measurements

Ten point-processing config implementations now compile as three ordinary
runtime translation units. The existing shared codec pattern owns public
functions through globally attached declarations; schema records keep their
original module ownership. No forwarding layer or new runtime API was added. Function linkage changes
from named-module attachment to global C++ attachment; all in-tree consumers
were rebuilt, and no external ABI compatibility is claimed.
Namespace bodies match the old sources byte-for-byte after the two private
helper renames; include/module setup falls by 42 lines and seven compiled units.
The repeated work removed is JSON parsing and template instantiation, not the
family-specific validation rules.

In the last 100 commits ending at the original selection baseline, 13 touched
these codecs; 12 of those changed multiple codecs and only one changed a single
codec without the shared header. Eleven changed the shared header. This is
retained workload context, collected before target results, not a prediction of
future savings or an acceptance rule.

The three owners group neighborhood (outlier, density, spacing, weights),
features (bilateral, keypoint, descriptor, construction), and normals/registration.
Mesh curvature, geodesics and other JSON users remain outside this batch.
These cohorts extend the existing FeatureConfigCodecs.Detail ownership pattern.

## Results

C108 records these local descriptive observations. Seconds are end-to-end target
wall times, including scans, compilation, archive updates and final linking.

| Scenario | Before median [range], s | After median [range], s | Change |
|---|---:|---:|---:|
| Clean target closure | 407.035 [407.025–407.045] | 402.482 [402.448–402.516] | 1.12% faster |
| No-op | 0.096 [0.096–0.097] | 0.099 [0.098–0.099] | 2.50% slower |
| Normal codec edit | 7.343 [7.340–7.346] | 7.574 [7.573–7.575] | 3.15% slower |
| Density codec edit | 7.079 [7.041–7.118] | 7.668 [7.663–7.672] | 8.31% slower |
| Descriptor codec edit | 7.208 [7.132–7.284] | 7.921 [7.900–7.942] | 9.89% slower |
| Shared JSON declaration header edit | 15.079 [15.061–15.097] | 10.413 [10.410–10.417] | 30.94% faster |
| Unchanged shared helper edit | 7.873 [7.865–7.882] | 7.827 [7.823–7.830] | 0.60% faster |

The shared-header rebuild closes the primary gate: 30.94% lower median and
separated observed ranges, with required compiler invocations falling 12 → 5.
Individual normal/density/descriptor edits cost an additional 0.231/0.588/0.713 s;
the largest regression is 9.89%, below the predeclared 50% limit.
No-op +2.4 ms and common-helper −46.9 ms are negligible at this scope.

Clean target wall time is 4.553 s (1.12%) lower in this sample population.
This small local difference is not established as a repeatable speedup: there
are only two observations per arm on a normal desktop, and source inspection
and remote review were permitted during sampling. The nonoverlapping ranges
describe these observations and do not establish statistical significance.

The sum of all clean compiler invocation durations is 1,572.323 → 1,553.698 s
(medians), including the receiving owners. The selected ten old sources sum
to 33.850 s versus 12.097 s for all three new owners (sum of per-source medians).
Those sums measure accumulated elapsed compiler durations under four-way
contention, not CPU time or target wall time. Clean compiler invocations fall
878 → 871. Every frozen gate passes; all negative outcomes are retained.

All 1,147 common configured commands match exactly across four samples.
The ten deleted and three added source commands have equivalent flags after
normalizing only source/output/dependency/module-map paths. The fixture repair
has identical bytes in both arms. No baseline or candidate result was discarded.

## Protocol and limits

Clang23 ci Debug Null/headless, IntrinsicRuntimeContractTests and its complete
closure, four jobs, launchers/caches disabled. ABBA, two observations per arm,
zero discarded measured samples; identical source/build paths, fresh tmpfs build
per sample, fingerprinted preinstalled dependencies and identical input pre-read.
This is warm-input local evidence, not a cold-filesystem or full-engine benchmark.
No competing builds or tests during sampling; normal desktop load, affinity and
governor remain uncontrolled. Source revisions and resolved owner paths are in
the manifest. Both arms contain the separately verified BUG-205 fixture repair.

Acceptance was frozen before measurements: shared-header edit >=15% faster with
separated ranges, clean compiler-duration sum reduced, clean target regression
<=2%, each representative codec edit regression <=50%. Relocated-source probes
touch the actual owner in each arm. Header probes require all 12 before and five
after consumers. Timings include scanning, all receiving-owner costs, archive
updates and links. Single-process peak RSS does not measure aggregate concurrent
memory or tmpfs pages. No Release, sanitizer, GPU, runtime-performance,
cross-host or statistical/general conclusion; canonical results remain
claim_eligible:false.

## Verification

Canonical ci IntrinsicTests build and CPU gate: 4,863 passes, zero failures and
one expected GLFW/LSan skip out of 4,864 selected (155.70 s).
523 focused config/method/compilation-locality checks passed before the independent
fixture include-order repair. Final integrated default CPU results supersede that
checkpoint. Fresh Clang20 runtime-contract and sandbox-integration executables
build/link; 193 focused runtime and 25 app config tests pass. Thirty tooling tests
cover the existing measurement checks and per-arm required-producer validation.
Strict layering, test layout, task policy, docs sync, links and workshop automation
pass; regenerated module inventory remains 429. Manual workshop rows 1–3 pass;
renderer/pass/recipe/scaffold rows are not applicable, with no temporary exceptions.

Claude Fable 5.1 reviewed candidate selection, fixed source diff and corrected
boundary-test mappings. Its two valid guard findings were fixed; all forbidden
module lists remain unchanged. Early stale CTest producer paths, one interrupted
CPU run during fixture repair and a subsequent stale dependency-scan failure are
retained as verification attempts, not benchmark samples. BUG-205's two fresh
Clang20 compiler failures and corrected commands are also retained. The final
integrated source is verified after these corrections.

## Evidence and disposition

- [Frozen manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_point_config_codecs.yaml): before `5dd61178cc935060d6d850b71224e412c9116b18`, after `7a177d289d8714482772c2cf7af96001ae4d9459`.
- [Summary and recomputed gates](../diagnostics/build011_point_config_codecs/summary.json), [protocol](../diagnostics/build011_point_config_codecs/protocol.json), [canonical results](../diagnostics/build011_point_config_codecs/results/), [source identity](../diagnostics/build011_point_config_codecs/source-identity.json), [codec/command mappings](../diagnostics/build011_point_config_codecs/relocated-command-equivalence.json), [verification](../diagnostics/build011_point_config_codecs/verification.json).
- [Raw evidence archive](../diagnostics/build011_point_config_codecs/raw-evidence.tar.gz) and [member hashes](../diagnostics/build011_point_config_codecs/archive-contents.json): all four raw runs, compile commands, Ninja logs/hotspots/DAGs, fingerprints, driver bytes, source checks, traces and earlier verification attempts.
- [Independent results audit](../diagnostics/build011_point_config_codecs/claude-results-final.txt), [artifact hashes](../diagnostics/build011_point_config_codecs/evidence-index.json). The initial review lacked access to temporary paths; its limitation is retained, and the audit was completed against repository evidence.

BUILD-011 closes at a CPUContracted endpoint with no deferred work in its scope.
BUG-205 separately closes the minimum-compiler fixture ordering regression
(`748612b4b`, `5dd61178c`); BUILD-011 source is `7a177d289`. This report and task
retirements are recorded by the enclosing evidence commit. BUILD-006 retains its
CI-013/CI-014/BUILD-005 prerequisites; RUNTIME-270's product contract is unchanged.

The [complete integrity audit](../diagnostics/build011_point_config_codecs/integrity-audit.json) checks every sample identity, command, timing, dependency fingerprint and compiler-duration sum, including the items Claude sampled only partially. Prior selection timing in source-accounting.json is explicitly labelled BUILD-010 evidence. No inference about host load causality is used.
