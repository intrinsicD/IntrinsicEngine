# Review

One document owns review in this repository. Interactive work gets the
**pre-merge sweep**; risk signals add the **deep sections**; the **audit
sweeps** run on demand, preferably overnight. The five previous instruments
(`review-checklist.md`, `architecture-review-checklist.md`,
`clean-workshop-review.md`, `agent-output-review-checklist.md`,
`drift-audit-checklist.md`) were merged here by the 2026-08 pair-workflow
redesign; their filenames remain as redirects so history keeps resolving.

## The pre-merge sweep (every change)

Answer four questions against the staged diff. Findings are delivered as
hints or fixed on the spot — the sweep is a conversation, not a gate.

1. **Scope** — one intent; mechanical moves and semantic edits not mixed; no
   drive-by cleanup outside the selected work; one task per PR unless
   explicitly batched.
2. **Layering** — dependency flow follows `AGENTS.md` §2; no cross-layer
   convenience imports; runtime wiring stays in `runtime`; run
   `python3 tools/repo/check_layering.py --root src --strict` when `src/` is
   touched (it covers both C++23 module imports and CMake
   `target_link_libraries(...)` edges).
3. **Tests** — changed behavior has a test; labels follow the taxonomy in
   `AGENTS.md` §7 / `tests/README.md`; the strongest relevant verification
   subset was run **in this session**, focused targets first; pass/fail read
   from the CTest run just executed, never from
   `Testing/Temporary/LastTestsFailed.log`; evidence build trees satisfy the
   C++23/Clang-20 requirement.
4. **Docs and notes** — docs/task notes updated only when a surface or
   structure actually changed; new or materially changed `.cppm`/headers have
   a short synopsis; comments state current invariants, not task history
   ([source documentation policy](source-documentation-policy.md)); generated
   inventories refreshed after module-surface changes; no unsubstantiated
   performance claims (a claim-shaped statement routes through `AGENTS.md`
   §8b).

Optional local hook: `tools/repo/githooks/pre-commit` runs the cheap
deterministic subset automatically — enable with
`git config core.hooksPath tools/repo/githooks`.

Unattended overnight completions additionally owe the enrolled evidence their
profile requires ([workflow-evidence.md](workflow-evidence.md)); interactive
work does not.

## Retirement closure

When a task moves to `tasks/done/`:

- Name the reached maturity level ([task-maturity.md](task-maturity.md):
  `Scaffolded`, `CPUContracted`, `Operational`, `ParityProven`, `Retired`) in
  the task status block, retirement commit, or completion summary.
- "Scaffold", "stub", "fail-closed", or "minimal" language in the closing body
  either names the follow-up task for the next maturity level or pins the
  scaffold as the intended endpoint in `Non-goals`.
- An `Operational` claim for rendering, Vulkan, asset ingest, hot reload, pass
  command bodies, runtime composition, or legacy retirement cites the
  backend-labeled or integration-labeled run that actually executed — CPU
  contract coverage alone does not prove `Operational`.

## Deep review: architecture (risk-gated)

Run when a change touches dependency boundaries, module ownership, source
layout, runtime wiring, or architecture docs — after presenting the
module-level impact and getting the explicit human OK required by the risk
gates in `docs/agent/prompt/prompt.md`.

**Layering and ownership**

- Owning layer/subsystem is explicit; new dependency edges are justified by
  the `AGENTS.md` §2 table (not merely allowlisted); no lower layer imports a
  higher layer; `runtime` remains the composition root.
- Allowlist entries carry `task`/`expires` metadata
  (`python3 tools/repo/check_layering_allowlist_quality.py --root . --strict`).
- No new public API exposes a higher-layer type to a lower layer (no
  ECS/Runtime types in a `graphics` public `.cppm` surface).

**Right-sizing and control surfaces**

- New abstraction surface (interfaces, forwarding facades, factories,
  registration/scheduling frameworks, event/command hops, dependency structs)
  has a *present* second caller, layering boundary, test double, or
  config/UI/agent variant axis (`AGENTS.md` §5 P1; the
  `intrinsicengine-right-sizing` skill owns the keep-list). Anything flagged
  as ceremony carries a right-sizing plan or follow-up task.
