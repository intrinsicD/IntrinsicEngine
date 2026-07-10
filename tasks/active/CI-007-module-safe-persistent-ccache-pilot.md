---
id: CI-007
theme: H
depends_on:
  - CI-003
---
# CI-007 — Pilot persistent module-safe ccache in CI

## Status
- In progress on branch `main` in this workspace; prior local branch
  `copilot/ci-007-module-safe-ccache` is stale relative to `origin/main`.
- Owner/agent: Codex CLI continuing the active task.
- Pilot scope: `pr-fast` only; no other gate consumes the persistent store.
- Current slice: Slice B hermetic module-interface invalidation probe complete
  locally after independent audit.
- Next verification step: collect Slice C hosted cold/warm/interface-change
  samples after the commits are published, then retain or roll back the pilot.

## Goal
- Persist only ccache's content-addressed store across CI runs and prove that it
  accelerates repeated C++23 module builds without reintroducing stale BMI/
  object failures.

## Non-goals
- No cache of `build/`, CMake state, Ninja metadata, object directories, or
  compiler BMIs.
- No direct-mode ccache for module consumers.
- No success-shaped fallback when ccache corruption or stale reuse is detected.

## Context
- Owner: CI workflow setup, `cmake/Dependencies.cmake`, and stale-module
  regression coverage.
- Audit of ccache 4.9.1 found that it passes `.cppm` compilation through and
  cannot use depend mode while direct mode is disabled. The safe pilot instead
  disables both modes and adds a deterministic digest of every repository
  module interface through `CCACHE_EXTRAFILES`, deliberately over-invalidating
  consumers when any exported interface changes.
- The 2026-07-09 `CI-003` audit found no active ccache in hosted workflow logs;
  workflows neither install it nor persist its store. Each PR workflow starts
  an independent cold compile taking roughly 15–22 minutes.
- Prior stale module/object incidents produced vtable/slot mismatches and
  unexplained crashes after `.cppm` changes. Build-tree/BMI caching is therefore
  explicitly unsafe until separately proven; this task does not attempt it.
- Cache statistics are part of the `CI-003` telemetry schema. Keys must separate
  compiler, preset, sanitizer, and toolchain/dependency inputs.

## Required changes
- [x] Install a pinned distro ccache version in a limited pilot workflow and
      configure a repository-independent cache directory and bounded maximum
      size.
- [x] Persist only that ccache directory with a key namespace including OS,
      exact Clang major/minor, preset, sanitizer identity, and hashes of
      `CMakePresets.json`, CMake toolchain/helpers, vcpkg manifests, and lock/
      overlay inputs; use a SHA suffix plus safe restore prefixes for reuse.
- [x] Preserve `CCACHE_NODIRECT=1`, disable ineffective depend mode, hash the
      complete module-interface digest through `CCACHE_EXTRAFILES`, and fail the
      pilot if configure does not report the expected launcher/mode/digest.
- [x] Zero statistics before build and publish hit/miss, cache size, errors,
      compile duration, and cold/warm identity through `CI-003`.
- [ ] Validate three scenarios: empty-cache full build, restored-cache
      unchanged full build, and restored-cache build after a representative
      exported `.cppm` layout/API change.
- [x] Add a stale-artifact probe that compares the cached result with a clean
      no-ccache build/test result for the interface-change scenario.
- [ ] Expand beyond the pilot only after full-gate correctness parity and a
      five-sample median/p95 comparison; document rollback/clear instructions.

## Slice plan
- **Slice A.** Add the `pr-fast`-only pinned ccache setup, safe cache key,
  external bounded store, module-interface digest, configured-toolchain and
  launcher guards, fail-closed machine-readable statistics, and workflow/config
  regressions. No speedup claim.
- **Slice B.** Add a hermetic Clang module-interface invalidation probe,
  require real unchanged-consumer hits plus interface-change misses, document
  safety/rollback policy, and verify cached versus clean output parity.
