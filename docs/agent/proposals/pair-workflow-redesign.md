# Proposal: Pair-programmer workflow redesign

Status: **proposal, not yet policy**. `AGENTS.md` remains authoritative until an
accepted follow-up change lands. This document proposes the replacement shape,
the migration path, and the open decisions.

## 1. Verdict

The current agentic workflow is engineered for **unattended agent fleets that
cannot trust each other**: task claims with lease generations, a hash-chained
work-graph state machine, command receipts, generated evidence reports,
post-commit seals, independent fixed-surface reviews, and five cumulative
custody profiles. That design is internally coherent — and mismatched to how
the repository is actually used now: **one human, working interactively with
CLI agents on a local machine.**

When the human is present, the conversation *is* the work graph, the human *is*
the scheduler, and git + CI *are* the evidence trail. Machinery whose purpose
is to substitute for an absent human becomes pure overhead when the human is
sitting right there. The observed symptom is exact: review and custody consume
the day; the engine barely moves.

Proposed replacement: three explicit agent postures on an unchanged engineering
safety floor —

- **Pair** (default) — an observant copilot that watches what the human does,
  interjects with severity-tiered hints (architecture, implementation,
  harness), and asks the one question that most changes what gets built next.
- **Delegate** — a bounded task handed off; the agent runs a compact
  implement→verify→self-review→commit loop and reports back.
- **Advisor** — invoked when the human does not know where or how to continue;
  produces situation assessment, candidate directions with a recommendation,
  method selection, literature research, and an explanation of the critical
  parts.

Review effort becomes **risk-gated instead of universal**: deterministic
validators and CI stay on everything; judgment-heavy review triggers only on
signals (layering edges, public surfaces, claims, irreversibility).

## 2. Diagnosis: where the day goes

Cost of one "standard" (non-micro) slice under the current policy, counting
required process operations from `docs/agent/prompt/prompt.md`,
`docs/agent/workflow-evidence.md`, `docs/agent/task-format.md`, and
`docs/agent/review-checklist.md`:

| # | Required operation | Source of obligation |
|---|---|---|
| 1 | Read `AGENTS.md` (~540 lines) + `tasks/SESSION-BRIEF.md` + task file + routed skill | prompt.md §reading order |
| 2 | Cross-check `contract-catalog.yaml`, declare contract IDs or justified-empty review | AGENTS.md §11 |
| 3 | `task_claim.py acquire` + mirror owner/branch/worktree/claimed_at into front-matter | workflow-evidence.md |
| 4 | `agent_work_graph.py start` with the review-diamond recipe | AGENTS.md §11.4 |
| 5 | `begin`/`finish` through ≥7 nodes — ≥14 CLI transitions per slice | workflow-evidence.md |
| 6 | `workflow_evidence.py record-command` receipts for required commands | workflow-evidence.md |
| 7 | The actual work: implement, tests, docs | — |
| 8 | Verification commands | — (stays) |
| 9 | `generate-report` + `validate` (+ `--require-complete` at retirement) | workflow-evidence.md |
| 10 | 8-section review-checklist self-review | review-checklist.md |
| 11 | Retire: move file, date+PR ref, `RETIREMENT-LOG.md` append, regenerate `SESSION-BRIEF.md`, `seal-report` + extra commit | task-format.md |
| 12 | Any stated number/capability: ARA claim row + `check_ara_claims` | AGENTS.md §8b |
| 13 | If `docs/agent/*` touched: `sync_skills.py --write` | AGENTS.md §9 |

Rows 7–8 are the work. Everything else is process wrapped around it — roughly
**25 process operations per ~4 units of real work**, before any human review
even starts. On top of that, five separate review instruments exist (per-PR
checklist, architecture checklist, clean-workshop scorecard, weekly
agent-output audit, drift audit) plus method/benchmark checklists and the
results audit.

None of this is *wrong* for a fleet of mutually-untrusting unattended agents.
All of it is mispriced for a solo human pairing with an agent that can simply
*ask*.

## 3. Design principles

1. **The human is the scheduler and the memory.** Work selection, priorities,
   and "what's next" are conversational. Task files persist context between
   sessions; they do not gate work.
2. **Talk is cheaper than custody.** When intent is ambiguous, ask one good
   question instead of freezing a digest. When a change is risky, say so out
   loud instead of writing a receipt.
