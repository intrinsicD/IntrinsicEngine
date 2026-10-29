---
id: RUNTIME-348
theme: F
depends_on: [ARCH-021, RUNTIME-347]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: the operation runtime stores the results of kernel density and point spacing in one session-owned store instead of per-operation sinks and snapshot members, whoever started the run (panel, agent, GPU transaction); a result of an older run never replaces a newer one; the inline fallback delivers through the caller's sink; messages, statuses and computed values are unchanged; tests that pinned the per-operation structure are rewritten (listed); no catalog contract covers the editor's result slots
---
# RUNTIME-348 — Operation results and delivery (ADR 0031 phase 1, slice 3)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
27. (2026-10-29) "Do 1. - 3." (3: start phase 0 and then the phase 1 pilot)
28. (2026-10-29) "Please do 1. and 2." (1: `RUNTIME-348`/`349` may rewrite tests that pin the
    per-operation session structure, keeping every behavioural assertion)

## Goal
- ADR 0031 Decision 7 (results, run identity and ordering) and Decision 3 (delivery of the inline
  fallback), for the two operations on the runtime. Today (read on the code):
  - the session keeps one optional result, one sink and one dismiss case per operation
    (`EditorPointFieldResultSinks`, `EditorPointFieldResultsSnapshot`, about 20 session lines and 4
    attachment lines for the two), and a later delivery always replaces the stored one
    (`retain`, `Runtime.EditorWorkspaceSession.cpp:813-821`);
  - only the panel stores results: it writes an immediate answer to its own state and the session
    sink (`PublishCommandResult`), passes the sink as the queued run's callback, and folds a GPU
    transaction's snapshot into the stored result itself (`DrawPointScalarTransaction`), so an agent
    run never reaches the slot and a GPU result is stored only while the panel is open;
  - the inline fallback returns its result without calling the sink.

