---
id: CI-005
theme: none
depends_on:
  - BUG-064
---
# CI-005 — ci-vulkan device probe short-circuit and per-merge re-tier

## Goal
- Stop spending ~24-25 min of build per PR push to run `gpu;vulkan` fixtures that finish in 6s (skipped/failed) on a GPU-less hosted runner, and move real GPU execution to a lane that actually has a device — increasing net GPU coverage, not decreasing it.

## Non-goals
- Do not delete or relabel any `gpu;vulkan` test, and do not skip GPU tests via flag renames (forbidden by `docs/agent/prompt/prompt.md`).
- Do not remove the ci-vulkan *build* verification of the `ci-vulkan` preset — only change how often and where it runs.
- Do not resolve BUG-064's headless-display defect here beyond what re-tiering requires; BUG-064 owns the diagnosis.

## Context
- Owner: CI/tooling; touches `.github/workflows/ci-vulkan.yml` and the nightly GPU lane wiring in `nightly-deep.yml`.
- The last 4 `ci-vulkan` PR runs (28955217166, 28950940552, 28948497790, 28946615304) each spent 1491-1545s building `ExtrinsicSandbox`+`IntrinsicTests`, then failed the `Run gpu;vulkan opt-in fixtures` step in 6s on the runner pinned at `ci-vulkan.yml:9` (`ubuntu-24.04`, no GPU). BUG-064 records the workflow's "signal value is currently zero" and warns a permanently-red check trains reviewers to ignore red gates.
- The fixtures `GTEST_SKIP` without a Vulkan device (comment at `ci-vulkan.yml:64-76`), so the PR job never executes GPU code paths — GPU coverage today is effectively nil on the hosted lane.
- `nightly-deep.yml` already has a `nightly-gpu-optional` job on `[self-hosted, linux, gpu]` gated by `vars.INTRINSIC_ENABLE_GPU_NIGHTLY` (lines 168-172) running `ctest -L "gpu|vulkan"` (line 205) — but it was `skipped` in run 28846130583 and has no alerting.
- Re-tiering a red gate requires the owning diagnosis to land first (`AGENTS.md` §"pre-existing/environmental CI failures"), hence the BUG-064 dependency.

## Required changes
- [ ] In `ci-vulkan.yml`, after the apt step (which installs `vulkan-tools`), add a probe step that runs `vulkaninfo --summary`, writes a `has-device` step output, and emits a loud job-summary line (`NO VULKAN DEVICE: gpu;vulkan fixtures would GTEST_SKIP`) when absent; gate the Build and test steps on `has-device`.
- [ ] Re-tier the `ci-vulkan.yml` trigger from `pull_request:` to `push: branches [main]` (and/or `merge_group:` if CI-008 lands the queue), keeping the exact build targets and ctest expression, so a `ci-vulkan`-preset compile break is still caught once per merged commit.
- [ ] Make the real GPU tier operational: land BUG-064's fix for `ExtrinsicSandbox.FramePacingDiagnosticCapture` (the raw `add_test` at `tests/CMakeLists.txt:1064-1077) and enable `INTRINSIC_ENABLE_GPU_NIGHTLY` so `nightly-gpu-optional` exercises the fixtures on hardware.

## Tests
- [ ] Confirm on a hosted (GPU-less) run that the probe short-circuits before the build and the job is green-with-skip, with the explicit summary line present.
- [ ] Confirm the `push: main` (or `merge_group`) trigger still builds the `ci-vulkan` preset and runs the fixtures.
- [ ] Confirm `nightly-gpu-optional` executes `ctest -L "gpu|vulkan"` on the self-hosted GPU runner and reports pass/fail (not `skipped`).

## Docs
- [ ] Update workflow docs and `tests/README.md`'s ci-vulkan section per `docs/agent/docs-sync-policy.md` to describe the probe, the re-tier, and where GPU fixtures actually execute.

## Acceptance criteria
- [ ] No PR push spends a ~24-min build to run a 6s skip; the ci-vulkan build check runs per merged commit instead.
- [ ] The `gpu;vulkan` fixtures execute on real hardware in the nightly GPU lane (previously never executed anywhere) — net GPU coverage increases.
- [ ] Skips are loud (job-summary + step output), never silent; a red run on `main` becomes a same-session `BUG-` task per `AGENTS.md`.

## Verification
```bash
# Static wiring checks:
grep -n "vulkaninfo" .github/workflows/ci-vulkan.yml
grep -n "merge_group\|push:" .github/workflows/ci-vulkan.yml
python3 tools/ci/check_workflow_names.py --root . || true
# Dynamic: confirm hosted run short-circuits; confirm nightly-gpu-optional runs on the GPU runner.
```

## Forbidden changes
- Deleting, relabeling, or `GTEST_SKIP`-weakening any `gpu;vulkan` test.
- Re-tiering while BUG-064 is unresolved (would move a red gate without diagnosis).
- Leaving the GPU nightly lane with no alerting that it ran (CI-007 owns the watchdog).
