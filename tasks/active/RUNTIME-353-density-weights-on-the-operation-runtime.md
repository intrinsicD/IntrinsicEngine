---
id: RUNTIME-353
theme: F
depends_on: [ARCH-021, RUNTIME-351]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, CI and the footprint count
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: compact density weights run through the operation runtime as a definition instead of their own copy of the point scalar lifecycle, and their results move to the session's operation result store; messages, statuses and values are unchanged; the operation's footprint is counted before and after with the phase 1 report's method; no catalog contract covers these operations
---
# RUNTIME-353 — Density weights on the operation runtime (the saving on an unfactored operation)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
28. (2026-10-29) "Please do 1. and 2." (1: tests that pin the per-operation structure may be
    rewritten, keeping every behavioural assertion)
31. (2026-10-30) "do 1." (answer to: after the phase 1 gate's size miss, 1 move density weights, an
    unfactored 860-line near copy, onto the operation runtime to measure the saving where the
    duplication is real; 2 phase 2 directly; 3 deeper cuts to the pilot; 4 revise the gate; 5 stop)

## Goal
- Measure what the operation runtime saves on an operation that has its own copy of the point scalar
  lifecycle (capture, admission, jobs, publication, GPU transaction): compact density weights,
  `Runtime.PointAnalysisOperations.DensityWeights.cpp` (453 lines).
- **Before (measured at `f4e369d9b`, method of `docs/reports/2026-10-30-phase-1-gate-footprint.md`,
  script `tasks/evidence/RUNTIME-353/dw_footprint.py`):** 14 files, 859 operation-specific lines
  (editor file 453, codec 178, panel 151, config interface 38, analysis interface 18 and wrappers 7,
  agent row 4, session 3, build list 2, registration 2, generated 3).

