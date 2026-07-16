---
id: CI-005
theme: H
depends_on:
  - CI-003
  - CI-004
  - BUG-106
  - BUG-107
---
# CI-005 — Make PR-fast a real touched-scope feedback gate

## Goal
- Introduce an unsanitized `ci-fast` preset and wire the existing conservative
  touched-scope planner into PR-fast so small changes receive useful feedback
  without paying for a near-full sanitized build and test run.

## Non-goals
- No replacement or weakening of the full merge CPU/sanitizer gates.
- No narrow routing for unknown source, module-interface, CMake, toolchain, or
  dependency changes; those must fall back to the broad gate.
- No CMake File-API dependency planner, generated reverse-dependency service,
  or new target-selection framework; repair the existing conservative map.

## Context
- Owner: CMake presets, `tools/ci/touched_scope.py`, PR workflow routing, and
  tooling regressions.
- Representative `CI-003` data: PR-fast took 25m, with ~20m06s in build and
  221.27s in 3,526 tests. The full CPU cohort had 3,592–3,594 tests, so the
  current "fast" selector is effectively full.
- `CMakePresets.json` sets `INTRINSIC_ENABLE_SANITIZERS=ON` in hidden `base`;
  `ci` inherits it. Dedicated ASan and UBSan jobs then rebuild separate
  sanitizer variants. Fast prototyping therefore pays sanitizer compile cost
  before the dedicated sanitizer gates report.
- Retired `CI-002` delivered `tools/ci/touched_scope.py`. It already maps
  changed paths to conservative targets/labels/structural checks, avoids C++
  work for docs/task-only changes, and broad-falls back for build-system or
  unknown scope, but no GitHub workflow consumes it.
- The helper is not yet safe to promote unchanged: a failed `git diff` returns
  an empty changed-file list and therefore no commands; undeclared mapped
  targets are silently filtered; the runtime map names the absent
  `IntrinsicRuntimeSelectionContractTests`; physics has no narrow mapping; and
  most mapped `.cppm` changes are treated as narrow rather than as dependency-
  graph uncertainty.
- `CI-004` supplies label-derived aggregates and the current
  `IntrinsicPrSmokeTests` candidate. Its source/compile closure has not yet been
  shown small enough for unconditional touched-scope use. Full merge-gate
  routing remains owned by `CI-009`.
- `BUG-106` must first make target labels/ownership truthful, and `BUG-107`
  must make a fresh configured target graph deterministic. Routing against an
  ambiguous registry or configure-history-dependent graph would only make the
  fast gate confidently incomplete.

## Required changes
- [ ] Add `ci-fast` configure/build presets with Clang 20 module scanning,
      tests enabled, Sandbox/benchmarks/CUDA/sanitizers disabled explicitly,
      and an explicit Null/headless platform/backend identity.
- [ ] Split planning into a pre-configure changed-file classification and a
      post-configure target/inventory validation. Docs/task-only changes finish
      structural checks without configure; source plans validate against the
      freshly configured canonical registry before build.
- [ ] Determine changed files from the PR base/head SHAs and make diff failure,
      empty/missing base refs, rename/delete ambiguity, or planner exceptions
      fail closed into the broad path rather than an empty success.
- [ ] Repair stale runtime mappings, add physics ownership, and make every
      module-interface, CMake/preset/toolchain, dependency-manifest, and unknown
      source change broad-fall back until real dependency evidence proves a
      narrower route.
- [ ] Reject an undeclared selected target or registry mismatch with an
      actionable failure/broad fallback; never silently discard a requested
      target from the command plan.
- [ ] Execute the conservative plan in PR-fast. Broad-fallback scopes build the
      complete PR-fast aggregate rather than the default `all` target.
- [ ] Measure the actual source/test closure and wall time of
      `IntrinsicPrSmokeTests`, declare an incremental p95 latency/compile-closure
      budget from comparable reference runs, and right-size the existing
      registry-derived aggregate if it exceeds that budget.
- [ ] Only after the candidate meets the declared budget, run the resulting
      bounded cross-layer smoke for source changes in addition to touched-owner
      tests. Until then, retain it in broad fallback rather than making every
      narrow plan pay an unmeasured closure.
- [ ] Preserve required full CPU, ASan, UBSan, and opt-in Vulkan checks outside
      this feedback gate.
- [ ] Publish selected files, reasons, targets, labels, test count, and broad-
      fallback decision as a machine-readable artifact and step summary
      alongside `CI-003` timing telemetry.

## Tests
- [ ] Extend touched-scope regressions for docs-only, tasks-only, one-layer
      source, cross-layer source, `.cppm`, CMake/preset/toolchain, workflow,
      dependency-manifest, rename/delete, and unknown paths.
- [ ] Add fail-closed cases for diff failure, zero changed files on a PR event,
      the stale runtime target, missing physics coverage, and an undeclared
      target in a configured registry.
- [ ] Add workflow integration fixtures proving each planner result executes
      the expected aggregate, CTest filter, and structural checks.
- [ ] Prove a planner error or missing base ref broad-falls back and cannot
      produce a success-shaped empty gate.
- [ ] Compare at least five representative docs-only, focused-source, and
      broad-fallback runs to the `CI-003` baseline by median/p95.

## Docs
- [ ] Update `AGENTS.md` and `docs/benchmarking/ci-policy.md` to distinguish
      local/PR-fast feedback from the required full merge confidence gate.
- [ ] Document `ci-fast`, the budgeted cross-layer smoke, broad-fallback
      triggers, and how developers reproduce the selected plan locally.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] Docs/task-only PRs complete structural validation without a C++ build.
- [ ] Focused source PRs build touched aggregates plus the measured/right-sized
      cross-layer smoke that meets the declared budget, while module/build-
      system/unknown changes run the broad fallback.
- [ ] The fast preset is unsanitized, and dedicated sanitizer jobs remain
      required.
- [ ] No changed-file or planner failure mode can yield an empty success.
- [ ] Every selected target exists in the configured canonical registry, and
      the measured PR-smoke closure is recorded rather than assumed small.
- [ ] Median/p95 feedback latency is reported for all three routing classes
      against the named `CI-003` baseline.

## Verification
```bash
cmake --preset ci-fast
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print
python3 tools/ci/touched_scope.py --root . --changed-file docs/build-troubleshooting.md --print
python3 tools/ci/touched_scope.py --root . --changed-file src/runtime/Runtime.Engine.cppm --print
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Making touched-scope PR-fast the only required merge confidence signal.
- Treating `.cppm`, CMake, toolchain, dependency, or unknown changes as narrow.
- Making the candidate cross-layer smoke unconditional before it meets its
  declared closure/latency budget.
- Silently succeeding when no changed-file plan can be computed.
- Silently filtering a mapped target that is absent from the configured graph.
