---
id: RUNTIME-357
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
  - geometry.element-domain-sources
  - geometry.property-coherence
  - method.engine-integration
---
# RUNTIME-357 — Kernel density: close adapter and lifecycle boundaries

## Operator questions and direction (verbatim)
1. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
2. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
3. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Close the remaining kernel-density plugin boundaries under the existing shared default
(Q1–Q3). Basic CPU Ready/Step/Restart/Accept/Discard is already tested centrally. Reuse the
point-spacing boundary fixtures and existing one-shot script instead of duplicating that test.

## Acceptance criteria
- [ ] Add the complete real-GPU one-shot lifecycle through this plugin's validated section and tool, including minimized completion and one undoable Accept.
- [ ] Test interactive run_control Accept after workspace detach and unloading a waiting CPU run: no publication, no retained callbacks after drain, and no duplicate completion.
- [ ] Round-trip nondefault start/auto_accept settings and prove an argument naming only start preserves the section's auto_accept for that run without persistent mutation.
- [ ] Keep existing domain/reference/overflow checks and add discriminating removal proofs for the newly covered boundaries; update the coverage matrix.

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
The common startup lifecycle remains its single owner. The fused GPU kernel stays LBVH-only,
and terminal-only results do not expose live-preview fields.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(KernelDensityPlugin|StartupPluginExecution)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -R '^KernelDensityPluginGpuSmoke\.'
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The GPU criterion requires actual execution on a capable host; a skip is unchecked coverage.
Extend the focused selector for any newly touched shared owner/UI suite. New regressions must
fail without their mechanism and pass after restoration. Full/sanitizer gates follow AGENTS.md.
