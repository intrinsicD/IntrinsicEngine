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

## Status
- In progress on 2026-07-17; owner: Codex; branch: `main`.
- The staged fail-closed planner and `ci-fast` workflow are live. The declared
  five-run broad-route smoke cohort passed its direct numerical checks, but a
  focused-owner graph audit showed that the measurement was not sufficient for
  admission.
- Next verification: right-size the smoke so its focused-owner increment meets
  the declared closure budget, then collect focused and docs-only route
  populations. The five hosted runs remain the broad-fallback population.

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
- At task start, hidden preset `base` enabled sanitizers and `ci` inherited
  them, so fast prototyping paid sanitizer compile cost before dedicated
  sanitizer gates reported. The new `ci-fast` identity is explicitly
  unsanitized and Null/headless; `CI-006` still owns the broader sanitizer
  topology.
- Retired `CI-002` supplied the initial touched-scope helper, but no workflow
  consumed it and several diff, target, runtime, physics, and module-interface
  failure modes could yield incomplete routing. This task replaced that path
  with staged merge-base planning and strict fresh-registry reconciliation.
- `CI-004` supplies the label-derived `IntrinsicPrFastTests` and
  `IntrinsicPrSmokeTests` aggregates. `BUG-106` and `BUG-107` made their
  ownership and configured graph deterministic before this route was enabled.
  Full merge-gate routing remains owned by `CI-009`.

## Measurement policy
- Declared on 2026-07-17 before reading any `ci-fast` hosted result:
  `IntrinsicPrSmokeTests` remains broad-fallback-only until at least five
  comparable `ubuntu-24.04`/Clang hosted samples exist at one source and preset
  identity.
- The candidate passes only when its unique incremental Ninja command closure
  is at most 5% of `IntrinsicPrFastTests` and nearest-rank p95 for its
  incremental build batch plus exact smoke test batch is at most 60 seconds.
  Median, p95, cache state, selected cases, and run IDs must all be retained.
- A separate admission slice may enable the smoke for focused source routes
  only after both closure and latency are shown in that routing context.
  Docs/task-only routes remain structural-only, and a failed or incomplete
  sample cannot count toward the population.

All five counted runs used commit
`1098922a321ba51759ab9b489bfbd8c8af05c562`, `ubuntu-24.04`, `ci-fast`,
Clang/scan-deps 20.1.2, ccache 4.9.1, the same configured registry and selected
test digests, an exact warm cache hit with 606 hits and zero misses/errors,
2,007 PR-fast commands, 12 incremental smoke commands, and 3,740 + 60 selected
cases:

| Run | Smoke closure | Smoke build + test |
| --- | ---: | ---: |
| `29582459870` | 12 / 2,007 (0.598%) | 19.406 s |
| `29582459918` | 12 / 2,007 (0.598%) | 19.395 s |
| `29582459867` | 12 / 2,007 (0.598%) | 19.331 s |
| `29582459970` | 12 / 2,007 (0.598%) | 18.610 s |
| `29582459959` | 12 / 2,007 (0.598%) | 19.495 s |

Broad-route median is 19.395 seconds and nearest-rank p95 is 19.495 seconds.
The 0.598% figure is the smoke increment after all PR-fast commands. The fresh
configured graph shows materially larger increments after focused owners:
geometry 856 commands (42.651%), graphics 348 (17.339%), assets 1,200
(59.791%), and platform 1,232 (61.385%). The candidate therefore remains
broad-only and must be right-sized before admission. Cold cache-prime run
`29580789612` was excluded from the counted population.

## Required changes
- [x] Add `ci-fast` configure/build presets with Clang 20 module scanning,
      tests enabled, Sandbox/benchmarks/CUDA/sanitizers disabled explicitly,
      and an explicit Null/headless platform/backend identity.
- [x] Split planning into a pre-configure changed-file classification and a
      post-configure target/inventory validation. Docs/task-only changes finish
      structural checks without configure; source plans validate against the
      freshly configured canonical registry before build.
- [x] Determine changed files from the unique merge base of the PR base/head
      SHAs and make merge-base/diff failure, empty/missing refs, rename/delete
      ambiguity, or planner exceptions fail closed into the broad path rather
      than an empty success.
- [x] Repair stale runtime mappings, add physics ownership, and make every
      module-interface, CMake/preset/toolchain, dependency-manifest, and unknown
      source change broad-fall back until real dependency evidence proves a
      narrower route.
- [x] Reject an undeclared selected target or registry mismatch with an
      actionable failure/broad fallback; never silently discard a requested
      target from the command plan.
- [x] Execute the conservative plan in PR-fast. Broad-fallback scopes build the
      complete PR-fast aggregate rather than the default `all` target.
- [ ] Measure the actual source/test closure and wall time of
      `IntrinsicPrSmokeTests` with comparable reference runs, evaluate them
      against the predeclared incremental p95 latency/compile-closure budget,
      and right-size the existing registry-derived aggregate if it exceeds that
      budget.
- [ ] Only after the candidate meets the declared budget, run the resulting
      bounded cross-layer smoke for source changes in addition to touched-owner
      tests. Until then, retain it in broad fallback rather than making every
      narrow plan pay an unmeasured closure.
- [x] Preserve required full CPU, ASan, UBSan, and opt-in Vulkan checks outside
      this feedback gate.
- [x] Publish selected files, reasons, targets, labels, test count, and broad-
      fallback decision as a machine-readable artifact and step summary
      alongside `CI-003` timing telemetry.

## Tests
- [x] Extend touched-scope regressions for docs-only, tasks-only, one-layer
      source, cross-layer source, `.cppm`, CMake/preset/toolchain, workflow,
      dependency-manifest, rename/delete, and unknown paths.
- [x] Add fail-closed cases for diff failure, zero changed files on a PR event,
      the stale runtime target, missing physics coverage, and an undeclared
      target in a configured registry.
- [ ] Add workflow integration fixtures proving each planner result executes
      the expected aggregate, CTest filter, and structural checks.
- [x] Prove a planner error or missing base ref broad-falls back and cannot
      produce a success-shaped empty gate.
- [ ] Compare at least five representative docs-only, focused-source, and
      broad-fallback runs to the `CI-003` baseline by median/p95.

## Docs
- [x] Update `AGENTS.md` and `docs/benchmarking/ci-policy.md` to distinguish
      local/PR-fast feedback from the required full merge confidence gate.
- [x] Document `ci-fast`, the budgeted cross-layer smoke, broad-fallback
      triggers, and how developers reproduce the selected plan locally.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] Docs/task-only PRs complete structural validation without a C++ build.
- [ ] Focused source PRs build touched aggregates plus the measured/right-sized
      cross-layer smoke that meets the declared budget, while module/build-
      system/unknown changes run the broad fallback.
- [x] The fast preset is unsanitized, and dedicated sanitizer jobs remain
      required.
- [x] No changed-file or planner failure mode can yield an empty success.
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
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Making touched-scope PR-fast the only required merge confidence signal.
- Treating `.cppm`, CMake, toolchain, dependency, or unknown changes as narrow.
- Making the candidate cross-layer smoke unconditional before it meets its
  declared closure/latency budget.
- Silently succeeding when no changed-file plan can be computed.
- Silently filtering a mapped target that is absent from the configured graph.
