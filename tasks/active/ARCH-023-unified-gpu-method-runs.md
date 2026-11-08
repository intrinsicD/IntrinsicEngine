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
  - geometry.element-domain-sources
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

## Reviewed implementation decisions
- StepRunsService is the single control owner. GpuTransactionCore supplies resident output,
  asynchronous Commit/Accept and release hooks to owner-driven step runs; accept-only transactions
  have no dummy compute step. K-Means does not acquire a second run around its existing driver.
- Shared execution options cover live preview, cadence, iteration limit and automatic acceptance;
  backend/strategy availability and method precision remain explicit. Preserve each method's
  numerical defaults and ranges, the iterative methods' execution defaults and current-format
  round trips, without compatibility aliases or a universal method-shape registry.
- One-shot execution defaults (coordinator's implementation choice, 2026-11-08, under the
  operator's Q6/Q7/Q9 request that every method's lifecycle work and be configured alike; the
  operator did not choose the value): every one-shot section and interactive start defaults to
  `kOneShotStepRunExecution` (start `ready`, auto_accept `never`); explicit batches (Apply entry
  points, waiting agent calls) use `kOneShotBatchExecution` (run, accept at the end). This
  changes the earlier compute-at-once interactive one-shot default; it is recorded once in
  `docs/architecture/engine-and-feature-modules.md`. Not a review approval of the task.
- The compute lane reuses the transfer/readback facade but has its own command pool and timeline.
  Upload/frame producers, compute/readback, published-preview visibility, deferred deletion,
  workspace reuse and plugin drain all need actual completion ordering. Existing buffer-family
  concurrent sharing is reused. No blanket render wait on unfinished method computation.
- The raw-buffer budget/cache is a separate block after completion-safe queue integration. Host
  caches may not retain destructors/code from an unloaded plugin. Active storage remains pinned.
- Brute-force queries share the existing nearest/kNN/radius contract but build no GPU hierarchy.
  Two real consumers: ICP and a plugin neighborhood path. Preserve float versus double contracts,
  deterministic ties, original IDs, exclusions and overflow counts. No unmeasured auto selection.
- Separate queue, common run migration and query strategies are implemented by separate Claude
  Opus workers in isolated worktrees; Codex integrates and verifies. Independent final reviewers
  receive the exact combined diff and actual test output. No recursive delegation by reviewers.
- The accepted proposal also requires Run = repeated logical Step, Pause at a safe boundary,
  Stop retaining a valid partial result, one revision-safe undoable Accept, Discard with no
  publication, generation-safe Reset, and optional latest-complete preview that drops rather
  than stalls. Preview-off must preserve results and avoid intermediate copies. GPU metrics
  collection must not silently change convergence semantics.
- All-method scope is the operator's explicit Q9 direction, including one-shot and hybrid
  entry points; it is not narrowed to the minimum required by AGENTS. Use the ARCH-022 R7
  inventory plus current plugins, with code-backed exceptions (graphics-only operations remain
  on graphics; unsupported GPU algorithms remain honestly unsupported). No silent GPU ports.
- Q9 establishes this work's order ahead of ARCH-022's remaining Q95 blocks. RUNTIME-311's
  shared transaction owner and pending lifetime proofs, and GRAPHICS-153's residency/cache
  work, feed this task; those tasks remain open until their own criteria are verified. This
  task is an ARCH-022 implementation slice, not gated on the umbrella's retirement.
- Quantitative performance remains unclaimed. A matched baseline can be run from preserved
  dbe5312f3 in an isolated worktree after pre-flight. Existing numerical behavior and submission
  page budgets stay fixed until the separately reviewed measurement authorizes a change.

### Render-independent progress (independent counter-review)

Opus review of tree `4893383d4ebf7587713ab038d9480a9e9d29c911` returned **REVISE**:
separate submissions still advance only after Present; each page also waits for a fresh graphics
tail. Complete plan step 3 using existing worker StepRuns: wait on actual graphics producer
transfer tickets, synchronize distinct VkQueues independently, add bounded worker-safe completion/
readback consumption, support pending worker-run Accept, and transfer optional GPU previews through
a nonblocking mailbox. Keep ECS, residency mutation and publication on the owner thread. Migrate
K-Means first, then all resident/paged GPU and hybrid owners through that same substrate. Shared
native queues retain a truthful hardware limitation. Tests must prove multiple logical steps
without frames, producer visibility, loose preview, cancellation and shutdown. No measured speedup
is claimed. Exact review/session/packet are in ignored `compute-progress-plan-review` evidence.

## Spatial acceleration review
Existing owners are Graphics.PointLBVH and its shared GLSL includes, composed by
Runtime.SpatialIndexCache. Reuse their checked output ABI and ranking semantics. Add only the
missing explicit exhaustive GPU strategy and resident recording path; do not merge distinct
precision contracts or replace K-Means' fused scan. Queries remain composable without CPU readback.

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
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(StepRuns|GpuPropertyResidency|GpuTransactionLifecycle|GraphicsCrossQueueTimeline|GraphicsQueueAffinity)\.'
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
```

## State
The shared lifecycle and queue implementation has passed deterministic verification and independent
review. Before closing the operator’s full Q9 scope, this task now completes render-independent
initial GPU sampling for consolidation/CPD and worker stepping for runtime CPD with Vulkan subsampling
(the review’s F3). That slice is implemented and deterministically verified; independent Opus review approved
its code, behaviour and verification at tree e031b367d8a5529ce69cf4b5d45022bf5b1374a8.
Overall closure review remains pending. The accepted core is frozen at tree a85668cebdebad9186cb8643230b94f8ac6c6592.
The independent Opus counter-review accepts the new slice's design and ownership, requiring
case-budget synchronization and bounded failure cleanup in the cancellation tests. These corrections
are implemented, including red-before-fix and mechanism-removal checks. The [verification/review note](../../docs/reviews/2026-11-08-unified-gpu-method-runs.md)
records the exact source trees, commands, combined GPU result, corrected fixture, skips and limits.
The [coverage matrix](../../docs/methods/gpu-method-coverage.md) owns the current family inventory;
the [method template](../../docs/methods/method-template.md) owns the reusable adapter pattern.

All production GPU/hybrid families and their CPU counterparts use the shared controls. Raster
texture bake and unsupported GPU requests retain their truthful behavior. Owner-side GPU Init,
preview collection and Accept still require frame transfer collection; worker iterations do not.
The other non-blocking internal simplifications are tracked in
[RUNTIME-355](../backlog/runtime/RUNTIME-355-method-execution-retirement-and-consistency.md).

The factory template is instantiated by nine typed callers in seven engine owner units; one
private erased entry point preserves the opaque cache implementation. Pool, fence and release
logic remain shared. CMake enforces the private host boundary, including a negative plugin-import
probe; the unchanged feature-boundary ceiling passes at 27. No new layer exception is introduced.

The operator's scope includes GPU/hybrid methods and their CPU counterparts. Pure CPU methods
without a GPU counterpart remain outside this implementation. Existing numerical contracts and
page budgets are preserved; no quantitative performance claim is made. Pre-existing documentation
failures are addressed separately by BUG-257 and the operator-approved BUG-258 historical citation
repair. Temporary evidence remains under ignored `tasks/evidence/ARCH-023/` until final acceptance.
