---
id: ARCH-024
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation; reviewed experiment packets and result records apply separately to performance runs
contract_schema: 1
contracts:
  - repo.task-contract-discovery
  - repo.source-documentation
  - runtime.engine-compilation-locality
  - runtime.spatial-query-locality
  - runtime.kernel-interface-locality
  - runtime.processing-compilation-locality
  - geometry.property-coherence
  - geometry.element-domain-sources
  - method.engine-integration
---
# ARCH-024 — Complete shared plugin execution and measure its performance

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


10. "Ja bitte. Dann mach 1-4. und anschließend erstelle bitte tasks für jedes Plugin welches noch nicht vollständig damit ausgestattet ist. Jedes neue Plugin soll außerdem gleich damit ausgestattet werden. Die übrigen Methoden werden noch zu Plugins migriert."

## Goal
Complete the operator-approved next steps after ARCH-023: integrate its reviewed implementation,
finish RUNTIME-355, make GPU Init/Accept/preview completion independent of rendering, and measure
performance through a reviewed bounded experiment. Then record each existing plugin's remaining
gaps and make the shared execution path the tested default for newly authored plugins. This serves
ARCH-022's resident job/preview target; the remaining method-to-plugin migrations keep their owners.

## Plan
1. Q9/Q10: obtain green required CI and integrate PR #1046; preserve its known real-GPU evidence
   and truthful skipped-check reporting. BUG-260 owns the earlier HTTP-504 bootstrap failure.
2. Q6/Q7/Q8/Q10: finish RUNTIME-355 in bounded blocks: K-Means reuses WorkerGpuPages, remove
   obsolete queued query/OwnerSteps paths and update their tests, align direct execution arguments,
   and add the outstanding discriminating removal tests or record concrete masking limits.
   Preserve QueueGpuCompute and GpuTransactionRecording; remove unreachable MayPreviewAt/Previewed
   calls together with Smoothing's always-true force parameter. Direct execution arguments require
   wait:false on waiting run tools; wait:true with such fields is rejected, never silently ignored.
3. Q5/Q7/Q10: reuse the runtime owner-thread idle/completion work and transfer CollectCompleted
   so GPU Init, preview visibility and Accept progress without BeginFrame/Present. Keep ECS,
   callbacks and publication on the owner thread; preserve rendering-only capture requirements,
   frame fences, shutdown ordering and input lifecycle. One private nonblocking completion helper
   serves minimized paths and BeginFrame-refused exits; preserve the existing Phase 10 ordering
   inline. Minimized engines always execute RunMinimizedFrameWork, including no-Idle-hook engines:
   command drain, both event pumps, readiness/completion drain and reap. The helper explicitly calls
   CollectCompleted(), then DrainGpuQueueCompletedTransfers(), then nonblocking device maintenance
   on minimized and BeginFrame-refused paths; minimized K-Means must reach Accept.
   No public pump API, WaitIdle, or artificial frame-index advancement. Add a device-owned const
   actual-frame-retirement query plus nonblocking device maintenance. The query distinguishes an
   open recording frame, submitted frame fences (main, async compute and transfer), and stamps
   that name no submitted frame. Unknown devices and device-lost fence queries conservatively
   retain resources; defaults never claim unknown completion. Never-submitted stamps are retired only with no corresponding
   open frame; submitted frames retire only after every relevant fence signals. Apply this to
   residency and method-scratch retirement, deferred GPU deletes and handle-pool reclamation;
   keep TickGeometryResidency frame-only. A held graphics frame must retain its referenced resources
   while unrelated completed compute-only resources retire. Reclassify compute agent tools apart
   from screenshot/raster operations. Verify preview-on completion at depths 2 and 3 with at least
   four iterations before Accept, no Idle hooks, cancellation, stale input, shutdown and held fences.
4. Q5/Q9/Q10: reuse existing public method benchmark APIs and schema-v2 manifests. Prepare a
   full-tier experiment packet for throughput, control latency, memory/traffic, brute-force/LBVH
   and preview-off/double/triple buffering. Freeze resolved controls, budgets, repetitions and
   quality gates; get independent Claude and Codex reviews before any results-bearing run. Produce
   raw-derived HTML reporting, uncertainty and limitations, including negative outcomes. Start with
   a bounded <=30-minute GPU campaign if the reviewed design supports useful detection; no speed
   claim follows from deterministic smoke tests. Add a shared validated preview buffer count (2/3)
   for live-preview paths through config, UI and agent readers; terminal-only rings keep documented
   fixed capacities. This realizes the operator's requested double/triple comparison.
   Engine-level measurements belong at the existing tests/benchmark seam, driven/sealed by the
   benchmark tooling; do not add runtime/ECS/private-Vulkan imports to benchmarks/ as precedent.
   Observe actual queue diagnostics through that seam. Compare only controls actually supported;
   reference quality, null/timing controls and observed preview drops accompany throughput.