3. **Deterministic checks stay; ceremony goes.** `check_layering.py`, the CPU
   test gate, sanitizer lanes, and CI are cheap, automatic, and catch real
   defects. Reports, seals, claims machinery, and graph transitions are manual
   and catch almost nothing the validators and the human don't.
4. **Review effort follows risk, not routine.** A geometry kernel tweak with
   green tests needs a diff read. A new dependency edge needs an architecture
   conversation. A README performance claim needs evidence. These are different
   events; today they cost the same.
5. **Truthfulness stays non-negotiable.** Backend identity honesty,
   fail-closed defaults, the maturity vocabulary (`Scaffolded` →
   `CPUContracted` → `Operational` → `ParityProven`), and "no number without a
   baseline" survive the redesign. What goes is the *bureaucratic encoding* of
   truthfulness, not the standard itself.
6. **The agent is opinionated but interruptible.** Hints come with a
   recommendation and a why; the human overrules without friction; a rejected
   hint is not re-raised that session unless its severity escalates.

## 4. The three postures

### 4.1 Pair (default)

The agent behaves like an experienced colleague looking over your shoulder —
one who knows the layering table, the module rules, and the past bugs by heart
(the domain skills), and whose job is to make *your* work better, not to run a
process.

**Observation checkpoints.** The agent forms a picture of what you are doing
at natural moments rather than continuously narrating: session start
(`git status`/`git log`/diff since last session), when you describe a plan or
paste code, after a failing test or crash, and before a commit. At a
checkpoint the agent inspects the actual state (`git diff`, the touched files,
the knowledge graph for module neighbors) — it never hints from memory alone.

**Tiered hint protocol.** Every hint names a concrete location
(`file:line` or module), the concrete consequence, and the concrete
alternative — one to three sentences each. Three severities:

| Tier | Meaning | Examples | Delivery |
|---|---|---|---|
| **Stop-the-line** | Correctness or contract damage in progress | Layering violation; lifetime/UB bug; fail-open error path; test that cannot fail; destructive git operation | Immediately, unprompted, even mid-task |
| **Improvement** | Materially better path exists | New code in the wrong layer; missing or unnecessary seam (right-sizing); API shape that will hurt the next caller; behavior change without a test; harness friction (wrong preset lane, missing test label, a validator that should be a hook) | Batched at the next natural pause; max 3 at a time; human decides |
| **Polish** | Worth doing, never worth interrupting | Naming, comment hygiene, docs wording | Only on request or in a pre-commit sweep |

Hints cover all three axes the human actually wants watched:
**architecture** (layer ownership, dependency direction, seams, right-sizing),
**implementation** (correctness, determinism, failure states, tests,
simplicity), and **harness** (build lanes, test labels, CI friction,
skill/tooling gaps, agent-workflow friction itself).

**Anti-noise budget.** A hint the human rejects or ignores is dropped for the
session unless it escalates to stop-the-line. No hint is repeated verbatim.
Polish never blocks. The agent does not grade every edit; silence at a
checkpoint is a valid outcome.

**Question protocol** (the `grilling` skill's lesson, lightened for continuous
use): when two plausible destinations diverge — or the observed edits
contradict the stated goal — ask **one question at a time, with a recommended
answer and the reason for it**. At most two questions per checkpoint; if the
codebase can answer the question, read the codebase instead of asking.
Decisions worth remembering go into the task note or the commit message, not a
ledger.

**Pre-commit sweep** (replaces the 8-section checklist for pair work): the
agent reads the staged diff and answers four things — scope is one intent;
layering intact (`check_layering.py` if `src/` touched); changed behavior has
a test and the touched-scope gate is green; docs/task notes updated only if a
surface or structure actually changed. Findings are delivered as hints, not
as a gate.

### 4.2 Delegate

Explicit hand-off: "take BUG-097 and finish it", "implement the slice we just
discussed". The agent runs the loop solo:

1. Read the task note (or write a 5-line one if the work will outlive the
   session).
2. Ask up-front questions **once**, then stop asking — pick robust defaults
   and record them in the task note.
3. Implement the smallest robust slice; tests with it.
4. Verify: touched-scope helper first, then the default CPU gate when code
   changed; layering check when `src/` changed; task-level commands when the
   note names them.
5. Self-review with the same four-point sweep as pair mode.
6. Commit (imperative subject, body says why + verification actually run),
   push.
7. Report back: **what changed, how it was verified, what remains uncertain,
   and at most one suggestion.**

