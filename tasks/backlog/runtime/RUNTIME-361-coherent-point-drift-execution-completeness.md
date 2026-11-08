---
id: RUNTIME-361
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
# RUNTIME-361 — CPD plugin: persist configuration and initialize on the worker

## Operator questions and direction (verbatim)
1. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
2. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
3. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Finish CPD's advertised shared execution/configuration path (Q1–Q3). The current plugin is
CPU rigid CPD with a GPU-backed position preview. It has no persistent section, maps points and
initializes its solver synchronously at start/restart, and exposes a hold_step_ms test hook.
Reuse the current solver, host edit capture and StepRuns; do not port the runtime GPU solver here.

## Acceptance criteria
- [ ] Add a persistent validated section for source/target, typed position binding, tolerance and shared execution options. Files, config tools, plugin tools and UI use one validate/apply path; overrides affect only the started run.
- [ ] Align batch/start/readiness tool conventions with the existing common contract and RUNTIME-355; keep truthful CPU solver versus device-preview reporting. Remove the startup test's config-only exception after real round-trip/admission coverage passes.
- [ ] Move world-space mapping and Solver::Initialize into worker initialization for every generation, including Restart; preserve required owner-thread capture/admission, current transform restrictions, cancellation and stale-input checks.
- [ ] Remove hold_step_ms from the public request/schema or move the needed deterministic test control behind an existing test seam; no production sleep option remains.
- [ ] Prove preview-on/off and pause/resume result equivalence, no intermediate publication, one undoable Accept, and resource-safe discard/unload on CPU and the existing device-preview route.
- [ ] Consume the shared metric curves owned by RUNTIME-360 without a second plot implementation. Config/worker work may proceed independently of that UI substep.
- [ ] Update method-template/workflow exception references and the coverage matrix. Record reuse of the existing runtime CPD GPU E-step when that method migrates under ARCH-022; do not advertise an unimplemented plugin GPU solver.

## Engine integration
| Surface | Required treatment |
| --- | --- |
| Least-structured input | Existing finite typed point-set properties; retain algorithm-specific normals or paired source/target requirements. |
| Compatible entity sources | Preserve all currently supported compatible mesh, graph and point-cloud element domains; no provenance/name narrowing. |
| RuntimeModule | Existing plugin registration and host editing/StepRuns adapters own readiness, work and lifetime. |
| Config/agent | One persistent validated section and canonical execution controls; actual backend and necessary exceptions remain explicit. |
| UI | Existing plugin window/readiness path uses shared run controls and only supported preview options. |
| Publication | Preserve same-cardinality position output on the originating typed domain, unrelated properties/topology, revision safety and one Accept undo entry. |
| End-to-end tests | Existing compatible-domain tests plus the specific real plugin-tool/config boundary cases above; actual GPU execution is distinguished from CPU mocks and skips. |

## Necessary exceptions and ownership
The current solver's CPU-only backend is explicit. Runtime CPD GPU integration remains with
the method-to-plugin migration owner ARCH-022 and existing METHOD-056 implementation; it is
not a second algorithm port. RUNTIME-360 owns shared curves, without gating independent CPD work.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(CoherentPointDriftPlugin|StartupPluginExecution)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -R '^CoherentPointDriftPluginGpuSmoke\.'
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The GPU criterion requires actual execution on a capable host; a skip is unchecked coverage.
Extend the focused selector for any newly touched shared owner/UI suite. New regressions must
fail without their mechanism and pass after restoration. Full/sanitizer gates follow AGENTS.md.
