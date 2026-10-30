---
id: RUNTIME-347
theme: F
depends_on: [ARCH-021, RUNTIME-346]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: kernel density and point spacing run through one operation runtime (resolve, readiness, capture, acquire, execute, gate, publish, deliver) with a stated definition contract instead of a family-private template; their entry points, results, messages and statuses are unchanged (pinned by the existing suites, no existing test edited); no behaviour changes in this slice; no catalog contract covers these operations
---
# RUNTIME-347 — Operation runtime core: kernel density and point spacing as definitions (ADR 0031 phase 0/1, slice 2)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `411e2214a`. Completed; the reviews and their verdicts are recorded in the Log.

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (answer to: 1 accept ADR 0031; 2 lift the "no generic property-write
    API" limit for operations; 3 start phase 0 and then the phase 1 pilot)

## Goal
- ADR 0031 Decisions 1, 3, 4 (`PointSamples` only), 5 (one scalar column) and 6 (the existing scalar
  GPU shape): one runtime drives every stage, a definition only declares names, parameters, its
  output and a pure `Compute`. The pilot's two operations already share the nearest thing to it,
  `PointFieldWork<M>` (`Runtime.PointFieldOperations.Processing.cpp:128-322`, 195 lines, with method
  records of 31 and 35 lines); this slice turns that family-private template into the runtime with a
  stated contract, and the method records into definitions.
- Footprint (read-only inventory, 2026-10-29): without tests and codecs, the two operations are about
  980 lines in about 20 files; their entry points are called by the panels, the agent and 12 test
  files, and tests assert the derived wordings ("Density estimation", "Radii estimation", "Density
  index snapshot does not match the selected samples.", "<Method> requires at least two live
  samples.", the "LBVH requires" prefix, `ActualBackend == "vulkan_lbvh"`). They stay exactly.
- The phase 1 sequence (each its own plan and review):
  - this slice: the runtime and the two definitions;
  - `RUNTIME-348`: results and delivery (a common header, invocation ids so an older run cannot
    overwrite a newer result, a generic session slot replacing the per-operation sink, snapshot,
    dismiss and retain lines; GPU results stored without the panel; the synchronous fallback
    delivering through the sink as ADR Decision 3 requires: today it only returns the result);
  - `RUNTIME-349`: placement and exposure. It starts with the exported framework module and the first
    instantiation across a module boundary (the definitions in `src/runtime/Modules/<Op>` with their
    section, the layering rule), removing this slice's internal header route (O1: a header proves
    nothing about ADR N3's template visibility); then `OperationRow<Def>`,
    `RegisterOperationSection<Def>`, a default `DrawOperation<Def>` panel, and the phase 1 gate
    measured (files, lines, build time, memory).
- This slice is a behaviour-preserving extraction, not the finished framework (O1).

## Plan
1. **Runtime** (`src/runtime/Editor/Operations/Runtime.OperationRuntime.hpp`, an internal template
   header next to the operations, in the directory the job drift guards scan; it becomes the
   exported framework module in `RUNTIME-349`, when the first definition leaves the editor):
   `OperationRuntime<Def>` with `Preview`, `Apply`, `ApplyConfigured`, `StartTransaction`,
   `MakeTransactionForTest`, `FoldTransaction` and `InputCatalog`, i.e. today's template functions
   (`Capture`, `Compute`, `Publish`, `StartGpu`, `FoldGpu`, `Preview`, `Apply`, `ApplyConfigured`),
   reorganized by stage:
   - **1 resolve:** the definition's `Validate` (the section codec), scene and stable entity;
   - **2-3 readiness and capture** (`PointSamples`): `CapturePointScalarField` (readiness verdict or
     execute capture), the minimum live count, the backend admission (LBVH limits, GPU admission);
   - **4 acquire:** the shared index after capture (`AcquirePointIndex`), when the backend needs it;
   - **5 execute:** on the worker or inline; the kNN rows for CPU LBVH, then `Def::Compute`;
   - **6 gate:** `ValidateQueuedJob` (abandoned, inputs and output current);
   - **7 publish:** `PublishPointScalarField` with the definition's history label;
   - **8 deliver:** `QueuedJobDelivery` (once; pending, rejected and unpublished wordings shared).
   The synchronous fallback computes and publishes inline and returns the result **without calling
   the sink**, as today (B4); ADR Decision 3's delivery through the sink is `RUNTIME-348`'s explicit
   change. The GPU path is today's `StartPointScalarGpu`/`PointScalarTransaction`, unchanged.
2. **Definition contract, a pilot specialization** (B3): the concept `PointScalarKnnDefinition` in
   the header (a missing member is a compile error naming it) covers what these two operations need
   and nothing more; it is not yet the ADR's general contract. Generalizing it (radius rows and an
   automatic support radius for density weights, typed multi-column outputs and a custom
   publication for outlier analysis, the `CpuKDTree` reference backend, a minimum live count of 1)
   is the first step of the slice that migrates the next operation, not speculative hooks now.
   Members: `Params`, `Result`, `Backend` (with `CpuOctree`, `CpuLBVH`, `VulkanLBVH`), the names the
   messages are built from (`Noun`, `Method`, `History`, `Evaluation`, `Failure`), `MinimumK`, the
   output reference of the parameters, `Validate`, `InitialResult`, `GpuParams`, `FoldGpuStats` and
   `Compute(const Params&, PointSamplesInput, Result&) -> std::optional<std::vector<float>>`, where
   `PointSamplesInput` is the live points and, for CPU LBVH, their kNN rows (self included). The
   record's unused `Lower` is dropped. Members the runtime reads from them:
   - `Params`: `StableEntityId`, `Backend`, `Positions`, `KNeighbors`, the output reference;
   - `Result`: `Status`, `Message`, `Succeeded()`, `RequestedBackend`, `ActualBackend`,
     `SlotCount`, `LiveCount`, `WrittenCount`, `IndexReused`, `CpuComputeMilliseconds`,
     `GpuInputUploadBytes`, `GpuInputCacheHits`, `CpuStageReadbackBytes`, `GpuQueryBatches`.
3. **Cancellation is delegated to the job service** (B4): it does not start the work of a job
   cancelled before it runs (`Runtime.JobService.cpp:479`) and checks again before publication
   (`RuntimeJobService.Cancel*` tests). The ADR's `OperationContext` (Decision 3, line 219) is
   **deferred, not removed**: neither geometry method takes a cancellation, so no pilot `Compute`
   could poll. Owner and trigger: the migration of the first definition whose method polls (CPD, UV
   regeneration or point construction), or phase 2 if outlier analysis' compute is chunked; recorded
   in `ARCH-021`. Removing it for good would need an ADR amendment.
4. **Kernel density and point spacing:** their method records become definitions in the same file
   (`KernelDensityOperation`, `PointSpacingOperation`); the exported entry points stay as one-line
   forwards to `OperationRuntime<Def>`; `PointFieldWork<M>`, the family template and its `Join` go.
- Not in this slice: results and session slots (`RUNTIME-348`); placement, exposure and the gate
  (`RUNTIME-349`); other operations (density weights is a near copy, the first candidate after the
  pilot); the found defects below.
- Found during the inventory, not fixed here (own slices, listed in `ARCH-020`):
  - the Vulkan path refuses a mismatched index snapshot with an empty message
    (`AcquirePointIndex` writes no diagnostic on `Mismatched`; the CPU path reports one);
  - `GpuQueryBatchSize` is persisted and drawn but never read (already listed);
  - `GpuNeighborhoodMilliseconds` is never written, `AdvancePointKnnRows` has no caller.

## Tests (predictions as assertions)
- Refactoring pin: every existing test of these operations passes unchanged (no test file edited):
  `KernelDensityOperations.*`, `PointSpacingOperations.*`, `PointScalarTransaction.*`,
  `QueuedEditorJobContract.*` (its drift guards scan this directory: at least 22 job identities
  with `.RequestedDomain`, no hand-written "already has an active" wording), `SandboxEditorSessionLifecycle.*`,
  `SandboxProcessingPanels.*`, `SandboxEditorPresentation.*`, `AgentOperations.*`,
  `ClusteringModule.*` (uses the kernel density test transaction), `*.SourceRules`.
- Characterization test added **before** the refactoring (it pins today's behaviour, so it passes on
  the old code and must still pass after; B2): `KernelDensityOperations.StaleQueuedRunsEndAtTheGate`:
  for both operations, a queued run whose input positions, deletion mask or output changed after
  submission is delivered as StaleEntity with "<Noun> estimation was cancelled or its source became
  stale; nothing was applied." (the gate's wording; `Publish`'s own check would say "<Noun> input or
  output changed before publication.").
- Mutations against the moved code, each with the assertion that must fail (B2):
  - the gate's currentness check removed (`ValidateBeforeApply` answers Current): the new
    characterization test's message assertion (the run reaches `Publish`, which reports its own
    wording);
  - the minimum live count check removed: `EditorPointReadiness.ScalarMinimumCountsAndCatalogMembershipRemainMethodSpecific`
    asserting "Kernel density requires at least two live samples." (`Test.SandboxEditorSessionLifecycle.cpp:1325`;
    the suite is the fixture's name, not the file's);
  - kernel density's `Noun` changed: `ThreeMethods/PointScalarTransaction.DuplicateStartIsPendingWithTheSharedMessage`
    ("Density estimation", `Test.PointScalarTransaction.cpp:452`) and the index mismatch message
    (`Test.KernelDensityOperations.cpp:337`);
  - the CPU LBVH kNN rows not built (empty rows passed): `ASSERT_TRUE(indexed.Succeeded())`
    (`Test.KernelDensityOperations.cpp:228`) and `Test.PointSpacingOperations.cpp:77`.
  Routing CPU LBVH to the octree method instead would give equal values by design (the two paths are
  checked for parity), so no output-level test can tell them apart; that route is pinned only by the
  code review.