5. Q8/Q10: after steps 1–4, use the source-backed plugin inventory to create or refine one task
   for each incomplete plugin, avoiding duplicate migration owners. Extend the existing plugin
   scaffold/authoring contract and executable checks so a new method plugin starts on the shared
   lifecycle/config path. Unsupported GPU or preview capabilities must remain explicit and tested;
   fixture-only plugins and non-method extensions are classified separately.

## Reuse and boundaries
Existing owners: Runtime.StepRuns, Runtime.WorkerGpuPages, GpuTransactionCore,
Runtime.SpatialIndexCache prepared query sessions, Engine::RunMinimizedFrameWork,
VulkanTransferQueue::CollectCompleted, validated execution schema/readers, method/plugin templates
and current benchmark runners. No additional scheduler or generic plugin framework is planned.
Serena resolves WorkerGpuPages but misses module-body engine symbols; rg and caller reads cover
that search gap. Algorithm mathematics and existing accepted element domains stay unchanged.

## Acceptance criteria
- [ ] PR #1046 integrated after its required checks pass; BUG-260 outcome recorded.
- [x] RUNTIME-355 criteria independently verified and its task retired.
- [ ] GPU Init, preview completion and Accept progress on the owner without rendering, with
      deterministic CPU and real-Vulkan tests for cancellation, stale input and shutdown.
- [ ] Reviewed performance campaign executed with matched controls, valid machine-readable results,
      raw-derived HTML report, uncertainty and clearly bounded conclusions.
- [x] Every actual plugin is inventoried; each incomplete plugin has one scoped task or explicitly
      refined existing owner, with necessary exceptions and missing evidence distinguished.
- [x] New method-plugin creation uses the shared execution/configuration path by default and
      executable checks reject bypasses while preserving explicit capability exceptions.
- [ ] Independent final review accepts the exact implementation, test evidence, report and task coverage.

## Engine integration
| Surface | Contract |
| --- | --- |
| Least-structured input | Preserve existing typed input/property domains and checked conversions. |
| Compatible entity sources | Preserve the current mesh, graph and point-cloud element-domain support. |
| Backend | Truthful requested/actual/fallback; no unimplemented GPU backend advertised. |
| RuntimeModule | Runtime composes services; graphics has no live ECS access; one owner publishes. |
| Config/agent | UI, agent and files share validated execution options and capabilities. |
| UI | Shared run controls; buffer count only where a device ring previews; one undo entry on Accept and no intermediate CPU publication. |
| Publication | Revision-safe Accept, one undo action; discard never publishes or prematurely frees. |
| Preview | Latest completed output; skipped preview never stalls compute or changes results. |
| Plugins | New adapters reuse host services; pending method migrations preserve these contracts. |
| End-to-end tests | Common lifecycle tests, no-render GPU tests, removal probes and reviewed measurements. |

