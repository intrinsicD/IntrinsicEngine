---
id: CI-009
theme: none
depends_on: []
---
# CI-009 — Scope-split the IntrinsicTests aggregate for the fast lane

## Goal
- Stop `pr-fast` from building test executables its gate never runs by adding a generated fast-lane aggregate derived from the same label registry ctest already uses, shrinking the uncacheable link tail of the PR build.

## Non-goals
- Do not merge, delete, or relabel any test executable (CI-001 forbids merging the executables into one binary).
- Do not remove any executable from the full `ci-linux-clang` build.
- Do not change test selection at ctest time — only what the fast lane compiles.

## Context
- Owner: CI/tooling; touches `tests/CMakeLists.txt` and `.github/workflows/pr-fast.yml` (optionally `ci-sanitizers.yml`).
- `IntrinsicTests` is a catch-all custom target depending on every registered test executable (`tests/CMakeLists.txt:1162-1173`, populated from the `INTRINSIC_REGISTERED_TEST_TARGETS` global property), yet `pr-fast` runs only `-L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'` (`pr-fast.yml:52`). The `INTRINSIC_REGISTERED_TEST_LABELS` global property (`:109-112`) already records each executable's labels, so a fast aggregate can be derived mechanically.
- Executables the fast gate never runs but still builds today include `IntrinsicRuntimeGraphicsCpuTests` (integration, `:1133-1137`), `IntrinsicRuntimePhysicsBridgeTests` (integration, `:988-994`), `IntrinsicRuntimeIntegrationTests` (`:914-920`), `IntrinsicBenchmarkTests` (`benchmark;slo;slow`, `:961-965`), and `IntrinsicGraphicsUnitTests` (gpu;vulkan label debt, `:1079-1083`).
- Honest sizing: in the `ci` preset the heavy Vulkan-gated executables are already absent, so the saving is the excluded executables' test TUs plus 3-5 fewer Debug+ASan links — low single-digit minutes — but it is nearly free and compounds with CI-004 (link steps are exactly what ccache cannot cache) and with CI-008's narrower default aggregate.

## Required changes
- [ ] Add a generated aggregate (e.g. `IntrinsicPrFastTests`) in `tests/CMakeLists.txt` derived from `INTRINSIC_REGISTERED_TEST_LABELS`, including exactly the executables whose labels match the fast-gate expression (`unit|contract` minus `gpu|vulkan|slow|flaky-quarantine`).
- [ ] Switch `pr-fast.yml`'s build step to build `IntrinsicPrFastTests` instead of `IntrinsicTests`.
- [ ] Add a `check_prerequisites`-style step (same tool/pattern as `ci-linux-clang.yml:86-103`) listing the fast-gate binaries plus a one-line `N executables in fast aggregate / M registered` log so the selection is auditable and a missing binary fails explicitly.
- [ ] Optionally apply the same derivation to `ci-sanitizers.yml` (its gate is `-L 'unit|contract|integration'`, so it also over-builds the benchmark target).

## Tests
- [ ] Confirm the fast aggregate contains exactly the label-matching executables (audit line + `check_prerequisites` step).
- [ ] Confirm a compile break in an excluded TU still fails the same PR via `ci-linux-clang`'s full `cmake --build --preset ci`.
- [ ] Confirm `pr-fast`'s ctest still selects and runs the same cases as today (only the build scope narrowed).

## Docs
- [ ] Update `tests/README.md` and workflow docs per `docs/agent/docs-sync-policy.md` to document the generated fast aggregate and how it is derived from the label registry.

## Acceptance criteria
- [ ] `pr-fast` builds only the executables its gate runs; the audit line reports the count and `check_prerequisites` fails loudly on a missing binary.
- [ ] Full-build compile coverage of excluded TUs stays same-PR detectable via `ci-linux-clang`.
- [ ] The fast build time drops measurably (low single-digit minutes), compounding with CI-004.

## Verification
```bash
grep -n "IntrinsicPrFastTests\|INTRINSIC_REGISTERED_TEST_LABELS" tests/CMakeLists.txt
grep -n "target" .github/workflows/pr-fast.yml
python3 tools/repo/check_test_layout.py --root . --strict
# Dynamic: compare pr-fast Build-step duration before/after via time_command telemetry on the landing PR.
```

## Forbidden changes
- Merging test executables into one binary, or removing any assertion (CI-001 forbidden list).
- Narrowing the ctest selection (this task changes only what is compiled).
- Hand-maintaining the fast-aggregate membership list instead of deriving it from the label registry.
