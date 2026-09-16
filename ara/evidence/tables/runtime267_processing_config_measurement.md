# RUNTIME-267 — Processing config editor dependency isolation

Retain the bounded app dependency cut. The local median settled editor-target
rebuild after a consolidation config-interface edit changes **34.245 → 27.372 s
(20.1% lower)**, with **19 → 16 compiler invocations**. All three after samples
are below all three before samples. This is a warm-input incremental observation
on one host, not a clean-build, whole-engine, runtime/GPU, cross-host or statistical
claim. All six canonical records remain `claim_eligible: false`.

## Matched comparison

Clean exact commits `be40663918749417275b06fb8268a00310688f7d` →
`6fb0807f49a8d660d820470e82613c1ad1a9c424`; benchmark
`build.engine_compile_iteration.processing_config_editor.v1`. Clang23 Debug,
ci-derived Null/headless, `ExtrinsicSandboxEditor`, four jobs, compiler caches and
launchers off. Tests are configured to satisfy CMake's headless app gate; the
measured editor-library target compiles no test objects and executes no tests.

Seconds: median [minimum–maximum]. Walls include scanning, CMake glob verification,
compilation and archive linking. Producer durations and critical paths come from
the same invocation's Ninja log; they are not summed into wall time.

| Scenario | Before | After |
|---|---:|---:|
| Config interface edit + target archives | 34.245 [34.225–34.349] | 27.372 [27.244–27.408] |
| Config implementation edit + target archives | 0.425 [0.421–0.431] | 0.432 [0.429–0.441] |
| Settled no-op | 0.088 [0.088–0.091] | 0.091 [0.090–0.093] |
| Interface rebuild: target DAG critical path | 24.030 [24.026–24.064] | 23.151 [23.104–23.204] |
| Interface rebuild: receiving MethodPanels compiler | 8.132 [8.101–8.184] | 8.238 [8.237–8.261] |

| Attempt | Interface wall | Implementation wall | No-op wall |
|---|---:|---:|---:|
| 01-before-1 | 34.225 | 0.431 | 0.088 |
| 02-after-1 | 27.408 | 0.441 | 0.091 |
| 03-after-2 | 27.244 | 0.432 | 0.090 |
| 04-before-2 | 34.245 | 0.425 | 0.091 |
| 05-before-3 | 34.349 | 0.421 | 0.088 |
| 06-after-3 | 27.372 | 0.429 | 0.093 |

Implementation edits compile one source in both arms; no-ops compile/link none.
Their overlapping ranges show no measured improvement. MethodPanels costs slightly
more after receiving the helpers: 8.132 → 8.238 s (+1.3%), with non-overlapping
ranges. The measured target includes that cost. The DAG
critical path excludes separately recorded build-system meta outputs and resource
scheduling delays. Full wall timing retains them. RSS is maximum single-process
RSS, not concurrent aggregate memory; no memory claim.

ABBAAB order (A = before), three samples per arm, same source/build paths. Each
sample follows untimed configure/reconciliation; arm changes force all changed C++
producers stale and verify their actual setup compilation. All 1,138 configured
compiler commands match across samples. Source/package hashes, clean revisions,
and per-sample module maps are checked. Exactly DomainPanels, MeshProcessingPanels
and PanelSupport leave the interface rebuild; no compiler sources are added.
Inputs are pre-read during hashing. Normal desktop noise remains; no affinity,
governor, turbo or filesystem-cache control. No competing builds/tests ran during
sampling. Three samples do not establish stable future speedup or significance.
Earlier runtime-only comparisons have different target scope and are not combined.

Two rejected attempts remain archived: tests-off/headless setup omitted the editor
target before any compilation/timing; then one no-op hit an unrecognized absolute
CMakeFiles/cmake.verify_globs metadata output before any config probe. BUG-199
fixes the shared accounting helper, with 26 regression tests and Claude review.
The corrected manifest/runner/helper identities were frozen before restarting all
six samples. Untimed initial prerequisite artifacts were reused and reconciled;
this is not a fresh-build comparison. No accepted sample was discarded.

## Implementation and verification

The shell's existing prepared storage owns the canonical point-cloud service frame;
context borrows it only during draw. A non-const lvalue constructor rejects frame
temporaries, and context resets before storage on draw completion and detach.
The frame joins its existing globally attached family records so unrelated app
consumers can forward-declare it. Consolidation declarations move to one private
header, definitions to existing MethodPanels.cpp. Two duplicated availability
flags disappear; three method consumers share an empty-frame fallback preserving
unattached-context behavior. Config validation, epoch guards and typed results stay
with their existing owners. Session completion/config dependencies remain necessary.

Accounting: **+34 net C++ lines, one private header and one CMake header entry;
zero new compiled files, modules or state owners**. This improves compile locality,
not source-line count. No alternate config, service facade or compatibility path.

Canonical ci configure and IntrinsicTests build pass. Full CPU: **4,666 passed,
one expected ASan-only lifecycle skip, zero failures of 4,667 selected** (150.51 s).
Focused CPU: 158 passed; after strengthening one valid-request rejection assertion,
its rebuilt nine-test subset passed. Production source was unchanged after the
full run. Fresh cache-off Clang20 compiled the complete runtime/editor closure and
all three actual caller test objects, then reconciled to no-op. This is compile
evidence, not Clang20 test execution. No GPU or sanitizer execution claim.

The actual compiler dependency guard fails on all three baseline consumers and
passes after the cut. Claude reviewed the plan, fixed source, protocol, harness
repair and final results; evidence packets retain corrections to unsupported review
allegations. Scope/layering/tests/docs and the clean-workshop scorecard are recorded
in verification.json. No new layering exceptions; renderer/pass/recipe unchanged.
RUNTIME-267 closes at CPUContracted. UI-037, GRAPHICS-105, LEGACY-043 and BUILD-006
retain their independent work and prerequisites.

## Evidence and reproduction

[Manifest](../../../benchmarks/ci/manifests/processing_config_editor.yaml) ·
[Samples and recalculation](../diagnostics/runtime267_processing_config/summary.json) ·
[Verification](../diagnostics/runtime267_processing_config/verification.json) ·
[Final checks](../diagnostics/runtime267_processing_config/final-checks.json) ·
[Archive index](../diagnostics/runtime267_processing_config/evidence-index.json) ·
[Claude results review](../diagnostics/runtime267_processing_config/results-review.txt) ·
[Raw evidence](../diagnostics/runtime267_processing_config/raw-evidence.tar.gz).

The archive retains six canonical results, setup/build logs, source/package hashes,
commands/maps, Ninja graphs/windows, scripts, both rejected attempts and reviews;
no objects or BMIs. Recreate the exact commits/toolchain/dependencies at the captured
or consistently adapted paths, provide an owned build directory with the recorded
marker and an absent output directory, then run the saved runner. An empty owned
build directory builds prerequisites outside timing. Use the archived shared helper
and canonical sealing/validation tools; run summarize.py to recalculate the report.
