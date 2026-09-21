# Canonical property instantiation — bounded compile measurements

BUILD-010 passes its incremental criterion: both heavy editor consumers improve
by at least 3% and 0.5 seconds, with non-overlapping sample ranges. The 1.85%
clean median reduction falls below the predeclared 2% clean-improvement threshold.
The shared-owner edit is 10.10% slower. These are small local development-cost
improvements, not a general/full-engine or statistical speedup claim (C106).

`Geometry.Properties.cpp` now owns explicit `PropertyRegistry::Add` and storage
instantiations for the nine canonical types. Generic custom types retain the same
templates. No algorithm body, module name, layout, dependency edge or translation
unit changes. The two production files gain 69 lines of includes and explicit
instantiation declarations/definitions; the duplication removed is generated code,
not source text. Consumer traces show no canonical Add/make_unique instantiation;
object symbols move the definitions and storage vtables to the existing owner.

## Matched results

Before `367a38fb4`, after implementation `a9e0bc149`. Clang 23 ci Debug,
Null/headless, four jobs, compiler cache disabled, identical source/build paths,
fresh tmpfs build per sample and fingerprinted preinstalled dependencies.
ABBA: two samples per arm, no discarded samples. Identical input pre-read;
this is not a cold-filesystem measurement. All 1,157 configured compiler commands
match. The runtime-contract executable and its complete dependency closure are
measured, including scanning, owner compilation, archive updates and linking.

Seconds: median (min–max); positive savings mean faster. Ranges are two samples,
not confidence intervals.

| Scenario | Before, s | After, s | Saved |
| --- | ---: | ---: | ---: |
| Clean target | 422.439 (421.779–423.099) | 414.635 (414.520–414.751) | +1.85% |
| No-op | 0.100 (0.099–0.102) | 0.100 (0.100–0.101) | overlapping noise |
| MeshMethods edit | 24.151 (24.005–24.297) | 22.953 (22.919–22.987) | +4.96% |
| ClusteringMethods edit | 21.514 (21.475–21.553) | 20.659 (20.653–20.664) | +3.97% |
| Scene serialization edit | 11.287 (11.284–11.291) | 10.989 (10.975–11.002) | +2.65% |
| Shared property-owner edit | 5.074 (5.067–5.081) | 5.587 (5.579–5.594) | -10.10% |

Every clean build executes 878 compiler invocations; every source edit executes
one, and every no-op executes zero. The shared owner is included in all clean
builds and separately probed. Scene serialization's 0.299 s saving misses the
0.5 s acceptance floor; no-op changes are negligible. No interface-edit/fanout
improvement is claimed. In the 100 commits ending at the baseline, MeshMethods
changed 13 times and ClusteringMethods 5 times, versus zero changes to the owner
implementation; this is context, not a projection of future time saved. That history check was performed during
sampling; it was not pre-registered and did not alter scenarios or acceptance.

Normal desktop load, governor and CPU affinity were uncontrolled; load averages
are retained (first clean baseline about 0.76, other samples about 2.1).
Two samples per arm support only this descriptive comparison.
Other executables, full-engine/Release/sanitizer builds and the compile cost of
new geometry regression cases outside this target were not timed. Runtime speed,
GPU execution and aggregate concurrent memory are unmeasured. Canonical results
remain `claim_eligible:false`.

## Verification and review

Canonical ci IntrinsicTests builds. The focused selector passes 226 cases; the
full CPU selector has 4,863 passes, zero failures and one expected GLFW/LSan skip
(159.28 s). Clang 20 separately builds/links IntrinsicGeometryTests and passes all
16 property cases. New coverage exercises all nine canonical kinds through the
compiled Add/importer Get/copy boundary, bool/default-fill/revision behavior,
type mismatch, and a custom struct's generic creation/clone path.

Claude Fable 5.1 reviewed the plan and fixed source diff. Module inventory is
unchanged at 429 modules; strict layering, task, test-layout and docs-sync checks
pass. An initial implementation compile needed the GLM vector includes; that
corrected diagnostic attempt is retained separately from all four successful
measurement samples. No source edits followed successful verification. Claude independently recomputed
all six scenario medians/ranges and confirmed acceptance through the consumer
gate only. It verified all indexed hashes and all 178 archived measurement files
against the original population. See the
[results audit](../diagnostics/build010_canonical_properties/claude-results-audit.txt).

## Evidence and reproduction

[Summary and per-source timings](../diagnostics/build010_canonical_properties/summary.json),
[protocol](../diagnostics/build010_canonical_properties/protocol.json),
[source identities](../diagnostics/build010_canonical_properties/source-identity.json),
[canonical results](../diagnostics/build010_canonical_properties/results/01-before-1.json),
[raw logs, runner, traces and source diff](../diagnostics/build010_canonical_properties/raw-evidence.tar.gz),
[hash index](../diagnostics/build010_canonical_properties/evidence-index.json).

Use the [frozen manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_canonical_properties.yaml)
and `tools/analysis/benchmark_compile_iteration.py` with a clean disposable
worktree containing both exact commits, the same preinstalled dependencies, an
absent build directory and a new output directory. The archive retains exact runner
bytes and all raw samples; the protocol records commands, paths and compiler identity.
