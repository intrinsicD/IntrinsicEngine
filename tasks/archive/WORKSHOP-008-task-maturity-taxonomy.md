# WORKSHOP-008 — Add task maturity taxonomy for scaffold, capability, parity, and retirement

## Status

- Status: done.
- Completion date: 2026-05-17.
- Branch: `claude/backlog-task-agent-prompt-FTnkb`.
- Commit reference: see the retirement commit on `claude/backlog-task-agent-prompt-FTnkb`.
- Reached maturity level: `CPUContracted` for the taxonomy itself — the
  vocabulary is defined, cross-linked, and exercised by structural docs
  checks; `Operational` adoption (every closure summary uses the taxonomy)
  is an emergent behavior owned by reviewers, not a one-shot gate.

## Goal
- Prevent "done" tasks from hiding unfinished architectural risk by adding a repository-wide maturity taxonomy that distinguishes scaffold completion, CPU contract coverage, operational backend capability, legacy parity, and legacy retirement.

## Non-goals
- Do not reopen or rewrite every historical task.
- Do not block all development on perfect taxonomy migration.
- Do not change code behavior.
- Do not delete legacy modules.

## Context
- The repo has many tasks that correctly complete scaffolding or contract slices, but future agents may misread those as full feature completion.
- This is especially risky for rendering, Vulkan, assets, hot reload, pass command bodies, and legacy retirement.
- A clean workshop needs precise status language so "foundation exists" does not mean "capability shipped."

## Required changes
- [x] Define a task maturity taxonomy in `docs/agent/task-format.md` or a linked doc:
  - `Scaffolded`: structure/API exists but behavior may be stubbed or incomplete.
  - `CPUContracted`: CPU/null/backend-neutral contract tests exist.
  - `Operational`: concrete backend/real runtime path works under appropriate labels.
  - `ParityProven`: non-legacy path has tests/evidence matching legacy behavior or an explicit non-goal decision.
  - `Retired`: legacy path/shim removed and docs/inventory updated.
  *(landed as the dedicated doc `docs/agent/task-maturity.md`; cross-linked from `docs/agent/task-format.md`, `/AGENTS.md` Related expanded docs table, and the per-PR/architecture review checklists.)*
- [x] Add optional but recommended `## Maturity` field guidance, or add maturity checklist expectations under existing sections without breaking validator compatibility.
  *(`docs/agent/task-format.md` documents the optional `## Maturity` section; `tools/agents/validate_tasks.py` is unchanged, so existing tasks remain valid.)*
- [x] Update task template under `tasks/templates/` if present.
  *(`tasks/templates/task.md` and `tasks/templates/method-task.md` carry HTML-comment guidance pointing at the taxonomy.)*
- [x] Update agent review checklist so agents state maturity explicitly when closing rendering/runtime/asset/backend tasks.
  *(`docs/agent/review-checklist.md` gains a "Maturity and closure" section.)*
- [x] Update docs around `tasks/done/` expectations so completed scaffold tasks must name follow-up gates when capability is incomplete.
  *(`docs/agent/task-maturity.md` "`Scaffolded` closure rule" + the per-PR checklist row state the rule.)*
- [x] Add a lightweight check or documentation rule that `tasks/done` entries with words like scaffold/stub/fail-closed/minimal must include a follow-up task reference or explicit non-goal statement.
  *(Chosen form: documentation rule under `docs/agent/task-maturity.md` "`Scaffolded` closure rule", referenced from `docs/agent/review-checklist.md`. The validator is intentionally not extended because the trigger words are domain language that legitimately appears in many tasks; reviewers enforce the rule.)*
- [x] Apply the taxonomy to the main rendering/Vulkan migration docs without mass-editing every task.
  *(`docs/migration/nonlegacy-parity-matrix.md` GRAPHICS-020 retirement-gates readiness column rewritten to use the taxonomy; preamble cross-links the taxonomy doc.)*

## Tests
- [x] Update `tools/agents/validate_tasks.py` only if the new maturity field becomes required; otherwise keep compatibility.
  *(`## Maturity` stays optional; validator unchanged.)*
- [x] Add focused validator tests if new task-policy checks are introduced.
  *(No new task-policy checks were introduced; the closure rule is a reviewer rule.)*
- [x] Run strict task policy validation.
  *(`python3 tools/agents/check_task_policy.py --root . --strict` passes on this branch.)*

## Docs
- [x] Update `docs/agent/task-format.md`.
- [x] Update `docs/agent/review-checklist.md`.
- [x] Update `docs/agent/architecture-review-checklist.md` if architecture-impacting task closure should include maturity.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` language to use maturity terms for rendering/Vulkan/runtime where helpful.

## Acceptance criteria
- [x] Agents have a standard vocabulary for partial completion versus full capability.
- [x] New tasks can be closed without implying false parity.
- [x] Rendering/Vulkan docs clearly distinguish scaffolded contracts from operational backend behavior. *(GRAPHICS-020 retirement-gates readiness column uses the taxonomy.)*
- [x] Task policy checks remain green.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

Verification this session (commands actually ran):

- `python3 tools/agents/check_task_policy.py --root . --strict` — passes.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passes.
- `python3 tools/docs/check_doc_links.py --root .` — passes (no broken
  relative links introduced).
- `python3 tools/repo/check_layering.py --root src --strict` — unchanged
  (no `src/` files were touched; the strict layering state of the tree is
  the same as before this PR, including any pre-existing diagnostics).

## Forbidden changes
- Do not require mass migration of all old task files unless validator changes demand it.
- Do not mark unfinished capabilities as parity-proven.
- Do not use maturity taxonomy to weaken acceptance criteria.
- Do not edit code in this task.
