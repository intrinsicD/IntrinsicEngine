---
id: CI-008
theme: none
depends_on:
  - CI-007
  - BUG-063
---
# CI-008 — Fail-closed touched-scope PR tier with a merge-queue full gate

## Goal
- Give PRs a fast, selective build/test tier driven by a hardened `touched_scope.py`, while the full canonical gates run once per PR at merge time — so docs-only and single-layer PRs finish in seconds-to-minutes instead of ~19-28 min, with coverage loss made structurally impossible and any selector uncertainty degrading to the full gate.

## Non-goals
- Do not weaken or delete the canonical full CPU gate; it moves tiers, it does not disappear (`AGENTS.md` §"default CPU correctness gate").
- Do not wire selection into CI before the fail-closed hardening and its regression tests land.
- Do not treat `touched_scope.py` as a replacement for the full PR/merge gate (`AGENTS.md:204-206` preserved verbatim).

## Context
- Owner: CI/tooling; touches `tools/ci/touched_scope.py`, its regression tests, `.github/workflows/{pr-fast,ci-linux-clang,ci-sanitizers}.yml`, and GitHub merge-queue/branch-protection settings.
- Both `pr-fast` and `ci-linux-clang` build on every PR today; `ci-linux-clang` also runs on push to `main`, so a full merge-side gate already exists to re-tier onto.
- `touched_scope.py` is a documented local-only aid (CI-002 explicitly scoped Actions wiring out as a non-goal — this is a follow-on, not a duplicate) and has verified defects that make its naive CI use dangerous:
  - **Fail-open diff**: `run_git_diff_name_only` returns `[]` with only a warning on git-diff failure (lines 91-97); an empty plan yields zero commands and exit 0 (lines 268-269, 349-352) — with a default shallow checkout the gate would go green having run nothing.
  - **No dependency closure**: `src/geometry/` selects only `IntrinsicGeometryTests` (line 42) though `physics`/`graphics`/`runtime` consume geometry (`AGENTS.md:71-84`); `.cppm` interface changes are treated like `.cpp`.
  - **Per-layer CMake blind spot**: 21 `src/**/CMakeLists.txt` files match the layer-prefix loop (lines 188-192) instead of the build-graph broad rule (line 127).
  - **Silent target pruning + proven drift**: `is_target_declared` drops undeclared targets without failing (lines 100-111); the map already references `IntrinsicRuntimeSelectionContractTests` (lines 47, 86), a target that no longer exists in `tests/`.
- Depends on CI-007 (the audit layer that asserts tiers cover every test and that triggers stay wired) and on BUG-063 (a flaky full gate bounces queued merges).

## Required changes
- [ ] Harden `touched_scope.py`: (a) fail closed — diff failure or empty plan escalates to the broad gate or exits non-zero, never exit-0-silent; (b) add dependency closure per `AGENTS.md:71-84` and escalate any `.cppm` interface change to the broad gate; (c) route `src/**/CMakeLists.txt` to the build-graph broad rule; (d) make undeclared-target pruning loud (fail, don't drop).
- [ ] Replace the regression cases that lock in the unsafe behavior (e.g. `Test.TouchedScope.py:70-81` asserting `.cppm` stays narrow) and add cases for fail-closed diff, dependency closure, CMake broad-routing, and drift detection (every mapped target exists in `tests/CMakeLists.txt`; every mapped label is in the allow-list).
- [ ] Wire the PR tier into `pr-fast.yml` with `fetch-depth: 0`, running `--print` and uploading the selection plan (changed files + per-file reasons) as an artifact before `--run` decides the build/test scope.
- [ ] Re-tier the full gates: switch `ci-linux-clang.yml`'s `pull_request:` trigger to `merge_group:`, keeping `push: branches [main]` as a post-merge backstop; optionally do the same for `ci-sanitizers.yml`. Enable the GitHub merge queue on `main` and mark the `merge_group` jobs required.

## Tests
- [ ] Run the expanded `Test.TouchedScope.py` (registered in CI by this task) covering fail-closed, dependency-closure, CMake-broad, and drift cases.
- [ ] Confirm a docs-only PR skips the C++ build; a single-layer `src/geometry` change builds `IntrinsicGeometryTests` plus its declared downstream dependents; a `.cppm` change escalates to the broad gate.
- [ ] Confirm the `merge_group` job runs the identical full-gate commands and blocks merge on failure.

## Docs
- [ ] Update `AGENTS.md`, `docs/architecture/test-strategy.md`, and workflow docs to describe the tiered topology, the fail-closed semantics, and the selection-plan artifact, per `docs/agent/docs-sync-policy.md`.

## Acceptance criteria
- [ ] Docs/tasks/tools-only PRs finish in `ci-docs`-like time; single-layer src PRs build a narrow target set; the per-push runner compute drops from ~133 min toward ~27 min, with the full gates paid once per PR at merge.
- [ ] Selector uncertainty (diff error, empty plan, unmapped path, `.cppm` change) always degrades to the broad CPU gate — never to silence — proven by regression tests.
- [ ] Every PR uploads a machine-generated selection plan; CI-007's audit fails if a gate loses both its `merge_group` and `push:main` triggers.
- [ ] Nothing enters `main` without the canonical CPU gate passing as a required `merge_group` check.

## Verification
```bash
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/ci/touched_scope.py --root . --changed-file docs/build-troubleshooting.md --print   # no C++ build
python3 tools/ci/touched_scope.py --root . --changed-file src/geometry/Geometry.PointCloud.IO.cpp --print   # geometry + declared dependents
python3 tools/ci/touched_scope.py --root . --changed-file src/graphics/rhi/RHI.Device.cppm --print   # .cppm -> broad gate
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Wiring selection into CI before the fail-closed hardening + regression tests land.
- Any path where an empty/failed selection plan yields a green gate.
- Removing the `push: branches [main]` backstop or the canonical merge-tier gate.

## Maturity
- Target: `Operational` — the selective PR tier and merge-queue full gate must be live and enforced on `main`, not merely scripted.
- Prerequisite ordering: CI-007 (audit) lands first as the coverage-detectability layer; BUG-063 (full-gate flake) is fixed or understood before enabling the merge queue.
