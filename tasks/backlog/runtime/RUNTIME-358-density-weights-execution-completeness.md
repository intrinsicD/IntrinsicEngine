---
id: RUNTIME-358
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive engineering with deterministic contract tests; no performance claim
contract_schema: 1
contracts:
  - repo.task-contract-discovery
  - repo.source-documentation
  - runtime.processing-compilation-locality
  - geometry.element-domain-sources
  - geometry.property-coherence
  - method.engine-integration
  - geometry.support-radius-policy
---
# RUNTIME-358 — Density weights: initialize on the worker and close boundaries

## Operator questions and direction (verbatim)
1. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
2. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
3. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Finish the density-weight plugin's shared execution contract (Q1–Q3). StartGpu currently
resolves an automatic support radius on the owner before submitting the run. Reuse
Geometry.SupportRadius::Analyze and the existing worker initialization path; retain the exact
radius/workload policy. Config persistence exists; this is not a new config implementation.

## Acceptance criteria
- [ ] Move automatic-radius analysis into worker initialization for each generation. Ready does not already run that numerical analysis; Step remains one logical one-shot step.
- [ ] Preserve radius-independent device/budget refusal before expensive analysis, manual-radius semantics, cancellation, stale captures and truthful failure diagnostics.
- [ ] Prove complete GPU Ready/Step/Restart/Run/Accept through the plugin adapter, including minimized completion; retain CPU/reference agreement and exact overflow refusal.
- [ ] Add interactive detach/Accept and waiting-CPU-unload checks, plus nondefault execution persistence and a start-only override preserving auto_accept. Reuse existing fixtures and the common startup lifecycle.
- [ ] Demonstrate worker-thread placement and refusal-before-analysis with deterministic controls; update support-radius-policy and the GPU coverage matrix. No stall improvement is claimed without a reviewed benchmark.

## Engine integration
| Surface | Required treatment |
| --- | --- |
| Least-structured input | Existing finite typed point-set properties; retain algorithm-specific normals or paired source/target requirements. |
| Compatible entity sources | Preserve all currently supported compatible mesh, graph and point-cloud element domains; no provenance/name narrowing. |
| RuntimeModule | Existing plugin registration and host editing/StepRuns adapters own readiness, work and lifetime. |
| Config/agent | One persistent validated section and canonical execution controls; actual backend and necessary exceptions remain explicit. |
| UI | Existing plugin window/readiness path uses shared run controls and only supported preview options. |
| Publication | Preserve same-cardinality scalar output on the originating typed domain, unrelated properties/topology, revision safety and one Accept undo entry. |
| End-to-end tests | Existing compatible-domain tests plus the specific real plugin-tool/config boundary cases above; actual GPU execution is distinguished from CPU mocks and skips. |

## Necessary exceptions and ownership
The fused kernel remains LBVH-only and scalar publication terminal-only. Reuse the same
resolved radius on CPU/GPU; do not introduce a second estimator, cache keyed only by entity/name,
or a new scheduling layer.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(DensityWeightsPlugin|StartupPluginExecution)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -R '^DensityWeightsPluginGpuSmoke\.'
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The GPU criterion requires actual execution on a capable host; a skip is unchecked coverage.
Extend the focused selector for any newly touched shared owner/UI suite. New regressions must
fail without their mechanism and pass after restoration. Full/sanitizer gates follow AGENTS.md.