- **Slice C.** Publish the workflow, collect empty/restored/interface-change
  full-gate evidence plus five comparable samples, then either retire the task
  or roll back the pilot. Expansion to other gates is explicitly deferred until
  this slice.

## Tests
- [x] Add workflow/config regression coverage for key dimensions, cache path,
      size cap, depend mode, and direct-mode disablement.
- [x] Run the default CPU gate from both cached and clean builds after the
      module-interface probe.
- [x] Run the repository stale-build regression/probe that exercises imported
      class layout or virtual surface changes.
- [ ] Record cache hit/miss and wall-time distributions against `CI-003`.

## Docs
- [x] Document cache key inputs, safety mode, size cap, clear/rollback
      procedure, and the prohibition on build/BMI caching.
- [x] Link the stale-build triage playbook from the CI cache documentation.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] CI persists only the ccache store, never build/BMI state.
- [ ] Warm runs show non-zero safe hits and a baseline-comparable build-time
      delta.
- [x] A `.cppm` interface/layout change invalidates affected importers and
      matches a clean no-cache build/test result.
- [x] Cache failures are visible and cannot silently substitute stale objects.

## Verification
```bash
ccache --version
ccache --zero-stats
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ccache --show-stats
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tests/regression/tooling/Test.CcacheWorkflow.py
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Audit correction (2026-07-10): the pre-audit attempts retained below do not
satisfy corrected Slices A/B. They used an immutable input-hash-only key,
misidentified the sanitizer/toolchain, fabricated zero stats when collection
was unavailable, and exercised a probe with zero cache hits while changing its
consumer sources. The corrected Slice A evidence follows those historical
attempts; Slice B must supersede the old probe evidence before its checkboxes
can close.

Slice A local verification run on 2026-07-10:

```bash
python3 tests/regression/tooling/Test.CcacheWorkflow.py -v
python3 tests/regression/tooling/Test.CiTiming.py -v
python3 tests/regression/tooling/Test.WorkflowConcurrency.py -v
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
```

Focused regressions passed: `Test.CcacheWorkflow.py` 5/5,
`Test.CiTiming.py` 7/7, and `Test.WorkflowConcurrency.py` 4/4. Task policy,
doc links, session-brief freshness, and test-layout checks passed. Root hygiene
ran in warning mode and reported pre-existing unexpected root entries `ara/` and
`imgui.ini`.

```bash
export CCACHE_DIR=/tmp/intrinsic-ci007-ccache
export CCACHE_MAXSIZE=2G
ccache --set-config=max_size="$CCACHE_MAXSIZE"
ccache --set-config=direct_mode=false
ccache --set-config=depend_mode=true
ccache --version
cmake --preset ci
python3 tools/ci/ccache_ci.py check-config --build-dir build/ci --repo-root . --expected-cache-dir "$CCACHE_DIR" --expected-max-size "2.0 GB"
ccache --zero-stats
cmake --build --preset ci --target IntrinsicPrFastTests
python3 tools/ci/ccache_ci.py write-stats --output /tmp/ci007-ccache-stats.json
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci --inventory build/ci/test-inventories/IntrinsicPrFastTests.txt
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

The local ccache pilot guard passed with ccache 4.9.1, external cache dir
`/tmp/intrinsic-ci007-ccache`, `direct_mode=false`, and `depend_mode=true`.
The PR-fast aggregate built, prerequisite inventory passed for 19 binaries, and
the PR-fast selector passed 3,595/3,595 in 67.61 s. The ccache stats payload
reported 0 hits, 36 misses, 4,748 KiB cache size, and 0 errors for this local
incremental run; this is not a speedup claim.

