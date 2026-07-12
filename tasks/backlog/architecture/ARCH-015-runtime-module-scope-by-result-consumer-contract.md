---
id: ARCH-015
theme: F
depends_on: []
---
# ARCH-015 — Runtime module scope: cluster methods by result-consumer contract (grilling + ADR)

## Goal
- Decide and record, via a grilling pass and an ADR, the scoping rule for
  `IRuntimeModule` implementations: what determines whether two methods share a
  module or get separate modules, so the module tree stays coherent as the
  Theme I research methods land.

## Non-goals
- No engine code changes in this task; it is a decision record. The
  `ClusteringModule` generalization it recommends is a separate implementation
  task.
- No change to the `IRuntimeModule` interface itself (it is already a thin
  lifecycle/registration seam and should stay that way).
- No new module implementations (DBSCAN, spectral, GMM, …); those follow the
  method workflow once the scoping rule is agreed.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.Module.cppm` and
  `src/runtime/Modules/*`.
- `IRuntimeModule` is **not** an algorithm interface: it is
  `Name / OnRegister / OnResolve / OnShutdown` plus `EngineSetup` for registering
  command handlers, sim systems, frame hooks, and event subscriptions. It never
  constrains what a method computes, so a fixed module interface cannot hinder
  research ideas — the algorithm lives entirely below it.
- `ClusteringModule` (retired seam `ARCH-012`) already scopes by *problem
  family*: it shares `ClusteringDomain`, `ClusteringBackend`, and the
  `ClusterLabelsChanged` event, and only `RunKMeans` / `KMeansRun*` are
  algorithm-specific. It routes work through the command → job → event → commit
  path while keeping `KMeans*` out of `Runtime.Engine`.
- Open question the user raised: should a runtime module cluster similar methods
  (a `ClusteringModule` holding K-Means, DBSCAN, spectral, …) or should each
  algorithm get its own module? And does a fixed interface hinder adding varied
  research ideas?
- Recommended answer to capture (subject to grilling): a module is scoped by the
  **problem family defined by its shared output/consumer contract**, not by the
  algorithm and not by a rigid per-method interface. For clustering the shared
  contract is per-element integer labels (`ClusterLabelsChanged`), which every
  clustering method emits and which the renderer/selection/visualization consume
  identically — so DBSCAN belongs *inside* `ClusteringModule` (a `RunDBSCAN`
  command + a DBSCAN backend reusing the shared domain/backend/labels
  vocabulary), not in a separate `DBScanRuntimeModule`. The open extension points
  where novelty lives — per-method `Run*` command structs (plain structs, per
  `AGENTS.md` §5 P1), backends behind the family's backend token with
  requested-vs-actual parity telemetry, and new output events — are all below the
  interface and unbounded.
- The decidable split test: introduce a new module (or extend the family's event
  set) only when a method's result cannot be expressed by the family's consumer
  contract. Worked cases to record: DBSCAN (hard labels + a noise sentinel →
  stays in `ClusteringModule`); GMM (soft membership probabilities → a different
  consumer contract → extend the family with a soft-membership event or a new
  module); hierarchical clustering (a dendrogram → different contract again).
- This is hard to reverse once modules proliferate, surprising to a future reader
  ("why is DBSCAN a command in `ClusteringModule` and not its own module?"), and
  a real trade-off — it meets the three-condition ADR test in
  `intrinsicengine-task-workflow`.

## Required changes
- [ ] Run a grilling pass (see `grilling` / `intrinsicengine-task-workflow`
      grilling alignment) to pin the initial runtime-module **family taxonomy**
      (e.g. clustering, registration, reconstruction, parameterization,
      PDE/solvers, sampling/consolidation) and confirm the result-consumer-contract
      scoping rule and the split test above.
- [ ] Write an ADR under `docs/adr/NNNN-runtime-module-scope-by-result-consumer-contract.md`
      recording: the scoping rule, the family taxonomy, the decidable split test,
      the worked cases (DBSCAN / GMM / hierarchical), and the statement that
      `IRuntimeModule` stays a lifecycle/registration seam.
- [ ] Link the ADR from the architecture index (`docs/architecture/index.md`) and,
      if it rises to contract level, note the module-scope rule in `AGENTS.md` §4.
- [ ] Record a follow-up implementation task to generalize `ClusteringModule`'s
      K-Means-specific completion event into a shared `ClusterRunCompleted` (or an
      explicit decision to keep per-algorithm completions) when the second
      clustering algorithm lands.

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes (ADR links resolve).
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.

## Docs
- [ ] The new ADR under `docs/adr/`.
- [ ] `docs/architecture/index.md` links the ADR.

## Acceptance criteria
- [ ] The runtime-module scoping rule is an explicit ADR with a family taxonomy
      and a decidable "when to split a module" test, not tribal knowledge.
- [ ] The `ClusteringModule` DBSCAN worked example is recorded as the reference
      instantiation.
- [ ] No engine code changed by this task.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing the `IRuntimeModule` interface as part of this decision task.
- Implementing new modules or backends here (that is method-workflow work).
- Applying the ADR's consequences to code before the ADR is agreed.