- New engine-tunable state is reachable from config files and agent/CLI paths,
  never UI-only, and round-trips through the validated preview-then-apply lane
  (P3). Frame/composition changes stay expressed as recipe data with a
  readable phase-list main loop (P5).
- New parallelizable algorithms declare their backend axis: a CPU/GPU hook
  exists or GPU execution is explicitly deferred to a task ID.

**Lifetime, concurrency, failure**

- Ownership model explicit (value, handle, unique owner, borrowed view); no
  hidden cross-system lifetime coupling; shims tracked with removal task IDs.
- Threading model explicit for touched paths; shared mutable state has a
  named synchronization strategy; no new blocking on hot paths without
  justification.
- Failure states are explicit and deterministic; new failure modes carry
  actionable diagnostics; fallback behavior is documented and truthfully
  reported (requested/actual/fallback).

**Shader push-constant compatibility** (renderer/pass changes)

- For any new or modified pipeline whose pass body calls
  `cmd.PushConstants(&pc, sizeof(pc))`, the selected shaders MUST declare a
  `layout(push_constant)` block mirroring the pushed struct byte-for-byte,
  with matching descriptor-set expectations. The CPU/null gate only proves the
  call was issued; a real Vulkan run silently reinterprets mismatched bytes.
  Never feed `RHI::GpuScenePushConstants` bytes into the legacy
  `assets/shaders/surface.{vert,frag}` / `surface_gbuffer.frag` /
  `shadow_depth.vert` pairs. See `src/graphics/renderer/README.md` ("Shader
  push-constant compatibility policy") for the GpuScene-aware inventory.

## Deep review: clean-workshop scorecard (risk-gated)

Run when a change alters a dependency boundary, adds a renderer
subsystem/member/pass, changes RHI/platform/runtime wiring, closes a
`Scaffolded`/parity task, or edits `tools/repo/layering_allowlist.yaml`.
Score each row `pass | finding | n/a`; a `finding` never blocks by itself but
must produce a follow-up task ID — never a bare TODO.

| # | Check | Evaluate by |
| --- | --- | --- |
| 1 | Promoted layer imports match `AGENTS.md` §2 | strict layering check clean; new edges allowed by the table, not just allowlisted |
| 2 | CMake target links match layer policy | new `target_link_libraries(...)` edges respect the §2 table |
| 3 | No public API exposes higher-layer types downward | inspect added `.cppm` export surfaces |
| 4 | Renderer growth lands in an owning seam | new renderer members/subsystems have a named owning seam, not another god-object field |
| 5 | New passes use typed IDs | frame-graph passes routed by typed identity (`FramePassId`), never stringly-typed lookup |
| 6 | Recipe dependencies are resource-driven | new recipe edges derive from resource read/write deps, or the explicit-ordering reason is recorded |
| 7 | Scaffold/parity closures have a maturity follow-up | the `Scaffolded` closure rule in [task-maturity.md](task-maturity.md) |
| 8 | Temporary exceptions have owner + expiry | every allowlist row and shim marker names an open removal task (`AGENTS.md` §13) |

Command bundle (wraps existing validators, adds no gate):

```bash
tools/ci/run_clean_workshop_review.sh . --strict
```

Record the scorecard in the change's review notes or
`docs/reviews/<YYYY-MM-DD>-clean-workshop-review.md`
(example: [2026-06-06](../reviews/2026-06-06-clean-workshop-review-example.md)).

## Audit sweeps (on demand — prefer overnight)

There is no fixed audit cadence (2026-08-14 decision). Run a sweep when the
operator asks, before release-shaped work, or as an unattended overnight job.
Findings land as `tasks/HINTS.md` entries or backlog/`BUG-` tasks with
evidence — never as ambient duties. Write dated reports to
`docs/reports/<YYYY-MM-DD>-<sweep>-audit.md`;
`python3 tools/agents/check_audit_cadence.py` reports last-report dates on
request. Remediation happens in follow-up tasks, not inside the audit.

### Output audit (window of agent-authored commits)

Pick a window (`git log --pretty=format:"%h %ad %s" --date=short --no-merges
--since=<START> --until=<END>`); score each row
`pass | findings | not-applicable`; budget ≤ 60 minutes — tighten rows rather
than relax the budget. Row numbers are stable for reports.

1. **Silent scope creep** — diffs outside the task's declared scope. Find:
   `git diff --stat <merge-base>..<head>` vs. the task note. Fix: follow-up
   task or separate revert.
2. **Decorative comments** — comment blocks narrating what names already say.
   Find: added comment blocks in the window's C++ diff. Fix: docs-touch note
   for the next toucher.
3. **Premature abstraction** — interface/wrapper/factory with a single
   implementation and no second caller in sight. Find:
   `grep -nE 'class I[A-Z]|virtual .*= 0;'` on added modules. Fix: fold-back
   task, or record the justifying seam.
4. **Documented-but-not-tested** — a doc/README claim with no test that would
   fail if it regressed. Find: docs diffs asserting behavior vs. `tests/`
   diffs. Fix: add the test or weaken to factual current state.
5. **Defensive validation at internal boundaries** — checks on trusted
   internal inputs with no contract or observed failure. Find: new
   `if (!ptr) return;`-style pre-checks before internal helpers. Fix: remove
   and regression-test the upstream contract; keep genuine boundary guards.
6. **Untracked compatibility shims** — shims/aliases without a §13 exception
   record and removal task. Find:
   `git grep -nE 'TODO|FIXME|deprecated|legacy|shim|backcompat|temporary'` on
   touched files, cross-checked against `tasks/active/`. Fix: add the record
   or remove the shim.
7. **Ceremony without shipped value** — a whole window of task-file shuffling
   with zero engine behavior change. Find: classify window commits
   (code / docs / task-only). Fix: surface to the operator; unblock real work.
8. **Half-finished seams** — new public symbols with no non-test caller.
   Find: `git grep -n '<symbol>'` per new export. Fix: wire the consumer or
   delete the seam.
9. **Aspirational docs without `(planned)`** — future state asserted as
   present. Find: present-tense claims in docs diffs vs. current source. Fix:
   add the marker, weaken the claim, or land the code.

### Drift audit (whole current tree)

State-scoped complement to the output audit; budget ≤ 45 minutes; sampling is
acceptable where noted. Row numbers are stable for reports.

1. **Generated-inventory drift** — fresh
   `python3 tools/repo/generate_module_inventory.py --root src --out /tmp/inv.md`
   differs from the committed inventory.
2. **Allowlist exception drift** — an allowlist row whose `task:` owner is
   retired or whose `expires:` elapsed
   (`check_layering_allowlist_quality.py --strict` plus an owner-liveness
   pass).
3. **Active-task branch drift** — `tasks/active/*.md` referencing merged PRs
   or vanished branches.
4. **Stale `(planned)` markers** — `git grep -n "(planned)" -- 'docs/**'
   'src/**'` naming features that have since landed.
5. **Aspirational claims without markers** — present-tense doc claims
   `git grep` cannot locate in source (sampling;
   `python3 tools/docs/check_docs_sync.py --root .` first).
6. **Dead public seams** — exported `.cppm` symbols with only test/self
   callers (sampling).
7. **Untracked TODO/shim markers** — bare `TODO|FIXME|XXX|HACK` or
   `shim|backcompat|temporary` bridges under `src/` without a task ID;
   `TODO(TASK-ID)` forms and plain technical uses ("temporary staging
   buffer") pass.
8. **Naming inconsistency** — one concept split across spellings
   (sampling probes).
9. **Cross-doc anchor rot** — links whose target file exists but whose
   `#anchor` was renamed (sampling; `check_doc_links.py` catches file-level
   rot).

### Hints triage

Sweep `tasks/HINTS.md`: delete resolved/obsolete entries; promote entries
older than 30 days to real task files or drop them; keep the ledger under
~100 lines.

## Method and benchmark review

Domain-specific review checklists live with their workflows:
[method-workflow.md](method-workflow.md) §"Review checklist" and
[benchmark-workflow.md](benchmark-workflow.md) §"Review checklist".

## Related

- [`/AGENTS.md`](../../AGENTS.md) — engineering contract (§2 layering, §5
  coding, §7 testing, §8b claims, §12 review, §13 exceptions).
- [prompt/prompt.md](prompt/prompt.md) — postures, hint tiers, risk gates.
- [task-maturity.md](task-maturity.md) — maturity taxonomy.
- [workflow-evidence.md](workflow-evidence.md) — unattended/custody evidence.
- `tools/agents/skills/intrinsicengine-right-sizing/SKILL.md` — the
  justified-complexity keep-list.