## Verification
Select affected executables from build/ci/test-inventories/RegisteredTestSources.tsv before each
block. Focused suites include StepRuns, GpuTransactionLifecycle, SpatialIndexCache, clustering,
RuntimeModule/AgentOperations and plugin/template contracts; real Vulkan checks exercise the
changed paths. Benchmark commands, controls and stop conditions belong to their frozen packet.
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_source_rules.py --self-check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## State
Independent Opus plan review (session d411cdf5-56d8-4802-b3f7-95a8b515410e) returned REVISE:
actual frame-fence retirement, all non-render exits, agent classification and a selectable buffer
count were missing. Its delta additionally identified scratch/deletion/handle-pool retirement,
no-Idle completion draining, unchanged Phase 10 order and the explicit UI contract. The plan above
incorporates these corrections and the final explicit transfer-drain call. The reviewer approved
all other deltas and stated approval applies once that one-line call/test addition is present
(session d411cdf5-56d8-4802-b3f7-95a8b515410e, plan-delta2); no layer exception is proposed.
RUNTIME-355's bounded implementation blocks can proceed under that review. The source-only scouts
found six method plugins plus a fixture; their per-plugin gaps are inputs to step 5, not completed work.
PR #1046 remains unmerged. At b3b718e98, full CPU, pr-fast, docs and release CI passed;
sanitizer fixture fixes (BUG-262) are reviewed and pushed at `f9bcae578`; docs, release and pr-fast checks are green; the CPU/sanitizer workflow is still running. Vulkan routing is corrected
(BUG-261), exposing BarrierValidationFailed in two operational fixtures; scope clarification is
pending under the operator’s drift rule. No failing gate was waived.
The unchanged-source baseline passed 236 focused CPU contract cases on commit a2b36352a.
Temporary packets/logs live in ignored tasks/evidence/ARCH-024; no measurement has started.
The query retirement passed 47 focused CPU cases. All six new F5 tests failed on the old
implementation before its integration. R355 removal probes failed as predicted and passed after
source restoration. The combined CPU build passed. Of 392 focused checks, three fixture/schema expectations
needed correction; their corrected paths and the plugin integration passed 93 focused checks.
The CPU/core Owner and minimized-completion removal probes failed exactly as predicted and
passed after restoration. Removing a startup-plugin scenario failed the inventory check;
restoring it passed both startup-plugin tests. Source-rule self-checks passed (273 rules).
The four affected Vulkan test targets built successfully after correcting an optional-result
access in the new consolidation test. The 62-case GPU block passed without skips, including
minimized K-Means, queue ordering and resource-retirement/error paths. The additional minimized
consolidation case passed with Vulkan farthest-point initialization and both preview capacities.
The full CPU build exposed exception use and missing imports in the new measurement harness;
its explicit-status correction passed independent source review, the full build and three
deterministic C++ controls. Full CPU execution passed 4,520 cases; two cases skipped for unavailable comma-decimal locale
and sanitizer instrumentation in the unsanitized preset. Native compute-wait and
completion-publication removal probes failed as predicted; both passed again after exact
source restoration on the separate NVIDIA queue.
Claude Opus reached its weekly quota; independent Codex agents reviewed subsequent engineering
blocks. The performance implementation design is reviewed, but final experiment preflight still
requires the unavailable Claude review as well as a fresh Codex review of the frozen packet.

The six-plugin default and performance harness passed independent Codex source reviews.
The harness passed nine deterministic Python self-checks; no performance measurement ran.
All 22 first-seed final-shape technical cases passed on the optimized NVIDIA build. These are
known-answer checks, not measurement evidence. All three new C++ evaluator controls failed
with their mechanisms removed and passed after exact restoration.
Six independently reviewed follow-ups are filed as RUNTIME-356 through RUNTIME-361 in the
runtime backlog. RUNTIME-360 owns shared metric curves and RUNTIME-361 consumes them;
existing method-to-plugin GPU migrations remain separately owned.
Its preview experiment measures publication under a rendered triangle workload, not consumption
of the method preview by the renderer. Final experiment preflight remains outstanding.

## Engineering checkpoint review (2026-11-08)
Independent reviewer `/root/agent_execution_review` returned: **“APPROVE: safe to commit and
push this explicitly incomplete ARCH-024 engineering checkpoint.”** Reviewed manifest
`893334ec33917c5a0277e392d2038a1cbdb21fe9a117b7c2e67844af996c05fc`, tracked diff
`d032ad6505392542cc62ce5c458619055f413d6fd1635d27e6e08ad1d4392ee7`; implementation
commit `b7e83f13d`. The reviewer accepted plugin inventory/follow-up ownership and the
new-plugin default criteria. It did not approve PR #1046 merge, BUG-263/264 closure,
performance results or overall task completion. Full CPU CI at `f9bcae578` also passed;
separate hosted sanitizer gates were still running at this checkpoint.

## PR restart correction (2026-11-09)
The PR review on `037ff5dca5` identified retained CPU farthest-point coordinate vectors
after Restart. The existing restart/prefix/undo test now uses nonuniform property weights
and a matching weighted reference; it failed after Restart before the correction. The
existing restart callback clears all three vectors after destroying the sieve, retaining
capacity. All 29 PointSamplingOperations/ResidentPointSampling cases pass after the fix.
No new execution abstraction, API or GPU algorithm is introduced.
