---
id: ARCH-021
theme: F
depends_on: [ARCH-020, RUNTIME-343, RUNTIME-344, RUNTIME-345]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: design record; the pilot tasks it names carry the evidence
maturity_target: Designed
contract_schema: 1
contracts: []
contract_review: a design ADR (0031) proposing a typed operation definition and one shared operation runtime; no code changes in this task; the pilot phases are separate tasks with their own contracts
---
# ARCH-021 — Declared operations over one shared operation runtime (design)

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
24. (2026-10-29) "Should operations really move out of the editor into their own modules?"
25. (2026-10-29) "And how is this done in modern triple a engines and 3d software? Do they do this
    in a better way?"
26. (2026-10-29) "Yes. Please design this properly" (answer to: write a design document for a new
    operation contract, have it reviewed, and pick the pilot)
27. (2026-10-29) "Do 1. - 3." (answer to: 1 accept ADR 0031; 2 lift the "no generic property-write
    API" limit for operations; 3 start phase 0 and then the phase 1 pilot)
28. (2026-10-29) "Please do 1. and 2." (answer to: 1 may `RUNTIME-348`/`349` rewrite tests that pin
    the per-operation session structure, keeping every behavioural assertion, recorded in ADR 0031 as
    the reading of the phase 1 gate; 2 fix the Vulkan path's empty message for a mismatched index
    snapshot now, as its own slice with a GPU regression test)
29. (2026-10-29) "ok, then do 1." (answer to: the phase 1 gate's 150 lines per operation looks out of
    reach; 1 build `RUNTIME-351` as planned and measure, then bring options; 2 deeper cuts first;
    3 pause and decide the gate now)

## Goal
- `docs/adr/0031-declared-operations.md`: an operation is one typed definition (parameters declared
  once, input and output kinds, a pure compute); one internal operation runtime does capture,
  execution, GPU preview, publication with undo, readiness, results and exposure. It is the route to
  M2's "a new option in at most 3 files" and to a `src/runtime/Editor` that holds only the framework.
- Not in this task: any code. The ADR names the phases (0: building blocks; 1: pilot kernel density
  and point spacing; 2: outlier analysis; 3: mesh remesh) and the pilot gate.

## Inputs (this session)
- Algorithm inventory of `src/runtime/Editor` (read-only agent, spot-checked): about 520 of 52,088
  lines are own algorithm code; the shares by concern are in the ADR's Context.
- Three operations traced end to end (read-only agent; two findings verified on the code: the unused
  `GpuQueryBatchSize` of the point scalar operations, and the unobserved cancellation of outlier
  analysis, kernel density and remesh): kernel density about 11 files and 425 specific lines
  (method trait `Runtime.PointFieldOperations.Processing.cpp:61-91`, shared template `128-323`);
  outlier analysis about 12 files and 1,890 lines (GPU transaction `Outliers.cpp:497-941`, publish
  `331-495`); remesh about 12 files and 830 lines (command `Runtime.MeshTopologyOperations.cppm:132-146`,
  job `Topology.cpp:2188-2254`, commit via `CommitMeshTopologyReplacement`).
- Inventory of shared building blocks (read-only agent): history and mutation helpers, job surface,
  `QueuedJobDelivery`, `GpuTransactionCore`, `PointScalarTransaction`, readiness, config sections,
  `BoundField`, result slots, input capture, agent rows, panel helpers; the constraints (method API
  contract, no exceptions/RTTI, `RUNTIME-311`'s limit, the "no generic property-write API" scope
  notes).
- Prior art checked against vendor documentation (Unreal modeling tools and transactions, Houdini
  SOP verbs, Blender undo); links in the ADR.

## Acceptance criteria
- [x] ADR 0031 written in the repository's ADR format and listed in `docs/adr/index.md`.
- [x] Independent review (Codex, fresh session) and its findings resolved or answered.
- [x] The operator decides on the ADR (accept, change, or reject) and on superseding the two earlier
      limits (Decision 9); the decision is recorded here and in the ADR's status.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Log
- 2026-10-29: ADR 0031 drafted from three read-only inventories (algorithm share, three operations
  traced end to end, shared building blocks) and vendor documentation; two findings verified on the
  code (unused `GpuQueryBatchSize`, unobserved cancellation).
- 2026-10-29: Codex design review (`adr0031.answer.md` in this session's scratchpad): revise. B1
  execution stages and their contracts; B2 a real GPU contract; B3 the property transaction does
  not cover outlier analysis' stamp or segmentation's two domains; B4 validate the payload before
  conversion, golden round trips, keep the agent rows' shape (config-sourced operations refuse
  `params`); B5 result storage without RTTI, run identity, module surface; B6 the architectural
  test is outlier analysis, accounting rules, failure classes, the three-files criterion and the
  25k objective kept. O1 prior art narrowed; O2 the `RUNTIME-311` limit is respected, the schema
  compiler reconciled with M2's fallback; O3 resource costs. All taken: Decision rewritten
  (stages table, GPU ownership table, output-kind rules, results and module surface), Validation
  with accounting and two gates.
- 2026-10-29: Codex delta review (`adr0031b.answer.md`): revise; B2-B4, O1, O3 resolved; four new
  findings, all taken: N1 apply never waits for the cached readiness verdict and captures afresh
  (the session tests that submit while readiness is pending stay valid); N2 an immutable
  per-session invocation id orders deliveries (the run key stays a lookup key); N3 the framework is
  one exported module for operation modules holding the template definitions, "internal" meaning
  not part of the app surface; N4 the two measured changes defined (adding an operation: at most 7
  files, 3 its own; adding an option: at most 3 files in total, M2's criterion).
- 2026-10-29: Codex second delta review (`adr0031c.answer.md`): N1, N3 resolved; N2 and N4 partly:
  ordering now applies across an operation's one result slot whatever the entity; a new operation's
  budget counts the regenerated module inventory and the tool schema baseline (at most 9 files, 3
  its own).
- 2026-10-29: Codex third delta review (`adr0031d.answer.md`): approve as a proposed design for the
  operator's decision; N2 and N4 resolved, no new contradiction. Open: the operator's decision.
- 2026-10-29: Operator decision (question 27): ADR 0031 accepted; the "no generic property-write
  API" scope limit is lifted for operations (Decision 9); phase 0 and then phase 1 start as their own
  tasks (`RUNTIME-346` onwards, listed in `ARCH-020`).
- 2026-10-29: The named operation table (read-only agent, phase 0 follow-up) is
  `docs/architecture/declared-operations-table.md`: 30 operations, about 50,290 lines now, about
  16,630 estimated kept, conservative removable 24,200-27,800 (the phase 2 gate asks for 5,000 net).
  Four sizes spot-checked against the code (consolidation module 5,750, clustering 2,774,
  `Outliers.cpp` 1,055 lines, the kernel density panel range). The framework's own size is not
  estimated.
  Codex (`RUNTIME-346` code review) spot-checked kernel_density (matches) and glyph_order (output
  is a float column; the agent's split over-counted its ranges by 72 lines): corrected.
- 2026-10-29: Staged deferral (`RUNTIME-347`, Codex plan review B4): Decision 3's `OperationContext`
  (cancellation polling inside `Compute`) is not built in the pilot, because neither pilot method
  takes a cancellation; the job service already skips work cancelled before it starts and checks
  again before publication. Trigger: the first migrated definition whose method polls (CPD, UV
  regeneration, point construction), or phase 2 if outlier analysis' compute is chunked. Removing
  it for good would need an ADR amendment.
- 2026-10-29: Operator decision (question 28): the phase 1 gate's "existing tests pass unchanged"
  keeps every behavioural assertion; tests that pin the per-operation structure the runtime
  replaces may be rewritten, each listed in its task. Recorded in ADR 0031 (Validation, Gates).
