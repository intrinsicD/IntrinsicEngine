---
id: BUG-181
theme: G
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

- [x] Generate the supported checker arguments using the selected build directory.
- [x] Ensure the check runs after the selected shader producer rather than before configure/build on a fresh tree.
- [x] Cover generated command arguments and ordering in the planner regression tests.

## Verification

```bash
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Plan and reuse — 2026-09-14
Continue the operator-authorized cleanup/open-point iteration alongside BUG-184,
with separate commits and shared final CPU verification. The existing shader
checker accepts `--dir`, while the planner emits `--root` before configure.
The default Null/headless ci-fast tree also lacks any shader-producing target:
existing shader targets are attached to opt-in GLFW/GPU tests or the sandbox.

Reuse `intrinsic_add_glsl_shaders` unchanged for a standalone optional
`IntrinsicShaderOutputs` build target; it shares the helper's existing compiled
shader-set target and does not make CPU executables depend on shaders. The
producer is available only when glslc exists; requesting unavailable compilation
fails rather than accepting stale output. Select it only for shader changes.
One small command constructor will serve printing and execution with the chosen
build directory. Execute the checker after successful build batches and keep the
route unbuilt on output-check failure. No new planner stage, helper file, shader
compiler implementation, renderer behavior or C++ interface is needed.

Tests execute the real checker's CLI against a pre-created output directory
containing unrelated stale output. Only the mocked producer writes the required
changed-source output; missing output and producer failure must fail closed.
Also build the actual target in the supported ci-fast preset and check outputs.
Claude receives a fixed source-only diff for review; no internal task notes.

## Review and implementation — 2026-09-14
The checker command now runs after all selected build batches, takes the chosen
`--dir`, and uses existing `--require` arguments for changed `.vert`, `.frag` and
`.comp` outputs. Successive local change records remove deleted/renamed sources
and retain the destination or restored source. Includes still rebuild through
the existing helper's include dependencies. Shader output targets remain absent
from CTest producer inventories.

Claude reviewed fixed source diffs twice. Fixed the stale-unrelated-output test
gap and clarified standalone plan wording. The existing helper already owns
include invalidation and compiler discovery; no duplicate compiler path or
planner stage was introduced. Source-sharing stayed limited to code/tests.
The suite passes 37 tests, including actual checker CLI failure propagation.
A fresh supported `ci-fast` configure in `build/bug181-shaders` used Clang 23,
Null graphics and Null/headless platform; the actual shader target compiled all
114 current outputs and the checker confirmed the required LBVH outputs.

## Final verification — 2026-09-14
Verified the combined BUG-184/BUG-181 source before separate implementation
commits. Supported Clang 23 preset builds passed with ccache disabled: native
`IntrinsicTests`, isolated ASan/UBSan `IntrinsicRuntimeContractTests`, and the
fresh Null/headless shader target. Full native CPU selected 4,612 cases:
4,611 passed, zero failed, one expected unsanitized GLFW/LSan-only skip.
Focused isolated ASan and UBSan each passed 29 JobService/engine-wiring cases
and all 66 editor mesh-method cases (95 per sanitizer). The original filename
regex did not select the editor suite; the additional runs used its actual
66 source-declared test names. No GPU execution or performance result claimed.

All 37 planner regressions and strict layering, allowlist, task policy,
document links, test layout, root hygiene and skill-mirror checks passed.
No source module interface changed. Four-point review: one intent per source
commit, no new layer edge, behavior covered by failing-before/passing-after
tests, owning docs and task records synchronized.

Local full logs, exact commands, fixed diffs, source hashes and Claude reviews:
`build/analysis/bug184-job-accounting-2026-09-14/` and
`build/analysis/bug181-shader-routing-2026-09-14/`. These are interactive local
verification records, not benchmark or research-claim evidence.
