---
id: RUNTIME-356
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
# RUNTIME-356 — Point spacing: prove the complete GPU plugin adapter

## Operator questions and direction (verbatim)
1. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
2. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
3. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Complete the remaining plugin-boundary proof for ARCH-022/ARCH-024 (Q1–Q3).
The common startup test covers CPU lifecycle/owner identity. The existing plugin GPU smoke
starts in Run; the complete GPU one-shot script currently exercises the shared editing service
directly. Reuse that script through this plugin's actual tool and validated config.

## Acceptance criteria
- [ ] Through the plugin tool/config adapter, prove Ready has zero steps and no publication; Step, Restart, Run and Accept preserve one-shot semantics and add exactly one undo entry.
- [ ] Exercise that adapter with the existing minimized-completion fixture: no Idle hook or manual render pumping, pending device work stays retained, and Accept/Discard remain revision-safe.
- [ ] Preserve the existing cancellation, stale-input, unload, eight-domain and CPU-reference checks; demonstrate the new test fails when the adapter bypasses the shared run protocol.
- [ ] Record actual backend/queue execution and skipped coverage truthfully; update the GPU coverage matrix without claiming unmeasured speedups.

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
No new numerical implementation or scheduler is indicated. The fused scalar GPU kernel is
LBVH-only; terminal-only scalar output needs no invented live-preview controls.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(PointSpacingPlugin|StartupPluginExecution)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -R '^PointSpacingPluginGpuSmoke\.'
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The GPU criterion requires actual execution on a capable host; a skip is unchecked coverage.
Extend the focused selector for any newly touched shared owner/UI suite. New regressions must
fail without their mechanism and pass after restoration. Full/sanitizer gates follow AGENTS.md.
