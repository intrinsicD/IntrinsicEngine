# CI-001 — Slim engine test runtime without losing coverage

- Status: in-progress (Slice 1 label hygiene, CPU CTest gate parallelism, and label guard complete; Slice 2a/2b shared Runtime RHI groundwork complete; Slice 2c RenderOrchestrator and Slice 2d HeadlessEngine fixtures complete; broader Slice 2 engine/Vulkan fixtures next)
- Owner / agent: ci — `tests/`, `cmake/IntrinsicTests.cmake` helper, `.github/workflows/`
- Branch: `claude/optimize-engine-tests-Js8Zh`
- PR: TBD.
- Next verification step: after each slice, run `cmake --preset ci`, `cmake --build --preset ci --target IntrinsicTests`, and the relevant CTest gate against the configured build directory (`ctest --test-dir build/ci -L "unit|contract" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)` for the PR-fast slice; `ctest --test-dir build/ci -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)` for the Linux-clang gate). `CMakePresets.json` does not currently define a `testPresets` entry, so the directory-based invocation matches the workflows in `.github/workflows/` and the canonical command in `AGENTS.md`. Compare wall-clock against a recorded baseline on the same branch.

## Goal

Reduce CTest wall-clock time for the PR-fast and Linux-clang gates without
removing assertions or weakening coverage. Target outcomes:

- PR-fast (`-L "unit|contract"`): keep ≤ 12 min.
- Linux-clang full CPU (`-LE "gpu|vulkan|slow|flaky-quarantine"`): drop from
  ~30 min toward ~12–15 min.
- Nightly-deep: drop from ~90 min toward ~30–40 min.

The optimization is structural (shared fixtures, label hygiene, parameterization,
re-layering of misplaced integration assertions). No deletion of test cases is in
scope; only consolidation of duplicates via `TEST_P` and movement of CPU-only
assertions out of engine-boot fixtures.

## Non-goals

- No deletion of existing test cases or assertions.
- No merge of the 22 separate test executables into one binary; the current
  layout supports clean label slicing and parallel CTest runs.
- No removal of geometry unit coverage (DEC, mesh ops, processing) — these are
  cheap per case and correctness-critical.
- No new test framework; GoogleTest + `gtest_discover_tests` stays.
- No changes to `methods/` or `benchmarks/` test harnesses.
- No change to GPU/Vulkan skip semantics on hosts without a device.

## Context

Audit of `tests/` (2026-05-06) recorded:

- 195 test files, ~66.6k LOC, 2,723 GTest cases across 22 executables.
- Framework: GoogleTest registered via `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)` through the helper at `tests/CMakeLists.txt:45–69`.
- Existing CTest labels (declared per executable in `tests/CMakeLists.txt`):
  `unit`, `contract`, `integration`, `regression`, `gpu`, `vulkan`, `benchmark`,
  `slo`, `platform`, `headless`, `graphics`, `core`, `ecs`, `geometry`,
  `assets`. `slow` and `flaky-quarantine` are referenced in the CI exclusion
  filter but never applied to any executable, so the filter is currently a no-op.
- Workflow gates:
  - `pr-fast`: `-L "unit|contract"`.
  - `ci-linux-clang`: `-LE "gpu|vulkan|slow|flaky-quarantine"`.
  - `nightly-gpu`: `-L "gpu|vulkan"` (self-hosted).
- Runtime is dominated by integration tests that boot a full headless engine
  + Vulkan + transfer manager + asset pipeline per `TEST()`. Concrete hot
  spots (line counts):
  - `tests/integration/graphics/Test.RuntimeRenderExtraction.cpp` (1,682)
  - `tests/integration/runtime/Test_RuntimeGraphics.cpp` (1,516)
  - `tests/integration/runtime/Test_IORegistry.cpp` (1,394)
  - `tests/integration/graphics/Test.RenderGraphLegacy.cpp` (1,167)
  - `tests/integration/runtime/Test_RuntimeSelection.cpp` (1,047)
  - `tests/integration/runtime/Test_RuntimeRHI.cpp` (1,040)
  - `tests/unit/runtime/Test_RuntimeFrameLoop.cpp` (1,039)
- Duplicated scene scaffolding visible across
  `Test_RuntimeSelection.cpp` and `Test_RuntimeSelection_Multi.cpp` (same
  scene; vary `PickMode`).
- No `TEST_P` parameterization is used anywhere in `tests/`.

This task implements the four-step plan summarized in the audit:

1. Populate the existing `slow` / `flaky-quarantine` labels.
2. Share the engine + Vulkan boot via a `::testing::Environment` /
   `SetUpTestSuite` fixture so it runs once per executable.
3. Parameterize duplicated scene/fixture variants with `TEST_P`.
4. Move CPU-only assertions out of engine-boot fixtures into the existing
   `unit/` and `contract/` layers.

Progress notes:

- 2026-05-06: Slice 1 completed by labeling heavyweight runtime/Vulkan-backed
  executables and benchmark/SLO coverage as `slow`, documenting label policy in
  `tests/README.md`, and keeping nightly CPU coverage opted into CPU-supported
  `slow` tests while default/PR gates continue to exclude them. Local configured
  gate counts on this workstation changed from 1,602 default CPU tests before
  the slice to 1,572 after Slice 1; the nightly CPU-equivalent filter retains
  the 12 benchmark/SLO `slow` tests and ran 1,584 CPU-supported tests.
- 2026-05-06: Follow-up low-risk CI sub-slice completed by adding
  `-j$(nproc)` to PR-fast, Linux-clang, and nightly CPU/sanitizer CTest
  commands so existing label filters run in parallel without changing selected
  coverage. Performance/SLO and optional GPU commands remain serial to avoid
  benchmark/GPU contention.
  Local verification on the configured `build/ci` tree passed PR-fast (1,584
  tests), Linux-clang/default CPU (1,590 tests), and nightly CPU-equivalent
  (1,602 tests, including 12 `benchmark|slo|slow` tests with two SLO skips).
- 2026-05-06: Added the task-required configure-time CTest label guard in
  `tests/CMakeLists.txt`; undocumented labels now fail configuration with the
  target name and label before GoogleTest discovery registers tests. The guard's
  allow-list is synchronized with `tests/README.md`. Local verification on the
  configured `build/ci` tree passed PR-fast (1,584 tests) and
  Linux-clang/default CPU (1,590 tests) after the guard was added.
- 2026-05-06: Slice 2a completed by adding
  `tests/support/RuntimeRhiTestEnvironment.hpp`, a lazy per-process headless
  `RHI::VulkanContext` + `RHI::VulkanDevice` owner for legacy runtime/RHI
  integration tests. `tests/integration/runtime/Test_RuntimeRHI.cpp` now borrows
  that shared device while retaining per-test descriptor allocators, transfer
  managers, pipeline builders, and queue submitters; skip semantics are routed
  through `CheckAvailable()` before each borrow. The reset contract is documented
  in `tests/support/README.md`. Focused CTest for the 22 migrated RHI cases
  passed, and the direct `IntrinsicRuntimeTests` filter ran all 22 migrated cases
  in one process through the shared environment. Because `gtest_discover_tests`
  still registers one CTest process per GTest case, this is Slice 2 groundwork;
  broader CTest wall-clock reduction still depends on the remaining Slice 2
  fixture/grouping work.
- 2026-05-06: Slice 2b completed by adding an additive grouped CTest helper and
  `IntrinsicRuntimeTests.RuntimeRHIGrouped`. The grouped entry runs the migrated
  `Test_RuntimeRHI.cpp` suites in one `IntrinsicRuntimeTests` process while
  preserving every individual `gtest_discover_tests` case for focused filtering.
  The focused grouped CTest passed locally. Optional GPU nightly routing is left
  unchanged in this sub-slice because the broad optional GPU pass currently has
  unrelated failures outside Runtime RHI; switching that workflow to grouped
  Runtime RHI routing should be done only after the existing optional GPU pass is
  green or explicitly quarantined. Local verification on the configured
  `build/ci` tree passed the grouped Runtime RHI CTest entry (1 test, running
  the migrated suites in one process) and the default CPU-supported gate (1,590
  tests).
- 2026-05-06: Slice 2c completed by migrating
  `RenderOrchestratorHeadlessTest` in
  `tests/integration/graphics/Test_RenderOrchestrator.cpp` to borrow
  `RuntimeRhiTestEnvironment`, keeping bindless/texture/descriptor resources
  local per test. CPU-only `ShaderRegistry` and `GeometryPool` checks no longer
  use the Vulkan-backed fixture. Added
  `IntrinsicRuntimeTests.RenderOrchestratorHeadlessGrouped` so the remaining
  headless RenderOrchestrator cases can run in one process while individual
  CTest cases remain registered. Local verification on the configured
  `build/ci` tree passed the grouped RenderOrchestrator CTest entry (1 test),
  the direct `IntrinsicRuntimeTests` filter for all 8 RenderOrchestrator cases,
  the default CPU-supported gate (1,590 tests), and PR-fast (1,584 tests).
