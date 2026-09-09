---
id: BUG-181
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive publication check found an existing tooling command mismatch; no engine or research result."
contract_schema: 1
contracts: []
contract_review: "Task concerns the existing CI planner and shader-output checker CLI; no engine ownership, method or geometry contract changes."
---
# BUG-181 — Touched-scope shader-output command uses an unsupported flag

## Goal

Make the touched-scope planner invoke shader-output verification with a configured build output directory after the shader producer has run.

## Observation — 2026-09-09

The local plan for the geometry integration diff emits `python3 tools/repo/check_shader_outputs.py --root .`. The checker accepts `--dir` and optional `--require`, and exits 2 for that emitted command because `--dir` is missing. `tools/ci/touched_scope.py` constructs the unsupported `--root` argument in the `shader_outputs` route. This precedes implementation commit `3276c7059`; it was not introduced by the geometry changes.

The direct check `python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require lbvh_query.comp.spv --require lbvh_build.comp.spv` succeeds and finds 118 outputs after the canonical build. Publication used this direct check and the full CPU gate. The planner itself still needs correction, including verifying producer ordering for a fresh build tree.

## Acceptance criteria

- [ ] Generate the supported checker arguments using the selected build directory.
- [ ] Ensure the check runs after the selected shader producer rather than before configure/build on a fresh tree.
- [ ] Cover generated command arguments and ordering in the planner regression tests.

## Verification

```bash
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/agents/check_task_policy.py --root . --strict
```