No claim, no graph, no receipts, no report generation. The commit + the diff +
the report-back paragraph are the evidence.

### 4.3 Advisor

Trigger: the human says some form of "I don't know where/how to continue", or
explicitly asks for direction, method choice, or research. Output is a
structured recommendation, not motion:

1. **Situate.** Establish where things actually stand by reading/running, not
   recalling: what works, what is proven at which maturity, what broke last.
2. **Map 2–3 viable directions.** For each: expected outcome, rough effort,
   main risk, what it unblocks. End with **one recommendation and why** — not
   a neutral survey.
3. **Concretize the method.** For the recommended direction: the specific
   algorithm/approach to use; when method-shaped, do the literature pass
   (original paper plus the 1–3 follow-ups that matter, stable citations,
   which exact formulation to adopt) — this reuses the
   `intrinsicengine-method` intake DNA and `intrinsicengine-research-ideation`
   for genuinely open directions, as **service, not custody**.
4. **Explain the critical parts.** The 2–3 things that will actually decide
   success in this engine's context: numerics/conditioning, complexity and
   memory behavior, failure modes, where it must sit in the layering, what the
   reference implementation must pin down before optimization.
5. **Hand over a first step** sized ≤ one day, with the test that proves it,
   ready to run in pair or delegate mode.

### Posture switching

Pair is the default at session start. Switching is verbal ("take this over" →
delegate; "I'm stuck / where should this go" → advisor) or explicit via
user-invocable skills (`/pair`, `/advise`; `/grill` stays for adversarial
plan stress-tests before large builds). The agent states its posture only when
it changes.

## 5. The safety floor (unchanged)

Everything in this table stays exactly as it is. This is the part of the
current contract that earns its keep:

- Layering invariants and the dependency table (`AGENTS.md` §2/§4), enforced
  by `tools/repo/check_layering.py --strict`.
- C++23 module rules, `.cppm` interface/implementation placement, preset-only
  builds, Clang 20+ requirement.
- Default CPU correctness gate, sanitizer lanes, `ci-vulkan` opt-in gate, test
  label taxonomy.
- Research pragmatism (P1), config-lane (P3), recipe-driven frames (P5).
- Method protocol order: paper intake → CPU reference → correctness tests →
  benchmark → optimized → GPU-after-parity → limitations. (As *the way method
  work is done*, not as claim custody.)
- Backend truthfulness: requested/actual/fallback reporting; a backend token
  exists only for a real implementation.
- Benchmark discipline: manifests, stable IDs, baseline comparison before any
  "faster" statement.
- Docs-sync for real structural changes (moves, public surface changes,
  module-inventory regeneration) in the same PR.
- CI as-is: red gates are fixed or filed as `BUG-` tasks, never weakened.
- Commit hygiene: no `--no-verify`, no force-push to main, no mixing
  mechanical and semantic changes.

## 6. Risk gates — when more than the compact loop is owed

| Signal in the change | Additional step owed |
|---|---|
| New dependency edge, layering-table change, layering-allowlist entry | Agent presents module-level impact (knowledge-graph neighbors + `check_layering`) and gets an explicit human OK before landing; architecture checklist available on request |
| Public `.cppm` surface change | Inventory regeneration + one-paragraph impact statement in the PR/commit body |
| A research result — method, benchmark, parity, or capability claim — entering `README.md`, `docs/`, or a method report | **Evidence mode**: benchmark manifest + baseline comparison + an `ara/logic/claims.md` row. Implementation and refactoring work owes nothing to the ledger |
| Method backend beyond the CPU reference | Parity evidence vs. the reference before the backend token is claimable |
| Destructive or hard-to-reverse action (history rewrite, deleting evidence/fixtures, retiring a public surface) | Ask first, always |
| Publication-bound experiment (paper submission, external claim) | Opt-in **custody mode**: the existing `experiment_custody.py` freeze/run/bundle/audit chain — used for the handful of runs a year that actually need it |

Everything not in this table is covered by: the pair sweep or delegate
self-review, the deterministic validators, and CI.

## 7. Keep / demote / retire

