# Agent Postures and Responsibilities

The pair workflow (`docs/agent/prompt/prompt.md`) replaced the earlier
five-role model (Architect / Implementation / Test / Review / Paper agents and
a rotating weekly-review ownership). Responsibilities now attach to
**postures** and **lanes**; one session may move through several.

## Pair (default posture)

- Observes the operator's work at checkpoints; hints in three tiers
  (stop-the-line / improvement / polish) across architecture, implementation,
  and harness.
- Asks one question at a time, with a recommended answer, when destinations
  diverge.
- Runs the pre-merge sweep ([review.md](../../../../../docs/agent/review.md)) before commits.

## Delegate (bounded hand-off)

- Owns the smallest robust slice end-to-end: implement, test, verify, sweep,
  commit, push, report.
- Asks questions once, up front; records chosen defaults in the task note.
- Honors the risk gates (dependency edges, public surfaces, claims,
  destructive actions) before landing.

## Advisor (direction and research)

- Situates from real state; maps 2–3 directions and recommends one.
- Owns paper intake for method-shaped work: literature pass, formulation
  choice, critical-parts explanation (`intrinsicengine-method`,
  `intrinsicengine-research-ideation`).

## Unattended overnight lane

- Executes night-ready tasks only, with the full claim / work-graph /
  completion-evidence custody ([workflow-evidence.md](../../../../../docs/agent/workflow-evidence.md)).
- Delivers a morning report: what changed, verification run, uncertainties,
  hints filed.

## Audits

- Audit sweeps (output, drift, clean-workshop, hints triage — see
  [review.md](../../../../../docs/agent/review.md)) run on demand, preferably overnight, at the
  operator's request. There is no rotating reviewer role and no fixed
  cadence; findings land as `tasks/HINTS.md` entries or backlog tasks.

## The operator

- The human is the scheduler and the memory of record: work selection,
  priorities, risk-gate approvals, and hint dispositions are theirs.
