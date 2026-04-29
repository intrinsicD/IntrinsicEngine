# RORG-133 — Final Reorganization Audit

## Goal
Confirm the repository now matches the final target configuration and enforcement posture defined by the reorganization backlog.

## Non-goals
- Introducing new engine/runtime/graphics features.
- Performing additional source-tree moves in this audit task.
- Rewriting historical migration records beyond factual completion updates.

## Context
This task is the closing audit gate for the IntrinsicEngine reorganization. It validates that structural migration, policy consolidation, CI split, and strict validation tooling are all in place and synchronized.

## Required changes
- Add this audit task file under `tasks/active/` with explicit completion checklist items.
- Track pass/fail evidence for each final-state requirement.
- Record remaining temporary exceptions (if any) with task IDs and owners.

## Tests
- Run strict task policy validation on `tasks/` metadata/sections.
- Run top-level expected-layout check in strict mode.

## Docs
- Keep this audit task synchronized with `tasks/active/0000-repo-reorganization-tracker.md` final-state status.

## Acceptance criteria
- [ ] Root layout matches target.
- [ ] `src_new/` removed.
- [ ] `src/legacy/` exists and is documented as temporary.
- [ ] `src/geometry/` is canonical geometry root.
- [ ] `methods/` exists with schema and template.
- [ ] `benchmarks/` exists with schema, smoke runner, and docs.
- [ ] `tests/` has unit/contract/integration/regression/gpu/benchmark/support.
- [ ] `docs/` has index, architecture, adr, methods, benchmarking, agent, migration, api.
- [ ] `tasks/` is structured and validated.
- [ ] `tools/` is categorized.
- [ ] Workflows are split and readable.
- [ ] PR template exists.
- [ ] AGENTS.md is canonical.
- [ ] CLAUDE/Copilot/Codex do not duplicate policy.
- [ ] CI strict checks enabled.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_expected_top_level.py --root . --strict
```

## Temporary exceptions
- None currently recorded.

## Owner
- Repository maintainers / architecture review rotation.