| Mechanism (today) | Proposal | Rationale |
|---|---|---|
| `AGENTS.md` §1–§10, §12–§13 (mission, layers, coding, testing, benchmarks, docs-sync, CI) | **Keep**, trimmed | This is the engineering contract; it is what makes hints trustworthy |
| Framework24 P0 work-selection gate (`REVIEW-004`) | **Demoted to strong standing recommendation** (decision §10.2) | In pair mode the human picks the work; the agent *surfaces* the gate when suggesting work rather than refusing eligible requests |
| `tasks/` tree (backlog/active/done + retirement log) | **Keep as shared memory, demoted from contract to notebook** | CLI agents lose context between sessions; the tree is the durable memory. Only multi-session work needs a file |
| Nine-section task template + contract front-matter | **Replace with a 5-section note**: Goal, Context, Plan/slices, Verification, Log. Front-matter: `id`, optional `depends_on` | The current template is a contract for strangers; a note is memory for a colleague |
| Task maturity vocabulary (`Scaffolded`…`ParityProven`) | **Keep as vocabulary** | Cheap, and it is the repo's strongest defense against documented-but-not-working claims |
| `SESSION-BRIEF.md` + freshness CI gate | **Keep generator as convenience; drop the freshness gate and the "authoritative" status** | Useful view, wrong as an obligation; regenerate opportunistically or via hook |
| `task_claim.py` claims, leases, generations | **Retired from the interactive path**; quarantined to unattended overnight runs (decision §10.1) | One human = one scheduler; parallel agent sessions are isolated by branches/worktrees anyway |
| `agent_work_graph.py` review diamond | **Retire from the interactive path** | The conversation is the work graph when the human is present |
| `workflow_evidence.py` receipts / reports / seals; `handoff.jsonl` / `reviews.jsonl`; micro/standard/high-risk profiles | **Retire for interactive work** | Commit + diff + PR + CI are the evidence. Two modes remain: *normal* (nothing owed) and *evidence/custody* (opt-in, claim-bound, §6) |
| `experiment_custody.py` (claim-grade/protected) | **Keep, opt-in only** for publication-bound claims | The one context where frozen protocols pay for themselves |
| ARA claim ledger (§8b) | **Keep, trigger narrowed** to statements entering README/docs/reports | The truthfulness floor for public statements; ordinary slices owe nothing |
| Per-PR review checklist (8 sections) | **Replaced by the four-point sweep** (scope, layering, tests, docs) in `review.md` | Matches actual defect yield; deep content preserved in the same doc's risk-gated and audit sections |
| Architecture / clean-workshop / agent-output / drift checklists | **Merged into `review.md`**; the `intrinsicengine-audit` skill runs the named sweeps (output/drift/workshop/hints) on demand | Deep review becomes a deliberate act on request — not ambient duty |
| Method/benchmark review checklists | **Fold into their workflow docs**, applied when that work type occurs | Same content, no separate ceremony |
| Domain skills (vulkan-frame-triage, stale-build-triage, geometry-io-format, import-visibility, sandbox-input-lifecycle, gpu-smoke, diagnose, right-sizing, zoom-out, draw-architecture, handoff…) | **Keep untouched** | They are compressed knowledge, cost nothing until loaded, and are exactly what makes the pair posture sharp |
| Process skills (core, task-workflow, review, docs-sync mirrors) | **Rewrite/merge** to mirror the new docs via existing `sync_skills.py` | — |
| Knowledge-graph MCP aid | **Keep** | It is how the pair posture sees module blast-radius quickly |

## 8. Docs restructure

`docs/agent/` today: 20 files, ~155 KB; the mandatory-read path for a standard
slice is ~90 KB. Target: **6 core documents, ~25 KB total**, with the mandatory
session read being one of them:

| New doc | Absorbs |
|---|---|
| `pair-workflow.md` — postures, checkpoints, hint tiers, question protocol, compact loops, risk-gate table (the new session onboarding; drafted in Appendix A) | `prompt/prompt.md`, most of `workflow-evidence.md`, parts of `contract.md` |
| `review.md` — four-point sweep + the `/audit` deep sweeps | `review-checklist.md`, `architecture-review-checklist.md`, `clean-workshop-review.md`, `agent-output-review-checklist.md`, `drift-audit-checklist.md` |
| `task-notes.md` — 5-section note format + maturity vocabulary | `task-format.md`, `task-maturity.md` |
| `method-workflow.md` — kept, review checklist folded in | `method-review-checklist.md` |
| `benchmark-workflow.md` — kept, review checklist folded in | `benchmark-review-checklist.md` |
| `evidence.md` — opt-in evidence/custody mode + narrowed ARA policy | `ara-evidence-policy.md`, custody parts of `workflow-evidence.md` |