## Acceptance criteria
- [x] `OperationRuntime<Def>` with the stated contract; kernel density and point spacing on it;
      `PointFieldWork<M>` removed.
- [x] Existing suites pass with no existing test edited; the characterization test passes before
      and after; the mutations are red.
- [x] Codex plan and code review (the code review also covers `RUNTIME-346`'s post-review fixes);
      structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
# the suites of every test file that exercises these operations (fixture suites included), the
# drift and locality guards and the job service; confirm the list with -N first (B1); 790 tests
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R '^(KernelDensity|KernelDensityConfig|KernelDensityOperations|PointSpacingConfig|PointSpacingOperations|ThreeMethods/PointScalarTransaction|QueuedEditorJobContract|QueuedEditorJobDriftGuard|RuntimeReuseDriftGuard|ProcessingCompilationLocality|RuntimeJobService|EditorKeypointAgent|EditorMeshFieldAgent|EditorParameterizationAgent|EditorPointReadiness|EditorQueuedPointJobs|SandboxEditorSession|SandboxEditorSessionLifecycle|SandboxProcessingPanels|SandboxEditorGeodesics|SandboxEditorGizmo|SandboxEditorPresentation|SandboxEditorProgressivePoisson|SandboxEditorUi|SandboxEditorWorkspaceContext|AgentArgumentSchema|AgentOperations|SandboxAgentServer|SandboxScreenshotWindow|ClusteringModule|ClusteringModuleResident|PointLBVHGpuSmoke|PropertyFilterGpuSmoke|RUNTIME298PointScalarResidency|DeclaredFieldContracts)\.|\.SourceRules$'
# the GPU path (moved code): the point scalar smokes in the Vulkan tree
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 300 -R '^(PointLBVHGpuSmoke|RUNTIME298PointScalarResidency)\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Plan from a read-only footprint inventory (files, entry points and callers, shared
  helpers and their other users, tests and asserted wordings, cancellation, surprises).
