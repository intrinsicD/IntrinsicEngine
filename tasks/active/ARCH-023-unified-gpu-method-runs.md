---
id: ARCH-023
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
  - method.engine-integration
---
# ARCH-023 — Shared GPU method execution

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
Implement ARCH-022's resident job/preview target across existing method entry points. Reuse StepRuns,
GpuTransactionLifecycle, GpuPropertyResidency, GPU workspaces and parallel primitives; keep algorithm
math in its owner. A separate compute submission lane, a common lifecycle and reusable neighborhood
queries are the shortest path to cheap new methods and responsive existing ones.

## Plan
1. Q5/Q9: route method compute submissions to an available separate compute queue, with explicit
   cross-queue ordering, sharing/ownership, completion, failure and shutdown behavior. Single-queue
   hardware keeps the same contract with truthful diagnostics. Never merely change the queue handle.
2. Q4/Q6/Q7: extend the existing run driver and transaction owners, then migrate iterative and
   one-shot callers. One logical Step may contain multiple bounded GPU submissions. Run uses the
   same steps; Accept publishes one current generation with one undo entry; discard does not free
   in-flight resources. Preserve CPU and hybrid algorithm contracts and unsupported combinations.
3. Q5/Q7: decouple compute progress, metric collection and latest-complete preview. Use completion-
   protected output leases, skip unavailable intermediate previews, retain terminal results. Share
   validated execution options; account for resident workspace resources and budget/reuse safely.
4. Q4/Q8/Q9: expose reusable resident neighborhood queries with explicit brute-force and LBVH
   strategies, nearest/kNN/radius semantics, deterministic ties, self exclusion, bounded output and
   overflow reporting. Reuse current kernels/geometry contracts; migrate real consumers.
5. Q1–Q9: method coverage matrix, executable method template and common contract tests; verify
   real deterministic GPU paths, CPU parity, stale results, cancellation, preview-off equality,
   unload and buffer reuse. Performance measurements require their own experiment pre-flight.

## Acceptance criteria
- [ ] Method compute uses a separate available queue and correct GPU-to-GPU ordering; fallback is reported.
- [ ] All existing method entry points have the common lifecycle or a documented, tested necessary exception.
- [ ] CPU, GPU and hybrid Run/Step agree; pause/stop/accept/discard have common semantics across UI and MCP.
- [ ] Preview cannot stall computation or expose incomplete/reused data; disabling it preserves results.
- [ ] Shared run configuration validates and round-trips; requested/actual backend and hybrid traffic are visible.
- [ ] Resident state survives steps and waits for acceptance; safe workspace/buffer reuse obeys resource budgets.
- [ ] Brute-force and LBVH GPU queries are reusable by multiple methods through one checked contract.
- [ ] New methods have a small working template and shared behavior tests; existing numerical contracts remain intact.
- [ ] Focused CPU and deterministic real-GPU tests pass, new tests fail without their mechanisms, final independent review approves the exact diff.

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
Target selection is expanded from the registered source inventory for each finished block; never
claim a command below ran before recording its actual output. Queue/RHI and broad interface changes
may require the full CPU gate; local method slices use focused targets.

```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

## State
Planning and independent counter-review. Clean baseline dbe5312f3; main's ci-linux-clang passes,
ci-docs run 42406828027 fails at strict markdown links (pre-existing; investigation separate).
No implementation or GPU/performance evidence yet. Review packets/logs stay in ignored
tasks/evidence/ARCH-023/. All acceptance boxes describe work still required.