- 2026-05-06: Slice 2d completed by migrating `HeadlessEngineTest` in
  `tests/integration/runtime/Test_HeadlessEngine.cpp` to borrow
  `RuntimeRhiTestEnvironment`, while keeping scene manager, transfer manager,
  descriptor resources, asset pipeline, and frame scope local per test. Added
  `IntrinsicRuntimeTests.HeadlessEngineGrouped` so all six HeadlessEngine cases
  can run in one `IntrinsicRuntimeTests` process while individual CTest cases
  remain registered. Local verification on the configured `build/ci` tree passed
  the grouped HeadlessEngine entry (1 test), grouped-plus-individual
  HeadlessEngine coverage (7 CTest entries), the default CPU-supported gate
  (1,590 tests), and PR-fast (1,584 tests).

## Required changes

Slice the work into independent commits in this order. Each slice must be
verifiable on its own.

### Slice 1 — Label hygiene (low risk, immediate PR-CI win)

- In `tests/CMakeLists.txt`, add `slow` to the `LABELS` list of executables
  that boot the full headless engine and/or initialize Vulkan in the default
  CPU path. At minimum:
  - the integration runtime executable hosting `Test_RuntimeGraphics.cpp`,
    `Test.RuntimeRenderExtraction.cpp`, `Test_RuntimeRHI.cpp`,
    `Test.RuntimeStreamingExecutor.cpp`, `Test.RuntimeRenderExtraction.cpp`.
  - the runtime asset/IO executable hosting `Test_IORegistry.cpp` and
    `Test.AssetLoadPipeline.cpp` (real-glTF parse paths).
  - the `tests/benchmark/slo/Test_ArchitectureSLO.cpp` 2,000-node FrameGraph
    and scheduler-contention SLOs.
- Do not retag pure CPU contract tests as `slow`.
- Update `tests/README.md` to document the `slow` and `flaky-quarantine`
  labels and when to apply them.
- Confirm `.github/workflows/*.yml` already excludes `slow` in the
  Linux-clang gate; if not, add the exclusion. Ensure `nightly-deep`
  includes everything (`-LE "flaky-quarantine"` only).

### Slice 2 — Shared engine/Vulkan fixture (largest runtime win)

- Introduce a `::testing::Environment` (or static `SetUpTestSuite` on a
  shared fixture base) under `tests/support/` that constructs the headless
  engine + Vulkan device + transfer manager + descriptor pools once per
  executable and tears them down once at exit.
- Migrate `Test_RuntimeGraphics.cpp`, `Test.RuntimeRenderExtraction.cpp`,
  `Test_RuntimeRHI.cpp`, `Test_RuntimeFrameLoop.cpp`,
  `Test_HeadlessEngine.cpp`, `Test.RuntimeStreamingExecutor.cpp` to consume
  the shared fixture instead of constructing per-test engines.
- Per-test state (scenes, asset registrations, framegraph compositions) must
  reset between tests; document the reset contract in `tests/support/`.
- Preserve `GTEST_SKIP()` semantics on hosts without Vulkan.
- Do not move tests across executables in this slice; layering boundaries
  are unchanged.

### Slice 3 — Parameterize duplicated fixtures with `TEST_P`

- Collapse `Test_RuntimeSelection.cpp` and `Test_RuntimeSelection_Multi.cpp`
  into a single parameterized fixture varying `PickMode` (`Replace`, `Add`,
  `Toggle`, plus single-vs-multi entity arity).
- Audit the geometry validator suites for the same pattern (same mesh,
  varied algorithm options) and convert obvious duplicates to `TEST_P`. Do
  not invent new coverage; only consolidate existing assertions.
- Each `INSTANTIATE_TEST_SUITE_P` must enumerate exactly the cases that
  existed before so coverage is preserved.

### Slice 4 — Re-layer CPU-only assertions

- Identify integration tests that assert pure-CPU logic (dirty flags,
  registry lookups, mode transitions) but pay full engine-boot cost.
  Concrete starting set flagged by the audit:
  - overlap between `Test.SelectionSystemContracts.cpp`,
    `Test.SelectionPassContracts.cpp`, and
    `Test_RuntimeSelection.cpp` selection-mode assertions.
  - material dirty-flag assertions duplicated across runtime integration
    and graphics contract layers.
