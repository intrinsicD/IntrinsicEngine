---
id: CI-005
theme: H
depends_on:
  - CI-003
  - CI-004
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
- No change to the ownership/layering decisions encoded in
  `tools/ci/touched_scope.py`.

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
- `CI-004` supplies label-derived aggregates and the always-on
  `IntrinsicPrSmokeTests` safety net. Full merge-gate routing remains owned by
  `CI-009`.

## Required changes
- [ ] Add `ci-fast` configure/build presets with Clang 20 module scanning,
      tests enabled, Sandbox/CUDA disabled, and sanitizers disabled explicitly.
- [ ] Determine changed files from the PR base/head SHAs and run
      `tools/ci/touched_scope.py` after configure to produce the build/test/
      structural plan as a machine-readable artifact and step summary.
- [ ] Execute the conservative plan in PR-fast. Docs/task-only changes run no
      C++ configure/build; broad-fallback scopes run the complete PR-fast
      aggregate rather than silently narrowing.
- [ ] Always run the small cross-layer `IntrinsicPrSmokeTests` contract for
      source changes, in addition to touched-owner tests, to catch composition
      regressions outside a single label.
- [ ] Add explicit handling for deleted/renamed files, merge commits, missing
      base refs, and planner failures; every uncertainty chooses the broad path.
- [ ] Preserve required full CPU, ASan, UBSan, and opt-in Vulkan checks outside
      this feedback gate.
- [ ] Publish selected files, reasons, targets, labels, test count, and broad-
      fallback decision alongside `CI-003` timing telemetry.

## Tests
- [ ] Extend touched-scope regressions for docs-only, tasks-only, one-layer
      source, cross-layer source, `.cppm`, CMake/preset/toolchain, workflow,
      dependency-manifest, rename/delete, and unknown paths.
- [ ] Add workflow integration fixtures proving each planner result executes
      the expected aggregate, CTest filter, and structural checks.
- [ ] Prove a planner error or missing base ref broad-falls back and cannot
      produce a success-shaped empty gate.
- [ ] Compare at least five representative docs-only, focused-source, and
      broad-fallback runs to the `CI-003` baseline by median/p95.

## Docs
- [ ] Update `AGENTS.md` and the CI workflow docs to distinguish local/PR-fast
      feedback from the required full merge confidence gate.
- [ ] Document `ci-fast`, the always-on smoke, broad-fallback triggers, and how
      developers reproduce the selected plan locally.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] Docs/task-only PRs complete structural validation without a C++ build.
- [ ] Focused source PRs build touched aggregates plus
      `IntrinsicPrSmokeTests`, while module/build-system/unknown changes run the
      broad fallback.
- [ ] The fast preset is unsanitized, and dedicated sanitizer jobs remain
      required.
- [ ] No changed-file or planner failure mode can yield an empty success.
- [ ] Median/p95 feedback latency is reported for all three routing classes
      against the named `CI-003` baseline.

## Verification
```bash
cmake --preset ci-fast
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --preset ci-fast --build-dir build/ci-fast --print
python3 tools/ci/touched_scope.py --root . --changed-file docs/build-troubleshooting.md --print
python3 tools/ci/touched_scope.py --root . --changed-file src/runtime/Runtime.Engine.cppm --print
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Making touched-scope PR-fast the only required merge confidence signal.
- Treating `.cppm`, CMake, toolchain, dependency, or unknown changes as narrow.
- Skipping the always-on cross-layer smoke for source changes.
- Silently succeeding when no changed-file plan can be computed.
