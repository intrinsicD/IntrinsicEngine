# GRAPHICS-020 — Legacy graphics retirement gates
## Goal
- Define objective gates for retiring legacy graphics, RHI, and runtime rendering modules after promoted graphics/runtime/assets/geometry parity exists.
## Non-goals
- No deletion of legacy modules in this task.
- No implementation of missing rendering features.
- No untracked compatibility exceptions.
## Context
- Owner: graphics migration planning with runtime/assets/geometry cross-links.
- `src/legacy` may contain transitional exceptions only when tracked, time-bounded, and paired with removal tasks.
## Required changes
- Map each retained legacy graphics-related module to a promoted owner, parity task, test gate, or explicit retirement decision.
- Track compatibility shims and removal follow-ups.
- Identify final blockers for deleting or isolating legacy render paths.
## Tests
- Run task policy, docs link, module inventory, and relevant layer/import checks when gates are updated.
## Docs
- Update `docs/migration/nonlegacy-parity-matrix.md` and generated module inventory when required by tooling.
## Acceptance criteria
- Every retained legacy rendering module has a documented owner/task/gate.
- No legacy dependency remains untracked in promoted final layers.
- Deletion readiness can be evaluated mechanically from documented gates.
## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Removing legacy source before parity gates and follow-up tasks are satisfied.
