---
id: RUNTIME-354
theme: F
depends_on: [ARCH-021, RUNTIME-353]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, CI and the gate's static counts
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: ADR 0031 phase 2, the architectural test, with outlier analysis (two columns, a stamp, a compacting mode, its own GPU transaction) runs through the operation runtime's stages 1-4, 6 and 8 with its custom publication, keeps its tests, and is measured against the phase 2 gate (at least 40% less operation-specific code, an added option in at most 3 files, a projected net saving of at least 5,000 lines); no catalog contract covers these operations
---
# RUNTIME-354 — Phase 2: outlier analysis on the operation runtime (the architectural test)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (accept ADR 0031, phase 0, the phase 1 pilot)
28. (2026-10-29) "Please do 1. and 2." (1: tests that pin the per-operation structure may be
    rewritten, keeping every behavioural assertion)
32. (2026-10-30) "ja mach 1." (answer to: after `RUNTIME-353`, 1 phase 2 directly (outlier
    analysis); 2 revise the phase 1 size gate; 3 normalize density weights' kept divergences; 4 stop)
33. (2026-10-30) "I think the full plan (1) is still the right approach. Please continue" (the full
    plan: 1b, 2, 3, 4, in that order)

## Goal
- ADR 0031 (`docs/adr/0031-declared-operations.md`, Validation, Gates, "After phase 2"):
  1. outlier analysis on the contract keeps its tests;
  2. its custom publication fits Decision 5 without bypassing stages 1-4, 6 and 8;
  3. its operation-specific code falls by at least 40%;
  4. adding an option to a migrated operation touches at most 3 files in total, its test included,
     measured by doing it;
  5. the projected net saving over the named operation table is at least 5,000 lines.
  A failed 2 or 4 rejects the contract (the glue-helper alternative is taken); a missed size is an
  implementation failure to fix within the phase. The operator chose phase 2 with the phase 1 size
  criterion missed (question 32).
- **Before (measured at `11764850c`, `tasks/evidence/RUNTIME-354/outlier_footprint.py`, the whole-block
  rules of `docs/reports/2026-10-30-density-weights-footprint.md`):** 13 files, 1,593 lines:
  - the editor file `Runtime.PointAnalysisOperations.Outliers.cpp`, 1,055;
  - the panel window and its registration, 224;
  - the hand-written codec in `Runtime.PointNeighborhoodConfigCodecs.cpp`, 201;
  - the result, transaction snapshot and declarations in `Runtime.PointAnalysisOperations.cppm`, 44;
  - the config interface, 42;
  - the config wrappers, 11;
  - one-line registrations: agent 4, session 3, build list 2, section list 2, generated 4, panel
    header 1.

  The 40% target is at most 956 lines.

## What outlier analysis does today (read in full at `11764850c`)
- Capture: section validation; an inactive attachment or no scene refuses ("Scene is unavailable.");
  the point input (execute: `CapturePointInput` with values; readiness: `PreparePointInput`); the two
  outputs' domains resolved and checked by `ValidatePointOutputs` ("Outlier"); watches of both outputs;
  live count; the `RemoveMarked` checks (topology-free point cloud, a current `AnalysisStamp`); method
  checks; LBVH limits; eleven Vulkan admission rules with their own messages; before-state of both
  columns.
- Compute (CPU): local distance ratio (kNN rows on CPU LBVH), statistical and radius (octree, or LBVH
  counts and means), into a uint32 mask and a float score.
- Publication: both columns and the `AnalysisStamp` in one history entry ("Detect outliers"), its own
  representability and staleness checks, revision rebase, stamp restore on undo.
- `RemoveMarked`: a separate immediate mode, compacting the point cloud property set in one history entry
  ("Remove marked points"), with selection clearing and dirty tags; no job.
- GPU: its own transaction on `GpuTransactionCore` (three rings: score, mask, the mask's presentation;
  its own workspace, readback, Accept with an optional sink replacing the run's sink).
- Apply: `RemoveMarked` immediate; Vulkan starts the transaction (auto-accept); CPU acquires the index,
  runs inline without a sink call or queues with `FinalizeAfterWorker`; configured Apply.
- Results reach the session only through the panel's sink slot (`EditorPointAnalysisResultSlot`).

## Plan (blocks; each block planned, reviewed, tested and committed on its own)
Revised after the Codex plan review (Log, 2026-10-30).
1. **Block 1: outlier analysis through the runtime's stages** (the gate's items 1-2).
   - **Shared stage helpers** extracted from `Runtime.OperationRuntime.cppm`, used by two concrete
     shapes:
     - stage 4 index acquisition;
     - the inline fallback;
     - the queued run (identity, busy refusal, delivery, job, gate, finalizer);
     - the store (`BeginRun`, `Keep`, retention);
     - configured Apply;
     - readiness preview.

     The point scalar shape (kernel density, point spacing, density weights) keeps its behaviour
     exactly; a point columns shape serves outlier analysis. There is no generic N-column default
     publication: outlier analysis declares its own, so it has no consumer yet and comes with its
     first user.
   - **Capture order of the columns shape** (today's outlier order, so the same error wins):
     1. section validation (`Def::Validate`);
     2. the scene check ("Scene is unavailable."). Today's capture also checks the attachment, but that
        check cannot fire: command resolution already returns an empty context for an inactive
        attachment (`Runtime.EditorProcessing.cpp:68-79`), so the shape has no such check;
     3. the entity ("Outlier target entity is stale or missing.");
     4. the point input: execute copies values (`CapturePointInput`); readiness prepares
        (`PreparePointInput`);
     5. the output domains resolved and `ValidatePointOutputs` with the definition's label;
     6. the result initialized, the output watches;
     7. the live count (the definition's text);
     8. `Def::Prepare`, in today's order and with today's messages:
        - `RemoveMarked`: its two checks (a topology-free point cloud, a current stamp), then capture
          ends: no method, LBVH or Vulkan check and no before-state (today's early return,
          `Outliers.cpp:152-162`);
        - Analyze: the method checks, the LBVH limits, the Vulkan admission rules;
     9. for Analyze executions, the before-state of every output column (after admission, as today).
   - **Stages per path:**
     - Analyze on the CPU: 1-3 capture, 4 index acquisition (non-reference backends), 5 `Def::Compute`
       on the worker or inline, 6 the queued gate (`ValidateQueuedJob` over abandonment and
       currentness), 7 the runtime's publication frame, then 8 store and delivery.
     - The publication frame (stage 7) is owned by the runtime, inline and queued alike:
       1. currentness of inputs and outputs (StaleEntity, "Outlier input or output changed before
          publication.");
       2. only an Applied run continues;
       3. then `Def::Publish` writes the two columns and the stamp in one history entry.

       The definition's publication no longer checks currentness itself.
     - `RemoveMarked` (`Def::Immediate` selects it): 1-3 capture; 4 an explicit step with no
       resource (no index is acquired); 6 the runtime's currentness and lifetime gate; 7 the
       compaction (`Def::PublishImmediate`); 8 store and return. It is synchronous, as today.
     - Vulkan: 1-3 capture; `Def::StartGpu` starts the existing transaction on `GpuTransactionCore`,
       whose acquisition, gate and delivery stay authoritative; 8 the store keeps the started answer
       and, through a `Retain` hook the transaction calls before its sink (Accept's sink replaces the
       run's sink and cannot replace it), the terminal result. Retention respects the attachment and
       run ordering (`RetainOperationResult` with the run's invocation), and a duplicate-start refusal
       is not stored.
   - **Divergences kept, stated:**
     - `InlineDeliversToSink = false`: today the inline path returns without a sink call. ADR 0031
       Decision 3 promises inline delivery to the caller's sink. This is a kept divergence that
       does not satisfy Decision 3: an open acceptance issue for the operator, as for density
       weights, and the report does not claim the delivery contract for these two operations.
     - `FinalizeAfterWorker` (the runtime's default; density weights alone use `FinalizeFromPending`).
     - The manual transaction start refuses a non-Vulkan backend or `RemoveMarked` before capture, with
       its result initialized to the request (`Method`, `RequestedBackend`). Its index acquisition keeps
       its failure mapping (InvalidProcessingParameters with the acquisition's diagnostic).
   - **Two commits:** 1a runs outlier analysis through the shared helpers and the columns shape with
     its code still in the editor's point analysis unit. 1b then places it in its module and exports
     the framework surface a custom definition needs through `Extrinsic.Runtime.OperationRuntime`
     (ADR 0031 Decision 7: operation modules reach the editor only through it).
     - That surface is today's editor-internal helpers the custom parts use: the property watches
       (`PointPropertyWatch`, `ObserveGeometryProperty`, `GeometryPropertiesCurrent`,
       `RebasePointPropertyHistory`), `MutableGeometryProperties`, index acquisition, the queued job
       wording, and the GPU transaction lifecycle on `GpuTransactionCore`.
     - It is exported as declarations of the existing functions, not as copies.
     - The test of the architecture: no exception to the source rule `OperationModuleBoundary`.
   - **Placement (1b):** `src/runtime/Modules/OutlierAnalysis`, a module apart from the config, as for
     density weights. The editor file goes. Results move to the session's operation result store; the family's
     outlier slot, sink, snapshot member and dismiss case go; the panel reads the store.
     - Today the panel forwards its own runs (CPU results, a refused GPU start, a finished transaction)
       into the session slot (`Sandbox.PanelSupport.hpp:300`, `Sandbox.MeshProcessingPanels.cpp` around
       2232-2253). Direct callers (agent, tests) reach no slot.
     - With the store, every caller's run is kept (the intended change).
     - The panel's forwarding goes, as `RUNTIME-348` did for the pilot.
   - **Block 1b plan (question 33):**
     - **A module apart from the config:** `Extrinsic.Runtime.OutlierAnalysisOperation`, files
       `Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cppm` and `.cpp`, as for density
       weights. The config interface sits in the `ConfigPropertyTypes` boundary, and its hand-written
       codec imports it.
     - **The interface** takes from `Runtime.PointAnalysisOperations.cppm`: the result, the
       `OutlierOperation` tag, the transaction handle and snapshot, and the entry-point declarations.
       `Extrinsic.Runtime.PointAnalysisOperations` re-exports the module, so its importers are
       unchanged.
     - **The implementation unit** holds the outlier code moved from the editor file (bodies
       unchanged; qualifiers become the exported names below) and the config wrappers moved from
       `Runtime.PointAnalysisOperations.cpp`, over `ApplyOperationConfig`/`ActiveOperationConfig`. The
       editor file goes.
     - **The framework surface:**
       - `Extrinsic.Runtime.OperationRuntime` includes the GPU lifecycle headers (`GpuFront.hpp`,
         `GpuTransactionLifecycle.hpp`) in its purview and imports `GpuPropertyBinding`;
       - it exports `Extrinsic::Runtime::OperationSupport`: using-declarations of the existing
         helpers a custom definition uses, no copies;
       - the list: the property watches (`PointPropertyWatch`, `ObserveGeometryProperty`,
         `GeometryPropertiesCurrent`, `RebasePointPropertyHistory`), `MutableGeometryProperties`,
         `AcquirePointIndex`/`PointIndexState`, `AppendPointKnnRows`, `MeshSupport::QueuedJobRejectedMessage`,
         `ToEditorCommandStatus`, `GuardEditorProcessingResult`, and the GPU lifecycle
         (`GpuTransactionCore`, `GpuRingAcquisition`, `GpuTransactionOf`, `GpuTransactionCurrent`,
         `AcquireGpuTransactionBack`, `GpuTransactionDeferralsExhausted`, `ReadyGpuTransaction`,
         `FinishGpuTransaction`, `FailGpuTransaction`, `GpuTransactionAcceptRefusal`,
         `BeginGpuTransactionAccept`, `GpuTransactionStartRefusal`, `SubmitGpuTransactionRun`,
         `DiscardGpuTransaction`).

       A spike (reverted) compiled and linked exported using-declarations of a function, a function
       template, a struct and an enum from an operation module.
     - **The test of the architecture:** the source rule `OperationModuleBoundary` keeps its two
       pinned exceptions and gains none. A mutation adding an editor-internal include to the new unit
       makes it red.
     - **Build lists and checks:** the point analysis boundary checks the new unit instead of the
       editor file; the new interface joins `ProcessingCompilationLocality.PointAnalysisResults`
       (no point cloud algorithm module behind it), as the config interface is; the module inventory is regenerated.
     - **Also:** `DefinitionTraits`' designated initializer names every field (a
       `-Wmissing-designated-field-initializers` warning from 1a).
     - **Tests:** a placement-only change, so no behaviour moves: the selection, the Vulkan outlier
       smokes, the boundary and source-rule tests, the docs checks. The footprint is counted
       (informational).
2. **Block 2: the derived config section for outlier analysis.** Rows bound to the record in the
   interface, replacing the hand-written codec's 201 lines; the golden payloads and every diagnostic
   unchanged (the derived codec generalized only where the outlier codec needs it). Its own plan.
   - **Block 2 plan:**
     - **Golden first.** `tests/data/config_section_codec_golden.json` gains `outlier_analysis`,
       captured from today's hand-written codec through the existing golden test
       (`DeclaredFieldContracts.ConfigSectionCodecsMatchTheirGolden`): the shared payloads plus
       outlier-specific ones:
       - the radius method with radius 0, 1e-50 and 1e-100;
       - another method with radius 1e-50 (accepted, stored 0);
       - `stddev_multiplier` and `score_threshold` at 1e-50 (accepted);
       - unknown method and operation tokens;
       - radius 0 under the radius method together with an empty positions name (the radius rule
         reports first);
       - mismatched mask and score domains, and equal names.

       It is captured with a one-off dump, recorded in the Log, and committed with the block. Green
       before the switch, it pins the switch.
     - **The derived codec, generalized only where the outlier codec differs:**
       - A Float row of `BoundField` may declare `Positivity`, a predicate on the record converted so
         far (numbers and tokens, references not yet). While it holds, a value that is not positive is
         refused with the row's `PositiveRefusal`, and a positive one that stores as zero with its
         `Refusal`. While it does not hold, there is no positivity or storage rule.
       - Rows without `Positivity` keep today's rule (kernel density and point spacing unchanged).
       - Outlier analysis: the radius row's predicate is the radius method ("Radius must be positive."
         / "Radius must remain positive in float storage."); `stddev_multiplier` and `score_threshold`
         declare a predicate that never holds (the outlier codec never refused them for storage).
     - **The other outlier rules, with existing features:**
       - the k and batch-size rows carry the combined refusal "k must be positive and GPU query batch
         size must be 1..16384.";
       - the method, operation and backend rows carry their "Unknown outlier …" texts;
       - the domain and distinct-name rule is the section's `Check`.

       The row order is the old table's, so validation stages and the schema's field order hold.
     - **Placement and callers:**
       - The rows and section go in `Runtime.OutlierAnalysisConfig.cppm`, which stays config-only and
         in `ConfigPropertyTypes`, with `extern template` declarations.
       - The explicit instantiations and `ToString` (from the row tokens) go in the outlier operation
         unit, as for kernel density.
       - The outlier part of `Runtime.PointNeighborhoodConfigCodecs.cpp` (201 lines) goes.
       - Callers use the generic functions and `kOutlierAnalysisFields`: the outlier unit, the panel,
         the section registration list, and the tests that name the old functions (structural,
         question 28).
     - **Conversion (Codex plan review):**
       - `BoundField` records `Narrow` (the member is a `float`), and the derived parser converts a
         number straight into such a member, as the hand-written codecs did.
       - Before, an integer literal above 2^53 rounded twice through `double`. The derived module
         documented that as a deviation since `RUNTIME-346`.
       - So kernel density and point spacing get their pre-pilot conversion back in that corner,
         which their golden does not contain.
     - **Tests:**
       - the golden unchanged across the switch; captured cases (61) include, from the review, an
         invalid operation with an invalid backend, positions and score sharing a name on one domain,
         mask and score on another domain than positions, and an integer literal above 2^53 for
         each float;
       - `ToString`'s tokens and "invalid" pinned directly
         (`OutlierAnalysisConfig.BackendNamesAreTheTokensAndInvalidValuesSayInvalid`; the method and
         operation overloads lost their last caller and were deleted, see the Log);
       - mutations: each comparison of the `Check` dropped, and the operation and backend rows
         swapped;
       - `IsDerivable` holds for the outlier section;
       - mutations, each red: `Positivity` ignored, the radius row's refusal texts removed, and the
         `Check` dropped (the golden's domain cases);
       - the selection with `DeclaredFieldContracts.*`, `SandboxConfigSections.*`,
         `EngineConfigSectionSchemas.*` and the agent schema baseline.
3. **Block 3: the panel from the rows, and the option trial.** Within question 32 (Codex plan review):
   only the field-table controls the option trial needs, not `RUNTIME-351`'s whole generated-panel
   block. Before the block, its plan names the operation and a real option consumed by `Compute`, whose
   codec, validation, schema and panel control derive from its row, and bounds the panel changes. The
   trial counts every edit: generated files and tests included.

   **Block 3 plan.** Two commits, so the second one's diff is the measurement.
   - **3a, the outlier panel's numbers from the rows.**
     - The six hand-written number controls (k, stddev multiplier, score threshold, radius, minimum
       neighbors, GPU query batch size) become one `DrawBoundFields` call over `kOutlierAnalysisRows`.
       `DrawBoundFields` and `DrawFieldControl` are the existing `RUNTIME-344` helpers in
       `Sandbox.PanelSupport`; nothing new in the framework.
     - Its keep predicate is the panel's visibility, unchanged:
       - `entity` is drawn by the entity picker;
       - k for the statistical and distance ratio methods;
       - the stddev multiplier for the statistical method, the score threshold for the distance ratio;
       - radius and minimum neighbors for the radius method;
       - the batch size for the Vulkan backend.

       Any other number row is shown, so a new option applying to every method needs no panel edit.
       Enums, references and the entity keep their controls (`DrawFieldControl` draws none for them).
     - Changes a user sees (completed after the Codex plan review):
       - the labels become the row names: "Neighbors k" → "K neighbors", "Standard deviation
         multiplier" → "Stddev multiplier", "GPU query batch size" → "Gpu query batch size";
       - k and minimum neighbors stay unsigned inputs (their range passes `int`) and clamp to the
         accepted range after an edit, so k = 0 becomes 1;
       - the batch size becomes a drag over 1..16384, and the three floats drags over their range
         with `%.6g`; radius 0 under the radius method is still refused by validation;
       - the order is the rows': under the radius method minimum neighbors comes before radius, and
         the Vulkan batch size comes before the radius, stddev and threshold controls;
       - each control gains the row's hover hint (no readiness markers: the window has no
         `ReadinessMarkerScope`).

       The method help texts stay.
     - Tests:
       - `Test.SandboxProcessingPanels` addresses the radius control by its new id
         (`"Radius##Outliers"`, the existing edit helper drives a drag's input mode). A structural
         rewrite under question 28; the assertions stay.
       - New: for every method and backend, the controls actually drawn (activated by id and observed
         active on the next frame, not `GetID`, which computes an id whether drawn or not) are
         exactly the expected ones. The expected set is a literal per method and backend for today's
         rows; every other number or bool row is expected in every combination, so a new option is
         covered without editing the test, and hiding it fails.
   - **3b, the option trial.**
     - Operation: outlier analysis. Option: `negative` (bool, default false), PCL's `setNegative`. It
       marks the inliers instead of the outliers, so "Remove marked points" keeps only the outliers.
     - `Compute` writes `mask = analysis.Mask[i] != negative` and counts the marked samples; it covers
       all three methods on both CPU backends. Scores are unchanged.
     - The Vulkan backend refuses it in `Prepare`, right after the removal's early return (removal
       reads the published mask and stays independent of the backend), so after capture but before
       any output snapshot, dispatch or publication: "Negative masks need a CPU backend." The mask ring
       is GPU-resident and the shader writes it, so honouring it there would touch the shader and its
       parameter block, outside the declared structure. The row's description says so.
     - The words that state the mask's polarity become neutral (Codex plan review): the panel's "mask
       (1 = outlier)" and "outliers: N", and its method help texts that say which samples are marked;
       `docs/architecture/outlier-analysis.md` documents the option and its CPU-only restriction.
     - Expected edits (prediction, written before the trial; the trial commit holds every edit the
       option needs, and the count is what it actually touches):
       1. `Runtime.OutlierAnalysisConfig.cppm` (the member and row);
       2. `Runtime.OutlierAnalysisOperation.cpp` (its use and the Vulkan refusal);
       3. `Test.OutlierAnalysis.cpp` (its test);
       4. `tests/data/config_section_codec_golden.json`: the outlier entry pins the canonical payload,
          schema and registration, all of which gain the field, so it is regenerated;
       5. `Sandbox.MeshProcessingPanels.cpp`: the polarity wording (not the control, which follows from
          the row);
       6. `docs/architecture/outlier-analysis.md`;
       7. (found before implementing, see the Log) `Runtime.DerivedConfigSections.cppm`: bool rows, a
          one-time framework capability.

       That is 6 files per option (7 for this first bool option) against the gate's 3. The codec, validation, schema, agent schema (its baseline
       test drops newly added fields, `Test.AgentOperations.cpp`), inventory and panel control follow
       from the row without edits. The extra three: the golden changes with every added field of a
       derived section; the wording and the documentation change because this option changes what the
       mask means. Whether they count is not reframed here; the count goes to block 4's verdict and
       the operator, and under the ADR as written more than 3 fails criterion 4.
     - An algorithmic option (for example PCL's sample standard deviation) was considered and not
       chosen: it changes the geometry kernel (`ClassifyStatisticalOutliers`) and the shader, which no
       operation structure removes.
     - Tests in `Test.OutlierAnalysis.cpp`, red without the change:
       - the default is false, and `negative = true` round-trips through the section;
       - on a fixture with known outliers, each method on both CPU backends, run through the configured
         execution, gives the complement mask and the complementary marked count, with the scores and
         deleted rows unchanged;
       - a removal after it keeps exactly the outliers, and Undo restores the cloud;
       - Vulkan with `negative` refuses with exactly the new message through preview and through
         application (other Vulkan checks also refuse, so only the exact text discriminates), with
         nothing published.

       Mutations, each red: `negative` ignored in `Compute`; the refusal removed; the row hidden in the
       panel (the visibility test).
   - Verification: the selection, `SandboxProcessingPanels.*`, the golden, the agent schema baseline
     unchanged, the Vulkan smokes; the trial's `git show --stat`.
4. **Block 4: measurement and the gate.**
   - The outlier footprint after, with the same script, against the 956-line target.
   - The projection over `docs/architecture/declared-operations-table.md`, recomputed from the measured
     savings of the migrated operations. The cumulative framework growth since phase 0 is counted once
     and subtracted. A custom-publication operation is credited only with the work the runtime actually
     takes over.
   - A tracked report and the gate verdict; the operator decides.

## Block 1 tests (predictions as assertions)
- Every behavioural assertion stays:
  - `OutlierAnalysis.*`, `OutlierAnalysisConfig.*`, `OutlierAnalysisOperations.*`, `OutlierTransaction.*`;
  - `QueuedEditorJobContract.*`;
  - `SandboxProcessingPanels.Outlier*`, `SandboxAgentServer.RunOperationOutlierAnalysisPublishesAndUndoes`;
  - the session tests;
  - the Vulkan smokes `RUNTIME297OutlierResidency.*` and
    `PointLBVHGpuSmoke.OutlierNeighborhoodsPublishAcrossDomainsAndCountDenseSupport`;
  - the `RUNTIME-353` selection (kernel density, point spacing and density weights unchanged).

  Structural rewrites (the family's outlier slot; the panel's forwarding) are listed in the Log, with
  each guarantee carried to the store as `RUNTIME-353` did.
- New, red before the change (outlier results in the store):
  - every caller: inline, queued (pending, then terminal), an immediate refusal, `RemoveMarked` called
    directly, the GPU start;
  - a duplicate refusal leaves the active run's slot;
  - an older completion never replaces a newer result;
  - the result is visible inside a reentrant sink (the store is written before the sink runs);
  - GPU retention on Accept (with and without its own sink), Discard, failure and cancellation;
  - a detached session stores nothing.
- New pins, green before and after, each red under its mutation:
  - competing failures keep today's winner: a removal on a mesh refuses with the topology message
    before any backend or Vulkan check; an invalid method parameter wins over LBVH limits;
  - a valid removal succeeds with a Vulkan backend, no device and k above the live count (the early
    return skips those checks);
  - the inline path makes no sink call (`InlineDeliversToSink` ignored);
  - a failed CPU worker invalidated before publication delivers the worker failure (the default
    finalizer kept; a pending-finalize mutation is red where reachable, else stated);
  - the queued gate's verdict: a stale run finalizes StaleEntity with the shared wording and leaves
    both columns and the stamp unwritten (a mutation dropping the gate is red through the finalizer's
    message, not only through publication's own check);
  - publication writes both columns and the stamp together, and undo restores all three;
  - `RemoveMarked` acquires no index (an LBVH backend without a spatial cache still removes).
- Mutations, each red:
  - every detected member ignored: `Prepare`, `Publish`, `Immediate`, `StartGpu`, `Retain`, the wording,
    `InlineDeliversToSink`;
  - the immediate path's gate dropped (where reachable; a synchronous removal cannot change between
    capture and gate, so this is stated if no test can see it);
  - not covered by a test, stated: the publication frame's currentness check (no change can come
    between the queued gate and publication, which the job service runs in one main-thread step, and
    the inline path is synchronous), kept as today's code has it.

## Acceptance criteria
Block 1:
- [ ] Outlier analysis runs through the runtime's stages 1-4, 6 and 8 with its custom publication and
      immediate mode; the editor file gone; results in the store.
- [ ] Every behavioural test unchanged and green; kernel density, point spacing and density weights
      unchanged.
- [ ] Codex plan and code review.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
# the RUNTIME-353 selection plus the outlier suites:
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R '^(KernelDensity|KernelDensityConfig|KernelDensityOperations|PointSpacingConfig|PointSpacingOperations|ThreeMethods/PointScalarTransaction|QueuedEditorJobContract|QueuedEditorJobDriftGuard|RuntimeReuseDriftGuard|ProcessingCompilationLocality|RuntimeJobService|EditorKeypointAgent|EditorMeshFieldAgent|EditorParameterizationAgent|EditorPointReadiness|EditorQueuedPointJobs|SandboxEditorSession|SandboxEditorSessionLifecycle|SandboxProcessingPanels|SandboxEditorGeodesics|SandboxEditorGizmo|SandboxEditorPresentation|SandboxEditorProgressivePoisson|SandboxEditorUi|SandboxEditorWorkspaceContext|AgentArgumentSchema|AgentOperations|SandboxAgentServer|SandboxScreenshotWindow|ClusteringModule|ClusteringModuleResident|PointLBVHGpuSmoke|PropertyFilterGpuSmoke|RUNTIME298PointScalarResidency|DeclaredFieldContracts)\.|\.SourceRules$|^(PointIndexAdmission|OperationResults)\.|^OperationModuleBoundary\.|^(DensityWeightOperations|DensityWeightConfig|DensityWeight)\.|PointScalarTransaction|ProcessingCompilationLocality\.(PointAnalysis|DensityWeightContracts|ConfigPropertyTypes|Family)|RuntimeReuseDriftGuard|SourceRules|^(OutlierAnalysis|OutlierAnalysisConfig|OutlierAnalysisOperations|OutlierTransaction)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
./build/ci-vulkan/bin/IntrinsicPointLBVHGpuTests --gtest_filter='RUNTIME297OutlierResidency.*:PointLBVHGpuSmoke.*:RUNTIME298PointScalarResidency.*'
python3 tasks/evidence/RUNTIME-354/outlier_footprint.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-30: Operator (question 32). Footprint before measured (13 files, 1,593 lines); the editor
  file read in full; plan written for review.
- 2026-10-30: Codex plan review (`plan354`, fresh session): revise. B1 the immediate path keeps
  stages 4 (no resource) and 6 (a gate before compaction); reconcile the kept no-sink inline path
  with Decision 3. B2 an explicit capture order and hook contract (attachment check, removal before
  method/LBVH/Vulkan checks, before-state after admission; the finalizer; the manual start's refusal
  before capture; retention before Accept's replacement sink, respecting lifetime and run order, not
  storing duplicate refusals). B3 named assertions and mutations, the gate's verdict asserted, the
  verification command resolved. B4 the projection subtracts the cumulative framework growth once and
  credits custom operations only for transferred work; the trial counts every edit. Scope: block 3's
  minimal controls are within question 32; two concrete shapes, no generic N-column publisher yet.
  Fact corrected: panel-driven runs already reach the slot through the panel's forwarding. Folded
  into Plan and Tests.
- 2026-10-30: Codex delta (`plan354b`): revise. The removal's early return stated (and pinned); the
  publication frame (currentness, Applied) owned by the runtime inline and queued; Decision 3's
  inline delivery left an open acceptance issue for the operator, not claimed; the capture's
  attachment check is unreachable (command resolution already refuses an inactive attachment), so
  no divergence, no mutation; the verification regex written out. Folded.
- 2026-10-30: Codex third round (`plan354c`): revise, on three test faults, fixed: the GPU failure
  injection now precedes the transaction (it copies its context); the detached-session check ends
  an existing transaction after detaching; the publication frame's currentness check is stated as
  not coverable by a test (no seam between gate and publication). Added after the round: block 1 in
  two commits (1a stages, 1b placement with the exported framework surface for custom definitions),
  found while designing; the next review is the code review of 1a.
- 2026-10-30: Block 1a implemented.
  - Runtime (`Runtime.OperationRuntime.cppm`):
    - shared stage helpers (`DefinitionTraits`, `BeginOperationRun`/`KeepOperationResult`,
      `AcquireOperationIndex`, `RunOperationOnCpu`, `ApplyConfiguredOperation`), which the scalar shape
      now uses;
    - the point columns shape (`PointColumnsWork`, `PointColumnsDefinition`, `PointColumnsRuntime`);
    - the wording takes the defaults (`Wording(defaults)`, density weights' too);
    - `GpuTransactionRefusal::OutputBusy`.
  - Outlier analysis is a definition on the columns shape, still in the editor unit (placement is
    1b):
    - capture checks in `Prepare` (the removal's early return kept);
    - `Compute`, the custom `Publish` behind the runtime's frame, `RemoveMarked` as `PublishImmediate`;
    - its transaction, with a `Retain` hook before its sink.
  - Results move to the store (tag `OutlierOperation`; the config's mode enum holds the old name);
    the family's outlier slot, sink, snapshot and dismiss case go; the panel reads and dismisses
    through the store and no longer forwards results.
  - Rewritten tests (question 28):
    - the session tests carry the outlier slot guarantees into the store lifecycle test, and the
      cross-slot checks move to keypoints;
    - the panel tests bind the outlier test transaction to the session's store (as `RUNTIME-348`
      did) and expect the other caller's Pending in the store for a duplicate;
    - the presentation source check names the store read;
    - the identity drift guard also scans `.cppm` (the shared identity moved there).
  - New tests: three store tests (they do not compile at `11764850c`: no `OutlierOperation`) and
    pins of today's behaviour (competing failures and the removal's bypass, the stale queued run's
    wording and no writes, both columns and the stamp with undo, no live sample, a duplicate GPU
    start not stored, the device run's `vulkan_lbvh`).
  - Verification (this session):
    - the RUNTIME-353 selection plus the outlier suites, 860/860; source rules 9/9;
    - mutations, each red: columns `Prepare` ignored, custom publication bypassed, `Immediate`
      ignored, `StartGpu` ignored (red after adding the `ActualBackend` assertion; it first survived),
      `Retain` ignored, wording ignored, `InlineDeliversToSink` ignored, the removal given an index,
      a busy start stored, the removal's store write dropped.
  - Interim size (not the gate's measurement): the footprint 1,593 → 1,522; the runtime 632 → 974.
- 2026-10-30: Block 1a reviews.
  - Codex code review (`review354a`, fresh session, diff `da8654ff…`): revise, on verification. It
    found no changed message, status, history entry or stamp on any outlier path, the store contract
    correct, and the structural rewrites keeping every guarantee. It asked for:
    - a test of the production start's retention: added, `ProductionStartRetainsItsTerminalResult`
      (a real start stores its queued answer, a Discard its terminal result);
    - Accept without a sink retaining: added to `GpuResultsAreRetainedWithoutThePanel`;
    - the Vulkan removal executed, not only previewed: added to `CompetingFailuresKeepTheirOrder`;
    - the failed-worker finalizer pin: stated as not reachable. Capture refuses non-finite samples,
      the distance ratio stays finite for finite input, and the neighbourhoods are complete (k is
      below the live count). Outlier analysis uses the runtime's default finalizer, so there is no
      outlier-specific mechanism to mutate;
    - the M4 rerun recorded: red after the `ActualBackend` assertion.
  - Ponytail review (separate agent): fix 1-3 first. Applied:
    - a CRTP base `OperationShape<Shape, Def>` holds readiness, configured Apply, retention, a stored
      refusal, stage 7's frame and stages 4-8 of the CPU run (index acquisition folded in), so the
      function-pointer helpers and the duplicated members go;
    - the five forwarding wrappers became the definition's out-of-line members (`Start`'s body is
      `StartGpu`; `PublishImmediate` is the removal);
    - `Prepare`, `Immediate` and `StartGpu` are required, not optional;
    - one retention closure;
    - two dead lines removed.

    Not applied:
    - the runtime's test-transaction and refusal helpers stay: in 1b the definition, placed in its
      module, cannot resolve commands itself;
    - the removal's currentness check before compaction stays: the plan review asked for it; both
      reviews agree it cannot fire.
- 2026-10-30: Codex delta (`review354b`, diff `bb3be594…`): approve, block 1a ("no behavior change
  from the CRTP refactor"; the store contract intact; the findings resolved). Its evidence note: M8
  had become a broader mutation (the immediate branch disabled); rerun as the focused one (an index
  acquisition added inside the removal): red. Vulkan outlier and scalar smokes 31/31 on the revised
  tree. Interim size: the footprint 1,593 → 1,498, the runtime 632 → 931.
- 2026-10-30: Committed `266137c3d` (block 1a); CI green (ci-linux-clang, ci-docs). Next is the
  operator's choice: the full plan (1b, 2, 3, 4), blocks 2-4 first with 1b after the verdict, or stop.
- 2026-10-30: Codex review of the 1b plan (`plan354d`): revise, one item: the scalar-publisher
  allowance of `RuntimeReuseDriftGuard` (`Test.QueuedEditorJobContract.cpp`) moves to the new
  file's path (it transfers an existing allowance, no new `OperationModuleBoundary` exception). The
  export surface is "sound", consistent with Decisions 5-7, complete and with nothing unneeded; keep
  the unit's `#pragma clang fp contract(off)`; update the runtime's synopsis comment. Folded.
- 2026-10-30: Block 1b implemented (question 33).
  - Outlier analysis lives in `Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cppm` and
    `.cpp`; the editor file moved (`git mv`), and the config wrappers moved from the family's unit.
  - The runtime includes the GPU lifecycle headers and exports `OperationSupport`: 25
    using-declarations, no copies.
  - Checks:
    - the point analysis boundary checks the new unit, and `PointAnalysisResults` also checks the
      new interface;
    - `RuntimeReuseDriftGuard`'s scalar-publisher allowance moved to the new path;
    - the identity drift guard also scans `src/runtime/Modules` (structural, question 28: the
      outlier GPU identity moved there; the threshold of 22 is unchanged);
    - `DefinitionTraits`' designated initializer names every field;
    - the module inventory is regenerated.
  - Verification (this session):
    - the selection 861/861; source rules 9/9;
    - `OperationModuleBoundary` fails when an editor-internal include is added to the new unit, and
      passes once it is removed: no new exception was needed (the architecture check of 1b);
    - inventory and doc links are clean.
  - Size: the footprint 1,498 → 1,546 (module boilerplate, the interface counted whole); the runtime
    931 → 973 (the exported surface).
- 2026-10-30: Codex code review of 1b (`review354c`, diff `8b8610b2…`): approve, no blocking
  findings ("placement-only in behavior"; all 25 exported names used; both guard edits keep their
  guarantees; no new `OperationModuleBoundary` exception). Its optional comment fix applied: the
  interface names both refusals that are not stored.
- 2026-10-30: Ponytail review of 1b (separate agent): six findings, all deletions or smaller forms,
  applied:
  - the interface's unused `PointScalarTransaction` re-export removed;
  - three dead lines in the point analysis interface removed;
  - six unused includes and imports the moved unit carried over removed;
  - `OperationSupport` placed in the existing export block;
  - the identity guard's loop shortened;
  - the repeated header comment removed.

  Re-run on the final tree: the selection 861/861, source rules 9/9, the drift guards, the Vulkan
  smokes 31/31; the inventory up to date. Size: the footprint 1,538, the runtime 968.
- 2026-10-30: Codex review of the block 2 plan (`plan354e`): revise.
  - B1: the derived parser converts through `double`, the hand-written one straight into the float
    (a different canonical for integer literals above 2^53); fixed by `Narrow`.
  - B2: three discriminating golden cases, and mutations of the single comparisons and of the stage
    order.
  - B3: pin `ToString` directly.

  `Positivity` was judged the smallest exact generalization ("no cheaper exact solution using only
  the existing hooks"). Folded.

  The golden was captured from the hand-written codec, with a one-off dump test that was then
  removed: `outlier_analysis`, 61 cases, plus defaults, non-default, invalid enum and registration.
  The stored kernel density and point spacing entries came out byte-identical.
- 2026-10-30: Block 2 implemented.
  - The outlier section is rows bound to `OutlierAnalysisConfig`, plus the section with its `Check`.
    The generic codec is instantiated once in the operation unit, and the hand-written codec is
    deleted. `BoundField` gains `Narrow` and `Positivity`/`PositiveRefusal`; callers use the
    generic functions.
  - Verified in this session:
    - the golden is unchanged across the switch;
    - the selection 904/904 and source rules 9/9;
    - the Vulkan smokes 31/31;
    - the inventory up to date, and doc links clean.
  - Mutations, each red against `DeclaredFieldContracts.ConfigSectionCodecsMatchTheirGolden`:
    - the radius predicate never holding;
    - the codec ignoring `Positivity`;
    - the radius storage refusal text removed;
    - `Narrow` ignored (the 2^53 witnesses);
    - each of the five `Check` comparisons dropped;
    - stddev without the may-store-zero predicate;
    - the operation and backend rows swapped (red on the registration and on a case).

    A first "Positivity ignored" mutation (the refusal text as `nullptr`) did not compile, and was
    replaced by the two above.
- 2026-10-30: Ponytail review of block 2 (separate agent): ship, two findings, both applied.
  - The method and operation `ToString` overloads had lost their last production caller (`rg` over
    `src/`), so they and their pins were deleted. Only the backend's overload remains, pinned by
    `BackendNamesAreTheTokensAndInvalidValuesSayInvalid`. This supersedes the plan review's B3 for
    the deleted overloads.
  - The never-positive lambda is named once (`OutlierAnalysisMayStoreZero`).

  Rebuilt; the selection 904/904 and source rules 9/9. Size: the footprint 1,470 (from 1,538); the
  runtime 968.
- 2026-10-30: Codex code review of block 2 (`review354d`, diff `99d2471c…`, same thread as 1a and
  1b): approve, no blocking findings. It found the observable behaviour preserved (defaults, canonical
  serialization, messages, precedence, conversions, references, schema, registration), and
  `Positivity` and `Narrow` narrowly scoped.
  - Its optional point, applied: a direct witness that kernel density and point spacing convert an
    integer literal above 2^53 straight to float
    (`DeclaredFieldContracts.FloatFieldsConvertNumbersStraightToFloat`; red on both with `Narrow`
    ignored).
  - Its note on the Vulkan smoke log: LeakSanitizer reports leaks at exit. The allocations are in
    Vulkan device initialization and driver calls (`PushConstants`, dbus). They have the same size
    since 1a and also appear in the full Vulkan run before this task (`RUNTIME-353`), so they are not
    attributed to this change. The smokes are a functional pass (31/31), not a clean sanitizer pass.
- 2026-10-30: Block 3 plan: Codex plan review (`plan354f`, fresh session): revise.
  - The panel wording and the documentation also change for `negative`, so the prediction is 6.
  - The refusal cannot precede capture.
  - The 3a inventory of visible changes was incomplete.
  - The visibility test must observe drawn controls.
  - The Vulkan refusal is pinned by its exact text.

  Folded. Delta (`plan354g`): approve.
- 2026-10-30: Block 3a implemented: the outlier panel's six numbers drawn by `DrawBoundFields` over
  `kOutlierAnalysisRows`, with the panel's method and backend predicate.
  - New test `SandboxProcessingPanels.OutlierNumberControlsFollowTheirRowsPerMethodAndBackend` covers
    every method × backend, observing drawn controls by activation; any further number or bool row is
    expected everywhere.
  - The radius edit in `OutlierActionsApplyTheirOwnRequestAndRetryRejectedConfig` addresses
    `"Radius##Outliers"` (structural, question 28).
  - Mutations, each red on the new test:
    - the old hand-written controls;
    - stddev shown for every method;
    - the batch size shown for every backend;
    - entity drawn as a number.
- 2026-10-30: Deviation found before 3b: the derived codec has no bool rows. `IsDerivable` rejects
  them, the serializer writes a number, the parser reads a number, and no check refuses a non-boolean.
  `negative` therefore also needs bool support in `Runtime.DerivedConfigSections.cppm`, a one-time
  framework capability (a second bool option would not need it). It goes into the trial commit and
  is counted: the prediction becomes 7 files, 6 per option plus the one-time framework edit. It is
  reported both ways.
