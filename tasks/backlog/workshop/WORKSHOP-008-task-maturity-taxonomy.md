# WORKSHOP-008 — Add task maturity taxonomy for scaffold, capability, parity, and retirement

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
- [ ] Define a task maturity taxonomy in `docs/agent/task-format.md` or a linked doc:
  - `Scaffolded`: structure/API exists but behavior may be stubbed or incomplete.
  - `CPUContracted`: CPU/null/backend-neutral contract tests exist.
  - `Operational`: concrete backend/real runtime path works under appropriate labels.
  - `ParityProven`: non-legacy path has tests/evidence matching legacy behavior or an explicit non-goal decision.
  - `Retired`: legacy path/shim removed and docs/inventory updated.
- [ ] Add optional but recommended `## Maturity` field guidance, or add maturity checklist expectations under existing sections without breaking validator compatibility.
- [ ] Update task template under `tasks/templates/` if present.
- [ ] Update agent review checklist so agents state maturity explicitly when closing rendering/runtime/asset/backend tasks.
- [ ] Update docs around `tasks/done/` expectations so completed scaffold tasks must name follow-up gates when capability is incomplete.
- [ ] Add a lightweight check or documentation rule that `tasks/done` entries with words like scaffold/stub/fail-closed/minimal must include a follow-up task reference or explicit non-goal statement.
- [ ] Apply the taxonomy to the main rendering/Vulkan migration docs without mass-editing every task.

## Tests
- [ ] Update `tools/agents/validate_tasks.py` only if the new maturity field becomes required; otherwise keep compatibility.
- [ ] Add focused validator tests if new task-policy checks are introduced.
- [ ] Run strict task policy validation.

## Docs
- [ ] Update `docs/agent/task-format.md`.
- [ ] Update `docs/agent/review-checklist.md`.
- [ ] Update `docs/agent/architecture-review-checklist.md` if architecture-impacting task closure should include maturity.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` language to use maturity terms for rendering/Vulkan/runtime where helpful.

## Acceptance criteria
- [ ] Agents have a standard vocabulary for partial completion versus full capability.
- [ ] New tasks can be closed without implying false parity.
- [ ] Rendering/Vulkan docs clearly distinguish scaffolded contracts from operational backend behavior.
- [ ] Task policy checks remain green.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Do not require mass migration of all old task files unless validator changes demand it.
- Do not mark unfinished capabilities as parity-proven.
- Do not use maturity taxonomy to weaken acceptance criteria.
- Do not edit code in this task.