## Plan
Revised after the Codex plan review (Log, 2026-10-30): every divergence stays as it is; the runtime
gains a detected definition member for each, with a default that keeps kernel density and point
spacing exactly as they are (their definitions do not change).
1. **Runtime generalization** (`Runtime.OperationRuntime.cppm`):
   - wording: `Def::Wording()` may return the runtime's `OperationWording` (stale target, output
     label, too few samples, LBVH limits, index mismatch, config unavailable, job label, job name,
     GPU history label); otherwise it is derived from `Noun`, `Method` and `History` as today.
     Publication messages stay derived from `Noun` ("Density" gives density weights' own);
   - `ReferenceBackend` (default `CpuOctree`) and `MinimumLiveCount` (default 2);
   - `Prepare(params, points, result, execute, admit, why)`: an optional capture step before the
     runtime's Vulkan admission. Density weights: on Vulkan Apply with an automatic radius, the
     radius-independent admission (`admit()`) runs before the analysis; an explicit radius (any
     backend, any purpose) or that analysis resolves the radius; readiness never analyses; the
     runtime then admits the resolved radius;
   - rows: kNN rows only when the definition declares `MinimumK`; otherwise CPU LBVH passes the index
     snapshot in `PointSamplesInput::Index`, and the runtime exports `QueryPointRadiusRows` (density
     weights' complete radius-row loop, moved) for the definition's `Compute`;
   - `GpuParams(params, result)` may take the result (the resolved radius);
   - messages: `Failure` and `Evaluation` become optional; a definition without them writes its own
     messages in `Compute` (kernel density and point spacing declare both and never write `Message`
     there, as today); a status `Compute` set on failure stands, as now;
   - B1: `InlineDeliversToSink` (default true; density weights false: the inline fallback returns
     without a sink call, `Test.DensityWeightOperations.cpp:386`);
   - B2: `FinalizeFromPending` (default false, `FinalizeAfterWorker`; density weights true,
     `Finalize()`). The two differ only for a worker that ended `GeometryProcessingFailed` and was then
     invalidated; for density weights the one such input is the CPU LBVH packed-row overflow (65,537
     coincident live samples with an explicit radius, about 17 GB of rows; the reviewer's
     counterexample). Kept by source inspection, without a test (a live NaN, tried, is refused at
     capture before any worker runs);
   - B3: the publication check gains `w->Abandoned ||` for every definition. For kernel density and
     point spacing it cannot fire: inline runs never set it, and the job service checks
     `ValidateBeforeApply` (which refuses an abandoned run) and calls `PublishCompletion` in the same
     main-thread step (`Runtime.JobService.cpp:707-722`), while `Abandoned` is set only on the main
     thread by the finalizer or a rejected submission;
   - B3: `RefuseInPlace(result, params)`: a refused transaction start writes only the request fields
     (density weights: backend and output) besides status and message; default replaces the result.
2. **Density weights as a definition** in `src/runtime/Modules/DensityWeight` (the pilot's
   placement): a new interface module `Extrinsic.Runtime.DensityWeightOperation`
   (`Runtime.DensityWeightOperation.cppm`) holds the result, the tag and the entry points, and a unit
   `Runtime.DensityWeightOperation.cpp` the definition, the entry points and the config wrappers
   (moved from `Runtime.PointAnalysisOperations.cpp`); the editor file goes. A module apart from the
   config, not the config interface as for the pilot: the config interface and the shared codec file
   that imports it sit in the `ConfigPropertyTypes` boundary, which forbids the editor modules the
   result needs (transitively), and the codec stays (item 3). Results move to the session's operation result store: the point analysis family's density
   weight sink, snapshot member, slot and dismiss case go; `Extrinsic.Runtime.PointAnalysisOperations`
   keeps re-exporting the module, so its importers are unchanged. The panel reads and dismisses
   through the store (the point field frame's store copy and dismiss, which hold every operation's
   results), passes no session sink, and stops folding a finished transaction
   (`UpdateEditorPointScalarResult` goes): the store's retention hook keeps the GPU terminal result,
   as RUNTIME-348 did for the pilot (ADR 0031 Decisions 3 and 7). B6: density weights' result joins
   the panel's `kStoredByOperationRuntime`, so the transfer counters stay shown.
3. **Codec:** density weights' hand-written codec stays in the shared codec file (its stage order and
   messages differ from the derived codec's; generalizing it would obscure the saving measured).
4. **Footprint (B4):** the script counts complete operation-owned blocks on both sides (the whole
   result struct and config wrappers, registration lines without the name), recomputes "before" at
   `f4e369d9b`, keeps 859 as the original proxy, reports framework lines added separately, and lists
   the files actually edited (shared tests included) apart from the operation's footprint. Its method
   goes into a tracked report.
- Not in this slice: other operations; the derived codec's generalization.

## Tests (predictions as assertions)
- Every existing behavioural assertion of density weights stays: `DensityWeightOperations.*`,
  `DensityWeightConfig.*`, `DensityWeight.*`, `ThreeMethods/PointScalarTransaction.*/2`,
  `QueuedEditorJobContract.*`, `EditorQueuedPointJobs.*`, `EditorPointReadiness.*`, the panel tests,
  the Vulkan smokes. Structural rewrites (the family slot, sink and snapshot member; the panel test
  reading `LastDensityWeightResult`) are listed in the Log (question 28).
- Kernel density's and point spacing's lists and the Vulkan binary pass unchanged.
- New, red before the change: density weights' results reach the store from any caller (inline,
  refused, queued, GPU start refused), `DensityWeightOperations.EveryRunReachesTheOperationResultStore`;
  B7: `GpuResultsAreRetainedWithoutThePanel` runs its density weights case (skip removed, typed store
  lookup), keeping every assertion (start, busy start, Accept with its own sink, Discard, failure).
- New pins, green before and after, red under their mutation: a refused transaction start keeps the
  caller's other result fields (`RefusedTransactionStartKeepsTheCallersOtherFields`); Vulkan readiness
  with an automatic radius on coincident samples stays enabled (readiness never analyses), added to
  `AutomaticSupportRadiusResolvesBeforeVulkanSubmission`. B6: the panel test's `transferShown` for
  density weights (`Test.SandboxProcessingPanels.cpp:5286`) already pins the counters.
- Arguments without a test: the `Abandoned` publication guard (unreachable, Plan 1); B2's flag
  (reachable only through the 17 GB overflow, Plan 1);
  the accounting (B4) is checked by rerunning the corrected script on both commits.
- Mutations (each red): `MinimumLiveCount` default for density weights (one-sample test); the
  admission before the analysis dropped (the float64 refusal assertion); the analysis run during
  readiness (the new readiness assertion); `Prepare` skipped (the coincident refusal); the wording
  ignored (message pins); `InlineDeliversToSink` ignored (`SynchronousOutcomeIsReturnedWithoutQueuedDelivery`);
  `RefuseInPlace` ignored (the refusal pin); the runtime's post-`Prepare` Vulkan admission dropped
  (`MissingDeviceRefusesAdmission/2`); the store write dropped (the store test); density weights dropped from `kStoredByOperationRuntime` (`transferShown`).

## Acceptance criteria
- [x] Runtime generalized with defaults; density weights on it; the editor file gone; results in the
      store.
- [x] Every behavioural test unchanged and green; footprint before and after reported.
- [x] Codex plan and code review; structural checks pass.
  (Reviewer's decision, Codex `review353b` on diff `8afa940c…`: "Verdict: approve. No blocking or
  optional findings remain."; test runs are this session's, Log.)

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R "<RUNTIME-349 list>|^(DensityWeightOperations|DensityWeightConfig|DensityWeight)\."
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 600 -R "<every test of IntrinsicPointLBVHGpuTests>"
python3 tasks/evidence/RUNTIME-353/dw_footprint.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-30: Operator (question 31). Footprint before measured (14 files, 859 lines); the editor file
  read in full; its divergences from the runtime listed in Plan 1.
- 2026-10-30: Codex plan review (`plan353`, HEAD `f4e369d9b`): revise. B1 keep the inline fallback
  without a sink call; B2 keep `Finalize()`; B3 keep the "Weight" output label, the `Abandoned`
  publication guard and the partial refusal overwrite; B4 count complete blocks symmetrically and
  report framework and edited files apart; B5 discriminating Vulkan admission-order and readiness
  assertions, and the full density test selection. Optional: keep customization narrow (no dummy
  members); keep the placement and the codec scope. Folded into Plan and Tests; delta sent back.
- 2026-10-30: Codex delta (`plan353b`): revise. B6 keep the panel's transfer counters (the stored
  result trait); B7 run the GPU retention test's density weights case; O1 tell tested items from
  arguments; O3 only the store test is red before. Folded. B2: the pin test was written and failed at
  capture (a live NaN is refused immediately); the reachability argument above replaces the flag,
  put to the reviewer.
- 2026-10-30: Codex third round (`plan353c`): B2 stays (the packed-row overflow is reachable, if
  expensive); no smaller counterexample; the NaN rebuttal holds. Resolved by the flag, kept by source
  inspection. B6, B7 and the labels addressed.
- 2026-10-30: Implemented. Runtime: optional definition members (wording, reference backend, minimum
  live count, `Prepare`, radius rows through `PointSamplesInput::Index` and `QueryPointRadiusRows`,
  two-argument `GpuParams`, `InlineDeliversToSink`, `FinalizeFromPending`, `RefuseInPlace`; `Method`,
  `Evaluation`, `Failure` and `MinimumK` optional; the `Abandoned` guard for all); the concept is
  renamed `PointScalarDefinition`. Density weights: `Modules/DensityWeight/Runtime.DensityWeightOperation.cppm`
  and `.cpp`; the editor file, the family's slot, sink, snapshot member and dismiss case, and
  `UpdateEditorPointScalarResult` are gone; the panel reads and dismisses through the store and its
  transaction helper lost the fold path and the sink parameter. Rewritten tests (question 28): the
  session test's density weight slot checks now use descriptors (per-slot and cross-slot assertions
  kept); the panel test reads `Last<DensityWeightOperation>()`; the GPU retention test's skip is gone.
  `tests/CMakeLists.txt`: the point analysis boundary checks the new unit instead of the removed
  file; the density weight contracts boundary also checks the new interface.
  Verification (this session): the RUNTIME-349 list plus the density weight suites and boundaries,
  819/819, none skipped (the density weight retention case now runs); source rules 9/9 and the
  self-check; the Vulkan binary 56 passed, 1 opt-in skip (`METHOD056VulkanCpdEStep.ScalingProfile`).
  Red before: `EveryRunReachesTheOperationResultStore` does not compile at `f4e369d9b` (no
  `DensityWeightOperation`); the two pins pass before. Mutations, each red: M1 minimum live count, M2
  admission before the analysis, M3 analysis during readiness, M4 `Prepare` skipped, M5 declared
  wording ignored, M6 inline delivery, M7 in-place refusal, M8 the post-`Prepare` admission (its first
  choice, `MissingDeviceRefusesAdmission/2`, refuses earlier for want of a cache and did not
  discriminate; the admission tests with a cache did), M9 the stored-result trait (panel
  `transferShown`), M10 the store write. jscpd (8 lines/70 tokens, `.cpp/.cppm/.hpp` over
  `src/runtime/Modules`, `src/runtime/Editor/Operations`, `src/app/Sandbox/Editor`): no clone touches
  the new or changed runtime files. Footprint (`dw_footprint2.py`, whole blocks): 883 lines in 14
  files before, 702 in 13 after; the runtime 518 → 632; report
  `docs/reports/2026-10-30-density-weights-footprint.md`.