```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

The default CPU-supported gate passed 3,654/3,654 in 77.71 s after building the
full `IntrinsicTests` aggregate.

Slice C local clean/no-ccache comparison attempt on 2026-07-10:

```bash
cmake --preset ci -B build/ci-no-ccache -DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND
cmake --preset ci -B build/ci-no-ccache -DCMAKE_IGNORE_PATH=/usr/bin/ccache
cmake --preset ci -B build/ci-no-ccache -DINTRINSIC_ENABLE_CCACHE=OFF
rg -n 'LAUNCHER = .*ccache|CCACHE_DEPEND|CCACHE_NODIRECT' build/ci-no-ccache/build.ninja
cmake --build build/ci-no-ccache --target IntrinsicTests
ctest --test-dir build/ci-no-ccache --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
ctest --test-dir build/ci-no-ccache --output-on-failure -R '^CoreTasks\.CounterEventCanBeRearmedAfterReady$' --timeout 60
```

The first two no-ccache configure attempts still resolved `/usr/bin/ccache`,
so the slice added explicit `INTRINSIC_ENABLE_CCACHE=OFF` support in
`cmake/Dependencies.cmake`. With that option off, configure reported
`CCache disabled by INTRINSIC_ENABLE_CCACHE=OFF`, the Ninja graph check found
no `ccache` launcher or `CCACHE_*` launcher environment references, and the
clean build completed. The initial full clean CPU CTest run failed 1/3,613 with
an ASan heap-use-after-free in `Scheduler::Reschedule(...)` during
`CoreTasks.CounterEventCanBeRearmedAfterReady`; the isolated rerun passed 1/1.
This is tracked as `BUG-078`.

BUG-078 follow-up and Slice C local clean comparison rerun on 2026-07-10:

```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
cmake --build build/ci-no-ccache --target IntrinsicTests
ctest --test-dir build/ci-no-ccache --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

The default `build/ci` CPU-supported gate passed 3,655/3,655 in 59.64 s after
building `IntrinsicTests`. The explicit no-ccache `build/ci-no-ccache`
comparison passed 3,614/3,614 in 60.44 s after building `IntrinsicTests`.
Hosted warm-run hit/wall-time distributions remain Slice C evidence.

Corrected Slice A verification run on 2026-07-10:

```bash
export CCACHE_DIR=/tmp/intrinsic-ci007-a-cache
export CCACHE_CONFIGPATH=/tmp/intrinsic-ci007-a.conf
export CCACHE_MAXSIZE=2G
ccache --set-config=max_size="$CCACHE_MAXSIZE"
ccache --set-config=direct_mode=false
ccache --set-config=depend_mode=false
cmake --preset ci
python3 tools/ci/ccache_ci.py configured-identity --build-dir build/ci --expected-sanitizer combined-project-default
python3 tools/ci/ccache_ci.py check-config --build-dir build/ci --repo-root . --expected-cache-dir "$CCACHE_DIR" --expected-max-size "2.0 GB"
ccache --zero-stats
cmake --build --preset ci --target IntrinsicTests
python3 tools/ci/ccache_ci.py write-stats --output /tmp/ci007-a-stats.json
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
python3 tests/regression/tooling/Test.CcacheWorkflow.py -v
python3 tests/regression/tooling/Test.CiTiming.py -v
python3 tests/regression/tooling/Test.WorkflowConcurrency.py -v
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/benchmark/compare_baseline.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 -m py_compile tools/ci/ccache_ci.py tools/ci/aggregate_gate_timing.py tests/regression/tooling/Test.CcacheWorkflow.py tests/regression/tooling/Test.CiTiming.py
ruff check tools/ci/ccache_ci.py tools/ci/aggregate_gate_timing.py tests/regression/tooling/Test.CcacheWorkflow.py tests/regression/tooling/Test.CiTiming.py
ruff format --check tools/ci/ccache_ci.py tools/ci/aggregate_gate_timing.py tests/regression/tooling/Test.CcacheWorkflow.py tests/regression/tooling/Test.CiTiming.py
```

