---
id: CI-004
theme: none
depends_on: []
---
# CI-004 — Warm compiler cache (ccache) for the non-authoritative PR build lanes

## Goal
- Cut the dominant PR feedback cost — the cold C++23-module compile that is 83% of `pr-fast` wall clock — by persisting a ccache across runs in the non-authoritative lanes, while keeping at least one from-scratch cold build of every PR commit as the source of truth.

## Non-goals
- Do not add ccache to `ci-linux-clang.yml` or `nightly-deep.yml`: they stay cold from-scratch builds and remain the staleness canaries.
- Do not change the `CCACHE_DEPEND`/`CCACHE_NODIRECT` configuration in `cmake/Dependencies.cmake` (it is automatic and BUG-015-mandated); do not switch to `direct` mode.
- Do not change build targets, presets, or test selection.

## Context
- Owner: CI/tooling; touches `.github/workflows/{pr-fast,ci-sanitizers,ci-vulkan}.yml` and optionally `ci-bench-smoke.yml`.
- Measured: `Build IntrinsicTests` in `pr-fast` is 952s of 1150s (run 28955217245) and 1381s of 1662s (run 28948497826); across a single push the five cold builds total ~6,673s (~111 min). No workflow installs ccache and no Actions cache covers a ccache dir — only `external/vcpkg-bincache` is cached.
- `cmake/Dependencies.cmake:5-18` already auto-enables ccache in the module-safe depend/nodirect mode (added for BUG-015 to prevent object reuse against stale module BMIs) *if the `ccache` binary is present* — so this is workflow-only wiring, no CMake change.
- The stale-BMI hazard is real (the `intrinsicengine-stale-build-triage` skill exists for it); the design confines any staleness-induced false green to a lane whose result is contradicted, in the same PR, by a cold authoritative build of the identical commit.

## Required changes
- [ ] Add `ccache` to the apt install lists in `pr-fast.yml`, `ci-sanitizers.yml`, and `ci-vulkan.yml` (and optionally `ci-bench-smoke.yml`).
- [ ] Add an `actions/cache@v4` step for `CCACHE_DIR` in each of those workflows, keyed per preset and per sanitizer matrix variant (e.g. `${{ runner.os }}-ccache-<preset>-<matrix.sanitizer.name>-${{ github.sha }}` with a `restore-keys:` prefix fallback) so `ci`/`ci-vulkan`/asan/ubsan object caches never cross-contaminate; include `hashFiles('src/**/*.cppm','cmake/**','CMakePresets.json')` in the key so module-interface changes cold-start.
- [ ] Export `CCACHE_DIR` to the cache path and add a `ccache --show-stats` step after each build so hit rates are visible in the log.
- [ ] Confirm `ci-linux-clang.yml` and `nightly-deep.yml` remain ccache-free.

## Tests
- [ ] On the landing PR, confirm from `ccache --show-stats` that the warm lanes show a non-trivial hit rate on the second run and that the `Build` step wall-clock drops materially versus the cold baseline.
- [ ] Confirm the authoritative `ci-linux-clang` build of the same commit still runs cold (no ccache step present) and passes.

## Docs
- [ ] Update workflow docs per `docs/agent/docs-sync-policy.md` §"CI/workflow changes" to record which lanes are warm vs cold and why (the authoritative-cold invariant), and note the GitHub 10 GB per-repo cache budget shared with `vcpkg-bincache`.

## Acceptance criteria
- [ ] Typical incremental-PR `pr-fast` build time drops from the ~16-23 min cold baseline toward single-digit minutes on a cache hit; unit/contract failure signal drops correspondingly.
- [ ] `ci-linux-clang` (cold, uncached) still builds and tests every PR commit and every push to `main`, so a stale-cache false green in a warm lane is caught in the same PR by a from-scratch build of that commit.
- [ ] `nightly-deep` remains a daily cold build, bounding any cache-induced divergence to ≤24h.

## Verification
```bash
# Static: confirm ccache wiring is present only in the intended lanes.
grep -L ccache .github/workflows/ci-linux-clang.yml .github/workflows/nightly-deep.yml   # both must have NO ccache
grep -l ccache .github/workflows/pr-fast.yml .github/workflows/ci-sanitizers.yml .github/workflows/ci-vulkan.yml
# Dynamic: read ccache --show-stats output and Build-step durations from the landing PR's Actions run.
```

## Forbidden changes
- Enabling ccache in `ci-linux-clang.yml` or `nightly-deep.yml`.
- Switching ccache out of depend/nodirect mode, or otherwise reintroducing the BUG-015 stale-BMI reuse hazard.
- Sharing one cache key across presets or sanitizer variants.
