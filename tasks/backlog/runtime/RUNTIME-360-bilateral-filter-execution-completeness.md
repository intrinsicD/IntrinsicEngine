---
id: RUNTIME-360
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
# RUNTIME-360 — Bilateral filter: prove resume equivalence and share metric curves

## Operator questions and direction (verbatim)
1. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
2. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
3. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Complete bilateral's remaining execution/UI boundaries (Q1–Q3). Its GPU path already uses
shared brute-force/LBVH queries and has pause coverage. Add CPU pause/resume equivalence and
make the existing StepRuns metric history visible through shared run controls.

## Acceptance criteria
- [ ] Prove CPU pause/resume produces the same final result as uninterrupted execution, with preserved iteration accounting, no pre-Accept publication and one undoable Accept.
- [ ] Put metric-curve presentation in the shared StepRunControls owner, using the existing bounded metric history/names. Reuse CPD's existing plotting behavior where compatible; do not add another telemetry store or plotting dependency.
- [ ] Wire bilateral and CPD as the two actual consumers and remove CPD's panel-owned plotting duplication. This task owns the shared curve work; RUNTIME-361 only consumes its result.
- [ ] Retain preview-on/off equivalence, configured double/triple buffering, in-place preview capability, cancellation and stale-input checks; add a discriminating CPU continuation test and a shared UI test.
- [ ] Update the shared-control architecture and GPU coverage matrix. Numerical kernels and query algorithms remain unchanged.

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
Live preview remains limited to in-place position output. Fully GPU bilateral iterations
remain GEOM-095; existing GPU/hybrid query reuse is preserved. Do not require adoption of
CpuKnnRows or IterativeLifecycleScript: neither is an existing facility.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(BilateralFilterPlugin|StartupPluginExecution)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -R '^BilateralFilterPluginGpuSmoke\.'
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The GPU criterion requires actual execution on a capable host; a skip is unchecked coverage.
Extend the focused selector for any newly touched shared owner/UI suite. New regressions must
fail without their mechanism and pass after restoration. Full/sanitizer gates follow AGENTS.md.