The configured-identity guard reported Clang 23.0.0 with the matching
`clang-scan-deps`, ccache 4.9.1, the combined project-default sanitizers,
external ccache paths, and a digest covering 394 module interfaces. The full
build completed 2,133 Ninja edges. Its isolated cache recorded 5
preprocessed hits, 647 misses, 243,876 KiB of cache data, zero ccache errors,
and 386 unsupported-source-language events for `.cppm` inputs; the five
within-build hits are diagnostic evidence, not a warm-cache or speed claim.
The default CPU-supported gate passed 3,655/3,655 in 60.94 s.

Focused regressions passed: `Test.CcacheWorkflow.py` 15/15,
`Test.CiTiming.py` 12/12, and `Test.WorkflowConcurrency.py` 4/4. Workflow YAML
parsing, all benchmark manifest/result/baseline validators, strict task
policy, documentation links and synchronization, the PR contract, strict
test layout, session-brief freshness, Python syntax compilation, and Ruff
format/lint checks also passed. This validates Slice A's immutable cache-key,
configured toolchain identity, fail-closed statistics, and coarse module
interface digest. The hermetic interface-invalidation probe remains Slice B;
hosted warm-run hit/wall-time distributions remain Slice C.

Corrected Slice B verification run on 2026-07-10:

```bash
python3 tools/ci/ccache_module_invalidation_probe.py \
  --work-dir /tmp/intrinsic-ci007-b/work \
  --cxx /bin/clang++-23 \
  --scan-deps /bin/clang-scan-deps-23 \
  --output /tmp/intrinsic-ci007-b/result.json
python3 tests/regression/tooling/Test.CcacheModuleInvalidationProbe.py -v
python3 tests/regression/tooling/Test.CcacheWorkflow.py -v
python3 tests/regression/tooling/Test.CiTiming.py -v
python3 tests/regression/tooling/Test.WorkflowConcurrency.py -v
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
cmake --build build/ci-no-ccache --target IntrinsicTests
ctest --test-dir build/ci-no-ccache --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_pr_contract.py
python3 tools/agents/generate_session_brief.py --check
```

The isolated Clang 23/ccache 4.9.1 probe passed four scenarios. The cold v1
build recorded 0 hits/2 misses and output `11`; the unchanged v1 rebuild kept
the implementation and importer byte-for-byte and mtime-stable, recorded 2
preprocessed hits/0 misses, and output `11`; changing only `Probe.cppm` to the
v2 layout dirtied and recompiled the importer, recorded 0 hits/2 misses, and
output `29`. The clean no-ccache v2 build also output `29`. Every cached
scenario reported zero ccache errors, and `.cppm` compilation was explicitly
classified as compiler pass-through.

The final serial default gates passed on an otherwise idle host: cached
`build/ci` passed 3,655/3,655 in 63.69 s and `build/ci-no-ccache` passed
3,655/3,655 in 63.10 s. Earlier concurrent attempts were discarded because a
simultaneous clean rebuild and then an unrelated CUDA extension build starved
the benchmark smoke past its 60-second timeout; the focused cached benchmark
subgate subsequently passed 2/2 in 11.39 s. Focused tooling and structural
regressions passed: `Test.CcacheModuleInvalidationProbe.py` 4/4,
`Test.CcacheWorkflow.py` 15/15, `Test.CiTiming.py` 12/12, and
`Test.WorkflowConcurrency.py` 4/4. Strict task policy and test layout,
documentation links and synchronization, the PR contract, session-brief
freshness, workflow YAML parsing, Python syntax compilation, and Ruff
format/lint checks also passed. Hosted full-gate warm/cache-hit distributions
and the five-sample comparison remain Slice C evidence.

## Forbidden changes
- Caching any configured build directory, BMI, `.o`, or Ninja state directly.
- Enabling ccache direct mode for C++23 module builds.
- Sharing cache namespaces across compiler or sanitizer identities.
- Declaring a speedup without clean/cached correctness parity and comparable
  baseline samples.
