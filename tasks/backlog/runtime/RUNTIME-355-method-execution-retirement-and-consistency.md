---
id: RUNTIME-355
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation; diff, focused tests and CI; no performance claim without reviewed measurement
contract_schema: 1
contracts:
  - repo.task-contract-discovery
  - repo.source-documentation
  - runtime.spatial-query-locality
  - runtime.kernel-interface-locality
  - runtime.processing-compilation-locality
  - geometry.property-coherence
  - geometry.element-domain-sources
  - method.engine-integration
---
# RUNTIME-355 — Retire method execution leftovers and align optional controls

## Operator questions and direction (verbatim, 2026-11-07)
1. "Wie sind die aufgebaut?"
2. "Sind die unterschiedlich?"
3. "Haben die eine ähnliche Architektur?"
4. "Kann und sollte man das vereinheitlichen?"
5. "Kann man das so aufbauen dass es extrem performant ist?"
6. "Für alle Methoden im Prinzip (bis auf notwendige Ausnahmen) gleich funktioniert und gleich konfiguriert werden kann?"
7. "Dass init, step, run, accept, dismiss, lockerer preview (double oder triple buffering) usw. Gleich funktionieren?"
8. "Dass es eine Schablone/Template für alle weiteren Methoden sein kann damit es konsistent performant ist und nicht immer wieder die gleichen Fehler gemacht werden?"
9. "Ja, genau so stelle ich mir das vor. Bitte implementiere eine separate compute queue und die anderen Punkte und sorge dafür dass diese Ziele erreicht werden und am Ende alle Methoden (auch später neue) so Funktionieren, existierende Verfahren und Methoden wiederverwendet werden können (als Beispiel: Nachbarschafts queries auf der gpu in verschiedenen Varianten von brute force bis gpu-lbvh). Nutze Claude Opus als subagenten ausgiebig dafür"


## Goal
Address the non-blocking F1, F2 and F4–F6 findings from the final ARCH-023 independent Opus review.
The migrated methods already use the shared run lifecycle; this follow-up removes remaining
internal duplication and unused paths, and makes startup and optional tool arguments more uniform.

## Acceptance criteria
- [ ] F1: Move K-Means' page submission/wait/readback onto WorkerGpuPages, preserving mid-run reseed ordering, stateful page planning, preview tokens and retirement.
- [ ] F2: Remove the four superseded owner-thread QueueGpu nearest/kNN/radius APIs and migrate their test coverage to prepared query sessions; retain no compatibility facade.
- [ ] F4: Remove unused OwnerSteps branches and GpuTransactionStepAllowance/Stepped/MayPreview helpers from the shared transaction core, preserving coverage of current worker paths.
- [ ] F5: Make optional direct execution arguments consistent between start tools and nonwaiting run tools through the existing validated configuration path.
- [ ] F6: Add focused removal probes for session-upload waiting and Abandon clearing Valid, Commit and Prepared; cover the other removal cases listed as unprobed in the coverage matrix's Verification limits, or record why a case cannot be probed. Preserve asserted predictions and restored-green checks.

## Engine integration
| Surface | Required treatment |
| --- | --- |
| Least-structured input | Existing method-specific plain typed views; spatial queries require float3 samples and live-row IDs, not mesh provenance. |
| Compatible entity sources | Preserve mesh vertex/edge/halfedge/face, graph node/edge/halfedge and point-cloud domains wherever the method currently accepts them. |
| Backend | Preserve CPU reference and hybrid variants; separate compute queue when supported; report actual path. |
| RuntimeModule | Existing method bindings use common run/step/accept/discard and resident ownership; preserve logical step boundaries. |
| Config/agent | Same validated execution options and controls as UI, round-trip config and truthful diagnostics. |
| Publication | Preserve domain, unrelated properties and topology for same-cardinality outputs; revision-safe publication. |
| UI | Shared job controls and preview, one accepted undo command, no intermediate CPU publication. |
| End-to-end tests | Existing domain/integration tests plus common lifecycle and actual GPU known-answer checks; no benchmark claims from smokes. |

All currently supported domains remain supported. New query variants operate on plain GPU views;
runtime owns element-slot mapping. No integration surface is deferred outside this task.

## Verification
Bound the affected suites from the registered source inventory before implementation. Use the
existing StepRuns, transaction, query-session, clustering, consolidation, CPD and agent-schema
contracts plus deterministic real-Vulkan tests; no performance claim or new benchmark is implied.
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_source_rules.py --root . --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Review context
F3 (worker GPU initial ordering and CPD Vulkan-subsample worker stepping) was implemented and
independently verified inside ARCH-023; it is not deferred to this task.

The final ARCH-023 reviewer (Opus session `3bed78aa-f0ae-4a1b-bf64-1407f84919d8`, source tree
`ee4ed57b0cd4c9951d31993427ade413889cb63d`) classified these as non-blocking. K-Means already
shares the run protocol; its local transport loop predates the common worker-page helper.
The owner-thread queue methods and OwnerSteps are no longer used by production method callers.
The [coverage matrix](../../../docs/methods/gpu-method-coverage.md) records these limits explicitly.