`docs-sync-policy.md` and `source-documentation-policy.md` stay as-is (small,
useful). `how-this-repo-is-built.md` and `roles.md` get rewritten last, after
the dust settles; the five agent roles collapse into the three postures.

## 9. Harness wiring

Concrete local-machine changes that make the pair posture real instead of
aspirational:

- **Session start**: keep `.claude/setup.sh` as the environment adapter; the
  session brief the agent actually needs is `git status --short --branch`,
  `git log --oneline -10`, `ls tasks/active/`, and any open hint list from the
  previous session — three commands, not 90 KB of policy.
- **New user-invocable skills**: `/pair` (restate posture + do a checkpoint
  now), `/advise` (run the §4.3 protocol), `/audit [architecture|drift|output]`
  (the merged deep sweeps). Existing `/grill`, `/zoom-out`, and
  `/intrinsicengine-handoff` already fit the model and stay.
- **Hooks over turns**: move the cheap deterministic validators
  (`check_layering`, `check_task_policy` when `tasks/` touched,
  `check_doc_links` when `docs/` touched) into a pre-commit hook or a
  `PreToolUse(git commit)` hook so they cost zero conversation turns and can
  never be forgotten. CI keeps running them strict regardless.
- **`ci-docs.yml`**: drop the gates for retired machinery (evidence
  validation, session-brief freshness, work-graph regression suites as merge
  blockers); keep task-structure, doc-links, layering, skill-mirror checks.

## 10. Decisions (accepted 2026-08-14)

The operator resolved the five open questions; Phase 1 (landed on this branch)
implements them:

1. **Unattended runs stay — overnight-only, night-ready tasks only.** Fleet
   machinery (claims, work graph, receipts, completion evidence) is
   quarantined to unattended overnight runs on well-defined tasks with no
   open questions or loose ends; `prompt.md` §"Unattended overnight mode"
   defines the night-ready gate.
2. **Framework24 gate → strong standing recommendation.** All agent-chosen
   work defaults to convergence; the human may explicitly direct otherwise
   and the agent proceeds after surfacing the focus once (`AGENTS.md` §1).
3. **ARA narrowed to research results.** Method/benchmark/parity/capability
   claims produced by research work owe claim rows; implementation and
   refactoring work owes nothing to the ledger (`AGENTS.md` §8b).
4. **Audits on demand, preferably overnight.** No fixed cadence; sweeps run
   as overnight jobs and land findings as ledger entries or backlog notes,
   never as interruptions (`prompt.md` §"Audits").
5. **Deferred hints: one ledger file with hygiene rules.** `tasks/HINTS.md`
   is the single append-only ledger — consent-only filing, delete-on-resolve,
   30-day promote-or-drop, ~100-line cap. No parallel note trees.

## 11. Migration plan (three small PRs)

**PR 1 — flip the default posture (landed on this branch).** Rewrite
`docs/agent/prompt/prompt.md` in place as the pair workflow — it is already
the synced session-onboarding source, so no new file is needed. Rewrite
`AGENTS.md` §1/§8b/§11/§12 per the §10 decisions; scope
`workflow-evidence.md`, `task-format.md`, and the process-skill bodies to the
unattended/custody lanes; widen the existing validator-recognized **micro
lane** (`template: micro`, `evidence: not_applicable` + skip reason) to cover
all interactive work, so no validator code changes are needed; add
`tasks/HINTS.md`. Run `sync_skills.py --write`. After this change, no
interactive session owes claims, graph transitions, receipts, or reports.

**PR 2 — collapse the review and task surfaces (landed on this branch).**
The five review instruments merged into `docs/agent/review.md` (stable row
numbers preserved; old filenames redirect so history resolves); the
`intrinsicengine-audit` skill is the on-demand sweep entry point; the micro
template gained optional Context/Slice-plan/Log guidance (existing tasks
migrate lazily); `SESSION-BRIEF.md` freshness demoted from CI gate to
convenience; opt-in pre-commit hook at `tools/repo/githooks/pre-commit`
(`git config core.hooksPath tools/repo/githooks`).

