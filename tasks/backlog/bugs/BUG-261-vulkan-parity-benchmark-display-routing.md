---
id: BUG-261
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: deterministic workflow-routing regression; no performance claim
contract_schema: 1
contracts: []
contract_review: CI display routing only; no engine layering, method or task-schema change.
---
# BUG-261 — Route Vulkan parity benchmark fixtures through the virtual display

## Operator direction (verbatim)
1. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Complete approved step 1: restore PR #1046's full Vulkan CI without weakening a gate.
Move the existing smoothing and sparse-solve parity benchmark producer/validator pairs into
the existing lavapipe/Xvfb selection; retain the displayless unavailable-environment checks.

## Acceptance criteria
- [x] Routing regression rejects the old workflow and passes with both complete fixture pairs selected.
- [x] Independent review accepts the exact diff.
- [ ] A fresh full Vulkan CI run executes the routed fixtures successfully.

## Observed failure
[PR #1046](https://github.com/intrinsicD/IntrinsicEngine/pull/1046), head `a2b36352a`:
[run 42483079585](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42483079585/job/128739029966)
built all promoted targets successfully on 2026-11-08. In the displayless CTest batch,
GEOM081VulkanPropertySmoothingBenchmark.Run and RUNTIME269VulkanSparseSolveBenchmark.Run
invoked tests that correctly skipped because GLFW found no supported platform. Their sealing
wrappers then failed because no raw JSON existed; both dependent validators were not run.
The following virtual-display batch was consequently skipped. This is routing failure,
not evidence of a numerical or GPU execution failure.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tests/regression/tooling/Test.WorkflowRouting.py
python3 tests/regression/tooling/Test.CiTiming.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The routing test must fail on the previous regex. Inspect the fresh GitHub Vulkan run's
virtual-display step before closing; local source checks alone do not establish CI success.

## Current verification
The old regex failed the added routing assertion; the corrected workflow passed all 20
WorkflowConcurrency checks. WorkflowRouting, CiTiming, strict task policy and strict doc links
passed. Independent Opus review `1bdd2e82-ed33-4f75-ba1c-eb678875f180` approved the exact two-file content in
patch digest `fa7041f64ec4f460a82e4e73371adb3ad2f65cdc10cdf740890aebb9fea696b3`;
the reviewer did not recompute the digest or run tests. Actual hosted Vulkan execution remains open.
