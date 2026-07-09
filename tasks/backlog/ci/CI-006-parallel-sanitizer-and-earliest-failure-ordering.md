---
id: CI-006
theme: none
depends_on: []
---
# CI-006 — Parallelize the sanitizer ctest and run diff-relevant tests first in pr-fast

## Goal
- Recover the ~4-6 min the sanitizer job loses to serial ctest, and surface a failure in the layer being edited within seconds of build completion — with the executed set provably a superset of today's gate.

## Non-goals
- Do not parallelize the deliberately-serial benchmark/SLO ctest steps (`nightly-deep.yml:121`) — CI-001 recorded that serialness as intentional to avoid benchmark/GPU contention.
- Do not change which tests run in `pr-fast` (only their order); pass 2 stays byte-identical to today's expression.
- Do not depend on the CI-008 touched-scope hardening; pass 1 uses `--print` labels only and degrades safely to pass 2.

## Context
- Owner: CI/tooling; touches `.github/workflows/{ci-sanitizers,pr-fast}.yml`.
- `ci-sanitizers.yml:64` runs `ctest -L "unit|contract|integration" ...` with no `-j` — the only CPU test step in the repo missing parallelism (`pr-fast.yml:52`, `ci-linux-clang.yml:104`, `ci-vulkan.yml:78` all pass `-j$(nproc)`). The asan test phase measured 457s serial (run 28955217283); the parallel `pr-fast` ctest covers a comparable case count in 160-244s on the same runners.
- CI-001's follow-up note (`tasks/done/CI-001-slim-engine-test-runtime.md`) kept only performance/SLO and GPU steps serial by decision — sanitizer serialness is an unowned accident, not policy.
- `tools/ci/touched_scope.py --print` already maps changed paths to ownership labels; using its labels for a first pass (no `--run`, no build coupling) makes the tests owning the diff execute first, then the unchanged full expression re-runs them (seconds) so nothing is dropped.

## Required changes
- [ ] Add `-j$(nproc)` to the `ctest` invocation at `ci-sanitizers.yml:64`.
- [ ] In `pr-fast.yml`, replace the single ctest with two invocations: pass 1 = `touched_scope.py --print` ownership label(s) intersected with the current expression (`-L '<layer>' -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'`); pass 2 = today's byte-identical `-L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)`.
- [ ] Ensure a failed/empty `touched_scope.py` plan degrades to running pass 2 alone (today's exact behavior) — pass 1 is best-effort ordering, never a gate.

## Tests
- [ ] Confirm the asan job's test phase wall-clock drops materially after adding `-j`.
- [ ] Confirm pass 2's printed ctest test-count total equals today's gate total (superset proof), and that a fault injected into a touched-layer test surfaces in pass 1.
- [ ] Confirm removing/breaking `touched_scope.py` output leaves pass 2 running the full gate.

## Docs
- [ ] Update workflow docs per `docs/agent/docs-sync-policy.md` to describe the two-pass ordering and the sanitizer `-j` change, noting the benchmark/SLO serial steps are deliberately untouched.

## Acceptance criteria
- [ ] The sanitizer job test phase runs parallel and drops ~4-6 min; the asan job total falls from ~1737s toward ~1450s.
- [ ] `pr-fast`'s executed test set is a provable superset of today's (pass 2 unchanged), visible in the workflow diff and in ctest's printed totals.
- [ ] Any newly surfaced parallel-contention flake becomes a `BUG-` task, not a quarantine (`AGENTS.md`; `tests/README.md` flaky policy).

## Verification
```bash
grep -n "ctest" .github/workflows/ci-sanitizers.yml   # confirm -j present
grep -n "ctest" .github/workflows/pr-fast.yml          # confirm pass 2 expression unchanged
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir build/ci --print
# Dynamic: compare asan test-phase duration and pr-fast ctest totals before/after on the landing PR.
```

## Forbidden changes
- Parallelizing benchmark/SLO ctest steps (deliberate serial decision).
- Narrowing pass 2 below today's `-L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'` expression.
- Treating pass 1 as sufficient or letting it gate merges.
