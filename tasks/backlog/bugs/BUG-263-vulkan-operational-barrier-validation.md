---
id: BUG-263
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: hosted CI failure tracking; cause and correction are not established
contract_schema: 1
contracts: []
contract_review: Records a CI failure only; implementation scope awaits the operator's drift decision.
---
# BUG-263 — Investigate operational Vulkan barrier validation in CI

## Operator direction (verbatim)
1. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Restore the required Vulkan gate for [PR #1046](https://github.com/intrinsicD/IntrinsicEngine/pull/1046),
without weakening validation or treating an unavailable execution path as successful.

## Observed failure
After BUG-261 corrected display routing, head `b3b718e98` reached the operational Xvfb batch in
[run 42488643692, job 128755942233](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42488643692/job/128755942233).
`IntrinsicLopFamilyGpuBenchmarkSmoke.Run` and `RUNTIME269VulkanSparseSolveBenchmark.Run`
failed with `VulkanRequestedButNotOperational`, status `RequestedButIncompleteGate`, reason
`BarrierValidationFailed`; their dependent validators could not run. The cause is not yet known. The same two failures recur at `f9bcae5789e6d5770bc2ce5440e1b26e95250cc2`
in [run 42516133462, job 128839443086](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42516133462/job/128839443086).

This is a newly exposed failure layer. The operator's AGENTS.md requires a scope decision
before further diagnosis or correction; that question is pending. Other ARCH-024 work continues.

## Acceptance criteria
- [ ] Operator authorizes diagnosis/correction of this failure layer.
- [ ] Identify the failing validation check from the exact CI path and reproduce or establish its cause.
- [ ] Apply a reviewed correction with a discriminating regression check; preserve validation gates.
- [ ] Fresh required Vulkan CI executes the affected fixtures successfully.

## Verification
Inspect the cited job's operational-fixture output and the next exact-head Vulkan CI run.
Source inspection or skipped local GPU tests cannot satisfy the final criterion.
