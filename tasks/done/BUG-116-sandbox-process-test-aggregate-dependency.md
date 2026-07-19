---
id: BUG-116
theme: G
depends_on: []
---
# BUG-116 — Sandbox process tests lack aggregate build dependencies

## Goal
- Make every aggregate that owns the promoted Vulkan Sandbox process tests
  build `ExtrinsicSandbox` before those registered CTests can run.

## Non-goals
- No change to Sandbox runtime behavior, test labels, selectors, sanitizer
  policy, or Vulkan capability requirements.
- No generic framework for manually registered process tests.
- No extra Sandbox build in CPU/Null configurations where the process tests
  are not registered.

## Context
- Status: completed on 2026-07-19 at `Operational`; owner: Codex; branch:
  `agent/bug-sandbox-aggregate-dependency-20260719`; retirement commit:
  this commit.
- Owner: root/test CMake build graph; no engine source layer changes.
- Symptom: a fresh `ci-vulkan` configure registers
  `ExtrinsicSandbox.FramePacingDiagnosticCapture` and
  `ExtrinsicSandbox.VulkanShutdownLsanContract`, but
  `cmake --build --preset ci-vulkan --target IntrinsicTests` completes
  2,403 build steps without creating `build/ci-vulkan/bin/ExtrinsicSandbox`.
  Both focused CTests then fail immediately with a missing executable.
- Expected behavior: building either applicable test aggregate materializes
  every executable needed by its registered CTests.
- Impact: the canonical promoted-Vulkan gate can fail before exercising either
  Sandbox process contract, despite its documented aggregate build command
  succeeding.
- Ranked, falsifiable hypotheses:
  1. `intrinsic_add_test_aggregate()` selects only targets registered through
     the intrinsic executable helper; the two manual `add_test()` entries use
     `$<TARGET_FILE:ExtrinsicSandbox>` without adding a target dependency.
  2. `ExtrinsicSandbox` is not defined by the `ci-vulkan` option combination.
  3. Another aggregate member provides a transitive dependency on the Sandbox
     executable.
  4. The executable is built at a different output path.
- Reproduction disproved hypotheses 2–4: the target exists, Ninja reports no
  Sandbox input for either aggregate, and no Sandbox executable exists at any
  configured target output after the aggregate build.
- Right-sizing: use two direct `add_dependencies()` calls under the exact
  condition that registers the manual process tests. A new registration
  helper or generalized manual-test inventory would add machinery for one
  executable and is outside this repair.

## Required changes
- [x] Under the promoted-Vulkan + GLFW Sandbox-test condition, make
      `IntrinsicTests` and `IntrinsicGpuVulkanTests` depend on
      `ExtrinsicSandbox`.
- [x] Keep CPU/Null aggregates unchanged when the manual Sandbox process tests
      are not registered.
- [x] Add an exact structural regression for the two manual registrations and
      their applicable aggregate prerequisites.

## Tests
- [x] Reproduce the missing executable after a fresh `ci-vulkan`
      `IntrinsicTests` aggregate build.
- [x] Make the structural regression fail before the CMake repair and pass
      afterward.
- [x] Reconfigure `ci-vulkan`, build `IntrinsicTests`, and prove the Sandbox
      executable is materialized through the aggregate.
- [x] Run both focused Sandbox process CTests without weakening their labels,
      timeouts, or assertions.

## Docs
- [x] Update the bug index and retirement log with the diagnosis and evidence.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening, promotion, and
      retirement.
- [x] No architecture documentation update: this repairs test-build
      prerequisites without changing an engine dependency boundary.

## Acceptance criteria
- [x] Both applicable aggregate target graphs include `ExtrinsicSandbox`.
- [x] CPU/Null configurations do not gain an unnecessary Sandbox executable
      dependency.
- [x] Both manual process tests execute after the documented aggregate build
      instead of failing for a missing executable.
- [x] Strict task, test-layout, layering, docs, and whitespace checks pass.

## Evidence
- Before the repair, a fresh `ci-vulkan` `IntrinsicTests` build completed all
  2,403 steps without `bin/ExtrinsicSandbox`. Both focused CTests then failed:
  frame pacing reported `No such file or directory`, and the shutdown contract
  reported process exit 127 for the same missing path.
- The exact tooling regression first failed because the two manual
  `$<TARGET_FILE:ExtrinsicSandbox>` registrations had no matching aggregate
  prerequisite block, then all eight build-aggregate regressions passed after
  the repair.
- A fresh repaired `ci-vulkan` graph lists `bin/ExtrinsicSandbox` under both
  `IntrinsicTests` and `IntrinsicGpuVulkanTests`; the executable was absent
  before the aggregate build and linked as build step 1,160/1,160.
- The unchanged promoted-Vulkan contracts passed 2/2 with zero skips:
  frame-pacing capture in 10.15 seconds and the five-frame
  Vulkan-shutdown/LeakSanitizer contract in 97.29 seconds.
- A fresh ordinary `ci` configure records
  `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=OFF`, registers neither process
  test, and leaves `ExtrinsicSandbox` out of the `IntrinsicTests` graph.
- Strict test-layout, layering, root-hygiene, task-policy, task-state-link,
  docs-link, session-brief, and whitespace checks passed.

## Verification
```bash
python3 tests/regression/tooling/Test.TestBuildAggregates.py
cmake --preset ci-vulkan --fresh
cmake --build --preset ci-vulkan --target IntrinsicTests
test -x build/ci-vulkan/bin/ExtrinsicSandbox
ninja -C build/ci-vulkan -t query IntrinsicTests
ninja -C build/ci-vulkan -t query IntrinsicGpuVulkanTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^ExtrinsicSandbox\.(FramePacingDiagnosticCapture|VulkanShutdownLsanContract)$' \
  --timeout 180
cmake --preset ci --fresh
ninja -C build/ci -t query IntrinsicTests
ctest --test-dir build/ci -N \
  -R '^ExtrinsicSandbox\.(FramePacingDiagnosticCapture|VulkanShutdownLsanContract)$'
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --root . --check
git diff --check
```

## Forbidden changes
- Registering, relabeling, excluding, quarantining, or weakening either
  Sandbox process contract.
- Building `ExtrinsicSandbox` through CPU/Null test aggregates that cannot
  register the process tests.
- Adding a generalized manual-process-test registry for this one executable.