## Plan
1. **Store** (`Extrinsic.Runtime.OperationResults`, `src/runtime/Editor`, imported by
   `EditorProcessing`): `OperationResultStore`, owned by the workspace session. Per operation it keeps
   the newest run's result (`std::shared_ptr<const void>`) with its invocation id.
   - `BeginRun()` hands out ids in submission order; the counter never goes back, not on dismiss or
     session reset.
   - Insertion is typed, `Store<Result>(invocation, result)`, so the key (the address of
     `OperationResultKey<Result>::Tag`) and the `static_pointer_cast` in `Last<Result>()` cannot
     disagree (O1). A result whose id is older than the stored one is refused; an equal id replaces
     (a GPU run's terminal result replaces its started answer).
   - `Dismiss<Result>()` removes the entry and with it the ordering barrier: a later delivery of any
     run refills the slot (specified and tested, O2).
   - A frame reads an immutable copy of the entries.
   - Keyed by result type, not by definition: for these two operations the result types and the
     definitions are one to one, and consumers (panel, agent) do not see the definitions yet. A
     temporary bijection, reconciled when definitions become public in `RUNTIME-349` (O1). The ADR's
     common header waits for its first reader there too; this slice does not complete Decision 7.
2. **Context:** `EditorProcessingContext` gains `OperationResultStore* OperationResults{}`; the session
   sets it; contexts without a session (most tests) store nothing. A store write happens only while
   the context's `AttachmentActive()` holds; that check captures only the attachment epoch, so a
   callback outliving its session stores nothing (O2). Writes happen before the caller's sink runs.
3. **Runtime stage 8** (`Runtime.OperationRuntime.hpp`): `Apply` takes an invocation id at stage 1.
   - Stored and returned (no sink): a genuine immediate failure (validation, entity, capture,
     admission, index acquisition, a rejected submission).
   - Neither stored nor delivered (B1): the duplicate-run refusal (`ActiveOutputJobRefusal`, Pending for
     another run's active job; the panel already suppresses it, `Sandbox.PanelSupport.hpp:295`) and a
     GPU start refused as busy. The id is still consumed.
   - Stored, then delivered: the queued Pending (stored only); the queued run's terminal result
     (stored, then the caller's sink once, as today); the inline result (stored, then the caller's sink
     once before returning, new, Decision 3; `FinishApply` copes, it uses the immediate result).
   - GPU (B2): retention is a hook of its own on the scalar transaction (`StartPointScalarGpu` gains a
     `retain` callback, kept for the transaction's life and called before the sink at its one
     terminal delivery), separate from the caller's sink, which Accept may replace
     (`Runtime.PointScalarTransaction.cpp:382`). A started transaction stores its first answer; the
     terminal result (Applied, Discarded, Failed) replaces it under the same id; ReadyToAccept is not
     a delivery. Density weights passes no hook.
   - The store write lives in the store's module, not in a file under `Editor/Operations`, so the job
     drift heuristic (`Test.QueuedEditorJobContract.cpp:549`, guarded sink plus finalizer in one file)
     keeps its meaning without a change (B4).
4. **Session, frame, panel, agent:** `EditorPointFieldResultSinks`, `EditorPointFieldResultsSnapshot`
   and `EditorPointFieldResultSlot` go, with their session members, dismiss cases, retain lines,
   bindings and resets and the attachment declarations; `EditorPointFieldPreparedFrame` carries the
   commands, the store copy and a guarded dismiss. The two panels read and dismiss through the store
   and pass no session sink. `UpdateEditorPointScalarResult` for the two **stays** (B3): it is both
   the panel's local fold and its eligibility for the transfer statistics row
   (`Sandbox.MeshProcessingPanels.cpp:204,224`); a test keeps those statistics visible. The agent rows
   are unchanged; their runs now reach the slot.
- Behaviour that changes on purpose (ADR Decisions 3 and 7): runs from any caller (agent, GPU
  transaction without an open panel) reach the slot; an older run's late result no longer replaces a
  newer one; the inline fallback calls the sink once.
- Not in this slice: the common header, the default panel and placement (`RUNTIME-349`); other
  operations (their family structs stay until they migrate).

## Tests (predictions as assertions)
- New (`Test.OperationResults.cpp`), red before where the behaviour is new:
  - store: an older id is refused, an equal id replaces; dismiss clears the barrier and keeps the
    counter; a copy does not change after later stores; unknown and different result types are
    separate;
  - `OlderRunNeverReplacesNewer`: run A queued, run B fails immediately (stored), A completes: the
    slot holds B, A's sink still receives A (red today: `retain` replaces);
  - `DuplicateRefusalLeavesTheActiveRunsSlot`: A queued (Pending stored), B refused as a duplicate:
    the slot still holds A's Pending, B's sink is never called; A completes: the slot holds A's result;
  - `AnyCallersRunReachesTheSlot`: an apply with its own sink (as the agent's) stores its terminal
    result (red today);
  - `GpuResultsAreRetainedWithoutThePanel`: a kernel density test transaction stores its started
    answer, then its terminal result under the same id, for an explicit Accept sink, automatic
    acceptance, Discard and a failed run (red today);
  - `InlineFallbackDeliversOnce`: without a job lane the sink receives the published result once and
    the call returns the same result (red today);
  - `DetachedOrDestroyedSessionStoresNothing`: a queued run of a detached or replaced attachment,
    drained later, leaves the new store empty; dismiss through an expired frame does nothing.
- Rewritten tests (operator decision 28), each listed when done:
  - structural: `SandboxEditorSessionLifecycle.PointFieldFramesRetainIndependentResultsAndRejectExpiredDelivery`
    (same guarantees through the store), the source-text test in `Test.SandboxEditorPresentation.cpp`,
    `Test.PointLBVHGpuSmoke.cpp` (builds `EditorPointFieldResultSinks`), the scalar transaction panel
    fixture (`Test.SandboxProcessingPanels.cpp:4921`, a sessionless context: bound to a store);
  - behavioural, changed on purpose: `SandboxProcessingPanels.DuplicateRunRefusalIsNotStoredAsThePanelsResult`
    (`:5847`): the externally started run now occupies the slot, so the assertion becomes "the
    duplicate leaves that run's slot unchanged and its completion replaces the Pending"; the
    refusal still never becomes the panel's result;
  - extended: the transaction test that the Accept sink is delivered exactly once also checks
    retention.
- Refactoring pin: the `RUNTIME-347` list (790 tests), `PointIndexAdmission.*`, the Vulkan binary
  (57).
- Mutations (O2: the first four alone would miss B1-B3): the ordering check removed; the duplicate
  refusal stored; the inline sink call removed; the GPU retention hook not called; Accept's sink
  replacing the retention; the attachment check bypassed; the store written after the sink.

## Acceptance criteria
- [x] Store, context member, stage 8 writes; the two operations' per-operation slots removed.
- [x] New tests red before, green after; rewritten tests listed; suites and Vulkan smokes green.
- [x] Codex plan and code review; structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -j8 -R "<RUNTIME-347 list>|^(PointIndexAdmission|OperationResults)\."
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 600 -R "<every test of IntrinsicPointLBVHGpuTests>"
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Plan from the code (session slots and retain, panel publication and GPU fold, agent
  apply path, processing context).
- 2026-10-29: Codex plan review (`plan348.answer.md` in this session's scratchpad): revise. B1 a
  duplicate refusal must not occupy the slot (it would bar the active run's result); B2 GPU
  retention needs its own hook, since Accept replaces the transaction's sink; B3 the panel's fold
  helper also enables the transfer statistics row, so it stays; B4 more tests to rewrite (the panel
  fixture without a session, the duplicate-refusal panel test, the Accept-sink test) and the job
  drift heuristic, avoided by keeping the store write out of `Editor/Operations`. O1 keying by
  result type stated as a temporary bijection, insertion typed; O2 lifetime, dismissal and ordering
  semantics specified and tested, more mutations. All taken (Plan, Tests). The attachment guard
  captures only the epoch (`Runtime.EditorWorkspaceSession.cpp:620`), checked.
- 2026-10-29: Implemented: `Extrinsic.Runtime.OperationResults` (`OperationResultStore`, its frame
  copy, `DismissOperationResult`, `RetainOperationResult`); `EditorProcessingContext::OperationResults`
  set by the session; stage 8 in `Runtime.OperationRuntime.hpp` (invocation at stage 1, immediate
  failures, the queued Pending, inline and terminal results stored, the duplicate refusal and a
  busy GPU start not; the inline result delivered to the sink once); the scalar transaction's
  `Retain` hook (`Runtime.PointScalarTransaction.cpp`, called before the sink at the one terminal
  delivery, not replaced by Accept; density weights passes none); the point field sinks, snapshot
  and slot enum removed with their session, attachment and frame lines; the two panels read and
  dismiss through the store, pass no session sink and keep `UpdateEditorPointScalarResult` (B3).
  New tests written first, red on the unchanged runtime: `OperationResults.OlderRunNeverReplacesNewer`,
  `DuplicateRefusalLeavesTheActiveRunsSlot`, `InlineFallbackDeliversOnce`,
  `DetachedSessionStoresNothing` (its last assertion, a run of the active attachment stores); the
  store's unit tests passed with the store module. Added after the implementation, shown red by
  the mutations: `ThreeMethods/PointScalarTransaction.GpuResultsAreRetainedWithoutThePanel`
  (explicit Accept sink, Discard; density weights skipped) and
  `OperationResults.TheStoreIsWrittenBeforeTheSinkRuns` (inline and queued).
  Rewritten tests (operator decision 28):
  - structural: `SandboxEditorSessionLifecycle.PointFieldFramesRetainIndependentResultsAndRejectExpiredDelivery`
    (the same guarantees through the store, results delivered by refused runs);
    `SandboxEditorPresentation.MeshProcessingPanelsPreserveLifetimeAndResultPublication` (the two
    sink literals replaced by the store reads); `SandboxProcessingPanels.ScalarTransactionTerminalResultsPersistAndDetachDiscards`
    (its sessionless context bound to a store, the folded statistics checked there);
    `PointLBVHGpuSmoke` kernel density and point spacing apps (the sink holder struct replaced by
    a callback; a store bound, the newest run's result checked: automatic acceptance);
  - behavioural, changed on purpose: `SandboxProcessingPanels.DuplicateRunRefusalIsNotStoredAsThePanelsResult`
    (the slot now holds the run started elsewhere; the refusal's wording never does);
    `PointLBVHGpuSmoke`'s CPU reference runs pass no sink (an inline run now calls its sink,
    Decision 3; the reference results were mixing into the GPU results the sink collects).
  `AcceptSinkDeliveredExactlyOnce` was not extended: the new retention test covers an explicit
  Accept sink with retention; automatic acceptance is covered on the device. (The first version of
  this note said a failed GPU run could not be tested without a device; wrong, see the next entry.)
  Results: the `RUNTIME-347` list plus `PointIndexAdmission` and `OperationResults` 802/802;
  `IntrinsicPointLBVHGpuTests` 57/57 (one benchmark skip). Mutations, each alone and restored, all
  red: ordering check removed (2 tests); duplicate refusal stored (2, including the panel test);
  inline sink call removed (2); GPU retention not called (3); Accept's sink replacing the
  retention (2); attachment check bypassed (the store's unit test; the queued path is also guarded
  by the job result guard); store written after the sink (1). Changed lines clang-format 20.1.8;
  layering and test layout clean. Lines: `src` +169 (the store module 121, stage 8 +51, the
  transaction hook +7; the point field slots -13 in the interface and -8 in the session).
- 2026-10-29: Codex code review (`review348.answer.md`): revise. B1 a GPU start refused because
  another run's transaction holds the output ring (`InvalidProcessingParameters`) was stored and
  then barred that run's terminal result: the scalar transaction snapshot now carries `OutputBusy`
  (set on both busy refusals) and the runtime stores a start's answer only without it; test: a
  second start while the first awaits Accept leaves the slot, the first's terminal result fills it.
  B2 the panel folded a finished transaction into the newest stored result, possibly another run's:
  the fold now needs the fold helper (density weights only), the transfer counters row is enabled for
  results the runtime stores (`kStoredByOperationRuntime`), and `UpdateEditorPointScalarResult` for
  kernel density and point spacing is gone (no caller), so the combination cannot occur; the
  fixture test asserts the counters row is shown. B3 two rewrites were weaker than the decision
  allows: the duplicate-refusal test again requires an empty slot for outlier analysis and, for
  kernel density, the elsewhere run's Pending unchanged until its result replaces it; the scalar
  transaction fixture binds its transaction to the session's store (through the session's
  processing context) and checks the frame's results again. B4 the module inventory regenerated.
  O1 a failed run (Accept submission refused) is retained before its sink runs, now tested; O2 a
  queued run of a destroyed attachment and one of a detached-and-replaced attachment, both drained
  to a terminal state, call no sink and leave the next session's results empty
  (`EditorQueuedPointJobs.OutlivedRunsStoreNothingInTheNextSession`).
  After the fixes: 803/803, Vulkan 57/57. Mutations, both red: the old status check for a busy GPU
  start (the retention test, both operations); the counters row only for folding results (the
  fixture test). Changed lines clang-format 20.1.8.
- 2026-10-29: Codex delta review (`review348b.answer.md`): B1, B3, B4, O1, O2 resolved; B2's code
  resolved, its regression test missing. Added `SandboxProcessingPanels.OlderGpuRunNeverRewritesANewerResult`
  (kernel density and point spacing): the panel's GPU run A is accepted after another caller's
  newer refusal B is stored; the window text logged over every frame from Accept to after A ends
  never shows A's terminal message and shows B's. With the old fold restored (the two
  `UpdateEditorPointScalarResult` overloads put back) it fails for both operations ("the older run's
  answer was shown"), while the fixture test still passes, so it is the test that pins B2.
  After it: 804/804.
- 2026-10-29: Codex second delta review (`review348c.answer.md`): approve; B2's two-run case
  covered (B stored before A is accepted, continuous capture to after A ends, the old fold fails it).
