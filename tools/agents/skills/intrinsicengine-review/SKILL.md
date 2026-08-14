---
name: intrinsicengine-review
description: Review procedures for IntrinsicEngine under the pair workflow. Owns the four-point pre-merge sweep (scope, layering, tests, docs), the retirement maturity-closure rules, and the risk-gated deep reviews — the architecture checklist (layering/ownership, right-sizing, lifetime, concurrency, failure states, shader push-constant compatibility) and the clean-workshop scorecard for boundary-touching changes. Use this skill before committing or reporting completion for a non-trivial change, when asked "is this ready to merge", when a change touches a dependency boundary, module ownership, renderer subsystem/pass, RHI/platform/runtime wiring, or a layering-allowlist entry, or when retiring a task. On-demand audit sweeps (output/drift/hints) are owned by `intrinsicengine-audit`.
---

# IntrinsicEngine Review

One reference owns review: `references/review.md` (mirror of
`docs/agent/review.md`). This skill routes into it.

## The pre-merge sweep (every change)

Answer four questions against the staged diff — findings are hints or fixes,
not a gate:

1. **Scope** — one intent; no mixed mechanical/semantic edits; no drive-by
   cleanup; one task per PR unless explicitly batched.
2. **Layering** — `AGENTS.md` §2 flow intact; run
   `python3 tools/repo/check_layering.py --root src --strict` when `src/` is
   touched (module imports + CMake link edges).
3. **Tests** — changed behavior tested; labels correct; strongest relevant
   subset run in this session; pass/fail from the CTest run just executed.
4. **Docs/notes** — updated only when a surface or structure changed; `.cppm`
   synopses present; comments state invariants, not history; inventories
   regenerated after module-surface changes; claim-shaped statements route
   through `AGENTS.md` §8b.

Optional hook: `git config core.hooksPath tools/repo/githooks` runs the cheap
deterministic subset at commit time.

## Retirement closure

Name the reached maturity level (`Scaffolded` → `ParityProven`); scaffold
language names its follow-up or pins the endpoint in `Non-goals`; an
`Operational` claim cites the backend/integration-labeled run that actually
executed. Full rules: `references/review.md` §"Retirement closure".

## Risk-gated deep review

Triggered by the risk gates in the pair workflow (new dependency edge, public
`.cppm` surface, renderer/pass changes, wiring changes, allowlist edits):

- **Architecture review** — `references/review.md` §"Deep review:
  architecture": layering/ownership, right-sizing (P1), config lane (P3),
  recipe-driven frames (P5), lifetime/concurrency/failure states, and the
  shader push-constant compatibility trap for renderer/pass changes.
- **Clean-workshop scorecard** — `references/review.md` §"Deep review:
  clean-workshop scorecard": eight rows scored `pass | finding | n/a`; every
  finding produces a follow-up task ID. Bundle:
  `tools/ci/run_clean_workshop_review.sh . --strict`.

## Audits

On-demand sweeps (output audit, drift audit, hints triage) are owned by the
`intrinsicengine-audit` skill and documented in `references/review.md`
§"Audit sweeps" — run them when asked or as overnight jobs, never as ambient
per-PR duties.

## Unattended completions

Overnight-lane retirements additionally owe enrolled evidence per
`intrinsicengine-task-workflow` (`workflow-evidence.md`); interactive work
owes the sweep only.