- Move CPU-only assertions down to `tests/unit/` or `tests/contract/`. Keep
  one integration smoke per system to prove wiring.
- Layer ownership rules from `AGENTS.md` §2 must hold: contract tests stay
  CPU-only and must not import live runtime services.

### Slice 5 — CTest knobs

- In `cmake/` (or the `intrinsic_add_test(...)` helper), set a default
  per-test `TIMEOUT 30` (down from the implicit 60s) and allow `slow`-
  labeled executables to override to a higher value.
- Document `ctest --preset ci -j$(nproc)` in `tests/README.md`.

## Tests

- All existing GTest cases must still register and pass; `ctest --preset ci`
  case count must not decrease except where `TEST_P` instantiations replace
  N previously separate `TEST()` cases with the same N parameterized cases.
- Add a guard in the `intrinsic_add_test(...)` helper (or a Python check
  under `tools/agents/`) that fails configuration if a CTest label outside
  the documented set is used. Documented set lives in `tests/README.md`.
- Per-slice verification commands are listed under **Verification**.

## Docs

- Update `tests/README.md` with:
  - the documented label set (`unit`, `contract`, `integration`,
    `regression`, `benchmark`, `slo`, `assets`, `build`, `core`, `ecs`,
    `geometry`, `graphics`, `headless`, `platform`, `runtime`, `glfw`, `gpu`,
    `vulkan`, `slow`, `flaky-quarantine`).
  - guidance for when to apply `slow` (any test that boots the full engine
    or initializes Vulkan in a non-`gpu`-labeled executable, or any test
    > 1 s wall-clock on the reference Linux-clang runner).
  - the shared-engine fixture contract introduced in slice 2.
- No changes to `AGENTS.md` or `docs/agent/`; this task does not alter
  policy, only test infrastructure.

## Acceptance criteria

- Baseline wall-clock for `pr-fast` and `ci-linux-clang` gates is recorded
  on the branch before slice 1 and after each subsequent slice using
  `ctest --test-dir build/ci ...` (matching `.github/workflows/`).
- After slice 1: `ci-linux-clang` excludes the newly tagged `slow`
  executables and the gate completes without losing any case the prior
  green run produced (case count unchanged at the union of `pr-fast` +
  `ci-linux-clang` + `nightly-deep`).
- After slice 2: the integration runtime executable wall-clock is reduced
  by ≥ 3× on the reference Linux-clang runner, with no test failure
  reintroduced.
- After slice 3: the consolidated parameterized suites enumerate exactly
  the cases the prior split files enumerated; CTest case count is
  preserved (or strictly increased).
- After slice 4: integration runtime executable line count drops measurably
  while `unit/` and `contract/` line counts increase by a comparable
  amount; no assertion is lost (verify via diff of `EXPECT_*` /
  `ASSERT_*` call sites pre- and post-move).
- Final: `nightly-deep` total wall-clock drops below 45 min on the
  reference runner.

## Verification

`CMakePresets.json` exposes a configure/build preset named `ci` but no
`testPresets` entry, so `ctest --preset ci` does not work. Use the
directory-based invocation that the workflows in `.github/workflows/` and
`AGENTS.md` already use. If a future slice introduces a CTest preset, update
this block.

```bash
# Configure + build.
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests

# PR-fast slice (matches .github/workflows/pr-fast.yml).
ctest --test-dir build/ci --output-on-failure \
    -L "unit|contract" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)

# Linux-clang full CPU gate (matches .github/workflows/ci-linux-clang.yml).
ctest --test-dir build/ci --output-on-failure \
    -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)

# Nightly-deep equivalent (matches .github/workflows/nightly-deep.yml).
ctest --test-dir build/ci --output-on-failure \
    -LE "gpu|vulkan|flaky-quarantine" --timeout 60 -j$(nproc)

# Repository policy guards.
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Per-slice timing capture (record before and after each slice).
ctest --test-dir build/ci -LE "gpu|vulkan|slow|flaky-quarantine" \
    -j$(nproc) --output-junit ctest.xml
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Deleting existing assertions to meet wall-clock targets.
- Merging the 22 separate test executables into one binary.
- Weakening or removing the `GTEST_SKIP()` paths on hosts without Vulkan.
- Adding `slow` to executables that do not actually boot the engine or
  initialize Vulkan.
- Tagging tests `flaky-quarantine` without an accompanying issue link in
  the test source.
