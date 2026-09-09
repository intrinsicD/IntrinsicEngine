---
id: BUG-178
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive build-tooling incident; the ICP implementation passes with ccache disabled, while the incremental cause needs isolated reproduction."
contract_schema: 1
contracts: []
contract_review: "No new engine contract is proposed; this note investigates the existing Clang/preset/module-cache verification path."
---
# BUG-178 — Clang 23 crashes during an incremental module rebuild

## Goal
- Isolate the incremental Clang 23 module-serialization crash observed during ICP integration, distinguishing cached artifacts and overlapping/edited build inputs from a compiler source defect.

## Evidence
- Intermediate cached builds crashed while serializing `std::basic_string_view<char>` in `Runtime.EditorWorkspaceAttachment.Detail.cppm`, `Runtime.EditorWorkspaceSnapshots.Models.cpp`, and `Runtime.EditorWorkspaceSession.cpp`. Logs: `/tmp/icp-build2.log`; Clang emitted `/tmp/Runtime-2331a4.cpp` and its shell reproducer.
- A subsequent preset rebuild with `CCACHE_DISABLE=1` passed, followed by the full CPU cohort and real Vulkan registration checks. No speculative source workaround was added. The precise cache-versus-build-input cause is not established.
- [ICP review](../../../docs/reviews/2026-09-08-icp-spatial-integration.md) records the successful verification and bounded diagnosis.

## Acceptance criteria
- [ ] Reproduce with one stabilized source surface and exactly one writer per preset tree, comparing enabled/disabled ccache and a fresh tree.
- [ ] Record the compiler/cache cause or a bounded non-reproduction; preserve the existing strict module/toolchain gates.
- [ ] Apply a regression-backed tooling fix only if the isolated evidence requires one; otherwise document the verified build discipline.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicCpuTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```
