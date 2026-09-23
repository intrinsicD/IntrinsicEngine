---
id: BUG-211
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive CI prerequisite repair with retained failure and shader producer validation
contract_schema: 1
contracts: []
contract_review: CI package provisioning only; no engine, dependency ownership or verification-policy change.
---
# BUG-211 — Provision the shader compiler in pr-fast

## Goal

Fix the pre-existing clean-runner prerequisite omission exposed by the shader
changes in [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044).
The touched-scope runner requests `IntrinsicShaderOutputs`, but CMake creates
that real producer only when `glslc` is available. The fast workflow omitted
the compiler already required by the shared session setup.

## Acceptance criteria

- [x] Install `glslc` with the fast workflow's existing system prerequisites.
- [x] Configure the `ci-fast` preset, build the real shader producer and check
  that the changed shader's SPIR-V output exists.
- [x] Run the existing touched-scope, prerequisite and workflow regressions;
  retain the original CI failure without weakening shader validation.

## Verification

```bash
cmake --preset ci-fast
cmake --build --preset ci-fast --target IntrinsicShaderOutputs -j2
python3 tools/repo/check_shader_outputs.py --dir build/ci-fast/bin/shaders --require property_texture_bake.frag.spv --require property_texture_bake.vert.spv --require uv_view/background.frag.spv
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tests/regression/tooling/Test_CiPrerequisiteGuards.py
python3 tests/regression/tooling/Test.WorkflowRouting.py
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
```

The original [CI failure](../evidence/BUG-211/ci-pr-fast-failure.log) is retained.
Local verification uses the installed Vulkan SDK compiler; the clean GitHub
runner verifies Ubuntu package provisioning after the repair is pushed.

## Completion

Retired 2026-09-23 at the CI-maintenance endpoint.
PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044),
the enclosing CI prerequisite and scheduling repair commit.

The ci-fast shader producer built all 114 shader outputs; all three existing modified entry points passed explicit output checks. Touched-scope (37), prerequisite (7), routing (11) and concurrency (20, retained under BUG-212) regressions passed. Actual Claude Opus 5.5 reviewed the package repair and corrected local proof. Clean-runner package provisioning remains for the pushed remote CI run.

[Shader output check](../evidence/BUG-211/shader-outputs.log),
[final Claude review](../evidence/BUG-212/claude-ci-final-review.md), and
[concurrency regression](../evidence/BUG-212/workflow-concurrency-after.log)
retain the results.
