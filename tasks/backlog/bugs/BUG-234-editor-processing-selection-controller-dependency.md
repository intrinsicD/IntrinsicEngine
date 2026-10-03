---
id: BUG-234
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive diagnosed dependency repair; compiler dependency gate and current CPU results provide evidence
contract_schema: 1
contracts: [runtime.processing-compilation-locality]
---
# BUG-234 — Generic editor processing imports the selection controller

## Goal
Restore the existing generic processing compilation boundary without changing GPU-output readiness behavior.

## Context
- Observed 2026-10-03 during the full CPU gate for the six property-inspection/diagnostics tasks: `EditorCompilationLocality.SelectionControllerBorrows` reports `Runtime.EditorProcessing.cpp -> Extrinsic.Runtime.SelectionController`.
- Evidence: `/tmp/intrinsic-mcp-large-full-cpu.log`, 5,808 selected tests, one dependency-check failure and one separate GLFW/LSan skip. The direct import and readiness conversion already exist at baseline `b752164e8`; this file is unchanged in the new feature diff. It is not a stale BMI or behavior-test failure.
- Reuse: `SelectionController::ToEntityHandle` delegates directly to the canonical `StableEntityLookup::ToEntityHandle`. Import that owner and call it directly; preserve its background sentinel and render-ID offset.

## Acceptance criteria
- [ ] Generic processing no longer depends on `Runtime.SelectionController`; the existing compiler dependency gate passes unchanged.
- [ ] GPU-output readiness uses the canonical stable/render-ID conversion with unchanged behavior.
- [ ] Affected runtime build/tests and the combined CPU gate pass; Claude reviews the bounded fix.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(EditorCompilationLocality|AgentOperations|SandboxAgentServer)\.'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