- 2026-10-29: Codex plan review (`plan347.answer.md` in this session's scratchpad): revise. B1 the
  verification missed the drift guards, the parameterized transaction suite, the resident
  clustering test, the config suites and the job service's cancellation tests: added, with a
  `ctest -N` check of the list. B2 two mutations could survive (`Publish` re-checks currentness;
  LBVH and octree agree by design): a characterization test pins the gate's wording, each mutation
  names its failing assertion, and the octree route is stated as unpinnable by outputs. B3 the
  concept is a pilot specialization with its consumed members listed; generalization belongs to
  the next migration. B4 `OperationContext` deferred with owner and trigger (not removed), the
  inline path stated as it is (no sink call). O1 taken: this slice is an extraction;
  `RUNTIME-349` starts with the framework module.
- 2026-10-29: Characterization test first: `KernelDensityOperations.StaleQueuedRunsEndAtTheGate`
  passed on the old code; with the old gate answering Current it alone turned red, reporting
  "... input or output changed before publication." (no existing test pinned the gate's wording).
- 2026-10-29: Implemented: `Runtime.OperationRuntime.hpp` (`PointScalarKnnDefinition`,
  `OperationRuntime<Def>` by stage: `Capture` 1-3, `Execute` 5, `Publish` 7, `Apply` with the index
  acquisition 4, the job's gate 6 and delivery 8; `Preview`, `InputCatalog`, `ApplyConfigured`,
  `StartTransaction`, `MakeTransactionForTest`, `FoldTransaction`); `KernelDensityOperation` and
  `PointSpacingOperation` replace the method records; the exported entry points are one-line
  forwards; `PointFieldWork<M>` and the unused `Lower` are gone.
  The first test list (399) missed fixture suites named unlike their files: a mutation (minimum
  live count removed) survived it, because `EditorPointReadiness` lives in
  `Test.SandboxEditorSessionLifecycle.cpp`. The list is now every suite of every test file that
  exercises these operations (790 tests): all pass. GPU path: `IntrinsicPointLBVHGpuTests` built in
  `build/ci-vulkan`, `PointLBVHGpuSmoke.*` and `RUNTIME298PointScalarResidency.*` 30/30.
  Mutations, each alone and restored, all red (O2: three on the first 399-test list, the fourth
  survived it and was red on the 790-test list): gate currentness removed (only the
  characterization test); minimum live count removed
  (`EditorPointReadiness.ScalarMinimumCountsAndCatalogMembershipRemainMethodSpecific`, 790 list);
  kernel density's `Noun` changed (6 tests, the index mismatch, the gate wording, three
  `QueuedEditorJobContract` wordings, `ThreeMethods/PointScalarTransaction.DuplicateStartIsPendingWithTheSharedMessage/0`);
  CPU LBVH rows not built (5 tests in both operations' suites).
  Complexity review: nothing to cut. Lines: `src/runtime/Editor` +238 (52,047 -> 52,285;
  `Processing.cpp` 402 -> 241, the new header 399): the moved code is now clang-formatted as the
  repository requires for changed lines (the old file used dense one-line statements), plus the
  concept (about 30 lines) and stage comments; the savings come with `RUNTIME-348`/`349` and the next
  migrations. Changed lines clang-format 20.1.8; after formatting the 790 tests pass again.
- 2026-10-29: Codex code review (`review347.answer.md`): approve, also for `RUNTIME-346`'s post-review
  fixes (`IsDerivable`'s name and type match, `ToString` from the row tokens, the glyph_order row);
  no changed message, status, result field or job identity found. Optional, both taken: O1 negative
  compile-time witnesses for `IsDerivable` (rows paired with another table by name, and by type,
  each `static_assert(!IsDerivable(...))` in the codecs file); O2 the mutation scopes stated
  exactly. These go to the next block's review (`RUNTIME-348`).
