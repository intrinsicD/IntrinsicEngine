---
id: BUG-262
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: deterministic fixture corrections; no performance claim
contract_schema: 1
contracts: []
contract_review: Test fixtures only; no method, public interface, layering, config or task-schema contract changes.
---
# BUG-262 — Correct worker-method sanitizer fixtures

## Operator direction (verbatim)
1. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Complete approved step 1: restore [PR #1046](https://github.com/intrinsicD/IntrinsicEngine/pull/1046)'s
sanitizer CI by correcting its test doubles and deterministic synchronization, without changing
production behavior, weakening assertions or extending timeouts.

## Observed failures
At head `a2b36352a`, [UBSan job 128739031183](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42483079849/job/128739031183)
reported 43 failing cases from two test-double `memcpy` calls with zero bytes and null span data:
KeypointAnalysisOperations line 463 and PointCloudConsolidationModule line 2221.
The fix skips only the copy for zero bytes; completion delivery and readback consumption remain intact.

[ASan job 128739031478](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42483079849/job/128739031478)
failed `ResidentLop.RunControlStaleAcceptAndInFlightDiscardPublishNothing` because worker cancellation
already delivered its terminal result while the device page remained held. The fixture now waits
for logical cancellation before releasing the page and checks unchanged output/history and one
terminal delivery across that release. Its held-page seam establishes pending device completion;
it does not establish native Vulkan allocation lifetime.

The same job failed `SandboxProcessingPanels.PropertySmoothingSessionSwitchesMethodsSolversAndInputs`
at action 340/402 when its 15-second deadline elapsed. Each click inserted twelve redundant idle
actions, each expanded to three frames. Remove those actions while preserving two ImGui propagation
frames per action, the existing in-flight-job gate, the deadline and every publication assertion.

## Acceptance criteria
- [ ] Both readback fixtures complete zero-byte pages without invalid `memcpy` arguments under UBSan.
- [ ] Discard completes while the device page is held and delivers no duplicate result after its release.
- [ ] The smoothing session completes with every result assertion and its original deadline under ASan.
- [ ] Independent review accepts the exact diff; fresh required sanitizer CI passes.

## Verification
The runtime sources belong to `IntrinsicRuntimeContractTests`; the panel source belongs to
`IntrinsicSandboxEditorIntegrationTests`. Run the following focused checks in the matching presets:
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(KeypointAnalysis|KeypointAnalysisOperations|KeypointResident|ResidentLop)\.|^SandboxProcessingPanels\.PropertySmoothingSessionSwitchesMethodsSolversAndInputs$'
cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci-ubsan --output-on-failure --timeout 60 --no-tests=error --parallel 1 -R '^(KeypointAnalysis|KeypointAnalysisOperations|KeypointResident|ResidentLop)\.'
cmake --build --preset ci-asan --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci-asan --output-on-failure --timeout 60 --no-tests=error --parallel 1 -R '^ResidentLop\.RunControlStaleAcceptAndInFlightDiscardPublishNothing$|^SandboxProcessingPanels\.PropertySmoothingSessionSwitchesMethodsSolversAndInputs$'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
Existing hosted failures supply the before-fix evidence; compare fresh sanitizer output after
the fixes. Source review accepted the fixture changes. The combined CPU build completed and all 93
focused CPU checks passed, including the affected keypoint, resident LOP and smoothing fixtures.
Fresh hosted sanitizer verification remains outstanding; CPU checks do not replace it. The same failures recurred
on head `b3b718e98`; no additional failure class appeared in those sanitizer jobs.
