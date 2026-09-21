# Accumulated test-fixture compilation measurements

Local descriptive observations for C104. This batch improves selected incremental
rebuilds; it does not establish a meaningful general clean-build improvement.
The shared fixture implementation becomes more expensive to rebuild.

Compared HEAD `b3c18fd17add1a555540b57ba388d62726d39e87` with local snapshot
`887415fb7e6df4c04caa154f06f64f04a422b731`. The snapshot contains only the existing
ten test/support files: shared fixture consolidation, unused dependency removal,
and out-of-line MockDevice lifetime. Its parent and full diff hash were checked;
no production code changed. The batch removes 262 net lines including its README.
This measures the combined batch, not causal attribution to each individual slice.

Protocol: Clang 23, ci Debug, Null/headless, four jobs, compiler cache disabled,
identical source/build paths, fresh tmpfs build per sample, identical preinstalled
dependency fingerprints, no package installation, source/dependency pre-read.
Before/after/after/before, two samples per arm, no discarded warmups. Normal desktop
host with no affinity/governor/turbo control or competing builds/tests. Raw load
averages are retained in summary.json; the first clean run started near 0.80,
versus roughly 2.6–2.7 for the others, so host conditions were not fully controlled. This is not a cold-filesystem experiment.
Scope: IntrinsicRuntimeContractTests and its dependency closure, not all engine
or test executables. All canonical results remain claim_eligible:false.

## Timings

Elapsed target-build seconds include scanning and linking. Positive savings mean
lower after median. Ranges are the two observations, not confidence intervals.

| Scenario | Before median (range), s | After median (range), s | Saved, s | Saved, % | Compiler invocations before → after |
|---|---:|---:|---:|---:|---:|
| clean | 428.676 (428.663–428.688) | 426.548 (425.796–427.300) | +2.128 | +0.50% | 878 → 878 |
| noop | 0.115 (0.108–0.122) | 0.101 (0.100–0.101) | +0.014 | +12.48% | 0 → 0 |
| clusteringmethods_test | 21.974 (21.882–22.066) | 21.488 (21.456–21.519) | +0.486 | +2.21% | 1 → 1 |
| meshmethods_test | 24.799 (24.785–24.813) | 24.008 (23.979–24.038) | +0.791 | +3.19% | 1 → 1 |
| models_test | 23.748 (23.683–23.813) | 17.723 (17.689–17.758) | +6.025 | +25.37% | 1 → 1 |
| scenecommands_test | 20.352 (20.337–20.366) | 20.152 (20.117–20.187) | +0.199 | +0.98% | 1 → 1 |
| visualization_test | 21.253 (21.246–21.261) | 16.787 (16.779–16.795) | +4.466 | +21.01% | 1 → 1 |
| fixture_impl | 8.570 (8.568–8.572) | 10.112 (10.098–10.127) | -1.542 | -18.00% | 1 → 1 |
| mock_impl | 5.150 (5.147–5.154) | 5.176 (5.173–5.180) | -0.026 | -0.51% | 1 → 1 |
| fixture_header | 67.902 (67.852–67.951) | 58.503 (58.478–58.527) | +9.399 | +13.84% | 20 → 20 |
| mock_header | 53.364 (53.297–53.431) | 35.166 (35.157–35.175) | +18.198 | +34.10% | 11 → 8 |

No-op differences are milliseconds and establish no useful saving. Clean-build
and SceneCommands differences are small. Models, Visualization and both header
probes show the clearest local savings. The fixture-owner rebuild increases
18.00%; its cost is included in every clean build and separately probed.

Both mock-header after samples omit exactly Models, SceneCommands and
Visualization from the before source set (11 → 8). Fixture-header fanout remains
20, including every in-target consumer, not just the six minimum required probes.
The manifest's unused minimum_interface_importers parameter is inherited from the
runner schema; no interface probe is present and no interface claim is made.
The full source sets, per-source clean compiler times, and load averages are in
summary.json. Other executables sharing these owners have not been timed.

## Verification and audit

- Four canonical results and 103 manifests validated; all 26 compile-tooling tests passed.
- Fresh canonical ci configure and IntrinsicTests build passed; CPU CTest selected
  4,862 tests: 4,861 passed, zero failures, one expected GlfwLifecycleLsan
  capability skip (162.88 seconds). Baseline executable: 1,193 passed, two
  expected Null-platform skips. No sanitizer or GPU execution is claimed.
- Both source snapshots passed fresh compiler trait checks for nothrow lifetime
  and copy/move restrictions. Snapshot parent, exact diff, runner and manifest
  hashes were checked. The ten files listed in `batch.diff` still match the measured after;
  the later SessionLifecycle diff is outside this measurement.
- Claude Fable medium reviewed the fixed diff, protocol, raw values and source
  coverage; final audit reports no blockers. Exact Fable 5.1 suffix is unverified.
  An initial noexcept concern was withdrawn after checking libstdc++ definitions
  and independently verified by compilation. All small/negative deltas are retained.
- Strict layering, test-layout, task-policy and explicit-file docs-sync passed;
  relative links, root hygiene, session-brief freshness and whitespace passed.
  See verification.json and retained logs. Those checks cover the ten-file
  measurement snapshot; the later SessionLifecycle change has separate verification.

## Evidence

[Summary](../diagnostics/runtime270_fixture_batch/summary.json),
[protocol](../diagnostics/runtime270_fixture_batch/protocol.json),
[canonical results](../diagnostics/runtime270_fixture_batch/results/01-before-1.json),
[source binding](../diagnostics/runtime270_fixture_batch/snapshot-binding.json),
[exact batch diff](../diagnostics/runtime270_fixture_batch/batch.diff),
[raw evidence](../diagnostics/runtime270_fixture_batch/raw-evidence.tar.gz),
[evidence hashes](../diagnostics/runtime270_fixture_batch/evidence-index.json).
The bundle retains exact runner bytes and snapshot commit metadata; the source
patch reconstructs the after tree from its baseline even if the local snapshot
commit is later pruned. The original measurement session left the working branch uncommitted.
Integration references are recorded in the active RUNTIME-270 task.

Reproduce the exact protocol with the fixture_batch manifest and retained
`runtime270_fixture_batch/runner.py`, a detached disposable worktree,
absent build directory and new output directory. Make identical preinstalled
vcpkg dependencies available; the runner disables installation and fingerprints
packages. All source snapshots must remain exact and clean during sampling. The retained
runner is byte-identical to the measured version. BUG-204 subsequently extracts
its unchanged consumer predicate into a function for negative-path tests; the
current canonical runner therefore has a different hash. Original result and
evidence hashes are unchanged.
