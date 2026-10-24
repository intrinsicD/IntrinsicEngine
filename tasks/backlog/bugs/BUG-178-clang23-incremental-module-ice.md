---
id: BUG-178
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive build-tooling incident; the ICP implementation passes with ccache disabled, while the incremental cause needs isolated reproduction."
contract_schema: 1
contracts: []
contract_review: "No new engine contract is proposed; this note investigates the existing Clang/preset/module-cache verification path."
---
# BUG-178 — Clang 23 crashes during an incremental module rebuild

## Goal
- Isolate the incremental Clang 23 module-serialization crash observed during ICP integration, distinguishing cached artifacts and overlapping/edited build inputs from a compiler source defect.

## Evidence
- Intermediate cached builds crashed while serializing `std::basic_string_view<char>` in `Runtime.EditorWorkspaceAttachment.Detail.cppm`, `Runtime.EditorWorkspaceSnapshots.Models.cpp`, and `Runtime.EditorWorkspaceSession.cpp`. Logs: `/tmp/icp-build2.log`; Clang emitted `/tmp/Runtime-2331a4.cpp` and its shell reproducer.
- A subsequent preset rebuild with `CCACHE_DISABLE=1` passed, followed by the full CPU cohort and real Vulkan registration checks. No speculative source workaround was added. The precise cache-versus-build-input cause is not established.
- [ICP review](../../../docs/reviews/2026-09-08-icp-spatial-integration.md) records the successful verification and bounded diagnosis.


### 2026-09-12 processing cleanup observation

The cached `ci-vulkan` rebuild of `IntrinsicPointLBVHGpuTests` failed in Keypoints
and Outliers with invalid UTF-8 diagnostics against compiler built-ins and a
Clang frontend crash. The same preset uses Clang 23 with ASan+UBSan. Include-only
review edits overlapped this build, so this run cannot distinguish source-input
changes from cache/module corruption and is not an isolated compiler reproducer.
The stabilized `CCACHE_DISABLE=1` retry built the complete target, followed by
nine passing affected GPU integration cases under ASan+UBSan. The linked cleanup
review records the commands and outcomes.
No source workaround or gate weakening was introduced. Evidence:
`build/analysis/point-inputs-2026-09-12/vulkan-build-cached-failure.log` and
`build/analysis/point-inputs-2026-09-12/vulkan-build-no-cache.log`.
See [processing cleanup review](../../../docs/reviews/2026-09-11-processing-complexity-review.md).

## Diagnosis — 2026-10-24

This was a bounded attempt to reproduce the crash in an incremental build without
ccache. Setup: an isolated worktree at `599f02ff1`, `build/ci-vulkan` (Debug,
ASan+UBSan, Ninja 1.11.1, CMake 4.3.2), `Ubuntu clang version 23.0.0
(++20260517031631+2f79d41e3e2e…)`, one writer, `--parallel 7`. ccache is not
installed on this host: `command -v ccache` prints nothing, and the configured
cache has `CCACHE_PROGRAM-NOTFOUND` with `INTRINSIC_ENABLE_CCACHE=ON`. Every build
recorded here is therefore cache-free. The expected outcome, written down
beforehand, was that both rebuilds succeed without a frontend crash.

1. The tree was configured (finished 01:21:48) and built from scratch as the
   baseline (finished 01:28:01; 1,958 steps, 911 compiles, including
   `IntrinsicPointLBVHGpuTests`). Configure and build both exited 0
   (`baseline-configure.log`, `baseline-build.log`, `baseline-rc.txt`). A no-op
   build afterwards reported `ninja: no work to do`.
2. A comment-only line was appended to
   `internal/Runtime.EditorWorkspaceAttachment.Detail.cppm`,
   `Runtime.EditorWorkspaceSnapshots.Models.cpp` and
   `internal/Runtime.EditorWorkspaceSession.cpp`, and `IntrinsicPointLBVHGpuTests`
   was rebuilt. Result: 26 steps, 20 compiles (the three files included), exit 0,
   28.8 s.
