---
id: RUNTIME-359
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
---
# RUNTIME-359 — Descriptor analysis: reserve every output atomically

## Operator questions and direction (verbatim)
1. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
2. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
3. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Close descriptor-analysis admission under the common execution contract (Q1–Q3).
The run currently reserves Outputs.front() although it publishes 33 columns. Reuse or extend
the canonical runtime output-reservation owner so overlapping writes cannot both be admitted.

## Acceptance criteria
- [ ] Reserve all 33 output columns atomically before work admission, using typed entity/domain/property identities and the shared reservation lifecycle.
- [ ] Refuse two descriptor runs with different first columns but an overlapping later column; also refuse overlap with an independently admitted scalar writer. Failed admission must leave no partial reservations.
- [ ] Release every reservation on discard, cancellation, stale failure and unload; retain atomic publication, checked conversion, one undo and unrelated properties/topology.
- [ ] Preserve the existing common CPU lifecycle and custom GPU Ready/Step/Restart/Run/Accept test. Add focused overlap/release regression and removal proofs through real plugin entry points.
- [ ] Update the coverage matrix and any affected public runtime contract docs; do not build a plugin-private lock registry.

## Engine integration
| Surface | Required treatment |
| --- | --- |
| Least-structured input | Existing finite typed point-set properties; retain algorithm-specific normals or paired source/target requirements. |
| Compatible entity sources | Preserve all currently supported compatible mesh, graph and point-cloud element domains; no provenance/name narrowing. |
| RuntimeModule | Existing plugin registration and host editing/StepRuns adapters own readiness, work and lifetime. |
| Config/agent | One persistent validated section and canonical execution controls; actual backend and necessary exceptions remain explicit. |
| UI | Existing plugin window/readiness path uses shared run controls and only supported preview options. |
| Publication | Preserve all 33 scalar columns on the originating typed domain, unrelated properties/topology, revision safety and one Accept undo entry. |
| End-to-end tests | Existing compatible-domain tests plus the specific real plugin-tool/config boundary cases above; actual GPU execution is distinguished from CPU mocks and skips. |

## Necessary exceptions and ownership
Terminal-only 33-column output has no fictitious live preview. Existing hybrid radius queries
remain LBVH-only; broader query support needs its own justification. Fully GPU FPFH remains
GEOM-092, not an implicit new kernel port in this task.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(DescriptorAnalysisPlugin|StartupPluginExecution)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -R '^DescriptorAnalysisPluginGpuSmoke\.'
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The GPU criterion requires actual execution on a capable host; a skip is unchecked coverage.
Extend the focused selector for any newly touched shared owner/UI suite. New regressions must
fail without their mechanism and pass after restoration. Full/sanitizer gates follow AGENTS.md.