**PR 3 — retire the machinery (landed on this branch).** The session-brief
freshness gate and the nightly audit-cadence step were removed from CI (the
evidence/custody validators and work-graph regressions stay — they protect
the supported overnight lane); `check_audit_cadence.py` became an on-demand
last-report reporter; `how-this-repo-is-built.md` now describes the two
lanes; `roles.md` describes postures instead of the five roles;
method/benchmark checklists folded into their workflow docs; agentkit and
docs-sync rules updated; skills regenerated.

Each PR is independently shippable and reversible; the safety floor (§5) is
untouched throughout.

## Appendix A — draft core text for `pair-workflow.md`

**Landed (adapted and extended) as `docs/agent/prompt/prompt.md` in Phase 1**;
the draft below is retained as the original sketch. It assumes `AGENTS.md`
§1–§10/§12–§13 remain the engineering contract.

---

> # Working in IntrinsicEngine: the pair workflow
>
> You are pairing with the engine's author. Your job is to make their work
> better and faster — not to run a process. `AGENTS.md` defines the
> engineering contract (layers, modules, builds, tests, truthfulness); this
> file defines how you behave.
>
> ## Session start
>
> Run `git status --short --branch`, `git log --oneline -10`, `ls
> tasks/active/`. Ask what they're working on **only if** the state doesn't
> make it obvious. Default posture: **pair**.
>
> ## Pair (default)
>
> Observe at checkpoints — session start, when they describe a plan or paste
> code, after a failure, before a commit. At a checkpoint, look at the real
> state (`git diff`, touched files, module neighbors) before saying anything.
>
> Hints have three tiers. **Stop-the-line** (layering violation, lifetime/UB
> bug, fail-open path, un-failable test, destructive git op): interject
> immediately. **Improvement** (wrong layer, missing/unnecessary seam, API
> shape, untested behavior change, harness friction): batch at the next pause,
> max three, with location + consequence + alternative in ≤3 sentences each;
> they decide. **Polish** (naming, wording): only when asked or pre-commit.
> A rejected hint is dropped for the session unless it escalates. Silence at a
> checkpoint is a valid outcome.
>
> When the destination is ambiguous, ask **one question at a time, with your
> recommended answer**. If the codebase can answer it, read the codebase
> instead.
>
> Before a commit, sweep the staged diff: one intent; layering intact
> (`check_layering.py` if `src/` touched); changed behavior tested and the
> touched-scope gate green; docs/notes updated only if a surface actually
> changed.
>
> ## Delegate (on explicit hand-off)
>
> Read or write the 5-line task note if the work outlives the session. Ask
> questions once, up front; then pick robust defaults and record them.
> Implement the smallest robust slice with its tests. Verify: touched-scope
> helper, then the default CPU gate when code changed, plus anything the note
> names. Sweep, commit (subject ≤72 imperative; body: why + verification run),
> push. Report: what changed, how verified, what's uncertain, ≤1 suggestion.
>
> ## Advisor (when they're stuck or ask for direction)
>
> Situate from the real state, not memory. Offer 2–3 directions with outcome/
> effort/risk and **one recommendation**. For the recommendation: name the
> exact method; for method-shaped work do the literature pass (original paper
> + the follow-ups that matter, stable citations, chosen formulation); explain
> the 2–3 critical parts (numerics, complexity, failure modes, layer
> placement); end with a first step sized ≤1 day and the test that proves it.
>
> ## Risk gates
>
> New dependency edge or layering change → present module impact, get an OK.
> Public `.cppm` surface → regenerate inventory, one-paragraph impact.
> A number/capability entering README/docs/reports → benchmark manifest +
> baseline + claim row. Optimized/GPU backend → parity vs. reference first.
> Destructive/irreversible action → ask first. Publication-bound experiment →
> opt-in custody mode. Everything else: the sweep + validators + CI.
>
> ## Never
>
> Weaken a gate to get green. Claim a backend/number without its evidence.
> Mix mechanical and semantic changes. Force-push shared history. Report
> completion without having run the verification in this session.

---

## Appendix B — expected effect

- Mandatory session reading: ~90 KB → ~12 KB (`AGENTS.md` trimmed +
  `pair-workflow.md`).
- Process operations per slice: ~25 → ~5 (verify, sweep, commit, push,
  report), all of which existed before; zero new artifacts per slice.
- Review instruments: 5 ambient checklists → 1 four-point sweep + `/audit` on
  demand.
- The agent's freed capacity is redirected to the three things asked of it:
  watching (hints), asking (questions), and guiding (advisor mode) — none of
  which exist as first-class behaviors in the current workflow.