3. The edits were reverted with `git checkout` and the target rebuilt. Result:
   20 compiles, exit 0, 28.8 s. A following no-op build reported no work.

Neither rebuild printed `PLEASE submit a bug report`, `Stack dump` or `error:`, and
no new clang crash reproducer appeared in `/tmp`.

Result: the crash did not reproduce in the incremental, cache-free, single-writer
build with a stable source. The comparison with ccache enabled versus disabled
cannot run on this host unless ccache is installed. No second fresh tree was
compared beyond the baseline.

Unrelated observation, not examined: `/tmp` holds 110 Clang 23.0.0 crash
reproducer scripts dated 2026-10-19 11:56–11:57. They name main files such as
`Runtime.SceneDocumentModule.cppm` in a worktree that no longer exists. Their
cause is unknown, and they cannot be rerun without that tree's module files.

No source, preset or gate changed. Logs are git-ignored under
`tasks/evidence/BUG-178/` (`baseline-configure.log`, `baseline-build.log`,
`baseline-rc.txt`, `environment.txt`, `predictions.txt`, `step0-noop.log`,
`step1-edited.log`, `step2-reverted.log`).

## ccache comparison — 2026-10-24 (second session)

The operator decided: "ccache installieren und nochmal testen". ccache was installed
without root from the Ubuntu jammy package (`ccache 4.5.1-1` plus its
`libhiredis0.14` dependency, unpacked with `dpkg -x` under `~/.local/opt/ccache`,
called through a small wrapper that sets `LD_LIBRARY_PATH`). It was on `PATH` only
for this session's commands. CI uses ccache 4.9.1, so this host tests an older
ccache. The cache directory is a dedicated `CCACHE_DIR`
(`IntrinsicEngine-night/_state/ccache-bug178`), so the user's default cache is
untouched. Source surface: commit `4c30f2b82`, clean worktree, one writer per
tree, preset `ci-vulkan` (Clang 23, Debug, ASan+UBSan), `nice -n 10`,
`--parallel 16`, target `IntrinsicPointLBVHGpuTests`. Each arm runs the scenario
from the first diagnosis: a full build, a no-op check, then one comment line
appended to the three named runtime files and a rebuild, then a `git checkout`
revert and a rebuild. A watchdog stops the build if free disk falls below 20 GB.

Arms:
- (a) ccache on, cold cache: new tree `build/ci-vulkan-ccache`, empty `CCACHE_DIR`.
- (b) ccache on, warm cache: delete and reconfigure the same tree, keep the cache.
- (c) `CCACHE_DISABLE=1` in the ccache-configured tree from (b).
- (d) fresh tree without ccache on `PATH` (`build/ci-vulkan-fresh`).

Predictions, written before any build:
1. Configure prints `Using CCache:` with the wrapper path for (a) and (b), and the
   compile rules carry the `tools/ci/ccache_ci.py launch` launcher.
2. All four arms build with exit 0 in every step. No log prints
   `PLEASE submit a bug report`, `Stack dump` or `error:`, and no new
   reproducer appears in `/tmp`.
3. ccache 4.5.1 does not cache the module-interface compiles (it does not know
   `-x c++-module` / `-fmodule-output`); they are counted as uncacheable and
   always run the compiler. Implementation units are cached.
4. (a): the full build is almost all misses. The revert rebuild hits the cache
   for the reverted implementation units, because their preprocessed content
   equals the full build's.
5. (b): the full build hits for most implementation units and is faster than (a).
6. (c) and (d): ccache statistics do not change.
Kill condition: a frontend crash stops the arm. Its logs and reproducer are kept,
and the failing command is rerun alone with and without ccache.

## Acceptance criteria
- [ ] Reproduce with one stabilized source surface and exactly one writer per preset tree, comparing enabled/disabled ccache and a fresh tree.
- [ ] Record the compiler/cache cause or a bounded non-reproduction; preserve the existing strict module/toolchain gates.
- [ ] Apply a regression-backed tooling fix only if the isolated evidence requires one; otherwise document the verified build discipline.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicCpuTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```