- 2026-10-30: Codex code review (`review353`, fresh session, diff digest `c3f1a3fe…`): revise. Blocking:
  the session test's density weight lifecycle assertions were replaced by descriptors, not carried
  over. Fixed: `PointFieldFramesRetainIndependentResultsAndRejectExpiredDelivery` now also covers
  density weights in the store (independent frame copies, replacement, dismissal, surviving another
  operation's dismissal, cleared on detach, an expired frame neither stores nor dismisses); red under
  M10. Optional: `CpuComputeMilliseconds` now also covers the success message and freeing the radius
  rows; stated in the report. Its other checks found every density weight path and kernel density and
  point spacing unchanged, and reproduced the counts. Ponytail review (separate agent): ship; applied
  the unused `Geometry.PointCloud.Kernels.Types` import and stale comment in the point analysis
  interface, `admit` as a plain lambda (no `std::function` for definitions without `Prepare`), and
  `Validate` reused by the config wrapper. Not applied: its finding that the `Abandoned` guard is
  dead; both reviews agree it cannot fire, the plan review (B3) asked to keep it. Re-run: the
  selection 819/819, source rules 9/9; footprint 883 → 701 (runtime 632).
- 2026-10-30: Codex delta (`review353b`, diff `8afa940cdba6…`): approve, no findings ("This preserves
  the base lifecycle guarantees through the new storage mechanism"; timing boundary "resolved by
  disclosure"; the cleanups preserve behaviour; numbers reconcile).
