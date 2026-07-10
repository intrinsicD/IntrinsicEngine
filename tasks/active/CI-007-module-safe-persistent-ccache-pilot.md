---
id: CI-007
theme: H
depends_on:
  - CI-003
---
# CI-007 — Pilot persistent module-safe ccache in CI

## Status
- In progress on branch `copilot/ci-007-module-safe-ccache`.
- Owner/agent: GitHub Copilot CLI.
- Pilot scope: `pr-fast` only; no other gate consumes the persistent store.
- Current slice: Slice A, fail-closed cache guard, persistence, and telemetry.
- Next verification step: run the hermetic workflow/config regression and the
  module-interface invalidation probe before exercising the default CPU gate.

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
- `cmake/Dependencies.cmake` already activates an installed `ccache` with
  `CCACHE_DEPEND=1` and `CCACHE_NODIRECT=1`, specifically because default
  direct/preprocessor hashing can miss imported BMI changes.
- The 2026-07-09 `CI-003` audit found no active ccache in hosted workflow logs;
  workflows neither install it nor persist its store. Each PR workflow starts
  an independent cold compile taking roughly 15–22 minutes.
- Prior stale module/object incidents produced vtable/slot mismatches and
  unexplained crashes after `.cppm` changes. Build-tree/BMI caching is therefore
  explicitly unsafe until separately proven; this task does not attempt it.
- Cache statistics are part of the `CI-003` telemetry schema. Keys must separate
  compiler, preset, sanitizer, and toolchain/dependency inputs.

## Required changes
- [ ] Install a pinned distro ccache version in a limited pilot workflow and
      configure a repository-independent cache directory and bounded maximum
      size.
- [ ] Persist only that ccache directory with a key namespace including OS,
      exact Clang major/minor, preset, sanitizer identity, and hashes of
      `CMakePresets.json`, CMake toolchain/helpers, vcpkg manifests, and lock/
      overlay inputs; use a SHA suffix plus safe restore prefixes for reuse.
- [ ] Preserve `CCACHE_DEPEND=1` and `CCACHE_NODIRECT=1`; fail the pilot if
      configure does not report the expected launcher/mode.
- [ ] Zero statistics before build and publish hit/miss, cache size, errors,
      compile duration, and cold/warm identity through `CI-003`.
- [ ] Validate three scenarios: empty-cache full build, restored-cache
      unchanged full build, and restored-cache build after a representative
      exported `.cppm` layout/API change.
- [ ] Add a stale-artifact probe that compares the cached result with a clean
      no-ccache build/test result for the interface-change scenario.
- [ ] Expand beyond the pilot only after full-gate correctness parity and a
      five-sample median/p95 comparison; document rollback/clear instructions.

## Slice plan
- **Slice A.** Add the `pr-fast`-only pinned ccache setup, safe cache key,
  external bounded store, configured-launcher guard, machine-readable
  statistics, and workflow/config regressions. No speedup claim.
- **Slice B.** Add a hermetic Clang module-interface invalidation probe,
  document safety/rollback policy, and verify cached versus clean output parity.
- **Slice C.** Publish the workflow, collect empty/restored/interface-change
  full-gate evidence plus five comparable samples, then either retire the task
  or roll back the pilot. Expansion to other gates is explicitly deferred until
  this slice.

## Tests
- [ ] Add workflow/config regression coverage for key dimensions, cache path,
      size cap, depend mode, and direct-mode disablement.
- [ ] Run the default CPU gate from both cached and clean builds after the
      module-interface probe.
- [ ] Run the repository stale-build regression/probe that exercises imported
      class layout or virtual surface changes.
- [ ] Record cache hit/miss and wall-time distributions against `CI-003`.

## Docs
- [ ] Document cache key inputs, safety mode, size cap, clear/rollback
      procedure, and the prohibition on build/BMI caching.
- [ ] Link the stale-build triage playbook from the CI cache documentation.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] CI persists only the ccache store, never build/BMI state.
- [ ] Warm runs show non-zero safe hits and a baseline-comparable build-time
      delta.
- [ ] A `.cppm` interface/layout change invalidates affected importers and
      matches a clean no-cache build/test result.
- [ ] Cache failures are visible and cannot silently substitute stale objects.

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

## Forbidden changes
- Caching any configured build directory, BMI, `.o`, or Ninja state directly.
- Enabling ccache direct mode for C++23 module builds.
- Sharing cache namespaces across compiler or sanitizer identities.
- Declaring a speedup without clean/cached correctness parity and comparable
  baseline samples.
