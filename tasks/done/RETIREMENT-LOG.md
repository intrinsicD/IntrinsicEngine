# Retirement Log

Append-only narrative record of retired tasks, newest first. When retiring a
task, append its summary block here (see `docs/agent/task-format.md`,
"Retiring a task") instead of editing `tasks/active/README.md`. Links are
relative to `tasks/done/`, which sits at the same depth as `tasks/active/`,
so blocks moved from the old active-README history work verbatim.

## Retired task narratives

[`CI-006`](CI-006-sanitizer-topology-and-test-parallelism.md) — sanitizer
topology isolation retired on 2026-07-17 at `Operational`. Unsanitized, ASan,
UBSan, and promoted-Vulkan presets now use explicit identities and distinct
build/install trees; required CPU jobs share one exact exclusion-only selector,
while performance work stays unsanitized. Fifteen hosted selection reports,
five per CPU identity, each reconciled 26 producers and 4,062 cases to digest
`07c9f615629327c0502cd4aa73c411de41693b88ccc0ea80dbabb624cb6cf08b`.
Five-sample total-time median/p95 seconds were `1771.333/1816.077` for
unsanitized, `2232.778/2288.766` for ASan, and `1733.820/1923.281` for UBSan.
The older `CI-003` populations remain formally non-comparable because their
selectors and sanitizer identities differ, so no causal speedup claim is made.
The Debug nanosecond SLO remains one explicit non-blocking nightly diagnostic
owned by `CI-009`; concurrency calibration remains owned by `CI-008`.

[`BUG-111`](BUG-111-github-artifact-finalization-403.md) — hosted artifact
finalization incident retired on 2026-07-17 with an external-service
disposition. ASan job `87915791947` passed 4,062/4,062 tests and validated its
timing result before `actions/upload-artifact@v4` received intermediary HTTP
403 finalizing that artifact; its immediately following selection upload
succeeded. The bounded `gh run rerun --job` recovery retained SHA `a7ae8e7f`,
reran only ASan as job `87928232616`, and finalized timing artifact
`8412801543` plus selection artifact `8412802000`. Required parity remains fail
closed and the failed attempt is excluded from claim-grade evidence.

[`CI-005`](CI-005-real-touched-scope-pr-fast-gate.md) — the real touched-scope
PR-fast gate retired on 2026-07-17. The new unsanitized Null/headless
`ci-fast` route classifies the merge-base diff before setup, skips C++ work for
structural-only changes, reconciles focused owner producers against a fresh
registry, and fails closed for module, build-system, dependency, unknown, or
planner-error scopes. Five comparable samples per route measured whole-job
median/p95 at 9/10 seconds for docs-only, 217/221 seconds for focused geometry,
and 684/714 seconds for broad fallback. The declared cross-layer smoke passed
its broad post-PR-fast budget at 19.395/19.495 seconds and 0.598% incremental
closure, but the configured focused-owner audit ranged from 17.339% to 62.083%
of the PR-fast graph for the affected owners. Focused admission was therefore
rejected rather than splitting production Runtime solely for CI; the smoke
remains broad-only, and full CPU, sanitizer, and capability gates remain
required.

[`CI-010`](CI-010-cpu-source-coverage-refactor-parity.md) — canonical CPU
source-coverage parity retired on 2026-07-17. The dedicated Clang 20 preset and
manual workflow derive all 26 CPU producers from the configured registry,
reconcile 4,062 selected records with 50 discovery profiles, 26 execution
profiles, 25 GoogleTest XML reports, and 26 `llvm-cov` objects, then publish
line/function/branch/region data and raw diagnostics. Schema-v2 execution
identity permits test-target splits while retaining exact normalized
case-working-directory identity. Hosted run `29575099005` passed at exact
commit `67adc536`, represented 524 production files, covered 70,207 regions
and 34,339 branch arms, and lost zero regions or branch arms under self-parity.
Coverage artifact `8405268613` and timing artifact `8405267687` retain the
claim-grade baseline.

[`BUG-106`](BUG-106-test-gate-capability-routing-drift.md) — the
capability-routing defect retired on 2026-07-17. Twelve CPU/mock sources and
three MockDevice RHI sources now belong to capability-truthful CPU targets,
the real Vulkan readback has a dedicated slow operational target, and the nine
duplicate frame-loop registrations are gone. The canonical registry now
rejects duplicate assertion-source ownership and the reconciler proves exact
target, label, case, source, aggregate, and discovered-test-environment parity.
Hosted Linux run `29565803940` reconciled 25 targets, 4,061 unique cases, and
334 sources before passing 4,062/4,062 selected CTest entries. Hosted Vulkan
run `29565813844` reconciled four fast targets and 41 cases; its operational
JUnit retained exactly three passes, zero skips, and a 2.22-second passing
readback.

[`BUG-107`](BUG-107-backend-target-graph-configure-history.md) — the
configure-history-dependent backend graph defect retired on 2026-07-17.
Platform and graphics-backend defaults now belong to the root before any
consumer, current CI presets pin their intended target identity, and one
configure diagnostic plus timing metadata records the five requested/resolved
identity fields. A real temporary-tree regression compares normalized Ninja
targets, registered targets and labels, CTest names, and cache identity across
fresh, unchanged reconfigure, second-fresh, and deliberate backend-transition
configurations for explicit Null/headless, Vulkan/Glfw, and Auto. The corrected
first clean `ci` tree registers all 32 test targets instead of gaining six
Vulkan targets only when warmed. Both fresh preset aggregate builds passed,
the focused platform/renderer targets passed, and the default CPU-supported
CTest gate passed 3,830/3,830.

[`PROC-027`](PROC-027-validator-rent-audit.md) — the validator rent audit
retired on 2026-07-17 after classifying every Python file in the 57-file
pre-verdict `tools/` snapshot and separately listing 13 shell and 3 CMake
wrappers. The evidence-backed verdict retains 53 tools, keeps
`check_audit_cadence.py` and `module_fanout.py` explicitly report-only, and
retires the repository PR-template checker plus AgentKit's uncoupled
prerequisite scaffold. Their CI, touched-scope, copied-file, common exit-code,
tooling-index, and open-task references were removed together; a regression
pins the conservative generic-tool fallback. AgentKit's end-to-end self-test,
strict task/state/docs/layering/layout/root/skill/workflow checks, the
`IntrinsicTests` build, and the exclusive default CPU-supported CTest gate
passed, with 3,830/3,830 selected cases.

[`DOCS-006`](DOCS-006-agentic-development-narrative.md) — the curated
outside-reader development narrative retired on 2026-07-17. The new
`docs/agent/how-this-repo-is-built.md` explains the backlog-to-archive
lifecycle, dependency-derived convergence view, three skill tiers, validator
ownership, and current CI workflow roles without introducing a second process
contract. Its worked `CI-004` example links each seed, promotion,
implementation, evidence, retirement, and archive commit and explains how to
audit the final claim boundary. Root, canonical documentation, agent-contract,
and skill-index navigation now expose the page. Strict link, docs-sync,
skill-mirror, root-hygiene, task-policy, and task-state gates pass.

[`BUG-088`](BUG-088-benchmark-smoke-hard-timeout-host-contention.md) — the
benchmark smoke lane-classification defect retired on 2026-07-16. Commit
`6856768a` labels the complete CTest fixture pair `slow`, gives its opt-in
runner a 120-second bound, and preserves the Run→Validate dependency. The
dedicated PR workflow now has the matching two-minute runner-step budget,
strictly validates every emitted JSON, and retains the complete result
directory. Seven hosted execution-phase samples justify classification only,
not a performance claim. Exact implementation-head benchmark run `29538352251`
completed the runner in 34.924 seconds, validated and uploaded all 22 results;
CPU run `29538352281` passed 3,789/3,789 without selecting the fixture; docs run
`29538352271` exercised all new policy and negative-validator regressions; and
pr-fast, Vulkan, ASan, and UBSan passed on the same SHA.

[`BUG-105`](BUG-105-runtime-module-ecs-structural-hazard.md) — the runtime
module/ECS structural hazard retired on 2026-07-16. Commit `f0ea3987` gives
every module sim system an implicit structural read because its context exposes
the live world, and marks the three promoted baseline ECS passes that add or
remove components as structural writers through the existing FrameGraph token.
The regression independently pins every baseline writer against an immediate
module reader; the focused selection passed 9/9, the original sanitizer
harness passed 100/100 repetitions, the clean default CPU gate passed
3,791/3,791, and repaired exact-head `pr-fast` run `29531364667` passed in
15m31s. No global lock, serial scheduler mode, new hazard abstraction, or
layering exception was added.

[`BUG-104`](BUG-104-kernel-convergence-regression-stale-snapshot.md) — the
kernel-convergence repository-snapshot regression retired on 2026-07-16.
Commit `fb582056` replaces only the stale `49/28` and `RUNTIME-178` output
assertions with the already-enforced `42/21`, two export imports, 31 public
getter names, and debt-free diagnostic. The Engine interface, checker, policy,
and all 18 synthetic/fail-closed cases are unchanged. The pre-fix suite failed
only the repository snapshot, then passed 19/19 after the correction; the
strict live checker passed independently, and repaired exact-head `pr-fast`
run `29524872998` completed successfully in 17m05s.

[`BUG-081`](BUG-081-warm-configure-budget-runner-variance.md) — the
warm-configure hosted-tail guard defect retired on 2026-07-16. Commit
`d3017621` applies one finite 40-second exact-vcpkg-hit limit across all seven
workflow call sites, derived as the slowest contemporary hosted-context p95
(`30.368 s`) plus a 25% margin and five-second rounding. The canonical policy
retains the raw run URLs, exact cache identities, image mapping, conventional
medians, nearest-rank p95s, and the conservative transfer policy for the
inactive self-hosted GPU context. `time_command.py` and its timing JSON remain
unchanged; the synthetic exact-hit overrun still returns failure, timing and
workflow-policy regressions passed 18/18, and all five repaired-head workflow
runs (including both sanitizer matrix contexts) passed Configure and reached
compilation.

[`BUG-103`](BUG-103-rendergraph-lifetime-test-culls-history-chain.md) — the
render-graph lifetime fixture drift retired on 2026-07-16 at `CPUContracted`.
Commit `566424a5` connects `HistoryRead` to the live `Present` root through the
existing `DependsOn()` contract and pins topological order `{0, 1, 2}`; the
compiler continues to cull disconnected passes and measure lifetimes in live
execution-rank space. The pre-fix test and compiler matched `origin/main`,
ccache-disabled rebuilds reproduced the failure, the corrected exact case
passed 20/20 in both `ci` and `ci-vulkan`, both `GraphicsRenderGraph` selections
passed 57/57, and the repaired promoted Vulkan/GPU label gate passed 286/286.

[`BUG-102`](BUG-102-object-space-normal-bake-layering-test-import-placement.md)
— the object-space bake source-layering test drift retired on 2026-07-16 at
`CPUContracted`. Commit `b17a8bde` updates only the two stale public CPU
request-queue import-placement predicates to match RUNTIME-178's intentional
Engine interface-to-implementation move. Every negative GPU queue ownership,
composition, dependency, and ready-frame-policy assertion remains unchanged,
and the 42-import interface convergence ratchet holds. The exact case passed
3/3 in both presets, `RuntimeEngineLayering` passed 21/21, and the repaired
promoted Vulkan/GPU label gate passed 286/286.

[`BUG-101`](BUG-101-fast-staged-uv-edge-grouping-quadratic.md) — fast-staged UV
edge grouping retired on 2026-07-16 at `Operational`. Commit `25d27068`
replaces the per-incidence vector-wide search with reserved normalized edge-key
lookup while preserving first-seen group and face order, then reuses those
groups for seam recording. Two deterministic geometry regressions plus a real
generated-grid runtime enrichment/close contract passed 3/3. The declared
runner emitted 22 strictly valid results; on one controlled local Clang 23
Debug ASan/UBSan host, the large-grid median changed from 1,031.895895 ms to
555.416594 ms (46.175% lower), normalized scaling changed from 1.726739 to
1.195250, quality L2 remained zero, and the exact output signature matched.
The evidence is explicitly local and is neither a broad claim nor a timing
gate. The aggregate build and default CPU-supported gate passed 3,830/3,830.

[`BUG-100`](BUG-100-manual-geometry-import-blocks-frame-loop.md) — manual
Sandbox geometry import retired on 2026-07-16 at `Operational`. Commit
`56352aa7` routes Mesh, Graph, PointCloud, ModelScene, and Texture2D commands
through the existing queued import lane: route state stays on the frame thread,
decode runs on the worker, and assets/ECS/selection/focus/history apply once on
the bounded main-thread drain. Shutdown now cancels blocked or apply-ready
imports before unregistering application policy. Commit `694b8b3f` updates the
stale-session regression for the queued `Pending` contract while retaining its
detach/reattach rejection proof. Focused runtime/diagnostic coverage passed
5/5, app presentation coverage passed 3/3, the repaired lifecycle test passed
1/1, and the aggregate default CPU-supported gate passed 3,830/3,830.

[`BUG-099`](BUG-099-binary-ply-pointcloud-skips-face-lists.md) — binary PLY
point-cloud list consumption retired on 2026-07-16 at `CPUContracted`. Commit
`bb985f61` adds endian-aware, bounds-checked consumption of non-vertex scalar
and list properties while rejecting negative, floating, overflowing, or
truncated counts and retaining strict vertex-list rejection. Both checked-in
endian fixtures and generated interleaving/malformed cases passed; the focused
binary selection passed 17/17 and the aggregate default CPU-supported gate
passed 3,830/3,830. `ASSETIO-011` retains the real Sandbox `Operational`
workflow matrix.

[`BUG-098`](BUG-098-frame-clock-samples-incomplete-frame-delta.md) — incomplete
frame-delta sampling retired on 2026-07-16 at `Operational`. Commit `c9f7067a`
makes `FrameDeltaClamped()` return the bounded previous completed-frame
duration instead of the few microseconds elapsed immediately after
`BeginFrame()`, while preserving first-frame zero and minimized-window sleep
exclusion. Six clock cases and the real Null-window `Engine::Run()` production-
delay File / Import tooltip path passed 7/7. The aggregate build and default
CPU-supported gate passed 3,830/3,830, with no Vulkan-specific follow-up owed.

[`BUG-094`](BUG-094-model-scene-node-semantics-selection.md) — the model-scene
node-semantics and selection defect retired on 2026-07-16 at `Operational`.
Commit `16c30545` retains the selected glTF scene's deterministic hierarchy,
finite local transforms, and shared primitive prototypes in the CPU asset;
`f16cf7b1` transactionally materializes explicit node and primitive-leaf ECS
entities, preserves repeated instances, and routes every leaf through the
canonical render/select authoring and one aggregate-focus completion;
`353a2fc8` adds the checked-in transformed/instanced fixture and real Vulkan
visibility/readback/picking proof; and `1663bab7` preserves tight orthogonal
TRS spheres while conservatively enclosing affine shear and makes the world
replacement regression count live entities rather than EnTT tombstones. The
complete affected CPU suites passed 44/44, the final promoted-Vulkan import and
existing click-pick pair passed 2/2 without skips on an NVIDIA GeForce RTX 3050
with driver 590.48.01, and the exact-head default CPU-supported gate passed
3,810/3,810 in 615.10 seconds. Assets remains CPU-only, runtime owns ECS
composition, and graphics continues to consume snapshots/views.

[`BUG-093`](BUG-093-file-import-prerequisite-gating-tooltips.md) — the Sandbox
File / Import prerequisite defect retired on 2026-07-16 at `Operational`.
Implementation commit `d97cd893` centralizes route, promoted-importer, and
payload-hint readiness in one runtime evaluator shared by presentation-model
construction and dispatch-time validation. The editable path, six ordered
payload choices, single-payload `Unknown` resolution, ambiguous PLY handling,
incompatible-hint rejection, and recognized-but-unavailable KTX/KTX2 paths now
fail closed without app-owned format policy. The app consumes runtime-owned
disabled reasons through one `AllowWhenDisabled` tooltip convention for the
payload chooser, payload rows, import button, and existing queue controls.
Focused coverage passed 6/6; the integration case ran the real File / Import
window inside Null-window `Engine::Run()`, hovered its disabled import control,
and observed an active tooltip. The aggregate build passed and the default
CPU-supported gate passed 3,792/3,792 in 468.28 seconds; strict structural
checks passed. `ASSETIO-010` retains asynchronous primary/companion-file
preflight, while `UI-037` retains the broader domain-action rollout.

[`BUG-083`](BUG-083-vulkan-sandbox-shutdown-lsan-leaks.md) — the Vulkan
Sandbox shutdown LeakSanitizer defect retired on 2026-07-16 at `Operational`.
Implementation commit `3cb91b98` maps the exact NVIDIA RTX 3050 / driver
`590.48.01` report's 116,425 bytes / 35 allocations without overlap:
113,760 bytes / 18 allocations enter unloaded driver frames through the
push-constant call path, 784 bytes / 14 allocations share the VMA buffer-bind
path, and 1,881 bytes / 3 allocations share
`dbus_connection_send_with_reply_and_block`. The replacement policy contains
only those three function/call-path entries and is applied only by the explicit
Vulkan process runner; broad loader, ICD, unknown-module, pthread, GLFW/X11,
and general GoogleTest suppressions are absent. The runner first requires the
unrelated named 4,096-byte engine leak to produce a direct-leak report and exit
86, then requires a clean Sandbox exit, five renderer-completed samples, and
an operational final device. It executed without skips and passed 1/1 on
NVIDIA in 14.81 seconds and 1/1 on lavapipe in 4.06 seconds. The exact-head
aggregate build and default CPU-supported gate passed 3,788/3,788 in 402.61
seconds, and the original GLFW close path passed 10/10 with empty compiled
defaults and no suppression file. Strict convergence, layering, test-layout,
task, state-link, documentation, root, PR-contract, workflow, skill-sync,
inventory, and diff checks passed. Independent evidence and final reviews
found no unmatched leak family, fail-closed gap, CI-registration defect, or
remaining overclaim. No production source, Vulkan lifetime, runtime behavior,
or backend-selection policy changed.

[`BUG-082`](BUG-082-glfw-x11-input-method-lsan-leak.md) — the GLFW/X11
input-method LeakSanitizer investigation retired on 2026-07-16 at
`Operational`. Implementation commit `fb549c96` preserves the unchanged
production lifetime after debugger evidence confirmed the GLFW 3.4/libX11
1.8.7 teardown reaches `glfwTerminate()`, input-method callback unregister,
and `XCloseIM()` before normal exit. A standalone sanitizer process now wraps
the terminate call and fails unless process-static teardown invokes it exactly
once; its negative control first requires an unsuppressed, named 4,096-byte
engine leak to produce a direct-leak report and exit 86. The dedicated contract
executed on live X11 without a capability skip and passed 1/1, the GLFW
platform intersection passed 2/2, and the original close-before-first-frame
runtime process passed 10/10 with leak detection enabled and no
environment-provided suppression file. The default CPU-supported gate passed
3,788/3,788 in 398.51 seconds. Strict task, test-layout, layering,
documentation, state-link, and diff checks passed, and independent review
found no remaining teardown-order, registration, fail-closed, or right-sizing
blocker. No production source, backend selection, module surface, XIM
suppression, or global sanitizer policy changed.

[`BUG-092`](BUG-092-scene-lifecycle-async-wait-frame-budget.md) — the
frame-count-only asynchronous scene lifecycle wait retired on 2026-07-16 at
`CPUContracted`. Implementation commit `627641fa` replaces the 256-frame stop
with a ten-second steady-clock deadline, a one-millisecond unsuccessful-poll
pause, and explicit satisfied/timed-out diagnostics in the test-local helper.
The checked-in regression reaches predicate call 257 and requires at least
200 ms elapsed; the direct timeout regression and all three end-to-end queued
scene-file contracts preserve success, error, snapshot, event-correlation,
history, and main-thread application assertions. All five cases passed 20
repetitions each, the scene-file trio passed 100 repetitions each, and a real
worker write delayed five seconds with `strace` passed in 5.151 seconds where
the old helper failed. The aggregate build and default CPU gate passed
3,787/3,787 in 402.05 seconds; strict task, state-link, test-layout,
documentation, layering, and diff checks passed. Independent review found no
remaining determinism, lifetime, timeout-semantics, or right-sizing blocker.
No production source, module surface, CMake policy, or runtime documentation
changed.

[`RUNTIME-178`](RUNTIME-178-restore-engine-convergence-budget.md) — the bounded
Engine convergence remediation retired on 2026-07-16 at `Operational`.
Implementation commit `109af4bd` makes the Engine-owned ImGui editor bridge and
asset residency service opaque without changing their lifetime order,
registers the existing render-extraction cache for direct Sandbox UV queries,
and removes three redundant geometry/material query facades. The authoritative
ratchet improves from the temporary 49 / 28 / 33 snapshot to 42 plain imports /
21 domain imports / 31 getter names, with two re-exports and no temporary debt.
Focused runtime coverage passed 165/165, the default CPU-supported gate passed
3,785/3,785 in 395.20 seconds, and both required promoted-Vulkan UV tests
executed without skips and passed. Strict convergence, layering, test-layout,
task, state-link, documentation, root, PR-contract, skill-sync, inventory, and
diff checks passed. Independent module-law, lifetime, right-sizing, and final
reviews found no blockers. Existing `BUG-088` owns a contention-only benchmark
timeout observed on the first loaded gate; `BUG-092` separately owns the
frame-count-only asynchronous scene-save test wait exposed in that run.

[`HARDEN-085`](HARDEN-085-enforce-runtime-engine-kernel-convergence-ratchet.md)
— the `Runtime.Engine` convergence ratchet retired on 2026-07-16 at
`CPUContracted`. Implementation commit `ea3b403e` adds a stdlib-only exact
allowlist-complement checker and JSON policy, guards public `GetX` names and
re-exports, requires policy updates on improvements, and validates temporary
debt arithmetic plus a unique open owner. The fixed 43 / 23 legacy-interim
reference remains explicit; the checked 49 / 28 / 33-getter snapshot is green
only with `RUNTIME-178` owning the `+6 / +5 / +1` budget debt. `pr-fast` and
touched-scope run both the live guard and synthetic failures, with independent
workflow-policy coverage. Checker regressions passed 19/19, touched-scope
10/10, workflow 5/5, and ccache workflow 15/15; strict task, documentation,
layering, root, test-layout, state-link, PR-contract, skill-sync, JSON, Python,
and diff gates passed. Independent architecture review found no remaining
parser, policy-lifecycle, CI-wiring, or right-sizing blocker.

[`RUNTIME-173`](RUNTIME-173-privatize-kmeans-gpu-job-queue-surface.md) — the
Sandbox-session-only K-Means GPU job queue surface retired on 2026-07-16 at
`CPUContracted`. Implementation commit `e8033e11` moves the unchanged queue
declaration into module-private Sandbox facade glue and reattaches its unchanged
implementation to `Extrinsic.Runtime.SandboxEditorFacades`; the genuine public
request/submission/result/status records remain on that facade. Named-module
importers drop from nine to zero, runtime modules from 80 to 79, and repository
modules from 387 to 386. The dedicated privacy guard and stale-copied-surface
regression preserve ownership and attachment-epoch fail-closed behavior;
focused coverage passed 31/31 and the default CPU-supported gate passed
3,785/3,785 in 399.36 seconds after a successful full build. Strict structural
and clean-workshop checks passed, and independent design, lifetime,
mechanical-equivalence, and right-sizing reviews found no blockers. Compile
timings remain diagnostic only; no aggregate speedup or new Vulkan operational
claim is made.

[`RUNTIME-171`](RUNTIME-171-privatize-asset-residency-service-surface.md) — the
Engine-only asset residency service module surface retired on 2026-07-16 at
`Operational`. Implementation commit `1384228f` replaces the standalone
`Extrinsic.Runtime.AssetResidencyService` BMI with an include-only declaration
attached to `Extrinsic.Runtime.Engine`; the existing implementation unit is
reattached to Engine with its method bodies unchanged. The service remains a
by-value member in the same position, preserving GPU cache/listener/model-
handoff ownership, maintenance behavior, and scene-borrower -> texture-handoff
-> listener -> cache teardown order. Runtime modules drop from 81 to 80 and
repository modules from 388 to 387; the dedicated `contract;runtime` guard
proves the old module/CMake/inventory entries remain absent, the private
declaration has one include owner, and raw residency state does not return to
Engine. Focused CPU coverage passed 79/79, runtime layering passed 21/21, and
the default CPU-supported gate passed 3,784/3,784 in 398.08 seconds after a
successful full build. Strict structural and clean-workshop checks passed, and
three independent reviews found no blockers. Compile timings are retained as
diagnostics only; no overall speedup is claimed.

[`RUNTIME-169`](RUNTIME-169-privatize-render-extraction-service-surface.md) —
the Engine-only render extraction service module surface retired on 2026-07-16
at `Operational`. Implementation commit `7c9ad87b` replaces the standalone
`Extrinsic.Runtime.RenderExtractionService` BMI with an include-only declaration
attached to `Extrinsic.Runtime.Engine`; the existing implementation unit is
reattached to Engine with its method bodies unchanged. The service remains a
by-value member in the same position, preserving its borrowed cache address,
pool ownership, statistics/frame-index state, and cache-before-renderer teardown
order. Runtime modules drop from 82 to 81 and repository modules from 389 to
388; the dedicated `contract;runtime` guard proves the old module/CMake/
inventory entries remain absent, the private declaration has one include owner,
and raw extraction state does not return to Engine. Focused CPU coverage passed
74/74, runtime layering passed 22/22, and the default CPU-supported gate passed
3,783/3,783 in 395.85 seconds after a successful full build. Strict structural
and clean-workshop checks passed, and three independent reviews found no
blockers. Compile timings are retained as diagnostics only; no overall speedup
is claimed.

[`RUNTIME-174`](RUNTIME-174-privatize-imgui-editor-bridge-surface.md) — the
Engine-only ImGui editor bridge module surface retired on 2026-07-16 at
`Operational`. Implementation commit `724a7489` replaces the standalone
`Extrinsic.Runtime.ImGuiEditorBridge` BMI with an include-only declaration
attached to `Extrinsic.Runtime.Engine`; the existing implementation unit is
reattached to Engine with its method bodies unchanged. The bridge remains a
by-value member in the same position, preserving renderer overlay attach/detach,
Begin/End bracketing, callback, capture, diagnostics, and teardown order.
Runtime modules drop from 83 to 82 and repository modules from 390 to 389; the
dedicated `contract;runtime` guard proves the old module/CMake/inventory entries
remain absent and the private declaration has exactly one owner. Focused CPU
coverage passed 52/52, runtime layering passed 23/23, and the default
CPU-supported gate passed 4,178/4,178 in 452.48 seconds after a successful full
build. Strict structural and clean-workshop checks passed, and three independent
reviews found no blockers. Compile timings are retained as diagnostics only; no
overall speedup is claimed.

[`BUG-090`](BUG-090-async-work-layering-test-stale-shutdown-owner.md) — the
stale async-work shutdown source assertion retired on 2026-07-16 at
`CPUContracted`. Implementation commit `406b44d0` updates only the two expected
call spellings to the current `ShutdownHooks::AsyncWork` delegation; Engine
still passes its owned `m_AsyncWorkService` into that hook, and every surrounding
positive and negative ownership check remains unchanged. The exact pre-fix
reproduction failed only those two expectations, then passed 1/1; the complete
opt-in `RuntimeEngineLayering` selection passed 24/24. Strict task, state-link,
documentation-link, test-layout, and diff checks passed, and independent review
found no blockers. No production source or test label changed.

[`RUNTIME-167`](RUNTIME-167-privatize-engine-frameloop-surface.md) — the Engine
frame-loop module surface retired on 2026-07-16 at `Operational`.
Implementation commit `25e3e7c5` replaces the one-consumer
`Extrinsic.Runtime.Engine:FrameLoop` partition with include-only private glue
consumed solely by `Runtime.Engine.cpp`. The 408-line helper namespace body and
Engine namespace body remain byte-identical; the inventory drops from 391 to
390 modules and the combined direct-import count from 124 to 93. The dedicated
`contract;runtime` guard proves the old module/CMake/inventory entries remain
absent and the header has exactly one production consumer. The privacy guard
passed 1/1, duplicated frame-loop contracts 18/18, focused CPU selection 54/54,
explicit resize coverage 1/1, unaffected opt-in layering assertions 23/23, and
the default CPU-supported gate 3,781/3,781 in 407.53 seconds. Strict structural
and clean-workshop checks passed, and three independent reviews found no
blockers. The recorded single-host timing remains diagnostic only; no compile
speedup is claimed. `BUG-090` separately owns the pre-existing stale async-work
shutdown assertion exposed by the opt-in layering binary.

[`RUNTIME-170`](RUNTIME-170-privatize-object-space-normal-gpu-queue-surface.md) —
the object-space normal bake GPU queue surface retired on 2026-07-16 at
`CPUContracted`. Implementation commit `ca17b9b9` folds the one-consumer GPU
participant, plan-provider adaptation, retained submissions, and diagnostics
into `ObjectSpaceNormalBakeService` opaque state while keeping the CPU request
queue public. The aggregate public interface shrank from 175 to 96 lines and
from 15 to 5 direct imports, and the inventory dropped from 392 to 391 modules;
the single-host timing sample is recorded without a speedup claim. Six queue
scenarios now drive record/drain behavior only through the service and
`JobService`, including stale discard, content-key reuse, and generation-safe
record/ready-publication failure cleanup. The focused selection passed 164/164,
the full CPU-supported gate passed 3,780/3,780 in 398.27 seconds, strict
structural and clean-workshop checks passed, and three independent reviews
found no blockers. `RUNTIME-129` retains production Vulkan plan-provider and
opt-in runtime smoke ownership.

[`BUG-089`](BUG-089-root-hygiene-rejects-canonical-and-ignored-state.md) —
strict root ownership retired on 2026-07-16 at `CPUContracted`. Implementation
commit `7671576b` makes `root_allowlist.yaml` the single policy for exact
tracked roots and bounded named local state, with the legacy top-level checker
preserved as a compatibility entrypoint. Tracked `ara/` and named
`imgui.ini`/`.ruff_cache/` state now pass; missing, malformed, duplicate,
overlapping, unknown-section, and broad-wildcard policy fails closed, as do
unknown source/Markdown roots even when a global Git ignore hides them. The
12-test regression, strict real-root check, task policy/state links, docs
links, workflow naming, and test layout passed; independent final review found
no blockers.

[`BUG-087`](BUG-087-task-validator-documented-root-silent-noop.md) — the
task-validator root contract retired on 2026-07-16 at `CPUContracted`.
Implementation commit `998f0e77` standardizes live agent documentation on
`--root tasks --strict`, makes strict zero-file discovery return nonzero with
the exact `active`/`backlog`/`done` directories searched, and runs a focused
Python regression in `ci-docs`. The regression proves the canonical repository
invocation discovers a nonzero task count and an existing empty task root fails
closed. Strict task policy/state links, skill-mirror freshness, documentation
links, workflow policy, and test-layout checks passed.

[`GRAPHICS-122`](GRAPHICS-122-uv-view-offscreen-render-target.md) — the
optional GPU-shaded parameterization UV view retired on 2026-07-15 at
`Operational`. Commits `c5655359`, `871d09eb`, `86d0d1d4`, and `d0e173dd`
delivered config/UI/agent parity, face-storage-aligned conformal diagnostics,
the typed `UvViewPass`/`UvViewColor` recipe declaration, and a concrete
graphics-owned retained target under the existing `RenderSubsystemRegistry`.
Runtime reuses selected `GpuWorld` residency, accepts distortion only when the
exact topology-to-face/position/UV fingerprint still matches, and presents a
completed bindless target through the existing EditorShell window; CPU/null
paths retain an explicit `CpuLayout` fallback. The focused ASan/UBSan selection
passed 77/77 and the full CPU-supported gate passed 3,780/3,780 in 407.98
seconds. The final-tree `gpu;vulkan` selection passed 2/2 without skips in 9.09
seconds: direct semantic checker/texel-density/texture/heatmap readback plus the
real Agent/CLI → `ReferenceTriangle` → EditorShell → ImGui path. Strict
layering/task/docs/generated-artifact checks and independent final review were
clean. The upgrade remains a derived view of one mesh entity, not a second ECS
entity, camera, or general viewport contract.

[`UI-036`](UI-036-sandbox-parameterization-editor-and-uv-split-view.md) — the
Sandbox parameterization editor and resizable UV split view retired on
2026-07-15 at `Operational`. Implementation commit `6502de0b` registered the
app-owned window, drove the `RUNTIME-176` validated config-then-command path,
rendered the pointer-free UV view model with checker/grid and aggregate
diagnostics, and kept strategy inventory limited to implemented methods; fit
correction `8e88a48b` kept the optional unit square inside the fitted pane.
Shared ImGui repair `1766253c` closed `BUG-085` and `BUG-086`, deliberately
batched because both traverse the same adapter/overlay/pass chain and shared
the same live Vulkan replay. The focused selection passed 255/255, the default
CPU-supported gate passed 3,753/3,753, the ImGui Vulkan smoke passed 3/3, and
the runtime acceptance passed 1/1. Validation-enabled production Vulkan
replays showed the applied LSCM diagnostics and contained checker/grid in
`/tmp/ui036-live/parameterize-ran-fixed4.png`, then selected the dense dolphin
mesh without assertion in `/tmp/ui036-live/dolphin-dense-selected.png`.
`GRAPHICS-122` remains an optional GPU-scale UV target, not a missing maturity
gate.

[`BUG-086`](BUG-086-imgui-adapter-omits-vtx-offset-capability.md) — the missing
ImGui vertex-offset renderer capability retired on 2026-07-15 at
`Operational`. Shared repair commit `1766253c` advertises
`RendererHasVtxOffset` and preserves non-zero command offsets through the
runtime adapter, graphics upload, and pass. It was deliberately batched with
`BUG-085` because both defects occupy the same adapter/overlay/pass chain and
used one validation-enabled live Vulkan replay. The 255/255 focused selection,
3,753/3,753 CPU gate, 3/3 ImGui Vulkan smoke, and 1/1 runtime acceptance all
passed; `/tmp/ui036-live/dolphin-dense-selected.png` records the dense selected
mesh above the former 65,535-vertex threshold without the assertion.
`GRAPHICS-122` continues to own optional GPU-scale rendering, not correctness
of the command-offset contract.

[`BUG-085`](BUG-085-imgui-overlay-drops-command-clip-rectangles.md) — ImGui
command-clip propagation retired on 2026-07-15 at `Operational`. Shared repair
commit `1766253c` carries finite framebuffer-relative scissors from the
runtime adapter through pointer-free overlay/upload records and applies each
one before its indexed draw, including native Y mirroring for the renderer's
negative-height Vulkan viewport. It was deliberately batched with `BUG-086`
because both defects occupy the same adapter/overlay/pass chain and used one
validation-enabled live Vulkan replay. The 255/255 focused selection,
3,753/3,753 CPU gate, 3/3 ImGui Vulkan smoke, and 1/1 runtime acceptance all
passed; `/tmp/ui036-live/parameterize-ran-fixed4.png` records the LSCM result
with the checker/grid contained inside its UV child pane and no panel-local
workaround.

[`RUNTIME-176`](RUNTIME-176-parameterization-runtime-config-integration.md) —
parameterization runtime/config integration retired on 2026-07-15 at
`CPUContracted`. Implementation commit `e8c3f73e` added stable config tokens
and typed LSCM, harmonic/Tutte, and BFF records; schema-v1 round trips and hot
apply; a configured sandbox facade; undoable storage-aligned `v:texcoord`
writeback; and a pointer-free UV/triangle/aggregate-diagnostics view model.
The CPU path preserves deleted vertex tombstones, rejects invalid topology,
config, narrowing, and solver outcomes without mutating UV state, and keeps
history closures free of session-owned cache lifetimes. Editor, agent/CLI, and
programmatic config sources produce identical state and deterministic UVs.
Focused ASan/UBSan contracts passed 13/13, the combined parameterization/config
selection passed 56/56, and the default CPU-supported gate passed 3,743/3,743
in 380.43 seconds. Strict architecture/task/docs checks and independent
right-sizing review found no blockers. `UI-036` now owns the visible
`Operational` split-view proof; no speculative optimized/GPU selector or
backend seam landed.

[`METHOD-023`](METHOD-023-boundary-first-flattening-reference-backend.md) —
Boundary First Flattening retired on 2026-07-15 at `CPUContracted`.
Implementation commit `4bf4f67b` added a typed disk-BFF strategy with
automatic conformal, approximate target-length, and prescribed exterior-angle
modes, paper-weighted best-fit boundary closure, separate Dirichlet and
grounded-Neumann solves, and harmonic or holomorphic interior extension. The
surface deliberately excludes cone/cut output that one UV per original vertex
cannot represent, reports rather than hides length/closure residuals, and
fails closed on invalid topology, geometry, boundary data, factorization, and
diagnostic outcomes. Analytic, scale, closed-form closure, deterministic, and
failure regressions plus the manifest-backed correctness smoke passed; the
post-review focused selection passed 43/43 and the default CPU-supported gate
passed 3,726/3,726 in 374.73 seconds. Runtime/config integration remains owned
by `RUNTIME-176`; visible editor operation remains owned by `UI-036`.

[`GEOM-063`](GEOM-063-unified-cpu-parameterization-strategy-dispatch.md) — the
unified CPU parameterization strategy dispatch retired on 2026-07-15 at
`CPUContracted`. Right-sizing removed the speculative global backend axis,
fallback telemetry, reserved future-method tokens, and generic mega-params;
the landed surface is a typed variant over the two parameter records that
actually exist, dispatching LSCM and Harmonic/uniform-Tutte through one
normalized status, UV, and shared-diagnostics result. Direct solver UVs remain
bitwise-identical and reachable, invalid input and nonconvergence fail closed
without UV payloads, and future methods add concrete payload alternatives only
when implemented. Final review aligned LSCM with the shared connected-manifold
disk preflight, rejected non-finite numeric inputs, and made dispatch reject
otherwise-successful solves whose diagnostics contain no usable faces. The
exact single-binary family selection passed 31/31, dispatch-only passed 6/6,
and the Clang-23 ASan/UBSan CPU-supported gate passed 9250/9250 in 977.67
seconds. Runtime/config/UI integration remains explicitly owned by
`RUNTIME-176`; real optimized/GPU policy remains with `METHOD-025`/`METHOD-026`
when second implementations exist.

[`GEOM-014`](GEOM-014-feature-aware-quadric-error-simplification.md) — the
feature-aware quadric-error simplification adaptation retired on 2026-07-15 at
`CPUContracted`. Closure corrected its attribution and scoped the implementation
as paper-inspired rather than equation-level parity, replaced a vacuous
survivor-vertex quality proxy with deterministic original-surface samples
queried against each 24-face result surface, added a translated sensitivity
control, proved the classical metric ignores every feature-aware control, and
made rejection diagnostics count evaluated candidates. The Clang-23 ASan/UBSan
`ci` build completed, the exact single-binary `Simplification*` selection
passed 29/29, all 93 registered matches passed in the full gate, and the
manifest-backed `geometry.simplification.fa_qem_quality.smoke` workload passed
its run/validation pair with strict validation of all 24 manifests and 20
emitted results. Review correction made the diagnostic count immutable corners
only and made the workload enforce every measured iteration plus its 5000 ms
budget; the emitted result recorded 8 corners, 0 failed iterations, and a
1255.539 ms mean. The benchmark is a correctness smoke and makes no paper
parity, performance, or adoption claim. No `Operational` follow-up is owed
because the task extends the deterministic CPU geometry kernel in place.

[`GEOM-019`](GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md) —
harmonic/Tutte parameterization retired on 2026-07-15 at `CPUContracted`.
Implementation commit `29d7a908` added explicit circle, square, and custom
boundary policies, cotangent-harmonic and uniform-Tutte weights, structured
failure states, shared GEOM-018 diagnostics, and a geometry-owned sparse-LDLT
solve without exposing Eigen. Corrective closure validation requires connected
manifold topology, one boundary loop, and Euler characteristic one, with
regressions for multiple loops, a punctured genus-one mesh, insufficient
vertices, deleted pins, and finite distortion diagnostics. The Clang-23
ASan/UBSan `ci` build completed and the exact `Parameterization|Sparse|DEC`
selection passed 143/143; layering, task, test-layout, and documentation checks
remained green. No `Operational` follow-up is owed because this is a
deterministic CPU geometry solver. Its retirement satisfies GEOM-063's remaining
dependency.

[`PROC-026`](PROC-026-task-archive-and-micro-template.md) — task archive
sweep and micro template retired on 2026-07-14. All 661 retired task files
moved from `tasks/done/` to the frozen `tasks/archive/` with ~1,600 inbound
links rewritten and zero broken links; archived IDs stay authoritative for
duplicate detection, `depends_on` resolution, and state-link guards
(`validate_tasks.py`, `generate_session_brief.py`,
`check_task_state_links.py`). Added `tasks/templates/task-micro.md`
(`template: micro`) reducing single-slice mechanical tasks to
Goal/Acceptance criteria/Verification, with strict-validation probes both
ways. Commits 865a61b and b6d4568.

[`BUG-074`](../archive/BUG-074-object-space-normal-bake-orphaned-cache-slot-livelock.md) —
object-space-normal-bake failed-slot retry livelock retired on 2026-07-13 at
`CPUContracted`. Both command-record and ready-frame-publication failures now
call `FailGpuProducedTexture` with the generation returned by
`BeginGpuProducedTexture`; the cache retires only that exact pending generation
and fails closed for absent, promoted, transfer-owned, or mismatched entries.
Forced failures prove a second explicit schedule succeeds immediately, while
direct cache regressions prove stale cleanup preserves a replacement and late
cleanup does not recreate a destroyed slot. The six-test causal selection
passed 100 repetitions, broader bake/cache coverage passed 49/49, and the exact
integrated 3,692-test default CPU gate plus strict structural checks passed.
The production Vulkan plan provider and end-to-end runtime seam remain owned by
`RUNTIME-129` before `Operational` can be claimed.

[`BUG-064`](../archive/BUG-064-ci-vulkan-framepacing-headless-display.md) — hosted
frame-pacing capture gate retired on 2026-07-13 at `Operational`. The
`ci-vulkan` workflow now installs Mesa's lavapipe software Vulkan driver and
scopes it with an isolated Xvfb server to the strict
`ExtrinsicSandbox.FramePacingDiagnosticCapture`; all other GPU fixtures retain
their documented capability-skip behavior, and no diagnostic assertion was
weakened. Three sequential hosted runs at the same exact head `7e735868` —
[29277091536](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29277091536),
[29278614647](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29278614647),
and [29280699135](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29280699135) —
executed and passed the strict capture in 5.74 s, 3.19 s, and 5.28 s. Each run
also passed all 59 broader CTest entries, timing aggregation and strict result
validation, and artifact upload. The formerly unconditional hosted failure is
therefore closed without converting the capture into a skip.

[`BUG-067`](../archive/BUG-067-jobservice-completion-state-lost-update-race.md) —
JobService completion-state lost-update race retired on 2026-07-13 at
`CPUContracted`. Production fix `ce1f590c` stores `AwaitingGate` while holding
the completion-queue mutex and before insertion, making queue visibility the
one-way handoff from worker-owned state transitions to drain-owned terminal
transitions. This slice added an explicit constructor-injected test hook after
queue publication and a real-service condition-variable interlock that pauses
the worker while `DrainCompletions` publishes. Mutating only the store back to
the historical post-publication location deterministically regressed the state
from `Published` to `AwaitingGate`, left one phantom in-flight/awaiting job, and
made reap return zero; the fixed path passed 100 repetitions, all 10 JobService
contracts, and the 3,679-test default CPU gate. The remaining state stores were
audited: all worker terminal exits precede queue visibility, `Running` precedes
work, cancellation only sets a flag, and only the drain writes state after
publication.

[`BUG-073`](../archive/BUG-073-object-space-normal-bake-read-before-gpu-write.md) —
object-space-normal-bake ready-frame accounting retired on 2026-07-13 at
`CPUContracted`. Production correction `fdeb0a6b` replaced the unsafe fixed
`issueFrame + 1` stamp with `issueFrame + FramesInFlight`; this closure names
that contract and adds a deterministic three-frame-in-flight regression over
the real `JobService` queue, `GpuAssetCache`, and material-binding path. The
texture stays `GpuUploading` and unbound at issue+1/+2, then promotes and binds
at issue+3; the four-test queue suite passed 100 repetitions under ASan/UBSan.
The existing graphics Vulkan bake/readback also matched the CPU texel contract
on an NVIDIA GeForce RTX 3050 with the repository's narrow LSan suppression;
without it, assertions passed before the known `BUG-083` shutdown leaks caused
a nonzero sanitizer exit. That run proves the graphics bake, not the exact
runtime readiness seam: the production Vulkan plan provider and end-to-end
runtime smoke remain explicitly owned by `RUNTIME-129` before `Operational` can
be claimed.

[`BUG-071`](../archive/BUG-071-onresolve-sim-systems-bypass-finalizeforboot.md) —
Runtime-module resolve registration finalization retired to `tasks/done/` on
2026-07-13 at `CPUContracted`. Production commit `a1704053` keeps sim-system
registration legal during `OnResolve` and moves the single schedule
finalization after every resolve callback, so duplicate identities, cycles,
and unprovided waits are checked against the complete register-plus-resolve
set before boot can return. A real `Engine::Initialize()` regression registers
the same identity across the two phases: it failed five of five repetitions on
the exact pre-fix parent `7e77e47f` because initialization did not die, then
passed against the production fix under Clang 23 with ASan/UBSan. Runtime and
feature-module architecture docs now state the phase contract explicitly. The
focused runtime-module selection passed 13/13, `IntrinsicTests` built, the full
default CPU-supported gate passed 3640/3640, and strict structural/docs checks
passed.

[`BUG-076`](../archive/BUG-076-asyncworkservice-shutdown-skips-derived-job-registry.md) —
derived-job shutdown quiescence retired on 2026-07-13 at `CPUContracted`.
Production fix `6d6f784a` joins the shared executor, drains completion and
readback queues, applies newly ready results, and then cancels every survivor so
an unreadied callback cannot resume after `ShutdownAndDrain` returns. The
unreadied regression failed deterministically against the intermediate
drain-only ordering, then both ready and unreadied contracts passed 100
repetitions under ASan/UBSan; the broader 9-test async-work selection, complete
3684-test default CPU gate, and strict structural checks pass. No
hardware-backed follow-up is owed.

[`BUG-075`](../archive/BUG-075-worldregistry-activate-while-destroy-pending.md) — world
activation-versus-destruction ordering retired on 2026-07-13 at
`CPUContracted`. Production fix `80c56dcc` makes destruction authoritative:
`RequestSetActiveWorld` returns `ResourceBusy` for destroy-pending or
destroy-announced records, while Maintenance revalidates queued activation
targets as `Live` so the reverse request ordering cannot produce an
active-and-destroy-announced world. Both deterministic runtime regressions pass
under ASan/UBSan together with the complete default CPU gate and strict
structural checks. No hardware-backed follow-up is owed.

[`CI-007`](../archive/CI-007-module-safe-persistent-ccache-pilot.md) — persistent
module-safe ccache pilot retired on 2026-07-13 at `Operational`. Retained a
bounded external ccache store in `pr-fast` only, with direct/depend modes
disabled, `.cppm` compilation passed through, every repository module
interface hashed through `CCACHE_EXTRAFILES`, fail-closed identity/statistics
guards, and no build/BMI state cached. Five comparable hosted cold samples
(0 hits / 575 misses per run) produced 1,406.634 s build median and 1,510.259 s
p95; five unchanged warm samples (575 hits / 0 misses) produced 609.476 s
median and 630.449 s p95, improvements of 56.7% and 58.3%, with every gate and
hermetic invalidation probe passing and zero ccache errors. Evidence-only
exported-API commit `4befbe1e` then restored the prior store, safely missed all
575 eligible objects after the module digest changed, and passed hosted run
`29211278659`; its clean no-ccache counterpart passed the 3,617-test default
CPU gate, and `fd97d4d1` removed the marker. No other gate consumes the store;
future expansion requires a separate task. The unrelated 22.002 s exact-vcpkg
configure-budget failure that stopped the first interface attempt before
ccache restore is tracked by `BUG-081`.

[`PROC-024`](../archive/PROC-024-theme-research-method-track.md) — research/method track
theme retired on 2026-07-11 at `Retired` (task-map/tooling only) after owner
approval. Created **Theme I — Research method implementation (P1)**: added
`"I": "Research method implementation"` to `THEME_NAMES` in
`tools/agents/generate_session_brief.py`, set `theme: I` front-matter on the 16
open members (`METHOD-003`/`004`/`005`/`006`/`007`/`014`/`015`/`016` and the
method-readiness seams `GEOM-013`/`014`/`019`/`024`/`058`/`059`/`060`/`061`),
added the Theme I section with rationale to `tasks/backlog/README.md`, and
regenerated `tasks/SESSION-BRIEF.md`. The research track (previously entirely
`Unthemed` while engine-plumbing themes carried explicit priorities — the
174-`GRAPHICS`-done vs 8-`METHOD`-done imbalance) is now a first-class P1 picker
target alongside Themes B and C. The `I` letter was freed by `PROC-010`'s
superseded Theme I. `generate_session_brief.py --check`, strict
`check_task_policy.py`, strict `check_task_state_links.py`, and
`check_doc_links.py` pass.

[`PROC-010`](../archive/PROC-010-encode-research-engine-invariants-in-contract.md) —
research-engine invariants retired on 2026-07-11 at `Retired` (contract/docs
only) after owner approval of sections A and B. Promoted the three
research-engine design principles into binding, always-on `AGENTS.md` §5
invariants — **P1** (research pragmatism: smallest construct, plain
structs/free-functions default, a seam needs a present justification), **P3**
(config lane is a first-class control surface reachable by files + agents/CLI +
UI co-equally, via a preview/validate/apply path), and **P5** (recipe-driven
frames + a readable named-phase main loop) — with three matching per-PR rows in
`docs/agent/review-checklist.md`, mirrored into `intrinsicengine-review` by
`sync_skills.py --write`. Section C (a new "Theme I — Research control surface")
was dropped: all nine proposed members had already retired, so it would have had
zero open members; the freed `I` letter went to `PROC-024`'s research/method
track. No engine code changed. `sync_skills.py --check`, `check_doc_links.py`,
and strict `check_task_policy.py` pass.

[`PROC-020`](../archive/PROC-020-sandbox-input-lifecycle-skill.md) — sandbox input/lifecycle
skill (playbook wave 2) retired on 2026-07-11 at `Retired` (docs/skill-surface
only). Authored
`tools/agents/skills/intrinsicengine-sandbox-input-lifecycle/SKILL.md`, a
SKILL.md-only cross-cutting discipline skill capturing the six runtime
frame-loop wiring pitfalls that regressed repeatedly in `ExtrinsicSandbox`, each
citing its evidencing retired task(s): ImGui capture must gate camera/gizmo/pick
input (`BUG-017`, `BUG-036`); window-close must route `WindowCloseEvent` →
`RequestExit()`, re-check before renderer work, and idle-wait before GPU teardown
(`BUG-027`, `BUG-037`, `BUG-054`); UI/gizmo edits after the fixed-step bundle
need `FlushPreRenderTransformState` (`BUG-024`); decode must never block the
platform poll thread (`BUG-021` drop-import member of the duplicated id); the
`+Y-down`/orbit-camera sign conventions with a pole-crossing test
(`BUG-020`, `BUG-039`→`BUG-040` fix-of-a-fix); and HiDPI window-vs-framebuffer
cursor scaling (`BUG-026`). The wiring vocabulary was cross-checked against the
live `Runtime.Engine.cpp`/`Runtime.Engine.FrameLoop.cppm`
(`imguiCapturesInput` from `m_ImGuiEditorBridge.WantsMouseCapture()`,
`RequestExitFromWindowClose`, `FlushPreRenderTransformState`), and the duplicated
`BUG-021` id was disambiguated to the drop-import task file. Registered in the
`intrinsicengine-core` routing table and the skills `README.md` discipline tier
(eight → nine disciplines; seventeen → eighteen skills). No engine code changed.
`sync_skills.py --check`, `check_doc_links.py`, and strict `check_task_policy.py`
pass, and the skill auto-discovers. This completes the playbook wave-2 skill trio
(`PROC-018`/`PROC-019`/`PROC-020`).

[`PROC-019`](../archive/PROC-019-geometry-io-format-skill.md) — geometry-IO format skill
(playbook wave 2) retired on 2026-07-11 at `Retired` (docs/skill-surface only).
Authored `tools/agents/skills/intrinsicengine-geometry-io-format/SKILL.md`, a
SKILL.md-only cross-cutting discipline skill distilling the importer/exporter
slice shape instantiated ~35 times across the `GEOIO-002A`..`002AG` / `GEOIO-003`
series: `geometry -> core` layering with parsers in an anonymous namespace behind
an unchanged `.cppm`, `Core::Expected<...Result>` readers and `*IOWriteStatus`
writers, strict parsing with untrusted-header-count validation bounded against
the payload before any reserve/read (origin `BUG-033`), the `Core::ErrorCode`
reader / `*IOWriteStatus` writer diagnostics taxonomy, `unit;geometry`
round-trip/determinism/fail-closed tests with committed text and byte-level
binary fixtures, and the `CPUContracted` closure wording that states whether an
`Operational` follow-up is owed. The template was verified against three exemplar
slices before writing (`GEOIO-002B` PLY ASCII exporter, `GEOIO-002D` binary STL
importer, `GEOIO-002E` binary PLY importer) and cross-checked against the live
`Geometry.HalfedgeMesh.IO.cppm` public surface. Registered in the
`intrinsicengine-core` routing table and the skills `README.md` discipline tier
(seven → eight disciplines; sixteen → seventeen skills). No engine code changed.
`sync_skills.py --check`, `check_doc_links.py`, and strict `check_task_policy.py`
pass, and the skill auto-discovers.

[`PROC-018`](../archive/PROC-018-import-visibility-contract-skill.md) — import-visibility
contract skill (playbook wave 2) retired on 2026-07-11 at `Retired`
(docs/skill-surface only). Authored
`tools/agents/skills/intrinsicengine-import-visibility-contract/SKILL.md` as a
SKILL.md-only cross-cutting discipline skill codifying the acceptance checklist
that turns a decoded import into a genuinely visible, selectable sandbox entity.
The seven checklist items — reference-triangle render-critical component parity
(`GeometrySources`/`RenderSurface`/`SelectableTag`/`VisualizationConfig`/`StableId`,
verified against `Runtime.ReferenceScene.cpp`), count-matched `v:normal`
(preserved/area-weighted/never-overwritten), resolved `v:texcoord` (authored or
generated atlas UVs), off-origin culling bounds plus one-shot camera focus,
non-blocking derived post-processing that never clobbers recomputed attributes,
deferred generated-normal/texture bindings, and never-silent receipt/queue/
completion logging — each cite the retired task(s) that proved them
(`BUG-022`/`023`/`038`/`041`/`043`/`044`/`045`/`047`/`048`/`050`,
`ASSETIO-006`/`007`/`008`). Registered in the `intrinsicengine-core` routing
table and the skills `README.md` discipline tier (six → seven disciplines;
fifteen → sixteen skills). No engine code changed. `sync_skills.py --check`,
`check_doc_links.py`, and strict `check_task_policy.py` pass, and the skill
auto-discovers via the `.claude`/`.codex` skill symlinks.

[`PROC-023`](../archive/PROC-023-canonicalize-skill-body-content.md) — skill-body
canonicalization retired on 2026-07-11 at `Retired` (docs/skill-surface only).
Three source-doc mirror skills had bodies 3–4× their `docs/agent/*` source
carrying normative content with no canonical home: `intrinsicengine-benchmark`
(`Anti-patterns`), `intrinsicengine-method` (the knowledge-graph claim→code aid
and the "How method work maps to the maturity taxonomy" mapping), and
`intrinsicengine-docs-sync` (`Decision rules for common cases`). Each body was
classified section-by-section (mirror vs extends/only-here); the only-here
sections were resolved by declaring them **skill-canonical** (resolution (b),
the discipline-skill model) rather than moving content, honoring the task's
"no content changes" Non-goal. Every affected body now carries an `Authority
(PROC-023)` note delimiting mirrored-from-source sections (source doc wins) from
skill-canonical sections (skill body is the home, `AGENTS.md` still wins on
anything it covers), and the skills `README.md` authority section records the
model per skill in a table. No `docs/agent/*` source changed, so no `--write`
re-sync was needed; `sync_skills.py --check`, `check_doc_links.py`, strict
`check_task_policy.py`, and diff-mode docs-sync all pass.

[`PROC-022`](../archive/PROC-022-tool-directory-readme-refresh.md) — tool-directory README
refresh retired on 2026-07-11 at `Retired` (docs-only). The three
`tools/*/README.md` files had drifted from their directories: `tools/ci`
documented 2 of 10 scripts, `tools/agents` omitted `check_codex_config.py` and
`check_todo_active_only.sh` while listing the shipped `validate_method_manifests.py`
under a "Planned moves" heading, and `tools/repo` omitted `check_pr_contract.py`,
`check_shader_outputs.py`, and `root_allowlist.yaml`. Each README now lists every
script and config present with a one-line purpose and its concrete CI wiring
(cross-checked against `.github/workflows/*.yml`). The stale "Planned moves"
(RORG-041/071) and "Compatibility entrypoints" (RORG-112, plus the canonical
`generate_module_inventory.py` mislabeled as a legacy wrapper) sections — all
referencing `done` tasks — were corrected to current state. A bidirectional
completeness check (every directory script appears in its README, every README
entry resolves to an existing file) plus `check_doc_links.py` and strict
`check_task_policy.py` pass. No script behavior changed.

[`BUG-080`](../archive/BUG-080-uv-atlas-promotion-smoke-timing-flake.md) — UV-atlas
promotion-smoke timing flake retired on 2026-07-10 at `CPUContracted`. Each
fixture now warms one pair, individually times five fast-staged/xatlas pairs in
alternating order, and gates on the median paired runtime ratio. Median backend
runtimes plus every raw backend and ratio sample remain in the result for
auditability; benchmark identity, dataset, quality gates, and strict 1.0/1.25
thresholds are unchanged. Twenty-five loaded-host runs produced 475/475 passing
result files with a worst robust fixture ratio of 0.604228, and the complete
3,658-test default CPU gate plus strict benchmark/structural validators pass.

[`BUG-070`](../archive/BUG-070-runtime-module-schedule-failclosed-guards-regressed.md) —
runtime-module fail-closed schedule regression retired on 2026-07-10 at
`CPUContracted`. Commit `7e77e47f` restored recoverable `InvalidArgument` for
duplicate `(module, system)` identities and `InvalidState` for cycles or
unprovided waits before any per-tick pass is appended. Direct contracts pin all
three cases; a real `Engine::Initialize()` death test proves the retained global
boot policy terminates an invalid duplicate schedule before execution. The
runtime architecture and `BUG-066` record now state the restored contract.
Focused runtime-module, default CPU, and strict structural gates pass.

[`BUG-072`](../archive/BUG-072-declarative-sim-signal-fields-no-per-tick-edge.md) —
declarative runtime-module signal regression retired on 2026-07-10 at
`CPUContracted`. Commit `f45371c6` translates every `WaitForSignals` and
`SignalLabels` entry into real named edges in each fixed-step `FrameGraph`, and
the baseline bundle exposes `TransformUpdate` through the same contract. The
closure regression registers a consumer before its producer, marks both passes
parallel-capable, declares no manual builder edges, and proves the compiled
layers and execution remain producer-before-consumer. Focused runtime-module,
default CPU, and strict structural gates pass; no host or GPU follow-up is owed.

[`PROC-021`](../archive/PROC-021-docs-sync-strict-mode-wiring.md) — structural CI
enforcement wiring retired on 2026-07-10 at `Operational`. `ci-docs` now fetches
full history, compares documentation rules against the pull request base SHA,
and fails strictly on missing synchronized docs; it also runs the
task-state-link checker strictly. A static workflow regression pins both
commands, their strict/diff arguments, base-history checkout, and its own CI
invocation. The canonical docs-sync policy, generated reference mirror, and
skill routing summary now describe current enforcement. Both strict validators,
the complete local docs-validation equivalent, skill sync, task policy, docs
links, test layout, and session-brief freshness pass.

[`BUG-069`](../archive/BUG-069-runtime-module-systems-scheduled-before-ecs-bundle.md) —
runtime-module baseline ECS ordering regression retired on 2026-07-10 at
`CPUContracted`. Runtime registers the promoted ECS bundle before module sim
systems, seeds its external signal labels during boot finalization, and derives
the declarative wait/signal edges in each fixed-step `FrameGraph`. A real-engine
contract mutates a transform in `OnSimTick` and proves a module declaring both
`WaitForSignals = {"TransformUpdate"}` and `Read<WorldMatrix>` compiles and
observes the current substep's matrix. The focused runtime selector and default
CPU gate pass. Implementation commits are `3102e60f`, `c3794716`, `aead3bb0`,
and `f45371c6`; `BUG-072` owns the remaining durable signal-unification audit
and explicit parallel-pass regression.

[`BUG-077`](../archive/BUG-077-architecture-backlog-index-links-retired-arch-tasks.md) —
architecture backlog state-link regression retired on 2026-07-10. Commit
`09183ea1` promoted the `Retired seam tasks` lead-in in
`tasks/backlog/architecture/README.md` to a recognized history heading, keeping
all seven `ARCH-007`..`ARCH-013` links available without classifying their done
records as active backlog. Strict task-state links pass with zero findings;
task policy, documentation links, and generated session-brief freshness also
pass. No task content or retirement decision changed.

[`CI-004`](../archive/CI-004-label-derived-test-build-aggregates.md) — gate-specific test
build aggregates retired on 2026-07-10 at `Operational`. Canonical test-target
and label metadata now derives `IntrinsicPrFastTests`, `IntrinsicCpuTests`,
`IntrinsicGpuVulkanTests`, and `IntrinsicPrSmokeTests`; specialized workflows
build only the executable closure selected by their CTest filters, while
`IntrinsicTests` remains the complete default aggregate. Hosted artifacts show
PR-fast edges down 10.3% but build time 2.6–4.2% above the CI-003 median, so no
PR-fast speedup is claimed. Three comparable Vulkan build phases show 32.6%
fewer edges and 30.4–49.8% lower build time; their main `gpu;vulkan` selectors
passed, while the unrelated isolated frame-pacing capture remains `BUG-064`.
Implementation commits are `e741293d`, `bc5c7cea`, and `99c579e0`.

[`BUG-078`](../archive/BUG-078-coretasks-counterevent-rearm-uaf.md) — Core.Tasks
`CounterEvent` rearm coroutine lifetime race retired on 2026-07-10 at
`CPUContracted`. A legal early-resume interleaving let one worker complete and
destroy a detached task frame while the worker that originally called
`resume()` still inspected the same handle with `done()`, producing an ASan
heap-use-after-free. Detached task frames now self-destroy through
non-suspending final suspend, and `Scheduler::Reschedule()` treats each handle
as a single-use resumption token with no post-resume access. The deterministic
regression waits for a frame-owned destructor sentinel before the original
`await_suspend()` returns and passed 100 repetitions; the default CPU gate and
the CI-007 clean no-ccache comparison also pass. `BUG-079` owns the adjacent
abandoned-parked-continuation reclamation gap.

[`CI-003`](../archive/CI-003-ci-gate-timing-observability-and-cancellation.md) — CI gate
latency observability and stale-run cancellation retired on 2026-07-09 at
`CPUContracted`. The stable
`ci.gate-latency.github-ubuntu-24.04.v1` profile now emits canonical
configure/build/test/total timing and contextual diagnostics from every
compile-heavy workflow; PR-scoped concurrency cancels only superseded runs.
The distinct manifest-backed aggregate baseline preserves the same five
pull-request commits across six gate populations, with all 30 jobs and 25 runs
verified against the authenticated GitHub API. Cold compile median/p95 evidence,
warm-vcpkg state, selected test counts, raw run/job IDs, compile hotspots, and
the invalid pre-BUG-064 Vulkan test-timing caveat are retained for
`CI-004..009`. No build tree, object, or BMI cache was introduced. Remote
publication remains blocked until the GitHub token has `workflow` scope; the
completed local commits are `f168f15d`, `78c4b152`, and `fbc3ae9d`.

[`BUG-063`](../archive/BUG-063-streaming-import-contract-tests-flaky-on-main.md) —
streaming-import contract-test flake retired on 2026-07-09. The two streaming
tests and the representative promoted-format test used the same external glTF
buffer path, so parallel CTest execution let the representative fixture remove
the buffer while background model-scene decoders still needed it. This produced
terminal `DecodeFailed`/`AssetDecodeFailed` results rather than exhausting the
256-frame wait budget. `TriangleGltfJson` now accepts its external-buffer URI
and all three fixtures use distinct matching paths. The exact `-j 3` repro
failed both streaming tests before the fix and passed 25 repetitions per test
afterward; the full 16-test format-coverage group passed 25 repetitions, and the
combined stacked default CPU gate passed 3635/3635. No production streaming
behavior changed.

[`BUG-066`](../archive/BUG-066-runtime-module-system-registration-order.md) —
RuntimeModule system ordering regression retired to `tasks/done/` on
2026-07-09 at `CPUContracted`. PR #1013 appended module systems directly to
the core FrameGraph, whose RAW/WAR/WAW hazards intentionally follow pass
registration order; reversing `AddModule` therefore reversed the system
schedule and ran a reader before its writer. `SimSystemDesc` now carries a
unique stable `PassName` plus explicit `WaitForSignals`/`EmitSignals`.
`ModuleRegistrationSink` topologically orders those causal edges with a stable
name tie-break before the FrameGraph applies resource hazards, and rejects
duplicate names or signal cycles. The original reversed-order failures and
new fail-closed cases pass; the default CPU gate passed 3635/3635. Core
FrameGraph semantics are unchanged, and `Operational` remains owned by
`ARCH-012`.

[`BUG-065`](../archive/BUG-065-vcpkg-egress-blocked-cryptic-403.md) — vcpkg bootstrap
egress block surfacing as a cryptic `curl: (22) ... 403` retired to
`tasks/done/` on 2026-07-09. Managed/cloud sessions that allow git-protocol
clones and the GitHub API but deny general HTTPS GETs to github.com (HTTP 403)
cannot download vcpkg's prebuilt tool binary (`vcpkg-glibc` release assets), so
`cmake --preset ci` aborts during `project()` with no useful breadcrumb. In-repo
mitigation: a new `tools/setup/vcpkg_preflight.sh` single-probe egress check
(emits `ready|reachable|blocked|unknown`, prints an actionable diagnosis on a
blocked host); `tools/setup/bootstrap_vcpkg.sh` now gates on it and fails closed
with that diagnosis (exit 3; `INTRINSIC_VCPKG_FORCE=1` overrides); and
`tools/setup/agent_session_setup.sh` always records the status to
`/tmp/intrinsic-session-setup.vcpkg` with an opt-in `--bootstrap-vcpkg` pre-bake.
Verified live in a blocked sandbox (preflight and bootstrap gate both emit the
diagnosis and exit 3; `bash -n` clean; status marker records `blocked`). The
egress policy itself is environment-level and out of repository scope;
`docs/build-troubleshooting.md` documents the operator fixes (allow the host, or
pre-bake vcpkg + a binary cache into the snapshot).

[`ARCH-011`](../archive/ARCH-011-runtimemodule-contract-service-registry.md) —
RuntimeModule contract, `EngineSetup`, and two-phase `ServiceRegistry` retired
to `tasks/done/` on 2026-07-09 at `CPUContracted`. The fifth and final additive
ADR-0024 kernel seam adds `Extrinsic.Runtime.ServiceRegistry` (compile-time
TypeToken-keyed `Provide`/`Require`/`Find`; a missing `Require` aborts boot
fail-closed, naming the requesting module and the missing service type) and
`Extrinsic.Runtime.Module` (`IRuntimeModule` with
`Name`/`OnRegister`/`OnResolve`/`OnShutdown`, the narrow `EngineSetup`
registration surface, `SimSystemDesc`, the `FramePhase`/`FrameHookContext`
frame-hook vocabulary, the inspectable `ModuleRegistrationSink`, and the
built-in `EngineWillShutDown` lifecycle event). `Engine` gains
`AddModule`/`EmplaceModule`, a two-phase `OnRegister`→`OnResolve` boot
(registration order is not load-bearing; the resolve loop names the active
requester for fail-closed diagnostics), module sim-system application into the
fixed-step FrameGraph after the built-in ECS bundle, module frame-hook
invocation at the `AfterCommandDrain`/`UiBuild`/`BeforeExtraction`/
`Maintenance` phases in `RunFrame()`, and a two-phase shutdown that publishes
and pumps `EngineWillShutDown` before running `OnShutdown` in reverse order. No
`Engine&` crosses the module surface (ADR-0024 D13). The change is additive:
existing `IApplication` tick/hook behavior is unchanged and no domain feature
is extracted yet. `tests/contract/runtime/Test.RuntimeModuleContract.cpp`
covers two-phase resolve under both add orders, fail-closed `Require`
(`EXPECT_DEATH`), registration-order-independent compiled schedules plus
phase-bucketed hooks, dependency-honored sim-system execution, and
announce-before-`OnShutdown`. Structural gates passed locally
(`check_layering.py --strict` 0 violations, `check_test_layout.py --strict`,
`check_task_policy.py --strict`, `check_doc_links.py`, module-inventory
regeneration); the C++ build and the default CPU-supported test gate could not
run in the cloud session (the vcpkg tool download is blocked by egress policy
with a 403 and no Ninja generator is present), so CI must confirm the build and
the new contract tests before merge. `Operational` remains owned by `ARCH-012`,
which extracts the ClusteringModule onto these seams end-to-end.

Active
[`ARCH-010`](../archive/ARCH-010-kernel-worldregistry-deferred-world-ops.md) —
Kernel WorldRegistry with deferred, two-phase world operations — retired on
2026-07-09 at maturity `CPUContracted`. `Extrinsic.Runtime.WorldRegistry`
now owns boot-world creation, `WorldHandle` keyed `ECS::Scene::Registry`
storage, active-world swaps, and two-phase destroy requests that announce
`WorldWillBeDestroyed`, cancel world-scoped jobs, then tear down at the next
maintenance boundary. `Runtime.Engine` resolves its single-world compatibility
scene through the active world, exposes `Engine::Worlds()`/`ActiveWorld()`,
and passes an explicit world handle into render extraction. Headless
`contract;runtime` coverage verifies create/get, deferred active swaps,
two-phase destroy plus `CancelAllForWorld`, and frame-0 active-world
availability before application initialization. Runtime docs and the generated
module inventory record the kernel mechanism/policy split. `Operational`
remains owned by `ARCH-012`.

Active
[`ARCH-009`](../archive/ARCH-009-kernel-jobservice-snapshot-in-result-out.md) —
Kernel JobService: snapshot-in/result-out background jobs — retired on
2026-07-09 at maturity `CPUContracted`. `Extrinsic.Runtime.JobService` now
provides typed `JobToken`/`WorldHandle` handles, cooperative
`JobCancellation`, `CpuPool` submission through the shared
`Core::Tasks::Scheduler`, a reserved `GpuQueue` target, main-thread
completion gating through `Runtime.KernelEvents`, world-scoped cancellation,
maintenance-phase completion reaping, and diagnostics counters. Headless
`contract;runtime` coverage verifies worker-thread execution, pump-B delivery
on the main thread, cancel-before-start, mid-flight cooperative cancellation,
finished-before-gate suppression, and `CancelAllForWorld` scope isolation.
The module inventory and runtime architecture docs were updated with the
FrameGraph-vs-JobService two-tier rule. `Operational` remains owned by
`ARCH-012`; `GpuQueue` execution is deferred to `RUNTIME-137`.

Active
[`RUNTIME-165`](../archive/RUNTIME-165-extract-async-work-service.md) —
Extract async work service out of Engine — retired on 2026-07-09 at maturity
`Operational`. `Extrinsic.Runtime.AsyncWorkService` now owns the live
`StreamingExecutor` and `DerivedJobRegistry`, constructs the registry over the
executor, and centralizes completion/readback drains, count-limited main-thread
apply, background pumping, shutdown draining, reset ordering, and derived-job
facade delegation. `Runtime.Engine` keeps lifecycle/frame ordering, dependent
subsystem wiring, and public derived-job compatibility facades while no longer
importing/storing the raw executor/registry or branching the frame-loop
maintenance path over both concrete types. Focused runtime async/job/import/
scene/layering coverage passed 56/56, `IntrinsicTests` built, and the default
CPU-supported gate passed 3646/3646.

Active
[`RUNTIME-164`](../archive/RUNTIME-164-extract-asset-residency-service.md) —
Extract asset residency service out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.AssetResidencyService` now owns
`Graphics::GpuAssetCache`, the cache's `AssetEventBus` listener token,
`AssetModelTextureHandoff`, `AssetModelSceneHandoff`, fallback bootstrap
delegation, pending material-binding re-resolution, and asset-residency teardown
ordering. `Runtime.Engine` keeps lifecycle/frame ordering, dependent subsystem
wiring, and public `GetAssetService()` / `GetGpuAssetCache()` compatibility
facades while no longer storing the raw cache/listener/model-handoff members or
constructing/resetting those objects directly. Focused runtime asset/import,
render-extraction, object-space normal bake, and Engine-layering coverage passed
93/93, `IntrinsicTests` built, and the default CPU-supported gate passed
3646/3646.

Active
[`RUNTIME-163`](../archive/RUNTIME-163-extract-render-extraction-service.md) —
Extract render extraction service out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.RenderExtractionService` now owns
the live `RenderExtractionCache`, configured `RenderWorldPool`, last extraction
stats, and frame-index counter. `Runtime.Engine` keeps render-frame phase
ordering, dependent subsystem wiring, and public compatibility facades while no
longer storing the raw cache/pool/stats/counter or calling render-extraction
facade helpers directly. Focused render-extraction, render-world pool, runtime
sandbox, and Engine-layering coverage passed, `IntrinsicTests` built, and the
default CPU-supported gate passed 3646/3646.

Active
[`RUNTIME-162`](../archive/RUNTIME-162-extract-gizmo-frame-service.md) —
Extract gizmo frame service out of Engine — retired on 2026-07-09 at maturity
`Operational`. `Extrinsic.Runtime.GizmoFrameService` now owns live
transform-gizmo interaction state, undo storage, selected-entity scratch,
gizmo/selection pointer interlock, and transform-gizmo packet building.
`Runtime.Engine` keeps frame phase ordering plus `GetGizmoInteraction()` and
`GetGizmoUndoStack()` as public compatibility facades while no longer importing
`GizmoInteraction`, storing the raw gizmo state, or calling the frame-loop gizmo
helper/packet builder directly. Focused gizmo, runtime sandbox, and
Engine-layering coverage passed, `IntrinsicTests` built, and the default
CPU-supported gate passed 3646/3646.

Active
[`RUNTIME-161`](../archive/RUNTIME-161-extract-object-space-normal-bake-service.md) —
Extract object-space normal bake service out of Engine — retired on 2026-07-09
at maturity `Operational`. `Extrinsic.Runtime.ObjectSpaceNormalBakeService`
now owns object-space normal bake GPU-queue lifetime, dependency setup,
ready-frame callback construction, JobService participant registration,
diagnostics access, pending-count access, and dependency clearing.
`Runtime.Engine` keeps lifecycle ordering and queue handoff to import,
scene-document, direct-mesh, and selected-mesh callers while no longer storing
`RuntimeObjectSpaceNormalBakeGpuQueue`, constructing its dependencies, or
registering its JobService participant directly. Focused object-space-normal,
JobService, and Engine-layering coverage passed, `IntrinsicTests` built, and
the default CPU-supported gate passed 3646/3646. `RUNTIME-129` remains the
owner of production Vulkan plan-provider and `gpu;vulkan` smoke closure.

Active
[`RUNTIME-160`](../archive/RUNTIME-160-extract-jobservice-gpu-queue-bridge.md) —
Extract JobService GPU queue bridge out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.JobServiceGpuQueueBridge` now owns
the renderer runtime-frame hook token, installs the JobService GPU-queue
command-recording bridge, detaches the hook before participant teardown, and
forwards the Engine-owned device-idle wait callback into participant shutdown.
`Runtime.Engine` keeps renderer/device ownership, JobService ownership, and
initialize/shutdown ordering while no longer storing a raw frame-hook handle or
calling `RegisterRuntimeFrameCommandHook(...)`,
`UnregisterRuntimeFrameCommandHook(...)`, or
`JobService::RecordGpuQueueFrameCommands(...)` directly. Focused JobService,
object-space normal bake, and Engine-layering coverage passed,
`IntrinsicTests` built, and the default CPU-supported gate passed 3646/3646.

Active
[`RUNTIME-159`](../archive/RUNTIME-159-extract-imgui-editor-bridge.md) —
Extract ImGui editor bridge out of Engine — retired on 2026-07-09 at maturity
`Operational`. `Extrinsic.Runtime.ImGuiEditorBridge` now owns the runtime
`ImGuiAdapter`, shared `Graphics::ImGuiOverlaySystem`, editor callback storage,
renderer overlay attach/detach, adapter initialize/shutdown, per-frame
Begin/End delegation, capture-state reads, and diagnostics access.
`Runtime.Engine` keeps frame phase ordering plus `SetImGuiEditorCallback(...)`
and `GetImGuiAdapter()` compatibility facades. Focused ImGui wiring,
SandboxEditorUi, Engine-layering, and frame-pacing coverage passed,
`IntrinsicTests` built, and the default CPU-supported gate passed 3646/3646.

Active
[`RUNTIME-158`](../archive/RUNTIME-158-extract-frame-pacing-diagnostics.md) —
Extract frame pacing diagnostics out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.FramePacingDiagnostics` now owns
the exported `RuntimeFramePacingDiagnostics` record and pure ImGui adapter /
render-graph counter mirroring helpers. `Runtime.Engine` keeps
`Engine::GetLastFramePacingDiagnostics()` as the public compatibility facade
and composes only phase timing writes plus helper delegation. Focused
frame-pacing, sandbox acceptance, ImGui adapter wiring, and Engine-layering
coverage passed, `IntrinsicTests` built, and the default CPU-supported gate
passed 3646/3646.

Active
[`RUNTIME-157`](../archive/RUNTIME-157-extract-selection-readback-state.md) —
Extract selection readback state out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.SelectionReadback` now owns
in-flight pick readback context retention, pending pick drain into
`Graphics::SelectionSystem`, completed readback FIFO drain, sequence-correlated
`SelectionController` hit/no-hit application, context-aware primitive
refinement, and the editor-facing refined primitive cache/generation.
`Runtime.Engine` keeps the public refined-primitive accessors as delegating
facades and owns only the composition call sites; `Runtime.SceneDocument`
clears the cache through the same state object during scene replacement.
Focused selection/readback/refinement/layering coverage passed,
`IntrinsicTests` built, and the default CPU-supported gate passed 3646/3646.

Active
[`RUNTIME-156`](../archive/RUNTIME-156-extract-runtime-module-schedule.md) —
Extract runtime-module schedule out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.ModuleSchedule` now owns
runtime-module sim-system/frame-hook records, deterministic dependency
ordering, frame-hook ordering, fixed-step `FrameGraph` pass insertion/context
construction, and frame-hook dispatch. `Runtime.Engine` still owns module
objects, built-in service provisioning, `OnRegister` / `OnResolve` sequencing,
and shutdown calls while delegating schedule storage/finalization/dispatch.
Focused runtime-module and Engine-layering coverage passed, `IntrinsicTests`
built, and the default CPU-supported gate passed 3644/3644.

Active
[`RUNTIME-155`](../archive/RUNTIME-155-extract-runtime-input-action-registry.md) —
Extract runtime input-action registry out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.InputActions` now owns the
input-action descriptor/service/context/handle API, handle allocation,
registration state, key-edge trigger checks, ImGui keyboard-capture
suppression, callback failure logging, and per-frame dispatch. `Runtime.Engine`
re-exports the API for compatibility and keeps `RegisterInputAction(...)` /
`UnregisterInputAction(...)` as delegating facades, while
`Runtime.Engine.FrameLoop.cppm` no longer carries input-action trigger or
dispatch helpers. Focused runtime input-action and Engine-layering coverage
passed, and the default CPU-supported gate passed 3644/3644.

Active
[`RUNTIME-154`](../archive/RUNTIME-154-extract-reference-scene-control.md) — Extract
reference-scene lifecycle control out of Engine — retired on 2026-07-09 at
maturity `Operational`. `Extrinsic.Runtime.ReferenceSceneControl` now owns the
reference-scene registry, provider registration/resolution, installed
population state, camera-seed caching, and provider teardown. `Runtime.Engine`
keeps `GetReferenceSceneRegistry()`, `IsReferenceSceneInstalled()`, and
`GetReferenceCameraSeed()` as public delegating facades, while
`Runtime.Engine.cpp` no longer names provider interfaces, default-provider
registration, population storage, or provider populate/teardown calls. Focused
reference-scene, Engine-layering, and runtime sandbox acceptance coverage
passed, and the default CPU-supported gate passed 3644/3644.

Active
[`RUNTIME-153`](../archive/RUNTIME-153-extract-mesh-primitive-view-controls.md) — Extract
mesh primitive-view controls out of Engine — retired on 2026-07-09 at maturity
`Operational`. `Extrinsic.Runtime.MeshPrimitiveViewControls` now owns the
legacy `MeshPrimitiveViewSettings` compatibility translation to authoritative
ECS `RenderEdges` / `RenderPoints` components. `Runtime.Engine` keeps only the
public compatibility facade and extraction-cache invalidation call, and no
longer imports render-geometry component policy or names the render component
types directly. Focused mesh primitive-view, scene lifecycle, and Engine-
layering coverage passed, and the default CPU-supported gate passed 3644/3644.

Active
[`RUNTIME-152`](../archive/RUNTIME-152-extract-runtime-device-bootstrap.md) — Extract
runtime device bootstrap out of Engine — retired on 2026-07-09 at maturity
`Operational`. `Extrinsic.Runtime.DeviceBootstrap` now owns runtime
device-selection policy, backend factory dispatch, Vulkan-requested breadcrumb
policy, and GPU asset fallback-texture descriptor construction. `Runtime.Engine`
keeps the concrete composition responsibilities that need live platform/window
state: creating the platform window, initializing the selected `RHI::IDevice`,
recording startup diagnostics, and wiring renderer-facing services. Focused
device-selection, Vulkan breadcrumb, and Engine-layering coverage passed.

Active
[`PROC-014`](../archive/PROC-014-task-state-index-done-link-cleanup.md) — Task-state
index done-link cleanup — retired on 2026-07-07 at maturity `Retired`. The
rendering and runtime backlog indexes now cite recently retired tasks from
live current-state sections as plain code spans instead of direct links into
`tasks/done/`, preserving the current-state facts while satisfying
`check_task_state_links.py`. No task dependencies, priorities, or engine code
changed.

Active
[`GRAPHICS-121`](../archive/GRAPHICS-121-vulkan-resource-sharing-async-compute.md) —
Vulkan resource sharing includes async compute — retired on 2026-07-07 at
maturity `Operational`. The promoted Vulkan backend now creates buffers and
images with every live render-graph submit queue family: graphics, async
compute, and transfer when present and distinct. Command contexts also carry
their bound queue family and sanitize Sync2 barrier stage masks so async
compute command buffers do not record graphics-only stages. The
`DefaultRecipeSurfaceGpuSmoke` Vulkan smoke passed 8/8, the default
CPU-supported gate passed 3596/3596, and the broader `gpu;vulkan` label
intersection improved to the existing unrelated 270/272 state
(`GraphicsRenderGraph.LifetimeFirstAndLastUseTracksPassIndices` and
`ExtrinsicSandbox.FramePacingDiagnosticCapture`).

Active
[`UI-033`](../archive/UI-033-compositional-appearance-domain-windows.md) —
Compositional Appearance domain windows — retired on 2026-07-07 at maturity
`CPUContracted`. Sandbox editor `Appearance` windows now use selected-entity
render-lane availability instead of exact provenance-domain matching, so
`PointCloud / Appearance` can expose point/vertex controls for point-cloud,
graph, and mesh entities when the point lane exists, while graph and mesh
appearance continue to layer edge/connectivity and surface/face controls.
Processing and raw Properties windows remain exact-domain gated. Focused
`SandboxEditorUi` coverage passed, and the default CPU-supported gate passed
3596/3596.

Active
[`GRAPHICS-120`](../archive/GRAPHICS-120-framegraph-compiler-executor-efficiency.md) —
Framegraph compiler/executor efficiency and hygiene polish — retired on
2026-07-06 at maturity `CPUContracted`. The framegraph now treats
`TextureUsage::ColorAttachmentRead` as a read-only barrier state, keeps
transient texture estimates routed through `RHI::EstimateTextureStorageBytes`,
publishes compile validation through explicit result plumbing instead of
`thread_local` state, emits executor/renderer barriers through shared indexed
sorted-packet ranges, and uses indexed/sorted compiler paths for packet
insertion plus duplicate typed pass-id validation. `RenderGraph::Reset()` and
stateful `RenderGraph::Compile()` now recycle pass declaration and compiler
scratch while keeping `CompiledRenderGraph` outputs value-owned. PR-fast smoke
benchmarks captured barrier-emission, compiler-indexing, and allocation-counter
improvements, and the default CPU-supported gate passed 3584/3584. Compile
caching, placed transient aliasing, and parallel command recording remain owned
by `GRAPHICS-117`, `GRAPHICS-118`, and `GRAPHICS-119`.

Active
[`BUG-061`](../archive/BUG-061-texture-bake-gpu-gate-and-service-context.md) —
Texture bake UI misses service context and GPU availability gate — retired on
2026-07-06 at maturity `CPUContracted`. Live sandbox editor contexts now pass
`Engine::GetAssetService()` into `SandboxEditorContext`, so selected-mesh
texture baking is no longer disabled solely because the runtime generated-texture
service is absent from the attached UI context. The bake controls and command
execution now also require an operational `RHI::IDevice`, keeping texture baking
disabled/refused on Null or fail-closed graphics backends so the CPU-backed
compatibility baker is not used from the default headless path. Focused
SandboxEditorUi coverage locks the service/device wiring, operational-device
success path, and non-operational refusal diagnostics.

Active
[`RUNTIME-144`](../archive/RUNTIME-144-post-import-processor-and-ux-policy-seam.md) —
Post-import processor and import UX-policy seam — retired on 2026-07-06 at
maturity `Operational`. `Runtime.Engine` now owns generic post-import
processor, import-authoring, import-completed, input-action, and runtime GPU
participant registries/dispatch only. The sandbox/default composition helper
`Extrinsic.Runtime.SandboxDefaultPolicies` installs the standard direct-mesh
generated-normal post-import processor, import authoring defaults, focus and
auto-select import completion UX, and `F` focus-on-selection action; the
sandbox app explicitly registers and unregisters that bundle. Bare-engine
contracts now prove imports and `RunFrame` remain minimal with no default
bundle installed, while sandbox fixture coverage preserves the prior default
behavior. GPU re-domaining of the generated-normal bake remains owned by
`GRAPHICS-104`/`RUNTIME-129`.

Active
[`RUNTIME-141`](../archive/RUNTIME-141-async-editor-method-command-lane.md) — Async
editor method-command lane — retired on 2026-07-05 at maturity `Operational`.
The Sandbox editor CPU method buttons for K-Means, Progressive Poisson,
mesh denoise/remesh/subdivide/simplify, ICP registration, mesh curvature,
mesh/graph/point-cloud vertex-normal recompute, point-cloud outlier removal,
and selected mesh UV regeneration now submit runtime-owned derived jobs with
main-thread snapshot/apply validation instead of running heavy solves inside
the ImGui callback. The selected-mesh UV panel reports queued/applying/complete
job state, a Null-window frame-loop contract proves render frames continue
while a deliberately slow editor job runs, and duplicate active
same-entity/same-domain/same-output submits return the existing pending handle
instead of enqueueing duplicate work. GPU readback adoption remains owned by
`RUNTIME-137`/`METHOD-014`, selected-entity derived-model caching remains owned
by `RUNTIME-138`, and model/texture/scene-file IO remains owned by
`RUNTIME-142`.

Active
[`UI-030`](../archive/UI-030-editor-frame-pacing-diagnostics.md) — Sandbox EditorUI
frame-pacing diagnostics — retired on 2026-07-05 at maturity `Operational`.
`Engine::RunFrame()` now publishes bounded phase timings, ImGui producer
diagnostics include editor callback and draw-data copy timings, promoted Vulkan
lifecycle diagnostics expose wait/acquire/submit/present microseconds, and
`ExtrinsicSandbox --frame-pacing-report` writes validated
`intrinsic.frame_pacing.v1` JSON for bounded local captures. The measured
default capture ranked present/fallback-frame-lifecycle time highest while
ruling out editor callback rebuilds, ImGui draw-data copying, render-graph
compile/execute work, selected-entity readback, and fixed-step simulation as
dominant causes. The default sandbox run also exposed a promoted Vulkan
validation-gate fallback (`BarrierValidationFailed` from shader-interface
warnings), now split to open `BUG-056` rather than folded into the diagnostic
task.

Backlog
[`GRAPHICS-115`](../archive/GRAPHICS-115-object-space-normal-gpu-dilation.md) —
Object-space normal GPU dilation pass — retired on 2026-07-04 at maturity
`Operational` on Vulkan-capable hosts and `CPUContracted` for backend-neutral
planning/resource/command contracts. Padded generated-normal bakes now consume
graphics-owned dilation resources: a fullscreen dilation pipeline and
same-extent sampled/color-target scratch texture, allocated through RHI handles
without leaking backend-native types. The recorder reserves sampled bridge slots
4/5 for output/scratch, ping-pongs one fullscreen pass per requested padding
texel, preserves covered texels, fills neighboring alpha-zero gutter texels on
GPU, and fails closed without a CPU dilation fallback when resources are
missing or the backend is non-operational. Focused CPU/null contracts and the
opt-in `ObjectSpaceNormalTextureBakeGpuSmoke` Vulkan smoke passed.

Backlog
[`BUG-055`](../archive/BUG-055-taskgraph-counterevent-latch-destruction-race.md) —
TaskGraph::Execute / CounterEvent latch-destruction race — retired on
2026-07-04 at maturity `CPUContracted`. `TaskGraph::Execute()` now keeps its
parallel completion state alive through shared ownership by the waiting caller
and every dispatched worker closure, with completion callbacks stored on that
state rather than stack-captured by worker lambdas. `CounterEvent::Signal()`
captures the scheduler wait token before publishing zero and treats the
zero-transition as the signaler's last event-member access. A new
`unit;core` stress regression repeatedly executes small parallel graphs; the
focused `CoreTaskGraph` repeat gate passed 50/50 under the sanitizer-enabled
`ci` preset, and the default CPU-supported gate passed 3476/3476. `CORE-005`
is now unblocked for the future nonblocking submit/completion-token API.

Active
[`GEOM-057`](../archive/GEOM-057-fast-uv-atlas-charting-and-packing.md) — Fast UV atlas
charting and packing replacement path — retired on 2026-07-03 at maturity
`ParityProven`. `Geometry.UvAtlas` now defaults to the geometry-owned
`FastStaged` backend with deterministic multi-face charting, per-chart solver
attempts, shelf packing, chart/seam diagnostics, property xrefs, and visible
xatlas compatibility fallback when explicitly enabled. The promotion benchmark
`geometry.uv_atlas.fast_staged_promotion.smoke` passed 7/7 fixtures with zero
fast flips, zero fast chart overlaps, zero quality error, and a mean
fast/xatlas runtime ratio of 0.282, so the default replacement decision is
gated by machine-readable benchmark output. Removing xatlas itself remains a
separate retirement decision.

Active
[`RUNTIME-125`](../archive/RUNTIME-125-aos-static-fast-lane.md) — Optional AoS fast lane
for static geometry — retired on 2026-07-02 at maturity `CPUContracted`. The
slice added the PR-fast SoA/interleaved probe benchmark and the planning-only
storage-lane plus static-to-dynamic promotion contracts, without changing the
default ADR-0022 SoA storage model. Operational AoS allocation, shader variants,
promote-on-edit behavior, and Vulkan parity evidence are split to open follow-up
`RUNTIME-139`.

Active
[`METHOD-013`](../archive/METHOD-013-progressive-poisson-disk-gpu-backend.md) —
Progressive Poisson-disk sampling: GPU backend contract slices — retired on
2026-07-02 at maturity `CPUContracted`. The runtime seam now has backend
selection/fallback telemetry, Vulkan planning and shader assets, recordable
dispatch contracts, upload/readback-copy ownership, parsed readback payloads,
and CPU-reference parity diagnostics. Public GPU result return,
`gpu;vulkan` parity tests, and benchmark metric extension are split to open
follow-up `METHOD-014`.

Active
[`GRAPHICS-104`](../archive/GRAPHICS-104-gpu-object-space-normal-texture-bake.md) —
GPU Object-Space Normal Texture Bake — retired on 2026-07-02 at maturity
`CPUContracted`. The graphics-owned zero-padding raster-bake plan/recording,
object-space-normal shader/material contract, GPU-produced cache texture
residency, and runtime queue/submission/binding helpers are now recorded as the
completed slice. Padded GPU dilation was split to follow-up `GRAPHICS-115`,
which is now retired; engine/import render-thread scheduling and material swaps
remain owned by `RUNTIME-129`.

Backlog
[`GRAPHICS-110`](../archive/GRAPHICS-110-imgui-upload-buffer-in-flight-safety.md) —
Per-frame/ring ImGui upload buffers for in-flight safety — retired on
2026-07-02 at maturity `Operational`. The ImGui, transient-debug, and
visualization-overlay upload helpers now partition host-visible upload storage
by frame-in-flight slot so a new frame cannot overwrite ranges still consumed
by an older frame. CPU contract tests cover deterministic multi-slot behavior,
and targeted `gpu;vulkan` ImGui/overlay/sandbox smokes passed on the
Vulkan-capable host. Retained overlay copy/upload reduction is retired by
follow-up `GRAPHICS-114`.

Backlog
[`LEGACY-012`](../archive/LEGACY-012-migrate-legacy-consumer-tests.md) — Migrate legacy
consumer tests to promoted coverage — retired on 2026-07-01 at maturity
`Retired`. The remaining bare legacy test consumers were removed from the
configured test graph or were already represented by promoted coverage. This
made the subtree deletion gates mechanical rather than feature-blocked.

Backlog
[`LEGACY-010`](../archive/LEGACY-010-delete-src-legacy-runtime.md) — Delete
`src/legacy/Runtime/` — retired on 2026-07-01 at maturity `Retired`. Runtime
was deleted first in the final sweep because no other legacy subtree depended on
it. CMake legacy runtime wiring and legacy allowlist rows are gone.

Backlog
[`LEGACY-008`](../archive/LEGACY-008-delete-src-legacy-graphics.md) — Delete
`src/legacy/Graphics/` — retired on 2026-07-01 at maturity `Retired`. Graphics
was deleted after Runtime so its legacy-internal consumers were gone before the
Interface/ECS/Asset/RHI/Core removals. The promoted `src/graphics/*` surfaces
remain the only graphics implementation roots.

Backlog
[`LEGACY-001`](../archive/LEGACY-001-delete-src-legacy-interface.md) — Delete
`src/legacy/Interface/` — retired on 2026-07-01 at maturity `Retired`. The
Interface subtree was removed after Runtime and Graphics no longer consumed it;
`Interface::GUI` remains a retired non-promoted endpoint.

Backlog
[`LEGACY-006`](../archive/LEGACY-006-delete-src-legacy-ecs.md) — Delete `src/legacy/ECS/`
— retired on 2026-07-01 at maturity `Retired`. The ECS legacy subtree was
removed after Runtime and Graphics, leaving `Extrinsic.ECS.*` as the ECS module
surface.

Backlog
[`LEGACY-004`](../archive/LEGACY-004-delete-src-legacy-asset.md) — Delete
`src/legacy/Asset/` — retired on 2026-07-01 at maturity `Retired`. The Asset
legacy subtree was removed after its legacy Runtime/Graphics consumers were
gone; promoted `Extrinsic.Asset.*` plus runtime handoff seams own the retained
asset behavior.

Backlog
[`LEGACY-009`](../archive/LEGACY-009-delete-src-legacy-rhi.md) — Delete `src/legacy/RHI/`
— retired on 2026-07-01 at maturity `Retired`. RHI was removed after Runtime,
Graphics, Interface, ECS, and Asset no longer consumed it; promoted
`Extrinsic.RHI.*` and `Extrinsic.Backends.Vulkan` own the retained RHI/backend
surface.

Backlog
[`LEGACY-005`](../archive/LEGACY-005-delete-src-legacy-core.md) — Delete
`src/legacy/Core/` — retired on 2026-07-01 at maturity `Retired`. Core was
removed last after all consumer subtrees and legacy test consumers were gone.
The generated module inventory now contains promoted modules only, and the
layering allowlist is empty.

Active
[`RUNTIME-134`](../archive/RUNTIME-134-progressive-poisson-interactive-playground.md) —
Interactive progressive-Poisson sampling playground in the Sandbox — retired on
2026-06-30 at maturity `CPUContracted`. The Sandbox now exposes METHOD-012 over
selected point-cloud and mesh inputs, including GEOM-035 mesh surface sampling,
validated `sandbox.progressive_poisson` config-control routing, debounced
reruns, point visualization via `p:poisson_level`, `p:poisson_phase`,
`p:poisson_splat_radius`, and `p:poisson_prefix_visible`, plus CPU backend id
and per-level accepted-count readouts. Headless runtime tests cover direct
command/direct-method equivalence, config-path equivalence, mesh sampling, and
deterministic property publication; the default CPU-supported gate passed.
GPU backend/parity remains blocked by `GRAPHICS-108` and is owned by
`METHOD-013`; future Sandbox backend-toggle UI is tracked by `RUNTIME-136`.

Active
[`PROC-013`](../archive/PROC-013-graphify-knowledge-graph-discovery-aid.md) —
Knowledge-graph discovery aid (graphify adapters + shared setup) — retired on
2026-06-29 at maturity `Scaffolded`. The optional graphify discovery path now
builds a deterministic module DAG plus paper/method/code graph from repository
parsers, registers the opt-in `knowledge-graph` MCP server, and shares setup
entrypoints under `tools/setup/` so provisioning is not Claude-only. The graph
remains a non-authoritative navigation aid; layering, method manifests, and
paper contracts remain the gates. Optional engine-module method edges, CI
smoke, and fixture adapter tests remain deferred until the graph becomes
load-bearing.

Active
[`GEOM-053`](../archive/GEOM-053-geometry-reuse-deterministic-sampling.md) — Geometry
reuse and deterministic sampling cleanup — retired on 2026-06-29 at maturity
`CPUContracted`. `Geometry.Random` now owns the shared deterministic seed-mixing
and Gaussian displacement helper used by graph and point-cloud utilities,
`Geometry.Sphere.Sampling` exposes seed-bearing random-sampling overloads while
preserving deterministic defaults, and graph edge-crossing orientation uses the
promoted robust predicate where it preserves layout semantics. Focused geometry
coverage pins deterministic sphere sampling and Gaussian noise reuse; broader
mesh conversion, normal-estimation, and domain-view adoption remain deferred to
dedicated compatibility or algorithm slices.

Active
[`GRAPHICS-107`](../archive/GRAPHICS-107-reconcile-framerecipe-renderrecipe-vocabulary.md) —
Reconcile the FrameRecipe vs RenderRecipe vocabularies — retired on 2026-06-28
at maturity `CPUContracted`. Renderer docs and the canonical frame-graph
architecture doc now identify `FrameRecipe*` as the live per-frame driver,
`RenderRecipe*` as the contract/config overlay, and
`FrameRecipeOverride` / `ProjectFrameRecipeOverride(...)` as the constrained
bridge between them. Focused renderer contract tests cover mapped optional-slot
feature disables, valid-but-unmapped extension-slot rejection, unknown slot
rejection, and fixed-core mutation/disable rollback without adding new contract
vocabulary or arbitrary pass-graph injection.

Backlog
[`DOCS-004`](../archive/DOCS-004-frame-graph-doc-recipe-config-lane.md) — Promote
frame-graph.md from stub and document the recipe-config lane — retired on
2026-06-28 as a docs-only synchronization task. `docs/architecture/frame-graph.md`
now documents `FrameRecipe*` as the live per-frame composition driver,
`RenderRecipeConfig` as the side-effect-free config overlay, runtime/editor/
agent edit lanes, boot and hot `render.default_recipe_config_path` activation,
and the fixed-core guard that limits overrides to supported optional slots and
binding/output changes. The legacy-background `rendering-three-pass.md` remains
context only.

Backlog
[`GEOIO-003`](../archive/GEOIO-003-mesh-pointcloud-io-breadth.md) — Mesh and point-cloud
IO breadth — retired on 2026-06-28 at maturity `CPUContracted`.
`Geometry::MeshIO::WriteOFF` now provides deterministic ASCII OFF export with
round-trip, determinism, invalid-topology, bad-path, and non-finite fail-closed
coverage. `Geometry::PointCloudIO` now exports strict ASCII readers for PTS,
PWN, CSV, 3D, and TXT with committed fixtures, deterministic reads, explicit
malformed/empty/non-finite diagnostics, and normal/color population for the
supported layouts. This is module-level geometry coverage; runtime/assets route
widening was intentionally left out of scope.

Backlog
[`UI-026`](../archive/UI-026-editor-curvature-analysis-window.md) — Sandbox EditorUI
curvature analysis window and principal-direction field — retired on
2026-06-28 at maturity `CPUContracted`. `Mesh > Processing > Curvature` now
routes through a runtime-owned command/result surface that builds a scratch
halfedge mesh from selected mesh `GeometrySources`, calls the `GEOM-040`
`Geometry::Curvature::ComputeCurvature` backend, and publishes canonical
`v:mean_curvature` / `v:gaussian_curvature` scalar properties plus
`v:principal_dir1` / `v:principal_dir2` direction fields when available.
Successful commits are undoable through `EditorCommandHistory`, stamp
`DirtyVertexAttributes`, and leave renderer/RHI uploads to deferred extraction.
`CurvatureVisualizationAdapter` reuses scalar colormap packets and emits
principal-direction vector-field packets when direction properties are present,
falling back to scalar-only diagnostics for absent or invalid direction data.

Backlog
[`GEOM-034`](../archive/GEOM-034-geometry-property-api-doc-audit.md) — Geometry property
API documentation audit — retired on 2026-06-28 at maturity `Scaffolded`
(documentation synchronization endpoint). The audit made
`docs/architecture/geometry-api-style.md` the coherent source for property
name lifetime, domain-prefix naming, validity, const lookup, bool/proxy access,
and descriptors, and replaced stale higher-layer shared-ownership `MeshView`
wording in `rendering-target-architecture.md` with links to the current
geometry-owned property/domain-view contracts.

Backlog
[`GEOM-042`](../archive/GEOM-042-mesh-normal-bilateral-denoiser.md) — Mesh normal-based
bilateral denoiser — retired on 2026-06-28 at maturity `CPUContracted`.
`Geometry::Smoothing` now exports the two-stage bilateral mesh denoiser with
face-normal filtering, normal-projection vertex updates, deterministic
diagnostics, and fail-closed handling for empty, non-manifold, degenerate,
non-finite, and invalid-parameter inputs. The default CPU gate passed after the
landed implementation, unblocking `UI-024`.

Backlog
[`GEOM-041`](../archive/GEOM-041-fem-laplacian-mass-stiffness-variants.md) — FEM
Laplacian mass/stiffness variants and edge-weight modes — retired on
2026-06-28 at maturity `CPUContracted`. DEC assembly now has Graph, Fujiwara,
and ModifiedNormal stiffness modes plus Sum, Barycentric, Voronoi, and
Galerkin mass modes, with `ClampedHalfedgeCotan` in mesh utilities and
row-sum/symmetry/SPD/fail-closed tests.

Backlog
[`GEOM-040`](../archive/GEOM-040-curvature-tensor-principal-directions.md) — Mesh
curvature tensor and principal directions — retired on 2026-06-28 at maturity
`CPUContracted`. `Geometry.Curvature` publishes `v:principal_dir1` and
`v:principal_dir2`, exposes `ComputeCurvatureTensor`, and reuses exported
`Geometry::PCA::SymmetricEigen3`; analytic curvature tensor tests and the
default CPU gate passed. This unblocks `UI-026`.

Active
[`RUNTIME-128`](../archive/RUNTIME-128-default-lit-material-for-materialless-imports.md)
— Default lit material for material-less imported primitives — retired on
2026-06-28 at maturity `CPUContracted`. Runtime model-scene materialization now
binds material-less primitives to a lazily created neutral lit StandardPBR
material while preserving slot 0 as the unlit missing/invalid material
indicator. The regression is covered by the runtime handoff contract test and
the default CPU gate.

Active
[`METHOD-012`](../archive/METHOD-012-progressive-poisson-disk-cpu-reference.md) —
Progressive Poisson-disk sampling: paper intake + CPU reference backend —
retired on 2026-06-28 at maturity `CPUContracted`. The method package now has
a deterministic CPU reference backend, manifest, docs, correctness tests, and a
smoke benchmark with quality metrics. Method and benchmark manifests validate,
and the default CPU gate passed, unblocking `METHOD-013` and `RUNTIME-134`.

Backlog
[`PROC-012`](../archive/PROC-012-resolve-duplicate-geom-027-id.md) — Resolve duplicate
`GEOM-027` task ID — retired on 2026-06-27 at maturity `Retired`. Two files
declared `id: GEOM-027`: the canonical property-name-lifetime contract (depended
on by `GEOM-033`/`GEOM-034`) and an unrelated research-control-surface /
shared-CPU-GPU backend-seam KMeans-exemplar task seeded by `9ed14b4`. The latter
was renumbered to `GEOM-052` (file rename via `git mv`, front-matter `id:`,
title, and every inbound reference in `tasks/backlog/README.md`, `PROC-010`,
`METHOD-013`, and `DOCS-003`), leaving `GEOM-027` bound to the property sequence
so its dependency graph is intact. `tools/agents/check_task_policy.py --root .
--strict` now reports 0 findings (was 1), and `check_doc_links.py` reports no
broken links.
[`BUG-046`](../archive/BUG-046-flaky-coretaskgraph-mainthread-ready-queue-ordering.md) —
Flaky `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` — retired
on 2026-06-24 at maturity `CPUContracted`. `TaskGraph::Execute()` now collects
newly-ready successors into a batch and publishes all main-thread-ready entries
under one queue lock before dispatch can drain them, preserving priority/cost
ordering for simultaneously-ready main-thread work. The regression no longer
uses the fixed `40ms` sleep and the focused repeat run plus default CPU gate
passed.

Backlog
[`BUG-049`](../archive/BUG-049-gpuworld-geometry-rebind-upload-barriers.md) — GpuWorld
geometry rebind lacks upload-to-read barriers — retired from stale backlog on
2026-06-22 at maturity `CPUContracted`; implementation landed in `843e4fb3`.
The audit confirmed `GpuWorld` tracks one-shot pending upload barriers for
direct `IDevice::WriteBuffer` paths, `Renderer` drains them before culling and
draw consumers, and the focused geometry-rebind plus dirty-extraction coverage
still passes. The task was already complete but remained listed under active
bugs.

Backlog
[`BUG-048`](../archive/BUG-048-direct-mesh-postprocess-overwrites-recomputed-normals.md) —
Direct mesh post-process overwrites recomputed normals — retired from stale
backlog on 2026-06-22 at maturity `CPUContracted`; implementation landed in
`843e4fb3`. The audit confirmed direct mesh post-process apply preserves
count-matched current `v:normal` data so editor-authored normals survive
deferred materialization, while generated normal texture registration remains
intact. Focused sandbox editor normal recompute regressions still pass.

Backlog
[`BUG-047`](../archive/BUG-047-surface-normal-texture-overrides-vertex-normals.md) —
Surface normal texture overrides vertex-normal shading — retired from stale
backlog on 2026-06-22 at maturity `CPUContracted`; implementation landed in
`843e4fb3`. The audit confirmed promoted forward/GBuffer shader contracts use
packed vertex normals for current surface shading and assert absence of
`mat.NormalID` / `normalTex` sampling. Tangent-space normal-map support remains
out of scope for this temporary attribute-normal policy.

Active
[`BUG-051`](../archive/BUG-051-mesh-color-visualization-property-buffer.md) — Mesh color
visualization lacks automatic property-buffer extraction — retired on
2026-06-22 at maturity `CPUContracted`. Runtime render extraction now
auto-emits mesh `glm::vec4` color property-buffer packets from mesh
`GeometrySources` for per-vertex/per-edge/per-face color-buffer
visualizations, including `v:color`, without requiring an explicit adapter
binding. Missing and unsupported color sources fail closed through adapter
diagnostics, and `VisualizationSyncSystem` now forwards the selected
per-element color-buffer domain into `GpuEntityConfig::VisDomain` so shader
lookup uses the configured vertex/face/edge domain. The structural vertex-color
stream and GPU SoA migration remain owned by RUNTIME-121/RUNTIME-122.

Active
[`BUG-050`](../archive/BUG-050-direct-mesh-first-upload-normals.md) — Direct mesh first
upload lacks computed normals — retired on 2026-06-22 at maturity
`CPUContracted`. The geometry-only runtime mesh materialization helper now
writes explicit or deterministic area-weighted fallback `v:normal` values
before ECS `GeometrySources` publication, so direct mesh imports and
progressive raw model-scene primitives carry count-matched normals on their
first upload. Authored normals remain authoritative, deferred UV/texture-bake
post-processing still applies back to the same entity through dirty extraction,
and generated normal texture bindings remain data-only until the promoted
texture path consumes them.

Active
[`PROC-009`](../archive/PROC-009-import-productivity-skills.md) — Import productivity
skills into repo skill surface — retired on 2026-06-22. The repo-local skill
surface now includes the third-party `teach`, `grilling`, and `grill-me`
productivity skills imported from `mattpocock/skills` commit
`6eeb81b5fcfeeb5bd531dd47ab2f9f2bbea27461`; `.claude/skills` and
`.codex/skills` see them through their existing symlinks to
`tools/agents/skills`. The import preserved the upstream MIT license notice in
`tools/agents/skills/THIRD_PARTY_LICENSES.md`, documents the skills as
standalone manual imports rather than generated `docs/agent/*` mirrors, and
adds a local `teach` guardrail so learning-workspace files are not created at
the IntrinsicEngine repo root without an explicit workspace. The existing
`sync_skills.py --check` gate remains scoped to IntrinsicEngine canonical-doc
mirrors and passed after the import.

Active
[`GRAPHICS-098`](../archive/GRAPHICS-098-gpu-transfer-facade.md) — High-level
`GpuTransfer` facade with correct barrier brackets — retired on 2026-06-22 at
maturity `Operational` on Vulkan-capable hosts (`CPUContracted` elsewhere).
`Extrinsic.Graphics.GpuTransfer` now composes existing RHI seams without adding
new RHI surface: async uploads return facade tickets over `TransferToken`s and
emit their one-shot `TransferWrite -> ShaderRead` ready barrier only from
`DrainCompleted(...)` after completion; the in-command path records
`CopyBuffer` plus the ready barrier on one submitted timeline; readbacks record
the caller-owned `TransferRead` bracket before entering the GRAPHICS-096
readback ring. CPU contract evidence covers completion-gated barriers,
same-timeline copies, readback delivery, fail-closed range validation through
GRAPHICS-095, and diagnostics. Promoted Vulkan evidence covers a device-local
upload/readback round-trip through the facade without `WaitIdle`. Runtime-owned
GPU readback jobs and property write-back remain tracked by `RUNTIME-126`.

Active
[`GRAPHICS-097`](../archive/GRAPHICS-097-async-texture-readback.md) — Async GPU-to-CPU
texture readback through the readback ring on `ITransferQueue` — retired on
2026-06-22 at maturity `Operational` on Vulkan-capable hosts
(`CPUContracted` elsewhere). `Extrinsic.RHI.TransferQueue` now exposes
`DownloadTexture(TextureHandle, TextureLayout, mip, layer, ReadbackSink)` as an
append-only non-blocking readback virtual over the GRAPHICS-096
`ReadbackToken`/`ReadbackSink` drain. Null and non-operational Vulkan fallback
queues fail closed with dropped-readback diagnostics. The live Vulkan queue
validates color `Tex2D` arrays and six-face cubemaps through
`Extrinsic.RHI.TextureUpload`, rejects depth-stencil/unsupported formats,
invalid sinks, missing `TransferSrc` usage, out-of-range subresources, and
non-`TransferSrc` source layouts, records `vkCmdCopyImageToBuffer` into a
recycled mapped readback slot without auto-transitioning the source texture,
and delivers exactly the requested mip/layer bytes from `CollectCompleted()`.
CPU evidence covers Null fail-closed behavior, mock subresource delivery, and
bad-format/subresource/layout drops; opt-in `gpu;vulkan` evidence covers a
multi-mip 2D-array texture upload, caller-owned `ShaderReadOnly -> TransferSrc`
barrier, mip/layer readback, and caller-owned transition back without using
`WaitIdle`. High-level barrier/facade ergonomics remain owned by GRAPHICS-098.

Active
[`GRAPHICS-096`](../archive/GRAPHICS-096-async-buffer-readback-ring.md) — Async GPU-to-CPU
buffer readback ring on `ITransferQueue` — retired on 2026-06-22 at maturity
`Operational` on Vulkan-capable hosts (`CPUContracted` elsewhere).
`Extrinsic.RHI.TransferQueue` now exposes `ReadbackToken`, `ReadbackSink`,
`DownloadBuffer(...)`, readback completion polling, and transfer diagnostics for
queued/completed/dropped downloads, staged bytes, and ring high-water. Null and
non-operational Vulkan fallback queues fail closed with dropped-readback
diagnostics, while the live Vulkan queue validates ranges through
`Extrinsic.RHI.BufferTransfer`, copies device buffers into recycled mapped
host-visible staging slots on the transfer timeline, and delivers sink bytes
only from `CollectCompleted()`. CPU evidence covers Null fail-closed behavior,
mock drain delivery, out-of-range rejection, and drain-gated completion; opt-in
`gpu;vulkan` evidence covers a device-local buffer round-trip through the ring
without routing through the legacy blocking `IDevice::ReadBuffer()` helper.
Texture readback remains owned by GRAPHICS-097, and high-level barrier/facade
ergonomics remain owned by GRAPHICS-098.

Active
[`GRAPHICS-095`](../archive/GRAPHICS-095-buffer-transfer-math-helper.md) — CPU-testable
buffer transfer math and validation helper — retired on 2026-06-22 at maturity
`CPUContracted`. `Extrinsic.RHI.BufferTransfer` now provides CPU-pure buffer
sub-range validation, non-power-of-two alignment helpers, destination dirty
range planning with optional coalescing and source-offset packing, and a
property-agnostic typed dimension/range validator for downstream property
binding. The module imports only `core` and RHI descriptors, has no backend or
device surface, is listed in the RHI README and generated module inventory, and
is covered by a 14-case `unit;graphics` BufferTransfer test suite. Operational
GPU readback/upload use remains owned by GRAPHICS-096, GRAPHICS-098, and
RUNTIME-126.

Active
[`UI-020`](../archive/UI-020-visualization-lane-uniform-color.md) — Visualization lane
uniform color controls — retired on 2026-06-19 at maturity `CPUContracted`.
The sandbox visualization command/model seam now distinguishes the selected
entity's default visualization config from optional surface, edge, and point
lane overrides. Domain visualization windows target their render lanes by
source-row presence instead of only by the mutually exclusive active domain, so
mesh vertices and graph nodes rendered as points can take an independent
uniform color. Runtime extraction resolves those lane overrides for mesh
surface/edge/vertex sidecars, graph line/point instances, and point-cloud
points; scene JSON persists the optional lane descriptors as CPU-only data.
Focused evidence passed the `IntrinsicRuntimeContractTests` build and 93/93
CPU/null contract tests across `SandboxEditorUi`,
`MeshPrimitiveViewExtraction`, `GraphGeometryExtraction`, and
`RuntimeSceneSerialization`.

Active
[`UI-019`](../archive/UI-019-visualization-uniform-color-edit.md) — Visualization uniform
color edit widget — retired on 2026-06-19 at maturity `CPUContracted`. Mesh,
graph, point-cloud, and top-level geometry visualization UI windows now expose
an ImGui `ColorEdit4` control when the selected entity's
`VisualizationConfig::ColorSource` is `UniformColor`. The edit path reuses the
runtime-owned visualization config command, preserves the rest of the config
payload when switching/editing uniform color, and does not add renderer/RHI/
asset ownership to UI. Focused evidence passed the
`IntrinsicRuntimeContractTests` build and all 51 `SandboxEditorUi` CPU/null
contract tests.

Backlog
[`GRAPHICS-089`](../archive/GRAPHICS-089-generated-uv-texture-sampling-vulkan-smoke.md) —
Generated-UV texture sampling Vulkan smoke — retired on 2026-06-19 at maturity
`Operational` on Vulkan-capable hosts. The opt-in runtime sandbox
`gpu;vulkan` smoke now imports an OBJ that omitted authored `vt` coordinates,
waits for the promoted Vulkan default recipe to become operational, uploads a
generated albedo texture through `Runtime.AssetModelTextureHandoff` and
`Graphics.GpuAssetCache`, binds it through the progressive material texture
path, and asserts the rendered surface samples that generated texture using
ASSETIO-008 generated `v:texcoord` values rather than default zero UVs or
shader-side UV fabrication. Final evidence passed `ci-vulkan` configure,
`cmake --build --preset ci-vulkan --target IntrinsicTests`, the targeted
`gpu;vulkan` CTest, and the default CPU-supported CTest gate.

Backlog
[`GRAPHICS-091`](../archive/GRAPHICS-091-unify-scalar-colormap-across-surface-line-point.md) —
Unify scalar-field / colormap visualization across surface, line, and point
passes — retired on 2026-06-19 at maturity `Operational` on Vulkan-capable
hosts (`CPUContracted` elsewhere). Promoted surface, line, and point shaders now
share `common/gpu_scene.glsl` visualization color resolution for material,
uniform, scalar-field, and per-element RGBA modes. `VisualizationSyncSystem`
writes matching `GpuEntityConfig` scalar/color contracts for surface, line, and
point, the opt-in runtime sandbox `gpu;vulkan` smoke proves line/point
scalar-field colormap pixels, and final CPU/non-GPU retirement evidence reran
the forward/deferred surface pipeline survival checks plus the shared helper
shader check.

Previously-active
[`RUNTIME-116`](../archive/RUNTIME-116-focus-camera-on-selection-command.md) —
Focus-camera-on-selection command (F key) — retired on 2026-06-19 at maturity
`CPUContracted`. Runtime now owns `Extrinsic.Runtime.CameraFocusCommand`, a
deterministic command surface that aggregates selected entities' refreshed
`World::Bounds` into a center-of-mass focus sphere, applies it through
`CameraControllerRegistry`, and marks the camera transition. `Engine::RunFrame`
binds the command to `F` after `FlushPreRenderTransformState`, suppresses it
while ImGui owns the keyboard, and rebuilds the render camera on success so the
same frame sees the reframed camera. PR #983 merged the command, the post-flush
review fix, runtime architecture docs, module inventory refresh, and the 13-case
`contract;runtime` `Test.RuntimeCameraFocusCommand.cpp` suite. No `Operational`
follow-up is owed; the thin key binding composes already-operational input and
camera-controller paths, and the reusable command closes at `CPUContracted`.

Backlog
[`GRAPHICS-092`](../archive/GRAPHICS-092-group-per-domain-params-and-line-width-residency.md) —
Group per-domain params in `GpuEntityConfig` and add line-width residency —
retired on 2026-06-18 at maturity `Operational` on Vulkan-capable hosts
(`CPUContracted` elsewhere). `GpuEntityConfig` now groups point and line
domain-specific settings into named sub-blocks while preserving shared
visualization fields; `RenderEdges::WidthSource` populates
`Line.LineWidth` / `Line.LineWidthBDA`; and the retained forward line shader
consumes those values while expanding the non-indexed `LineQuads` topology.
The final slice added an opt-in runtime sandbox `gpu;vulkan` smoke that authors
a 12 px reference-triangle line width, reads back the GPU config, confirms the
edge/point draw lanes remain emitted, and samples the default-recipe
backbuffer for the configured line overlay.

Backlog
[`GRAPHICS-094`](../archive/GRAPHICS-094-retained-point-size-bda-consumption.md) —
Consume per-point size BDA in retained point shader — retired on 2026-06-18
at maturity `CPUContracted`. The retained forward point shader now resolves
pixel size from `GpuEntityConfig::Point.PointSizeBDA[sourceVertexIndex]` when
that BDA is populated, otherwise falling back to uniform
`GpuEntityConfig::Point.PointSize`, with the existing clamp applied to both
paths. The slice added shader-source contract coverage, preserved the
`GpuEntityConfig` layout, updated renderer/architecture docs, and leaves no
required `Operational` follow-up unless future visual point-size readback smoke
coverage is explicitly opened.

Backlog
[`LEGACY-042`](../archive/LEGACY-042-retire-legacy-asset-pipeline-test.md) — Retire
legacy Asset.Pipeline test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/assets/Test_AssetPipeline.cpp` and the
dedicated `IntrinsicRuntimeTests.AssetPipelineHeadlessGrouped` CTest were
removed because they verified the old `Runtime::AssetPipeline`
main-thread-queue, material-list, `RHI::TransferToken` polling, and direct
`AssetManager` finalization surface. Promoted asset streaming/upload ownership
is split across `Extrinsic.Asset.LoadPipeline`, `AssetService`,
`Graphics.GpuAssetCache`, and runtime model/texture handoffs. `LEGACY-004`
remains blocked by 6 remaining test consumers and 50 legacy-internal consumers;
`LEGACY-005` remains blocked by 18 remaining test consumers and 133
legacy-internal consumers; `LEGACY-009` remains blocked by 14 remaining test
consumers and 83 legacy-internal consumers; `LEGACY-012` owns the remaining
test cleanup.

Backlog
[`LEGACY-041`](../archive/LEGACY-041-retire-legacy-asset-manager-core-test.md) — Retire
legacy Asset.Manager core test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/assets/Test_CoreAssets.cpp` was removed
because it verified the old `Core::Assets::AssetManager`
async/cache/lease/clear/TryGetFast compatibility surface, including a
compile-only dependency on legacy `Graphics::Material`. Promoted asset
ownership uses `Extrinsic.Asset.Service`, `Asset.Registry`,
`Asset.PayloadStore`, `Asset.LoadPipeline`, `Asset.EventBus`, and runtime-owned
asset-to-graphics handoff contracts rather than the old pointer-returning
manager/lease API. `LEGACY-004` remains blocked by 7 remaining test consumers
and 50 legacy-internal consumers; `LEGACY-005` remains blocked by 19 remaining
test consumers and 133 legacy-internal consumers; `LEGACY-008` remains blocked
by 36 remaining test consumers and 22 legacy-internal consumers; `LEGACY-009`
remains blocked by 15 remaining test consumers and 83 legacy-internal
consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-040`](../archive/LEGACY-040-retire-legacy-asset-manager-safety-test.md) —
Retire legacy Asset.Manager safety test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/assets/Test_CoreAssetSafety.cpp` was
removed because it verified the old `Core::Assets::AssetManager`
loader-safety/error-path compatibility surface: copyable loader constraints,
pointer-returning `GetRaw` / `AcquireLease` errors, and null pointer load
failures. Promoted asset ownership is split across `Extrinsic.Asset.Service`,
`Asset.Registry`, `Asset.PayloadStore`, and `Asset.LoadPipeline`, whose tests
already cover retained captured-loader reload, reload failure preservation,
wrong-type reads, dead-handle errors, failed-load cleanup, load-state
transitions, and event ordering. `LEGACY-004` remains blocked by 8 remaining
test consumers and 50 legacy-internal consumers; `LEGACY-005` remains blocked
by 20 remaining test consumers and 133 legacy-internal consumers; `LEGACY-012`
owns the remaining test cleanup.

Backlog
[`LEGACY-039`](../archive/LEGACY-039-retire-legacy-element-selection-test.md) — Retire
legacy element-selection test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy
`tests/integration/runtime/Test_ElementSelection.cpp` was removed because the
old mutable `Runtime.Selection::SubElementSelection` vertex/edge/face set API is
not promoted. Current sub-primitive workflows use
`Extrinsic.Runtime.PrimitiveSelectionRefinement`, engine-owned refined-pick
caching, and editor selection models. `LEGACY-006` remains blocked by 19
remaining test consumers and 37 legacy-internal consumers; `LEGACY-010` remains
blocked by 11 remaining test consumers; `LEGACY-012` owns the remaining test
cleanup.

Backlog
[`LEGACY-038`](../archive/LEGACY-038-retire-runtime-selection-modes-test.md) — Retire
legacy runtime selection modes test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy
`tests/contract/runtime/Test.RuntimeSelectionModes.cpp` was removed after the
retained add/toggle/replace/background multi-selection behavior was covered by
promoted `Extrinsic.Runtime.SelectionController` tests. The old
`Runtime.SelectionModule::GetSelectedEntities` raw-entity helper is not a
promoted endpoint; promoted consumers use stable-id snapshots and explicit
selection queries. `LEGACY-006` remains blocked by 20 remaining test consumers
and 37 legacy-internal consumers; `LEGACY-010` remains blocked by 12 remaining
test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-037`](../archive/LEGACY-037-retire-legacy-asset-ingest-service-test.md) —
Retire legacy AssetIngestService test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy
`tests/unit/assets/Test_AssetIngestService.cpp` was removed instead of migrated
because it verified only the old dependency-heavy
`Runtime.AssetIngestService` constructor and copy/move trait shape. Promoted
asset ingest behavior is owned by `Extrinsic.Runtime.AssetIngestStateMachine`,
promoted asset import bridges, and runtime model/texture handoffs from
`RUNTIME-101` and related asset/runtime tasks. `LEGACY-004` remains blocked by
9 remaining test consumers and 50 legacy-internal consumers; `LEGACY-005`
remains blocked by 21 remaining test consumers and 133 legacy-internal
consumers; `LEGACY-008` remains blocked by 37 remaining test consumers and 22
legacy-internal consumers; `LEGACY-009` remains blocked by 16 remaining test
consumers and 83 legacy-internal consumers; `LEGACY-010` remains blocked by 13
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-036`](../archive/LEGACY-036-retire-legacy-event-bus-test.md) — Retire legacy
EventBus test — retired on 2026-06-18 at maturity `CPUContracted`. Legacy
`tests/unit/core/Test_EventBus.cpp` was removed instead of migrated because the
promoted ECS layer owns CPU-only event payloads, not the old
`ECS::Scene::GetDispatcher()` delivery surface, while promoted runtime owns
selection/hover mutation through `SelectionController`. Legacy
`GpuPickCompleted` and `GeometryUploadFailed` stay runtime/graphics-owned
diagnostics rather than ECS events. `LEGACY-006` remains blocked by 21
remaining test consumers and 37 legacy-internal consumers; `LEGACY-010` remains
blocked by 14 remaining test consumers; `LEGACY-012` owns the remaining test
cleanup.

Backlog
[`LEGACY-035`](../archive/LEGACY-035-resolve-legacy-rhi-deferred-destruction-tests.md) —
Resolve legacy RHI deferred-destruction tests — retired on 2026-06-18 at
maturity `CPUContracted`. The Vulkan `SafeDestroy*` cases split out of the
legacy runtime maintenance-lane test were retired as legacy RHI implementation
detail: promoted Vulkan keeps deferred deletion behind private backend-local
`DeferDelete` / frame-slot queues and does not expose legacy timeline-value or
unconditional-flush semantics through promoted `RHI::IDevice`. `LEGACY-009`
remains blocked by 17 test consumers and 83 legacy-internal consumers; no new
legacy RHI test consumer was added.

Backlog
[`LEGACY-034`](../archive/LEGACY-034-resolve-runtime-frame-loop-maintenance-tests.md) —
Resolve legacy runtime frame-loop and maintenance tests — retired on 2026-06-18
at maturity `CPUContracted`. Legacy
`tests/unit/runtime/Test_RuntimeFrameLoop.cpp` and
`tests/unit/runtime/Test_MaintenanceLane.cpp` were removed after retained
CPU/null frame-loop, platform, render-frame, maintenance, operational
transition, and shutdown ordering mapped to promoted
`Extrinsic.Core.FrameLoop` / `Extrinsic.Runtime.Engine` contracts. The legacy
feature-catalog rollback mode remains retired, and the backend-facing Vulkan
deferred-destruction checks were split to `LEGACY-035`. `LEGACY-005` remains
blocked by 22 remaining test consumers and 133 legacy-internal consumers;
`LEGACY-008` remains blocked by 38 remaining test consumers and 22
legacy-internal consumers; `LEGACY-009` remains blocked by 17 remaining test
consumers and 83 legacy-internal consumers; `LEGACY-010` remains blocked by 15
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-033`](../archive/LEGACY-033-retire-runtime-engine-config-test.md) — Retire legacy
RuntimeEngineConfig test — retired on 2026-06-18 at maturity `CPUContracted`.
Legacy `tests/unit/runtime/Test_RuntimeEngineConfig.cpp` coverage was removed
instead of migrated because its `Runtime::EngineConfig` scalar validation fields
do not map to the promoted `Extrinsic.Core.Config.Engine` value-type surface.
Promoted config defaults and runtime engine/device-selection behavior remain
covered by promoted core/runtime tests. `LEGACY-010` remains blocked by 17
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-032`](../archive/LEGACY-032-resolve-runtime-system-bundles-test.md) — Resolve
legacy `Runtime.SystemBundles` test migration — retired on 2026-06-18 at
maturity `CPUContracted`. Legacy
`tests/unit/runtime/Test_RuntimeSystemBundles.cpp` coverage was removed instead
of migrated after its assertions were mapped: retained fixed-step ECS activation
is covered by `Extrinsic.Runtime.EcsSystemBundle`, named graphics lifecycle
contracts are covered by existing graphics/runtime tests, and the old global
`Core.SystemFeatureCatalog` ordering/toggle behavior is not promoted.
`LEGACY-005` remains blocked by 24 remaining test consumers and 133
legacy-internal consumers; `LEGACY-006` remains blocked by 22 remaining test
consumers and 37 legacy-internal consumers; `LEGACY-010` remains blocked by 18
remaining test consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-031`](../archive/LEGACY-031-retire-ecs-framegraph-systems-test.md) — Retire
legacy ECS frame-graph systems test — retired on 2026-06-18 at maturity
`CPUContracted`. Legacy `tests/unit/ecs/Test_FrameGraphSystems.cpp` coverage was
removed instead of migrated because promoted ECS transform hierarchy, bounds
propagation, render-sync, and `Extrinsic.Runtime.EcsSystemBundle` contract tests
cover the retained fixed-step bundle behavior, while `AxisRotator` is recorded
as sample/demo behavior rather than canonical ECS. `LEGACY-005` remains blocked
by 25 remaining test consumers and 133 legacy-internal consumers; `LEGACY-006`
remains blocked by 23 remaining test consumers and 37 legacy-internal
consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-030`](../archive/LEGACY-030-retire-ecs-entity-commands-test.md) — Retire legacy
ECS entity-command test — retired on 2026-06-18 at maturity `CPUContracted`.
Duplicate legacy `tests/unit/ecs/Test_EntityCommands.cpp` coverage was removed
instead of migrated because promoted `Extrinsic.Runtime.EditorCommandHistory`
contract tests cover undo/redo and hierarchy delete planning, while promoted
ECS scene/bootstrap/hierarchy tests cover typed lifecycle and hierarchy
mutation. `LEGACY-005` remains blocked by 26 remaining test consumers and 133
legacy-internal consumers; `LEGACY-006` remains blocked by 24 remaining test
consumers and 37 legacy-internal consumers; `LEGACY-012` owns the remaining
test cleanup.

Backlog
[`LEGACY-029`](../archive/LEGACY-029-retire-core-benchmark-test.md) — Retire legacy
Core.Benchmark test — retired on 2026-06-18 at maturity `CPUContracted`.
Legacy-only `tests/benchmark/Test_Benchmark.cpp` coverage for
`Core.Benchmark::BenchmarkRunner` was removed rather than promoted; its retained
pass-timing telemetry assertions now live in promoted
`tests/unit/core/Test.CoreProfiling.cpp`. Benchmark manifests, runner JSON, SLO
thresholds, and baselines were not changed. `LEGACY-005` remains blocked by 27
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-028`](../archive/LEGACY-028-architecture-slo-test-promoted.md) — Migrate
ArchitectureSLO test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. The benchmark/SLO test now imports promoted
`Extrinsic.Core.FrameGraph`, `Extrinsic.Core.Tasks`, and
`Extrinsic.Core.Tasks.CounterEvent` as
`tests/benchmark/slo/Test.ArchitectureSLO.cpp`; thresholds, warmup counts, and
measured workload sizes were left unchanged. `LEGACY-005` remains blocked by 28
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-027`](../archive/LEGACY-027-core-memory-test-promoted.md) — Migrate CoreMemory
test to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`.
Retained memory allocator coverage from
`tests/unit/core/Test_CoreMemory.cpp` now imports promoted
`Extrinsic.Core.Memory`, `Extrinsic.Core.Error`, and
`Extrinsic.Core.Telemetry` as `tests/unit/core/Test.CoreMemory.cpp`. The
smaller `Test.Core.MemoryLegacy.cpp` parity file was folded into the expanded
promoted test, and the legacy-linked core aggregate no longer builds the old
memory consumer. `LEGACY-005` remains blocked by 29 remaining test consumers
and 133 legacy-internal consumers; `LEGACY-012` owns the remaining test
cleanup.

Backlog
[`LEGACY-026`](../archive/LEGACY-026-retire-core-dagscheduler-test.md) — Retire legacy
Core.DAGScheduler test — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test_DAGScheduler.cpp` was removed because it only exercised
the old `Core::DAGScheduler` compatibility API; promoted
`Extrinsic.Core.Dag.Scheduler`, graph-compiler, and task-graph tests cover the
retained scheduling contract, including explicit dependencies, resource
hazards, weak reads, duplicate-access handling, reset behavior, deterministic
compiles, diagnostics, and stress cases. `LEGACY-005` remains blocked by 30
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-025`](../archive/LEGACY-025-retire-core-inplace-function-test.md) — Retire
legacy Core.InplaceFunction test — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test_InplaceFunction.cpp` was removed because
`Core.InplaceFunction` has no promoted `Extrinsic.Core` endpoint and is
recorded in the parity matrix as legacy-only cleanup. Remaining legacy
runtime/graphics/RHI consumers are owned by their subtree cleanup tasks.
`LEGACY-005` remains blocked by 31 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-024`](../archive/LEGACY-024-retire-core-feature-catalog-tests.md) — Retire
legacy Core feature-catalog tests — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test_FeatureRegistry.cpp` and
`tests/unit/core/Test_SystemFeatureCatalog.cpp` were removed because
`CORE-002` retired the global feature catalog shape instead of promoting it
under `src/core`. Runtime/graphics/app legacy feature-registry consumers remain
owned by their subtree cleanup tasks. `LEGACY-005` remains blocked by 32
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-023`](../archive/LEGACY-023-retire-core-commands-test.md) — Retire legacy
Core.Commands test — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test_CoreCommands.cpp` was removed because `CORE-002` retired
the legacy global core command service and `RUNTIME-102` owns the promoted
runtime/editor command-history endpoint. Promoted coverage remains in
`tests/contract/runtime/Test.EditorCommandHistory.cpp` and related runtime UI
tests. `LEGACY-005` remains blocked by 34 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-022`](../archive/LEGACY-022-core-framegraph-test-promoted.md) — Migrate
CoreFrameGraph test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test.CoreFrameGraph.cpp` and
`tests/unit/core/Test.CoreFrameGraphTypeTokenHelper.cpp` now import promoted
`Extrinsic.Core.FrameGraph`, `Extrinsic.Core.Hash`, and
`Extrinsic.Core.Tasks` instead of the bare legacy aggregate `Core` module. The
test uses promoted `FrameGraph` construction/accessors and checks
`FrameGraph::Execute()` results. The focused core targets and `CoreFrameGraph`
CTest filter passed. `LEGACY-005` remains blocked by 35 remaining test
consumers and 133 legacy-internal consumers; `LEGACY-012` owns the remaining
test cleanup.

Backlog
[`LEGACY-021`](../archive/LEGACY-021-core-profiling-test-promoted.md) — Migrate profiling
test to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test.CoreProfiling.cpp` now imports promoted
`Extrinsic.Core.Telemetry` and `Extrinsic.Core.Hash` instead of bare legacy
`Core.Telemetry` / `Core.Hash`, preserving `ScopedTimer`, `TelemetrySystem`,
`TimingCategory`, and present-timing coverage with the `Test.<Name>.cpp`
convention. The focused core targets and profiling/telemetry CTest filter
passed. `LEGACY-005` remains blocked by 37 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-020`](../archive/LEGACY-020-core-tasks-test-promoted.md) — Migrate CoreTasks test
to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`. The full
`tests/unit/core/Test_CoreTasks.cpp` scheduler, coroutine, `CounterEvent`,
wait-token, telemetry-export, and job lifetime coverage now imports promoted
`Extrinsic.Core.Tasks`, `Extrinsic.Core.Tasks.CounterEvent`, and
`Extrinsic.Core.Telemetry` as `tests/unit/core/Test.CoreTasks.cpp`. The smaller
legacy-suffixed promoted wrapper test was removed as duplicate coverage. The
focused core targets and `CoreTasks` CTest filter passed. `LEGACY-005` remains
blocked by 38 remaining test consumers and 133 legacy-internal consumers;
`LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-019`](../archive/LEGACY-019-strong-handle-test-promoted.md) — Migrate
StrongHandle test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. The full `tests/unit/core/Test_CoreHandle.cpp` coverage now
imports promoted `Extrinsic.Core.StrongHandle` as
`tests/unit/core/Test.CoreStrongHandle.cpp` and uses the exported
`StrongHandleHash` for unordered containers. The smaller legacy-suffixed
promoted wrapper test was removed as duplicate coverage. The focused core
targets and `StrongHandle` CTest filter passed. `LEGACY-005` remains blocked by
39 remaining test consumers and 133 legacy-internal consumers; `LEGACY-012`
owns the remaining test cleanup.

Backlog
[`LEGACY-018`](../archive/LEGACY-018-retire-interface-panel-registration-test.md) —
Retire legacy Interface panel-registration test — retired on 2026-06-18 at
maturity `CPUContracted`. The legacy-only
`tests/contract/ui/Test_PanelRegistration.cpp` consumer was removed from the
runtime test source list instead of migrated because `Interface::GUI` panel
registration is not a promoted endpoint; current promoted UI/app coverage lives
in `SandboxEditorUi` contract tests and the app-to-runtime-only dependency
test. `LEGACY-001` now has zero external test consumers and remains blocked by
six legacy-internal Graphics/Runtime files.

Backlog
[`LEGACY-017`](../archive/LEGACY-017-core-hash-test-promoted.md) — Retire duplicate legacy
CoreHash test — retired on 2026-06-18 at maturity `CPUContracted`. Duplicate
legacy `tests/unit/core/Test_CoreHash.cpp` coverage was deleted in favor of the
existing promoted `Extrinsic.Core.Hash` coverage, now named
`tests/unit/core/Test.CoreHash.cpp` with the `Test.<Name>.cpp` convention. The
affected core targets and `CoreHash` CTest filter passed. `LEGACY-005` remains
blocked by 40 remaining test consumers and 133 legacy-internal consumers;
`LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-016`](../archive/LEGACY-016-log-ring-buffer-test-promoted.md) — Migrate
LogRingBuffer test to promoted Core — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/core/Test.LogRingBuffer.cpp` now imports promoted
`Extrinsic.Core.Logging` instead of bare legacy `Core.Logging`, and the touched
independent test uses the `Test.<Name>.cpp` naming convention. The focused core
target and `LogRingBuffer` CTest filter passed. `LEGACY-005` remains blocked by
41 remaining test consumers and 133 legacy-internal consumers; `LEGACY-012`
owns the remaining test cleanup.

Backlog
[`LEGACY-015`](../archive/LEGACY-015-core-error-test-promoted.md) — Migrate CoreError test
to promoted Core — retired on 2026-06-18 at maturity `CPUContracted`.
`tests/unit/core/Test.CoreError.cpp` now imports promoted
`Extrinsic.Core.Error` instead of bare legacy `Core.Error`, and the touched
independent test uses the `Test.<Name>.cpp` naming convention. The focused core
target and `CoreError` CTest filter passed. `LEGACY-005` remains blocked by 42
remaining test consumers and 133 legacy-internal consumers; `LEGACY-012` owns
the remaining test cleanup.

Backlog
[`LEGACY-014`](../archive/LEGACY-014-runtimegraph-core-test-import.md) — Remove unused
RuntimeGraph legacy Core test import — retired on 2026-06-18 at maturity
`CPUContracted`. `tests/unit/geometry/Test_RuntimeGraph.cpp` no longer imports
bare legacy `Core`; the focused geometry target and `RuntimeGraph` CTest filter
passed. `LEGACY-005` remains blocked by 43 remaining test consumers and 133
legacy-internal consumers; `LEGACY-012` owns the remaining test cleanup.

Backlog
[`LEGACY-013`](../archive/LEGACY-013-promoted-core-import-migration.md) — Migrate
promoted Core imports off legacy modules — retired on 2026-06-18 at maturity
`CPUContracted`. Promoted geometry/runtime now import `Extrinsic.Core.*`
instead of bare legacy `Core.*`, promoted geometry no longer links
`IntrinsicCore`, and the promoted-src bare legacy import grep is clean. Four
directly affected geometry tests now consume promoted Core memory/error types;
`LEGACY-005` remains blocked by 44 remaining test consumers and 133
legacy-internal consumers. `LEGACY-012` owns the remaining test cleanup, while
legacy-internal consumers retire through the Runtime-first to Core-last subtree
deletion order.

Backlog
[`LEGACY-011`](../archive/LEGACY-011-src-legacy-feature-reimplementation-map.md) —
Value-gated legacy feature reimplementation map — retired on 2026-06-18 at
maturity `Scaffolded`. Every retained/deferred legacy feature candidate now has
a named done-task decision or an explicit future trigger, keeping semantic
reimplementation separate from the mechanical per-subtree deletion tasks.
Remaining legacy retirement work is consumer-grep cleanup in `LEGACY-012`,
the promoted-src Core import migration called out by the migration audit, and
mechanical Runtime-first to Core-last subtree deletion ordering.

Active
[`UI-018`](../archive/UI-018-sandbox-menu-first-ui.md) — Sandbox menu-first UI defaults —
retired on 2026-06-17 at maturity `CPUContracted`. The sandbox editor now
starts with only the main menu bar visible. Top-level panels open from `View`,
and PointCloud/Graph/Mesh domain windows remain closed until selected from
their menus, without changing panel models, command routing, or runtime
ownership.

Active
[`UI-014`](../archive/UI-014-uv-backend-and-texture-bake-controls.md) — UV backend and
texture bake controls — retired on 2026-06-17 at maturity `CPUContracted`.
The sandbox editor now exposes selected-mesh UV diagnostics, xatlas-backed UV
regeneration commands, property-catalog-driven bake source controls, generated
texture bake command routing, and live ImGui controls for preserve/regenerate
policy, atlas settings, target semantic, encoder, and output size. UI remains
headless-safe and routes geometry mutation, bake work, generated texture
payloads, and binding updates through runtime-owned command/history surfaces.

Active
[`UI-017`](../archive/UI-017-bound-render-state-inspector.md) — Bound render state
inspector — retired on 2026-06-17 at maturity `CPUContracted`. Selected mesh,
graph, point-cloud, and composition models now expose bound-state rows for
render lanes, presentation slots, defaults, property buffers, authored and
generated textures, readiness diagnostics, disabled command surfaces, and
derived-job/bake progress without UI storing renderer handles, raw property
pointers, worker state, or live asset-service references.

Active
[`RUNTIME-115`](../archive/RUNTIME-115-selected-mesh-bake-command-surface.md) — Selected
mesh bake command surface — retired on 2026-06-17 at maturity
`CPUContracted`. Runtime now owns a selected-mesh texture-bake command surface
that validates source entity/domain/property, encoder, UV availability,
resolution, generated texture key, and binding compatibility before work is
scheduled. Successful synchronous or derived-job bakes reload generated
`AssetTexture2DPayload` data through `AssetService` and optionally update
progressive presentation bindings through `EditorCommandHistory`; stale
derived-job applies are discarded deterministically.

Active
[`UI-016`](../archive/UI-016-geometry-property-catalog-and-binding-usability.md) —
Geometry property catalog and binding usability — retired on 2026-06-17 at
maturity `CPUContracted`. `Extrinsic.Runtime.SandboxEditorUi` now builds
selected-entity property catalogs for mesh vertex/edge/halfedge/face, graph
vertex/edge, and point-cloud point domains, including internal/connectivity,
canonical, user, generated, supported, and unsupported rows. Supported
scalar/label/vector properties show selected-value previews, and compatible
binding targets keep dimension/domain mismatches visible with deterministic
disabled reasons.

Active
[`BUG-045`](../archive/BUG-045-progressive-raw-mesh-uv-fallback.md) — Progressive raw mesh
surface UV fallback — retired on 2026-06-17 at maturity `CPUContracted`.
Raw mesh surface packing now falls back to zero GPU U/V values when imported
geometry has missing, mismatched, or non-finite `v:texcoord`, while extraction
still records UV fallback diagnostics. Dropped no-UV OBJ files now create a
mesh entity and upload a raw surface in the frame where the import event becomes
visible; deferred UV atlas generation and UV-dependent texture bakes still wait
for real resolved UVs. Existing direct import, dropped-file, progressive
model-scene, and close-path runtime regressions passed.

Backlog
[`GRAPHICS-090`](../archive/GRAPHICS-090-progressive-render-data-operational-smoke.md) —
Progressive render-data operational smoke — retired on 2026-06-16 at maturity
`Operational`. The promoted runtime sandbox GPU smoke now exercises a
progressive scene with mesh defaults/pending slots, a generated mesh texture
slot becoming ready, graph edge property-buffer presentation, unsupported and
previous-output-retained states, extraction diagnostics, and material texture
binding resolution counters. The `ci-vulkan` target built on this host and the
`gpu;vulkan` `ProgressiveRenderDataReachesOperationalFrame` test passed without
introducing live runtime/ECS/AssetService imports into graphics.

Backlog
[`UI-015`](../archive/UI-015-progressive-render-data-inspector.md) — Progressive render-data
inspector — retired on 2026-06-16 at maturity `CPUContracted`. The sandbox
editor inspector now exposes data-only progressive entity shape, lane/slot
state, compatible and incompatible property choices, slot default/property
commands routed through `EditorCommandHistory`, per-entity derived-job rows, and
composition child summaries. The UI remains a command/model consumer and does
not own geometry algorithms, asset IO, worker state, texture baking, or graphics
resources.

Backlog
[`RUNTIME-114`](../archive/RUNTIME-114-progressive-import-enrichment-pipeline.md) —
Progressive import enrichment pipeline — retired on 2026-06-16 at maturity
`CPUContracted`. Model-scene mesh leaves can now publish raw decoded geometry
immediately, attach progressive surface bindings, and queue observable
`StreamingExecutor`-backed UV atlas, vertex-normal, normal-bake, and albedo-bake
jobs through `DerivedJobRegistry`. Main-thread apply updates current ECS
properties and generated presentation descriptors only; generated texture upload
and material binding residency remain on the existing runtime texture handoff
path.

Backlog
[`RUNTIME-113`](../archive/RUNTIME-113-progressive-domain-presentation-extraction.md) —
Progressive domain presentation extraction — retired on 2026-06-16 at maturity
`CPUContracted`. Runtime extraction now consumes progressive descriptor
snapshots for mesh surface defaults/texture slots, mesh face-domain diagnostics,
graph vertex/edge property-buffer domains, point-cloud color/scalar/size/normal
descriptors, pending/failed/unsupported states, and previous-output retention
without blocking on derived jobs.

Backlog
[`RUNTIME-112`](../archive/RUNTIME-112-entity-derived-job-graph.md) — Entity derived-job
graph and snapshots — retired on 2026-06-16 at maturity `CPUContracted`.
Runtime now owns a `StreamingExecutor`-backed derived-job registry with stable
entity/domain/source/binding keys, explicit dependencies, deterministic
snapshots, follow-up scheduling, stale-result discard, cancellation/delete
handling, previous-output retention, main-thread apply, and fail-closed GPU
domain diagnostics.

Backlog
[`RUNTIME-111`](../archive/RUNTIME-111-progressive-render-data-descriptors.md) —
Progressive render-data descriptor contracts — retired on 2026-06-16 at
maturity `CPUContracted`. Runtime now has shared mesh/graph/point-cloud
presentation descriptors, slot/source/readiness/generated-output policy,
property compatibility diagnostics, and scene serialization for progressive
bindings while excluding raw property pointers, transient jobs, and GPU handles.

Backlog
[`RUNTIME-110`](../archive/RUNTIME-110-progressive-entity-render-data-pipeline.md) —
Progressive entity render-data pipeline clarification — retired on 2026-06-16
at maturity `Scaffolded`. The accepted planning contract makes mesh, graph,
and point-cloud leaves equal first-class render-data domains; separates
render-lane intent components from per-entity presentation bindings; records
stable property/slot/generated-output descriptors instead of raw property
pointers or GPU handles; and defines asynchronous derived jobs with dependency
visibility, stale-result discard, previous-output retention, and main-thread
apply. ADR-0021 now captures the hard-to-reverse architecture decision.
Implementation is split into `RUNTIME-111` descriptor contracts,
`RUNTIME-112` entity derived-job graph, `RUNTIME-113` progressive extraction,
`RUNTIME-114` import enrichment, `UI-015` inspector/debug visibility, and
`GRAPHICS-090` opt-in backend smoke.

Active
[`BUG-044`](../archive/BUG-044-runtime-import-postprocess-queue.md) — Runtime mesh import
post-process queue — retired on 2026-06-16 at maturity `CPUContracted`.
Direct mesh imports now publish decoded raw geometry before derived
materialization work. Missing normals, missing/invalid UV resolution, atlas
generation, and generated normal texture baking run on `Runtime.StreamingExecutor`
and apply back to the same ECS entity on the main thread with geometry dirty
tags. The import result reports the mesh entity immediately with zero generated
texture counts, while a later frame resolves finite texcoords/normals and
registers the generated normal material binding. Focused direct import,
model-scene, dropped-file, and mesh-normal runtime contract tests passed.

Active
[`ASSETIO-008`](../archive/ASSETIO-008-default-uv-atlas-materialization.md) — Default UV
atlas materialization for imported meshes — retired on 2026-06-16 at maturity
`CPUContracted`. Runtime mesh materialization now validates authored UVs through
the `Geometry.UvAtlas` contract, preserves valid authored coordinates by
default, and invokes the xatlas-backed default atlas backend when UVs are
missing or invalid before ECS population and generated texture bakes. The
materialization options expose preserve/regenerate policy, atlas resolution,
padding, texels-per-unit, required-vs-optional UV failure behavior, and a
replaceable backend for tests/future algorithms. Seam-split output preserves
normals, colors, scalar/vector fields, and `v:source_vertex` / `f:source_face`
provenance; diagnostics distinguish authored-preserved vs generated UVs,
invalid authored UVs, backend failures, seam splits, chart count, and atlas
dimensions. Direct mesh imports and model-scene handoff generated normal/albedo
bakes now use resolved UVs instead of skipping solely because the source omitted
UVs. Focused runtime/UV atlas tests, module inventory regeneration, strict
layering/test-layout/task checks, and doc-link checks passed.

Active
[`GEOM-025`](../archive/GEOM-025-uv-atlas-backend-xatlas.md) — UV atlas backend contract
and xatlas default — retired on 2026-06-16 at maturity `CPUContracted`.
Geometry now exposes `Geometry.UvAtlas`, a backend-neutral UV atlas API with
authored-UV validation/preservation, explicit failure/provenance diagnostics,
source-vertex/source-face xrefs, seam-aware `MeshSoup::IndexedMesh` output,
GEOM-018 quality metrics, and a caller-supplied backend replacement seam. The
default CPU backend is pinned to `jpcy/xatlas` through the repository vcpkg
overlay port and linked privately by geometry; no runtime/assets/graphics/ECS
layer imports or public xatlas headers were introduced. Focused geometry tests,
`IntrinsicTests` target build, module inventory regeneration, strict layering
and test-layout checks, task checks, and doc-link checks passed.

Active
[`GEOM-018`](../archive/GEOM-018-parameterization-distortion-map-quality-diagnostics.md)
— Parameterization distortion and map-quality diagnostics — retired on
2026-06-16 at maturity `CPUContracted`. Geometry now exposes
`Geometry.Parameterization.Diagnostics`, a deterministic CPU diagnostics
surface for mesh positions plus per-vertex UVs that reports explicit invalid
input counts, flipped elements, conformal/area/symmetric-Dirichlet/stretch
metrics, boundary length distortion, and seam-discontinuity placeholders.
Existing LSCM quality summaries are populated through the shared evaluator, and
the smoke benchmark runner now emits
`geometry.parameterization.diagnostics.smoke`. Focused geometry tests,
benchmark validation, module inventory regeneration, strict layering/test
layout/task checks, and doc-link checks passed.

Active
[`ASSETIO-005`](../archive/ASSETIO-005-asset-import-queue-progress.md) — Asset import
queue and progress UI — retired on 2026-06-16 at maturity `Operational`.
Runtime now exposes stable AssetIO queue snapshots over the promoted ingest
state machine, including operation identity, source/path metadata, coarse
queued/running/apply/upload/terminal stages, timestamps, determinate or
indeterminate progress, diagnostics, cancellation, and clear-completed
behavior. `Engine` owns snapshot polling plus command routing, while the
sandbox editor's File / Import window consumes data-only rows and does not own
asset, ECS, graphics, or worker-thread state. Focused runtime/UI queue coverage,
the default CPU-supported CTest gate, and strict layering/task/docs checks
passed.

Active
[`GRAPHICS-088`](../archive/GRAPHICS-088-resolved-uv-rendering-and-bake-residency.md) —
Resolved UV rendering and bake texture residency — retired on 2026-06-15 at
maturity `CPUContracted`. Graphics now treats packed mesh UVs as resolved
texture coordinates for surface material sampling, generated normal/albedo
bindings, the `Material.DefaultDebugUVs` checker material, and UV-backed
fragment-bake descriptors. The generic `RUNTIME-109` bake contract is consumed
through data-only generated texture semantics and source dirty stamps for
scalar, label, vector, standard material, and displacement-intent bake
descriptors; graphics still does not generate UVs or import runtime, ECS,
`AssetService`, geometry backends, or `xatlas`. Operational generated-UV Vulkan
sampling proof is deferred to `GRAPHICS-089` after `ASSETIO-008`.

Backlog
[`RUNTIME-109`](../archive/RUNTIME-109-extensible-mesh-attribute-texture-bakes.md) —
Extensible mesh attribute texture bake pipeline — retired on 2026-06-15 at
maturity `CPUContracted`. Runtime now exposes a generic CPU mesh attribute
texture bake request over resolved UVs for vertex and face source domains,
finite scalar float/double, label `uint32`, and `glm::vec2`/`glm::vec3`/
`glm::vec4` properties. Encoders cover scalar colormap, linear scalar,
label palette, vector2, vector3, normal, and RGBA outputs, while the existing
generated normal/albedo helpers remain wrappers over the generic seam. Stable
generated texture keys omit dirty stamp so rebakes target reload of the
intended CPU payload instead of minting unbounded generated assets. The baker
does not generate UVs; missing-UV import materialization remains under
`ASSETIO-008`, and operational renderer/Vulkan proof remains under
`GRAPHICS-088`. Focused runtime bake coverage passed.

Active
[`RUNTIME-101`](../archive/RUNTIME-101-asset-ingest-state-machine.md) — Asset ingest
state-machine migration — retired on 2026-06-15 at maturity `CPUContracted`.
Runtime now owns a backend-neutral ingest request/result state machine for
manual imports, dropped files, and reimport over promoted `AssetService`,
`Asset.ImportRouter`, `Runtime.StreamingExecutor`, and existing materialization
handoffs. `Engine::ImportAssetFromPath(...)`, synchronous dropped non-geometry
imports, deferred dropped-geometry main-thread apply, and
`Engine::ReimportAsset(...)` share deterministic diagnostics, duplicate active
request suppression, and stale completion guards. Reimport reloads the same
`AssetId` transactionally through `AssetService` and does not recreate
standalone geometry entities or revive scene-file `AssetSourceRef` coupling.
Focused runtime/import coverage and strict docs/task/layering checks passed.

Backlog
[`RUNTIME-107`](../archive/RUNTIME-107-headless-engine-loop-coverage.md) —
Headless-capable `Engine::Run()` loop coverage — retired on 2026-06-15 at
maturity `Operational`. `Core::Config::WindowConfig` now exposes an explicit
`WindowBackend` selector: `Configured` preserves the CMake-selected platform
backend, while `Null` routes `Platform::CreateWindow` to the deterministic
headless backend that is always compiled. The BUG-030 `Engine::Run()`
regressions now set `WindowBackend::Null`, so viewport click selection,
inspector transform flush, platform/drop import, and close-event assertions
execute on displayless hosts instead of skipping. Configured GLFW windows that
initialize already closed log a runtime zero-frame warning and still do not
fall back to Null silently. Focused runtime contract/integration coverage
passed 7/7 with no skips.

Backlog
[`RUNTIME-103`](../archive/RUNTIME-103-geometry-algorithm-execution-queue.md) —
Geometry algorithm execution queue — retired on 2026-06-15 at maturity
`CPUContracted`. The value gate found that current promoted editor workflows
do not justify a runtime async geometry algorithm queue: `UI-004` already
routes CPU K-Means over mesh vertices, graph nodes, and point-cloud points
through one deterministic synchronous `SandboxEditorUi` command that publishes
label/color properties, stamps `DirtyVertexAttributes`, and fails closed for
invalid targets or inputs. No request/result/cancellation/progress queue API
or CUDA follow-up is owed for current workflows. Future asynchronous
scheduling, centroid entities, topology mutation, broader algorithms, or
compute backends require new value-gated tasks with concrete consumers.
Focused K-Means/SandboxEditorUi runtime coverage and strict task/docs/layering
checks passed.

Backlog
[`RUNTIME-105`](../archive/RUNTIME-105-remove-streaming-graph-bridge.md) — Remove the
deprecated `GetStreamingGraph()` TaskGraph bridge — retired on 2026-06-15 at
maturity `Retired`. The promoted runtime no longer exports
`Engine::GetStreamingGraph()`, no longer owns a private streaming
`TaskGraph`, and no longer converts per-frame graph passes into
`StreamingExecutor` tasks during maintenance. `Runtime.Engine` now owns only
the persistent `StreamingExecutor` path for async asset IO / geometry
processing work, and `src/runtime/README.md` documents that current state.
The `RuntimeEngineLayering` source-inspection harness was corrected to inspect
`Core.FrameLoop.cpp`, where the promoted frame-loop implementation lives, so
the existing runtime layering prefix covers the bridge deletion. Focused
runtime frame-loop, streaming-executor, and layering prefixes passed, and the
default CPU-supported CTest gate passed.

Active
[`INFRA-001`](../archive/INFRA-001-vcpkg-manifest-mode.md) — Move third-party
dependencies to a vcpkg manifest — retired on 2026-06-15 at maturity
`Operational`. The build now resolves third-party C/C++ packages through the
root `vcpkg.json` manifest, the repository-local vcpkg toolchain, and
repository overlay ports. The retired FetchContent fallback, dependency-cache
knobs, and `external/cache` developer flow are gone from current build/tooling
docs. CI workflows bootstrap vcpkg, restore `external/vcpkg-bincache/`, export
`VCPKG_BINARY_SOURCES`, and time cache-backed configure with
`tools/ci/time_command.py`; exact primary-key cache hits over 10 s fail the
configure step. Local CPU, Vulkan, headless, raw-IDE, and fresh-clone checks
passed. Final GitHub Actions evidence came from `ci-linux-clang` run
`27533474526`, job `81376962604`: exact primary-key cache hit and
`Configure (ci preset) elapsed: 8.271 s`, with the job concluding success.

Active
[`BUG-043`](../archive/BUG-043-dropped-obj-missing-uvs-invisible.md) — Dropped OBJ without
UVs loads but is invisible — retired on 2026-06-14 at maturity
`CPUContracted`. Runtime mesh materialization now preserves valid authored
`v:texcoord` and writes deterministic finite projection fallback UVs when
imported OBJ/model-scene mesh payloads omit or invalidate texture coordinates.
The fallback runs before direct ECS materialization, model-scene handoff, and
generated attribute texture bakes, so render extraction can upload the mesh
surface instead of failing closed with `MeshGeometryMissingTexcoords`. The
renderer packer remains strict: surface `MeshVertex::U/V` still comes only from
`v:texcoord`, never oct-encoded normals or shader-side fabrication. Focused
runtime contract tests, adjacent mesh/import coverage, and the full
CPU-supported CTest gate passed; xatlas-quality default atlas work remains
owned by `ASSETIO-008` and `GEOM-025`.

Backlog
[`RUNTIME-108`](../archive/RUNTIME-108-resolved-uv-render-residency.md) — Remove mesh UV
normal fallback — retired on 2026-06-13 at maturity `CPUContracted`. Runtime
mesh surface packing now treats `MeshVertex::U/V` as texture coordinates only:
`PackMesh` and `BuildSurfaceTriangleFaceMap` require count-matched finite
`v:texcoord`, report `MissingTexcoords` or `NonFiniteTexcoord` for invalid
inputs, and extraction records matching counters while skipping unrenderable
surface uploads. Reference/procedural meshes and runtime test fixtures now
author UVs. Generated atlas/materialization remains with `ASSETIO-008` and
`GEOM-025`, renderer operational proof remains with `GRAPHICS-088`, and generic
texture-bake expansion remains with `RUNTIME-109`.

Active
[`ASSETIO-007`](../archive/ASSETIO-007-direct-mesh-generated-normal-texture.md) —
Direct mesh generated normal texture binding — retired on 2026-06-13 at
maturity `CPUContracted`. Direct mesh imports now use the same default normal
policy as model-scene imports: authored `v:normal` vectors are preserved when
present and area-weighted unit normals are synthesized when absent. When the
CPU mesh also has matching `v:texcoord`, runtime bakes that `v:normal`
property into a generated normal texture asset, requests texture upload, and
registers a data-only `MaterialTextureAssetBindings` record keyed by stable
render id. `RenderExtractionCache` resolves the binding onto the
extraction-owned material sidecar during extraction, keeping ECS free of
graphics handles. Meshes without bakeable texture coordinates still import and
render through the existing material fallback with CPU normals intact. Focused
runtime contract tests passed, `IntrinsicTests` built, and the default
CPU-supported CTest gate passed.

Active
[`GRAPHICS-087`](../archive/GRAPHICS-087-vertex-color-property-texture-bake.md) —
Bake vec3/vec4 vertex color properties to surface albedo textures — retired
on 2026-06-12 at maturity `CPUContracted`. Runtime now exposes the shared
`Extrinsic.Runtime.MeshAttributeTextureBake` helper for finite mesh vertex
`glm::vec3`/`glm::vec4` properties with `v:texcoord`, preserves typed vertex
properties through asset mesh materialization, and lets model-scene handoff
create generated albedo child texture assets when authored base-color textures
are absent. Generated albedo textures route through the existing texture
upload/material binding path and surface shaders consume them via
`MaterialParams::AlbedoID`. Focused runtime/graphics/asset tests passed,
`IntrinsicTests` built, and the default CPU-supported CTest gate passed.

Active
[`BUG-042`](../archive/BUG-042-point-sphere-impostor-depth.md) — Promoted impostor
spheres do not intersect surfaces correctly — retired on 2026-06-12 at
maturity `CPUContracted`. The promoted retained point path now matches the
legacy shape: the `Points` cull bucket emits six vertices per source point,
the forward point pipeline is triangle-list with depth writes enabled, and
sphere mode reconstructs the front sphere surface in view space before writing
corrected `gl_FragDepth`. Point selection remains on the unexpanded
`SelectionPoints` bucket. Focused renderer lifecycle/selection regressions
passed, including `ForwardPointSphereImpostorsWriteCorrectedDepth`, and the
default CPU-supported CTest gate passed.

Active
[`ASSETIO-006`](../archive/ASSETIO-006-generated-normal-map-bake.md) — Generated
normal-map bake from mesh vertex normals — retired on 2026-06-12 at maturity
`CPUContracted`. Runtime now bakes generated linear RGBA8 normal textures from
named mesh vertex `glm::vec3` properties plus `v:texcoord`, preserves decoded
texture coordinates through asset mesh materialization, and creates generated
normal child assets for model-scene materials that lack authored normal maps.
The generated texture uses the existing texture handoff/material binding lane,
so shaders consume it through `MaterialParams::NormalID` without asset or
graphics layer ownership inversions. Focused bake/handoff regressions and the
default CPU-supported CTest gate passed.

Backlog
[`BUG-041`](../archive/BUG-041-asset-mesh-vertex-normals.md) — Asset mesh vertex normals
are lost during runtime materialization — retired on 2026-06-12 at maturity
`CPUContracted`. Geometry/model decoders already produced `v:normal` payloads
for formats that supplied normals, but both runtime halfedge materialization
paths rebuilt meshes from positions and face indices only, dropping the normal
property before ECS `GeometrySources` population. Runtime now shares
`BuildRuntimeHalfedgeMeshWithNormals(...)` across direct mesh imports and
model-scene primitive handoff: explicit per-vertex normals are copied, missing
source normals are filled with deterministic area-weighted unit normals, and
the direct-import renderable fallback preserves the same normal data when
strict shared topology fails only for renderable non-manifold/winding
diagnostics. `MeshGeometryPacker` also encodes available mesh normals into the
existing 20-byte surface vertex layout's U/V channel. Focused runtime
regressions cover explicit OBJ normals, computed fallback normals, model-scene
handoff, and packer output; the default CPU gate passed.

Backlog
[`BUG-040`](../archive/BUG-040-orbit-camera-vertical-drag-sign.md) — Orbit camera
vertical drag sign — retired on 2026-06-12 at maturity `CPUContracted`. The
`BUG-039` quaternion orbit fix preserved the legacy algebraic `-yDelta` pitch
sign, but in the promoted app's screen-space input convention Y grows downward.
That made a mouse-up drag place the camera below the target and point upward;
mouse-down did the inverse. A new runtime controller regression red-gated the
small-drag sign with `Position.y == -0.62373507` and `Forward.y == 0.2079117`
for mouse-up. Orbit pitch now applies `+yDelta` around the camera-local right
axis; fly/free-look signs are unchanged. The focused sign regression,
`RuntimeCameraControllers` suite, and default CPU gate passed.

Backlog
[`BUG-039`](../archive/BUG-039-orbit-camera-rotation-lock.md) — Orbit camera rotation
lock — retired on 2026-06-12 at maturity `CPUContracted`. The promoted orbit
controller had reused scalar yaw/pitch state and a fixed world-up view, so a
large vertical drag clamped at the pitch pole instead of continuing like the
legacy trackball camera. Orbit now stores accumulated orientation derived from
the seed forward/up vectors, applies drag deltas as quaternion rotations around
the current camera-local up/right axes, derives view forward/up from that
orientation, and keeps existing radius, zoom, focus, yaw diagnostic, and WASD
panning behavior. The new runtime contract regression red-gated the lock with
`Forward.z == -0.0174523834` and `Up.y == 1`, then passed after the quaternion
orbit fix; the full camera-controller suite and default CPU gate passed.

Backlog
[`BUG-038`](../archive/BUG-038-sandbox-dropped-file-diagnostics.md) — Dropped file
imports fail silently in the sandbox — retired on 2026-06-12 at maturity
`CPUContracted`. The event path itself was already wired: focused contracts
showed runtime platform-drop dispatch and valid dropped OBJ/OFF imports reach
the import/materialization path, so asset IO, generic IO, GPU upload, and
renderer visibility were downstream of the reported silence. The actual bug was
observability at the runtime boundary: dropped imports recorded last-import
state for the editor panel but did not log receipt, route/queue decisions, or
completion. `Engine` now logs file-drop receipt, empty-path rejection,
geometry-vs-synchronous import routing, successful streaming queue submission,
queue-submission rejection, and shared import success/failure from
`RecordAssetImportEvent`. A new runtime contract regression red-gated a missing
OBJ drop with no logs, then proved receipt/queue/failure breadcrumbs plus a
failed `RuntimeAssetImportEvent` with payload `Mesh` and `FileNotFound`.

Active
[`BUG-037`](../archive/BUG-037-window-close-stale-run-state.md) — Window close can leave
runtime running — retired on 2026-06-12 at maturity `CPUContracted`. The
runtime close path now normalizes native `IWindow::ShouldClose()` exits through
`Engine::RequestExit()` when `Engine::Run()` leaves its outer loop, closing the
state gap where a platform/native close flag could end the loop while
`Engine::IsRunning()` still reported true. `RunFrame()` continues to handle
close before renderer work when the flag is observed at the platform-frame
boundary. New runtime/ImGui wiring regressions cover native close before the
first frame and native close after representative camera/UI/selection input,
red-gating the stale run-state bug before the fix and passing after the runtime
state normalization. The default CPU-supported correctness gate passed.

Active
[`BUG-036`](../archive/BUG-036-ui-input-capture-leak.md) — UI-captured input leaks into
engine controls — retired on 2026-06-12 at maturity `CPUContracted`. Dear
ImGui capture state is now surfaced through `ImGuiAdapter` for both mouse and
keyboard input, and `Engine::RunFrame()` samples that state once after the UI
frame before routing runtime input consumers. Mouse capture continues to block
viewport-selection picks, while mouse or keyboard capture suppresses camera
controller updates and transform-gizmo input for the frame without mutating the
platform raw input context. Focused contract coverage red-gated the leak by
forcing Dear ImGui capture while raw `W`, Shift, and mouse input were present;
the fixed path leaves runtime camera, gizmo, and selection consumers idle under
UI capture and keeps existing behavior when the UI does not capture input.

Backlog
[`RUNTIME-106`](../archive/RUNTIME-106-render-component-domain-composition.md) — Render
component domain composition — retired on 2026-06-12 at maturity
`CPUContracted`. Mesh, graph, and point-cloud rendering now share the promoted
user-facing composition contract: `GeometrySources::BuildConstView(...)`
selects the geometric domain, while `RenderSurface`, `RenderEdges`, and
`RenderPoints` component presence selects render lanes supported by that domain.
Mesh `RenderEdges` and `RenderPoints` reuse the existing runtime primitive-view
sidecars directly from ECS components, so mesh wireframe and vertex rendering no
longer require `RenderSurface` or `MeshPrimitiveViewSettings`. The legacy
primitive-view editor/engine command surfaces translate to `RenderEdges` /
`RenderPoints` for compatibility, and extraction no longer treats the settings
map as authority. Graph lane toggles remain on the shared graph residency handle
but repack deterministically on component changes, and point-cloud
`RenderSurface` / `RenderEdges` requests fail closed with diagnostics and no
stale point residency. Focused CPU/null coverage proves mesh edge-only,
point-only, and combined lanes, graph lane toggles, point-cloud unsupported
lanes, UI render-hint command routing, scene serialization of `edges`, and
engine compatibility translation; the default CPU gate remains the retirement
gate and no `Operational` follow-up is owed by default.

Backlog
[`BUG-028`](../archive/BUG-028-mesh-primitive-view-ui-rendering.md) — Mesh primitive view
UI toggles do not render — retired on 2026-06-11 at maturity `CPUContracted`.
The promoted mesh edge/vertex view path is runtime extraction-cache sidecar
state, not legacy ECS `MeshEdgeView` / `MeshVertexView` components. The fix
extends `MeshPrimitiveViewSettings` with vertex style/radius, exposes those
controls through sandbox UI commands and command history, writes retained point
`GpuEntityConfig::PointMode` / `PointSize` for the derived vertex sidecar every
frame, derives edge-view wireframes from halfedge/face topology when explicit
edge rows are absent, and updates the forward point shader to draw flat
circles, screen-space sphere impostors, and normal-aligned surfel ellipses from
the shared UV normal payload. Mesh vertex views compute face-area weighted
normals from promoted halfedge/face topology, point clouds forward `v:normal`
when present, and graph nodes carry the no-normal sentinel. Focused CPU/null
tests prove the UI command path, edge/vertex sidecar extraction, derived
wireframe fallback, OBJ mesh primitive views, config reuse updates, and shader
compilation; broader GPU screenshot proof remains in the working-sandbox
acceptance lane.

Backlog
[`UI-013`](../archive/UI-013-domain-render-hint-controls.md) — Sandbox EditorUI domain
render hint controls — retired on 2026-06-11 at maturity `CPUContracted`.
Promoted mesh, graph, and point-cloud rendering paths were already present in
runtime extraction and renderer passes; this slice closed the editor workflow
gap by adding `ApplySandboxEditorRenderHintCommand(...)`, typed render-hint
domain-window model fields, and ImGui controls for selected-domain
`RenderSurface`, `RenderEdges`, and `RenderPoints` components. Commands are
undoable through `EditorCommandHistory` when available, graph edge-lane edits
force runtime graph residency to repack, and uniform retained-point radius/type
settings now flow through `VisualizationSyncRecord` into `GpuEntityConfig`.
Retained-line per-entity width rasterization remains renderer-owned future
work; this slice stores the promoted component value and keeps graphics free of
live ECS reads.

Backlog
[`BUG-026B`](../archive/BUG-026B-vulkan-click-pick-readback-smoke.md) — Vulkan
click-pick readback smoke — retired on 2026-06-11 at maturity `Operational`.
The opt-in `gpu;vulkan` runtime sandbox smoke now waits for the promoted Vulkan
device to become operational, submits a real `SelectionController::RequestClickPick`
at the projected center of `ReferenceTriangle`, and verifies the GPU readback
selects the triangle through the runtime controller rather than the hierarchy
selection shortcut. The smoke asserts `Engine::GetLastRefinedPrimitiveSelection()`
reports a mesh face hit with resolved face/edge/vertex IDs plus a depth-derived
world/local cursor on the triangle plane, then submits a far-background click
and verifies the no-hit readback clears selection and the refined primitive
cache. The run passed on NVIDIA RTX 3050 / NVIDIA driver 590.48.01, upgrading
the BUG-026 fix to `Operational`.

Backlog
[`GRAPHICS-086`](../archive/GRAPHICS-086-rhi-retirement-parity-and-cuda-decision.md) —
RHI retirement parity and CUDA decision — retired on 2026-06-11 at maturity
`CPUContracted`. The audit maps legacy `RHI.CommandUtils`,
`RHI.PersistentDescriptors`, `RHI.Swapchain`/`RHI.Image`, and
`RHI.SceneInstances` to promoted `ICommandContext`/`ITransferQueue`/submit-plan
seams, backend-local Vulkan descriptor/swapchain/image ownership,
backend-neutral RHI handles/descriptors/present modes, and renderer-owned
`GpuWorld`/`RHI::GpuInstanceData` state. CUDA is removed from the promoted
default path because no current runtime, graphics, method, or benchmark
consumer needs it; future CUDA must open a new opt-in method/backend task with a
concrete workload and verification plan. `LEGACY-009` is now blocked by
consumer-grep/subtree ordering rather than an unnamed RHI/CUDA parity gap.

Backlog
[`GRAPHICS-084C`](../archive/GRAPHICS-084C-visualization-property-buffer-vulkan-smoke.md) —
visualization property-buffer Vulkan smoke — retired on 2026-06-11 at maturity
`Operational`. The existing visualization-overlay GPU smoke now submits
graphics-owned property-buffer upload descriptors for vector-field position and
vector arrays, verifies `RenderGraphFrameStats::VisualizationPropertyBuffers`
accepted/uploaded both descriptors without deferral or resource errors, and
records `VisualizationOverlayPass` on the promoted Vulkan path only after
packet BDA publication succeeds. The task keeps runtime/ECS out of graphics;
`GRAPHICS-086` later retired the broader RHI/CUDA audit.

Backlog
[`GRAPHICS-084`](../archive/GRAPHICS-084-visualization-property-buffer-residency.md) —
visualization property-buffer residency — retired on 2026-06-11 at maturity
`CPUContracted`. Runtime visualization adapters now emit copied CPU property
arrays as `VisualizationPropertyBufferUploadDescriptor` records when external
BDAs are absent. The renderer copies descriptor payloads into retained snapshot
storage, validates supported scalar/color/vector descriptors centrally, uploads
or reuses renderer-owned `RHI::BufferManager` storage buffers, publishes BDAs
into scalar/color/vector/isoline packets before `ValidateVisualizationPackets`
runs, and reports diagnostics for unsupported types, invalid shape,
non-finite values, stale dirty stamps, upload deferral, and invalid resources.
Runtime/UI stay data-only and do not own GPU resources. The opt-in Vulkan
operational proof was retired by `GRAPHICS-084C`; this retirement does not
claim a fresh `gpu;vulkan` host run.

Backlog
[`GRAPHICS-085`](../archive/GRAPHICS-085-overlay-packet-backend-parity.md) — overlay
packet backend parity — retired on 2026-06-11 at maturity `CPUContracted`.
The task composes the retained overlay-like backend lanes classified by
`RUNTIME-104` without adding a runtime/editor overlay creation API or a new
graphics packet class. A new graphics contract test submits transient debug
triangle, line, and point packets together with visualization vector-field and
isoline packets in one frame, then proves both `TransientDebugSurfacePass` and
`VisualizationOverlayPass` record with per-lane submitted/recorded diagnostics
and no missing-pipeline skips. Selectable overlay-like workflows remain covered
by ordinary renderable and primitive-view selection/outline snapshots; packet-
only visualization overlays remain visual-only because no immutable selection
metadata is added. Existing opt-in transient-debug and visualization-overlay
`gpu;vulkan` smokes remain the operational evidence path, but this retirement
does not claim a fresh Vulkan host run.

Backlog
[`RUNTIME-104`](../archive/RUNTIME-104-derived-overlay-producer-lifecycle.md) — derived
overlay producer lifecycle — retired on 2026-06-11 at maturity
`CPUContracted`. The value gate found no current promoted workflow requiring a
new persistent runtime overlay producer API. Legacy mesh/graph/point child
overlays are represented by ordinary `GeometrySources` entities when runtime/UI
imports or authors data; mesh edge/vertex overlays use component-driven
runtime-owned primitive-view sidecars; transient line/point/triangle overlays remain on
transient debug packets; vector-field and isoline overlays remain data-only
visualization packets emitted by `Runtime.VisualizationAdapters`. The
vector-field packet path is covered by runtime extraction regression coverage
and creates no child ECS entity, so the legacy parent/child cleanup invariant is
satisfied for current workflows without graphics importing ECS or storing RHI
handles in components. Backend command-shape proof remains open under
`GRAPHICS-085`; selected property-buffer residency remains `GRAPHICS-084`.

Backlog
[`BUG-027`](../archive/BUG-027-sandbox-dragdrop-close-mesh-views.md) — sandbox
drag/drop, close, and mesh primitive-view regression — opened and retired on
2026-06-11 at maturity `CPUContracted`. The reported sandbox path had three
runtime-wiring failures: direct platform close events reached the engine
listener but were ignored, the live frame loop polled the X-button close event
and continued into ImGui/render work before re-checking `ShouldClose()`,
dropped/direct standalone geometry imports materialized entities without
selecting them, and the promoted mesh primitive-view UI therefore had no
selected mesh to control after drag/drop. The fix wires `WindowCloseEvent` to
`Engine::RequestExit()`, delegates the `RunFrame()` platform phase to
`Core::ExecutePlatformBeginFrameContract(...)` so a close observed during
`PollEvents()` returns before renderer work, carries the materialized entity
handle out of standalone mesh/graph/point-cloud import, and selects that entity
after geometry import and camera focus. Regression coverage replays
`WindowDropEvent`/`WindowCloseEvent` through the runtime platform-event handler,
imports OBJ and OFF meshes through `Engine::Run()`, proves the imported mesh is
the active selection, drives the promoted primitive-view command surface,
asserts edge/vertex view uploads through `RenderExtractionCache`, and pins the
close-button timing with frame-loop/layering contracts. A narrow
`Engine::DispatchPlatformEventForTest(...)` seam exists only to replay platform
events through the same handler installed as the live window listener.

Active
[`BUG-026`](../archive/BUG-026-click-pick-readback-entity-zero-and-depth.md) — viewport
click selection dead: render-id zero collision, UINT clear punning, and
missing depth readback — opened and retired on 2026-06-10 at maturity
`CPUContracted`. Clicking in the sandbox selected nothing because two
defects stacked: (1) the render id written to the GPU instance table was the
raw `entt::entity` cast, so the default `ReferenceTriangle` (first entity of
a fresh registry, handle 0) collided with the picking drain's
`EntityId == 0` background sentinel and every click on it published NoHit;
(2) `PickingPass` cleared its `R32_UINT` ID targets with the scene-color
light-blue float clear, which the Vulkan backend bit-punned into
`0x3DCCCCCD`, so background clicks published phantom hits silently rejected
as stale. Fixed by centralizing the render-id convention as
`entt handle + 1` (`StableEntityLookup::ToRenderId`, 0 reserved for
background, `entt::null` wraps to 0) across extraction / selection /
refinement / gizmo packets, dedicating a zero-clear attachment pair to the
ID targets, and making the Vulkan backend value-convert clear colors for
integer formats (`ToVkClearColorValue`). The same task added the missing
depth readback the original selection design called for: 16-byte
`Picking.Readback` slots now carry a `SceneDepth` pixel sample, the drain
publishes `HasDepth`/`Depth` + the request pixel, `Engine` captures a
per-`Sequence` pick context (inverse view-projection, viewport, pick ray,
pixel-radius scale) and replays it on readback consume, and
`RefinePickReadbackResult` unprojects the cursor (`UnprojectPickDepth`),
reports it in world + entity-local space (`CursorFromDepth`, `WorldCursor`,
`LocalCursor`), anchors the closest-vertex/edge/face refinement with it, and
feeds the ray fallback for hint-less hits (pixel radius scaled by hit
distance under perspective; kept at the depth-invariant pixel footprint
under orthographic cameras such as the top-down controller — review
follow-up, 2026-06-11). Why the gates
missed it: CPU contracts seeded readback bytes directly (never entity 0,
never the real clear), and the `gpu;vulkan` smokes exercised hierarchy
selection, which bypasses the readback path. 12 new regression tests lock
the conventions; `Operational` (real Vulkan click round trip) owned by
`BUG-026B`.

Backlog
[`LEGACY-002`](../archive/LEGACY-002-seed-src-legacy-retirement-backlog.md) — seed
retirement tasks for remaining `src/legacy/` subtrees — retired to
`tasks/done/` on 2026-06-10. The deliverables had been complete since
2026-06-06 (the `LEGACY-003..010` per-subtree deletion tasks, the
architecture README "Legacy retirement" section with dependency-ordered
hints, and the `docs/migration/legacy-retirement.md` sequencing links);
the file stayed in backlog only because ~54 layering-allowlist rows still
named `LEGACY-002` as their open umbrella owner. `HARDEN-082` (the
metadata-only rebinding follow-up the task's context required) moved
those rows to their per-subtree owners, so no allowlist row references
`LEGACY-002` and the seed retires. Remaining legacy retirement is owned
by `LEGACY-001` and `LEGACY-004..010`, each gated on its consumer-grep
prerequisite.

Backlog
[`HARDEN-082`](../archive/HARDEN-082-rebind-legacy-allowlist-umbrella-rows.md) —
rebind legacy allowlist umbrella rows to per-subtree owners — opened and
retired to `tasks/done/` on 2026-06-10 at maturity `Retired`
(metadata-only governance rebind). All 54
`tools/repo/layering_allowlist.yaml` rows still naming the `LEGACY-002`
seeding umbrella moved to their concrete per-subtree retirement owners by
`file_glob` prefix (9 rows each to `LEGACY-004` Asset, `LEGACY-005` Core,
`LEGACY-006` ECS, `LEGACY-008` Graphics, `LEGACY-009` RHI, `LEGACY-010`
Runtime), with each row's `expires` text rewritten from the satisfied
"until LEGACY-002 seeds ..." condition to "until LEGACY-00N deletes ...".
No rows were added or removed and no glob changed; the strict layering
check stays green with the allowlisted-violation count unchanged at 1187.
This is the rebinding follow-up that `LEGACY-002`'s context required
before the seed itself could retire.

Backlog
[`HARDEN-078`](../archive/HARDEN-078-track-untracked-todo-temporary-markers.md) —
track or resolve untracked TODO / temporary markers in promoted src —
retired to `tasks/done/` on 2026-06-10 at maturity `Retired` (pure marker
hygiene). The `Core.Filesystem` dead commented `CallbackRegistry` import
and bare TODO were resolved by decision (a): watchers keep explicit
per-watch `ChangeCallback` injection (the already-implemented behavior),
recorded as a short policy note; no behavior change. The
`Engine::GetStreamingGraph()` temporary TaskGraph bridge now has a
tracked removal owner per `AGENTS.md` §13: the new `RUNTIME-105` backlog
task (the promoted tree already has zero bridge consumers), named in both
the `[[deprecated(...)]]` message and the runtime README streaming note.
The drift-audit Row 7 greps over `src/core/**` and `src/runtime/**`
return only task-ID-tracked markers. Default CPU gate green at
retirement.

Backlog
[`RORG-031A`](../archive/RORG-031A-architecture-foundation.md) — architecture
foundation backlog seed — retired to `tasks/done/` on 2026-06-10. The
seed's job was converting the legacy living backlog's architecture items
into structured, independently executable tasks, and that exists: the
`tasks/backlog/architecture/` queue carries the LEGACY-001..012 retirement
series, `HARDEN-078`, `INFRA-001`, and a category README with explicit
consumer-grep gates; architecture governance tooling
(`check_layering.py`, `check_docs_sync.py`,
`generate_module_inventory.py`) exists and runs strict in CI; and
migration/CI dependencies are recorded as gates and front-matter
`depends_on` edges. Open architecture work remains independently tracked
by its own task files.

Backlog
[`PROC-008`](../archive/PROC-008-category-readme-state-history-split.md) — category
README state/history split — retired to `tasks/done/` on 2026-06-10,
completing Theme H. Slice A mechanically split every
`tasks/backlog/<category>/README.md` into open lists and verbatim
`## Retired` history sections (open entries cite retired tasks as plain
code spans; emptied lists carry explicit none-open lines; the workshop
pack's completed execution record was re-headed as history;
`bugs/index.md` already conformed via `Verified / Closed`). Slice B
extended `check_task_state_links.py` with `validate_category_indexes`:
heading-stack scanning of category indexes where done-links are findings
unless under a history-marked heading
(retired/history/closed/completed/resolved/verified/done), ATX headings
require a trailing space so inline PR references like `#921` cannot pop
the stack, and sections that interleave done prerequisites with open work
by design — the rendering dependency DAG — opt out explicitly with
`<!-- state-link-guard: allow-done-links -->` while rendering's non-DAG
sections were de-linked. `docs/agent/task-format.md` step 4 and the skill
mirrors document the convention. The throwaway done-link probe produced
exactly one finding and was removed. With PROC-001..008 retired, Theme H
has no open members.

Backlog
[`METHOD-011`](../archive/METHOD-011-sph-fluid-reference-backend.md) — SPH fluid
reference backend — retired to `tasks/done/` on 2026-06-10 at maturity
`CPUContracted`. `methods/physics/sph_fluid_reference/` ships the
deterministic weakly compressible SPH `cpu_reference` backend (Mueller
2003): Poly6 density with self-contribution, clamped ideal-gas pressure,
symmetric Spiky-gradient pressure force, viscosity-Laplacian force,
semi-implicit Euler, half-space boundary planes with restitution-scaled
normal reflection, and deterministic O(N^2) index-ordered neighbor
enumeration with an advisory `MaxNeighborLimit` whose overflow is
reported, never truncated. Diagnostics cover validation codes, total
mass, density statistics (average/min/max, `MaxCompression`
incompressibility proxy, mean relative density error), neighbor counts,
max velocity, kinetic energy drift, and the non-finite fail-closed
fallback. Thirteen `unit;physics` tests pin kernel closed forms and
numeric normalization, uniform-grid density recovery (~1% interior
error), exact symmetric-pair momentum conservation, viscosity smoothing,
the free-fall closed form, the toy column drop over a floor plane,
overflow reporting, invalid-input validation, determinism, and the
fallback. The `physics.sph_fluid_reference.smoke` benchmark emits
validated result JSON (static-grid interior density error ~0.0098 vs
0.05 threshold). With METHOD-009/010/011 all retired, Theme C has no
open members; optimized/GPU backends and runtime integration open as new
tasks per the roadmap gates.

Backlog
[`METHOD-010`](../archive/METHOD-010-xpbd-cloth-shell-reference-backend.md) — XPBD
cloth and shell reference backend — retired to `tasks/done/` on 2026-06-10
at maturity `CPUContracted`. `methods/physics/xpbd_cloth_reference/` ships
the deterministic XPBD `cpu_reference` backend over triangle-mesh cloth
state: position prediction, iterative compliant constraint projection with
per-constraint Lagrange multipliers (structural unique-edge constraints
plus opposite-vertex bending pairs across interior edges, both built
deterministically by `BuildClothFromTriangles`), half-space collision
projection with collision inputs as pure method parameters (sphere
colliders declared but unsupported and counted), and position-derived
velocities. Diagnostics cover validation codes (including
`InvalidTopology` for repeated/out-of-range triangle indices),
degenerate-triangle and coincident-constraint counts, stretch/bend
residuals (max, L2), convergence against the residual tolerance,
kinetic/mechanical energy drift, and the non-finite fail-closed fallback.
Fourteen `unit;physics` tests pin the builder topology, rigid projection,
hanging-patch convergence, bend response, pinned vertices, collision
floor, degenerate/invalid handling, non-convergence reporting,
determinism, and the fallback. The `physics.xpbd_cloth_reference.smoke`
benchmark emits validated result JSON (final max stretch residual
~6.2e-4 vs 5e-3 threshold). Optimized CPU/GPU backends remain forbidden
until a future task names this package as its oracle. Theme C's remaining
open member is `METHOD-011` (SPH fluid).

Backlog
[`METHOD-009`](../archive/METHOD-009-particle-spring-reference-backend.md) — particle
and mass-spring reference backend — retired to `tasks/done/` on 2026-06-10
at maturity `CPUContracted`. `methods/physics/particle_spring_reference/`
ships the deterministic `cpu_reference` backend for particle dynamics and
mass-spring systems: semi-implicit Euler integration, Hooke springs with
Provot-style axial damping, pinning via zero inverse mass, global drag, and
machine-checkable diagnostics (validation codes for invalid
timestep/particle/spring, pinned and degenerate-spring counts, post-step
spring residuals max/L2, kinetic/total energy drift, the `omega*dt`
stability ratio with limit flag, and a non-finite fail-closed fallback that
returns the input state unchanged). Twelve `unit;physics` correctness
tests pin free-fall closed form, rest-length equilibrium, exact
momentum/center-of-mass conservation, bounded harmonic energy drift,
damped hanging-spring analytic equilibrium, pinned/degenerate/invalid
handling, instability fallback, and repeated-step determinism. The
`physics.particle_spring_reference.smoke` benchmark manifest + workload
emit validated result JSON with exact conservation error as the quality
metric. Optimized CPU/GPU backends remain forbidden until a future task
names this package as its oracle. Theme C's remaining open members are
`METHOD-010` (XPBD cloth/shell) and `METHOD-011` (SPH fluid).

Backlog
[`RORG-031C`](../archive/RORG-031C-runtime-composition.md) — runtime composition
backlog seed — retired to `tasks/done/` on 2026-06-10. The seed's job was
to replace the unnamed runtime composition narrative gap with concrete
child tasks, and that is done: `RUNTIME-099` (explicit lifecycle pipeline
with shutdown determinism, `CPUContracted`), `RUNTIME-100` (scene
lifecycle), `RUNTIME-102` (editor command history), `RUNTIME-103`
(geometry algorithm execution queue decision), and `RUNTIME-104` (derived
overlay producer lifecycle) are retired. `RUNTIME-101` (asset ingest state
machine) was independently tracked after this seed and retired on
2026-06-15, synchronized with the `LEGACY-011` feature map. Theme A now has
no open members.

Backlog
[`BUG-025`](../archive/BUG-025-contact-manifold-normal-convention.md) — geometry
contact manifold normals violate the documented A→B convention — retired to
`tasks/done/` on 2026-06-10 at maturity `CPUContracted`. Root cause was two
kernel inversions: `EPA_Solver` negated the closest-face outward normal of
the A−B Minkowski polytope (which is already the A→B direction), and
`Contact_Analytic(Sphere, AABB)` computed the normal from the box-closest
point toward the sphere center (B→A) in both its shallow and
deep-penetration branches. Fix: EPA returns `searchDir` directly,
`Contact_Fallback` derives `ContactPointB = ContactPointA - Normal * Depth`
(the same world point under the corrected normal), and the sphere-AABB
analytic path is A→B in both branches with consistent contact points. New
`unit;geometry` convention tests pin every analytic overload, the
reversed-argument dispatcher, and the GJK/EPA fallback for
sphere/capsule/OBB pairings in both argument orders
(`ContactManifold.Convention_*`). The physics-layer orientation guard and
its regression test stay as defense in depth. Geometry label 1263/1263 and
physics label 21/21 at retirement. Theme G has no open members.

Backlog
[`BUG-024B`](../archive/BUG-024B-sandbox-transform-edit-vulkan-pixel-shift-smoke.md) —
Vulkan pixel-shift smoke for sandbox transform edits — retired to
`tasks/done/` on 2026-06-10 at maturity `Operational`. The opt-in
`gpu;vulkan` smoke `RuntimeSandboxAcceptanceGpuSmoke.InspectorTransformEditShiftsReferenceTrianglePixels`
applies the promoted Inspector transform-edit command through the live
`EditorCommandHistory` path on a mid-run frame (after that frame's
fixed-step bundle) and asserts the rendered `ReferenceTriangle` moved: the
frame center returns to the background and the analytically projected
shifted sample contains the triangle. Passed on NVIDIA GeForce RTX 3050 /
driver 590.48.01 (focused 1/1; full smoke suite 6/6), upgrading the BUG-024
fix from `CPUContracted` to `Operational`. Theme G has no open members.

Backlog
[`BUG-024`](../archive/BUG-024-sandbox-transform-edit-rendering.md) — sandbox transform
UI edits do not move rendered triangle — retired to `tasks/done/` on
2026-06-10 at maturity `CPUContracted`. Root cause: Inspector/gizmo
transform edits run after the fixed-step ECS bundle, so render extraction
observed a stale `Transform::WorldMatrix` and the rendered model matrix
never moved within the frame. Fix: `Extrinsic.Runtime.EcsSystemBundle` now
exports `FlushPreRenderTransformState` (direct `TransformHierarchy` →
`BoundsPropagation` → `RenderSync` pass), invoked by `Engine::RunFrame()`
after the variable tick, ImGui editor hook, and gizmo drive — before
transform-gizmo packet build and extraction. Regression coverage:
engine-level `RuntimeSandboxAcceptance.InspectorTransformEditFlushedToRenderStateSameFrame`
(verified failing with the flush disabled), extraction-level
`RuntimeRenderExtraction.UiTransformEditModelReachesRenderWorldAfterPreRenderFlush`
(asserts the render-world model translation), and flush-helper contract
tests in `RuntimeEcsSystemBundle`. Default CPU gate passed 2882/2882 at
retirement. `Operational` (Vulkan pixel-shift smoke) owned by `BUG-024B`.

Previously-active
[`PROC-006`](../archive/PROC-006-audit-cadence-lapse-visibility.md) — audit cadence
lapse visibility retired to `tasks/done/` on 2026-06-09. The slice added
`tools/agents/check_audit_cadence.py` (agent-output limit 14d, drift limit
42d, report-only by default, `--strict` for local use only), a non-blocking
nightly-deep report step, last-report dates in the `tasks/SESSION-BRIEF.md`
audits section (dates rather than ok/overdue so the brief stays
deterministic under the CI freshness check), and lapse-visibility notes in
both audit checklists. No PR gate depends on audit recency. Theme H's
remaining open leaf is `PROC-008` (category README state/history split).

Previously-active
[`PROC-004`](../archive/PROC-004-task-front-matter-and-generated-session-brief.md) —
structured task front-matter + generated session brief retired to
`tasks/done/` on 2026-06-09 at maturity `Operational`. Slice A gave all 44
open tasks YAML front-matter (`id`/`theme`/`depends_on`) with strict
validation in `validate_tasks.py` (id↔title match, resolvable dependency
edges). Slice B added `tools/agents/generate_session_brief.py` and the
committed, `ci-docs.yml`-freshness-checked `tasks/SESSION-BRIEF.md`
(active tasks; per-theme unblocked/blocked with first unmet dependency),
and adopted it as mandatory session reading in `docs/agent/prompt/prompt.md`
and the `intrinsicengine-core` skill, demoting the two task READMEs to
on-demand depth. Slice C (anchor-prose retirement) was skipped — PROC-003
had already reduced anchors to open-endpoint entries. The audits surface in
the brief is owned by `PROC-006`.

Previously-active
[`PROC-003`](../archive/PROC-003-split-task-index-state-from-retirement-history.md),
[`PROC-007`](../archive/PROC-007-onboarding-prompt-tightening.md),
[`PROC-005`](../archive/PROC-005-align-structural-check-mode-contract-text.md),
[`PROC-002`](../archive/PROC-002-task-id-uniqueness-and-allocation-rule.md), and
[`PROC-001`](../archive/PROC-001-skill-mirror-sync-generator-and-ci-gate.md) —
the first five Theme H agentic-workflow hardening slices retired to
`tasks/done/` on 2026-06-09 on branch
`claude/agentic-workflow-analysis-kohifk`. PROC-001 added
`tools/agents/sync_skills.py` (generate-and-verify skill mirror sync with
link rewriting and a `ci-docs.yml` gate) and repaired 11 drifted mirror
files. PROC-002 added the task-ID uniqueness pass to `validate_tasks.py`
with five grandfathered historical collisions and the max+1 allocation
rule in `task-format.md`. PROC-005 replaced the stale "warning mode"
structural-check claims in `AGENTS.md` §10 and `docs/agent/contract.md`
with the strict-CI reality plus a tracked-exception rule. PROC-007
deduplicated the onboarding prompt against `AGENTS.md` §2/§5/§7/§9/§12 and
gave loop mode an `N = 3` default plus a per-iteration push checkpoint.
PROC-003 created this retirement log, trimmed `tasks/active/README.md`
532→21 lines and `tasks/backlog/README.md` 334→212 lines to state-only,
and added the `validate_state_only_indexes` regrowth guard; category-README
cleanup is owned by the follow-up `PROC-008`. Remaining Theme H leaves:
`PROC-004` (front-matter + session brief), `PROC-006` (audit cadence
visibility), `PROC-008`.

Previously-active
[`HARDEN-079`](../archive/HARDEN-079-core-module-implementation-splits.md),
[`GEOM-021`](../archive/GEOM-021-meshsoup-module-implementation-split.md),
[`GEOM-022`](../archive/GEOM-022-remaining-geometry-module-implementation-splits.md),
[`HARDEN-080`](../archive/HARDEN-080-ecs-module-implementation-splits.md),
[`PLATFORM-005`](../archive/PLATFORM-005-platform-module-implementation-splits.md),
[`GRAPHICS-083`](../archive/GRAPHICS-083-graphics-rhi-module-implementation-splits.md), and
[`RUNTIME-096`](../archive/RUNTIME-096-runtime-module-implementation-splits.md) —
promoted module implementation split batch retired to `tasks/done/` on
2026-06-07. The implementation split landed in `bfcd2751`; retirement was
held until the default CPU gate passed after rebuilding `IntrinsicTests` with
`CCACHE_DISABLE=1`, explicitly building `IntrinsicBenchmarkSmoke`, and rerunning
`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'` for 2816/2816 passing tests.
The earlier rendergraph ASan failure was stale incremental C++23 module layout
state from ccache/module artifacts, not a source defect in the split.

Previously-active
[`WORKSHOP-007`](../archive/WORKSHOP-007-dependency-driven-default-recipe.md) —
dependency-driven default frame recipe retired to `tasks/done/` on
2026-06-06. The slice removed blanket previous-pass chaining from
`BuildDefaultFrameRecipe`, kept explicit pass dependencies as an intentional
graph API rather than a default recipe behavior, exposed explicit dependency
edges in compiled pass declarations/debug dumps, and added contract coverage for
resource-derived ordering, side-effect order, barrier packet ordering, and
picking/selection/debug/postprocess feature combinations.

Previously-active
[`WORKSHOP-006`](../archive/WORKSHOP-006-extract-render-prep-pipeline.md) —
render-prep pipeline extraction retired to `tasks/done/` on 2026-06-06. The
slice added the `Extrinsic.Graphics.RenderPrepPipeline` module, moved
CPU-side `PrepareFrame` prep ordering out of the renderer, retained task-graph
and sequential ordering coverage, added fail-closed missing-input/task-graph
diagnostics, and made renderer lifecycle diagnostics reject `ExecuteFrame`
after failed prep.

Previously-active
[`ARCH-001`](../archive/ARCH-001-physics-layer-ownership-and-ecs-integration.md)
— physics layer ownership and ECS/runtime integration contract retired to
`tasks/done/` on 2026-06-05 at maturity `Retired`. The slice accepted
`src/physics` as the simulation-world layer through ADR-0019 with
`physics -> core, geometry` dependencies, updated `AGENTS.md`, architecture
docs, agent skill mirrors, label policy, and layering tooling, and opened
`PHYSICS-001..003` follow-up tasks for world/runtime sync,
broadphase/narrowphase, and solver diagnostics. `HARDEN-064` has since retired
the ECS collider/rigid-body authoring contract under the
no-solver-handles-in-ECS boundary, and `PHYSICS-001` has retired the first
CPU-only physics world/runtime bridge. `PHYSICS-002` is the next open physics
runtime-readiness leaf retired on 2026-06-06; `PHYSICS-003` is now the next
open physics runtime-readiness leaf.

Previously-active
[`GRAPHICS-040C`](../archive/GRAPHICS-040C-aa-recipe-selection-and-integration.md)
— AA recipe selection + post-chain integration retired to `tasks/done/` on
2026-06-05 at maturity `Operational`. The slice added the explicit
`FrameRecipeAAOptions` selector, mode-specific FXAA/SMAA pass compilation,
temporal `ReconstructionPass` routing with retained history imports,
input/output extent splitting, renderer-side reference-TAA execution, and
`ReconstructorAppliedFrames` / `HistoryDisocclusionPercent` / jitter
diagnostics. Vendor reconstructor backend children remain unopened.

Previously-active
[`GRAPHICS-040B`](../archive/GRAPHICS-040B-reconstructor-interface-and-reference-taa.md)
— `IReconstructor` interface + reference TAA retired to `tasks/done/` on
2026-06-05 at maturity `CPUContracted`. The slice added the vendor-free
`Extrinsic.Graphics.Reconstruction` module with `IReconstructor`,
`ReconstructionHints`, and `ReconstructionResult`, a CPU-contracted
`ReferenceTAAReconstructor` using 5x5 YCoCg variance clipping,
exposure-aware history weighting, reset invalidation, and disocclusion
fallback reporting, plus a retained `RGBA16_FLOAT` ping-pong
`ReconstructionHistorySystem` with retire-window coverage. Recipe selection and
post-chain integration are retired in `GRAPHICS-040C`; vendor children remain
unopened.

Previously-active
[`GRAPHICS-040A`](../archive/GRAPHICS-040A-jitter-and-motion-vectors.md) —
camera jitter + motion-vector buffer retired to `tasks/done/` on 2026-06-05 at
maturity `CPUContracted`. The slice added a deterministic Halton(2,3)×16
temporal jitter helper, projection-matrix jitter override, authoritative
`TemporalCameraViewSnapshot::JitterOffset`, opt-in `MotionVectors`
frame-recipe resource/attachment shape, `NoJitterNoHistory` suppression, and
graphics contract coverage for jitter replay, projection math, and motion-vector
target gating. Reference TAA reconstruction retired in `GRAPHICS-040B`; recipe
selection and post-chain integration retired in `GRAPHICS-040C`.

Previously-active
[`GRAPHICS-039D`](../archive/GRAPHICS-039D-cluster-async-compute-affinity.md) —
cluster build/assignment async-compute affinity retired to `tasks/done/` on
2026-06-05 at maturity `CPUContracted`. The slice tagged
`ClusterGridBuildPass` and `LightClusterAssignmentPass` with
`RenderQueue::AsyncCompute`, proved capability-absent demotion to graphics
through the framegraph/RHI queue-affinity helpers and submit-plan builder,
preserved single-queue correctness, updated the renderer docs, and added
frame-recipe contract coverage for async-capable and async-absent profiles.

Previously-active
[`GRAPHICS-039C`](../archive/GRAPHICS-039C-cluster-surface-shader-integration.md) —
clustered surface-shader integration + recipe wiring retired to `tasks/done/`
on 2026-06-05 at maturity `CPUContracted`. The slice added scene-table BDA
publication for `ClusterLights.Headers` / `ClusterLights.Indices`, cluster-grid
metadata in `GpuSceneTable`, renderer-owned retained cluster buffers and
pipeline leases, default-recipe reads for forward `SurfacePass` and deferred
`CompositionPass`, shared GLSL clustered-light iteration with a full-loop
fallback, CPU parity coverage for known-cell clustered accumulation, renderer
lifecycle assertions for scene-table publication/rebuild survival, and touched
shader compilation. Async-compute affinity is retired in `GRAPHICS-039D`.

Previously-active
[`GRAPHICS-039B`](../archive/GRAPHICS-039B-light-cluster-assignment.md) —
light-to-cluster assignment + overflow diagnostics retired to `tasks/done/` on
2026-06-05 at maturity `CPUContracted`. The slice added
`ClusterLightCellHeader`, retained `ClusterLights.Headers` /
`ClusterLights.Indices` / `ClusterLights.Counter` imports, the
`light_cluster_assign.comp` shader asset, a deterministic CPU assignment helper
over existing `LightSnapshot` values, conservative point/spot inclusion,
directional-light skip, 256-contributor clamp diagnostics, frame-recipe
`LightClusterAssignmentPass` ordering after `ClusterGridBuildPass`, and
contract coverage for shape inclusion, empty cells, overflow, counter clearing,
command shape, and diagnostic publication. Surface-shader consumption and
async-compute affinity remain `GRAPHICS-039C/D`.

Previously-active
[`GRAPHICS-039A`](../archive/GRAPHICS-039A-cluster-grid-build.md) — cluster grid
resource + build pass retired to `tasks/done/` on 2026-06-04 at maturity
`CPUContracted`. The slice added the `Extrinsic.Graphics.LightClusters` module,
the default 80 px tile / 24 logarithmic Z-slice froxel-grid contract, per-cell
view-space AABB construction with clamped partial-edge tile bounds,
`ClusterGrid.AABBs` resource/import semantics, default-recipe
`ClusterGridBuildPass` ordering after depth/HZB, the
`cluster_grid_build.comp` shader asset, and contract coverage for dimensions,
log-Z slicing, empty beyond-far mapping, partial edge tiles, AABB bounds,
resource usage, dispatch shape, and frame-recipe gating. Light assignment,
shader consumption, and async-compute affinity remain `GRAPHICS-039B/C/D`.

Previously-active
[`GRAPHICS-038E`](../archive/GRAPHICS-038E-hzb-conservatism-gpu-smoke.md) —
opt-in `gpu;vulkan` HZB conservatism smoke retired to `tasks/done/` on
2026-06-04 at maturity `Operational` on Vulkan-capable hosts. The slice added a
test-only HZB conservatism compute shader, a Vulkan smoke that dispatches the
two-phase predicate on real GPU storage buffers, CPU parity checks against
`ComputeTwoPhaseCullPartition(...)`, known-visible no-over-rejection,
disocclusion rescue, persistent rejection, invalid-previous-sample
conservatism, frustum-first rejection, and selection-bucket exemption coverage.
The default CPU/null contracts remain unchanged; production HZB storage-image
descriptor publication remains future backend descriptor integration.

Previously-active
[`GRAPHICS-038D`](../archive/GRAPHICS-038D-camera-transition-and-selection-exemption.md) —
camera-transition skip heuristic and selection-bucket occlusion exemption
retired to `tasks/done/` on 2026-06-04 at maturity `CPUContracted`. The slice
added snapshot-carried explicit camera-transition flags, delta-threshold
stale-HZB detection, `HzbStaleSkipCount` diagnostics, shader push-constant
flags, hard frustum-only phase-1 routing for selection buckets, runtime
camera-controller one-shot transition signaling, contract/integration coverage,
and renderer/runtime docs sync. `Operational` opt-in GPU/Vulkan conservatism
proof remains owned by `GRAPHICS-038E`.

Previously-active
[`GRAPHICS-038C`](../archive/GRAPHICS-038C-two-phase-cull-shader.md) —
phase-1/phase-2 cull shader extension and per-bucket buffer doubling retired
to `tasks/done/` on 2026-06-04 at maturity `CPUContracted`. The slice added the
`GpuCullBucketPhases` ABI, phase-1/phase-2 indirect output surfaces per bucket,
diagnostics counters, shader phase-output selection, renderer reset/table/barrier
wiring for both phases, `GetBucketPhase(kind, phase)`, deterministic CPU
visible/rejected/rescued partition coverage, and rendering docs sync. The
camera-transition/selection exemption remains `GRAPHICS-038D`; concrete Vulkan
HZB reject-list publication, phase-2 recull, and opt-in `gpu;vulkan`
conservatism proof remain `GRAPHICS-038E`.

Previously-active
[`GRAPHICS-038B`](../archive/GRAPHICS-038B-hzb-build-compute.md) — HZB build
compute shader + dispatch wiring retired to `tasks/done/` on 2026-06-04 at
maturity `CPUContracted`. The slice added `assets/shaders/hzb_build.comp`, the
pure HZB build-plan selector, backend-neutral per-mip fallback recording,
default-recipe `HZBBuildPass` wiring after `DepthPrepass`, renderer-owned
`HZB.Current` import/pipeline lease plumbing, null-RHI dispatch/barrier
contracts, shader-output verification, and rendering/debug-view docs sync.
Single-pass/SPD-style storage-image publication and opt-in `gpu;vulkan`
conservatism proof remain owned by `GRAPHICS-038E`.

Previously-active
[`GRAPHICS-037D`](../archive/GRAPHICS-037D-multi-queue-vulkan-recording.md) —
Vulkan multi-queue recording retired to `tasks/done/` on 2026-06-04 at
maturity `Operational` on Vulkan-capable hosts. Slices A-D landed
async-compute/transfer queue-family discovery, Sync2 queue-family token
translation, the backend-neutral RHI submit-plan/context seam, per-affinity
Vulkan command-buffer submission with timeline waits/signals and ownership
transfer barriers, default-recipe async histogram routing, and opt-in
`gpu;vulkan` readback smoke coverage. Capability-absent hosts keep the
single-queue path through queue-affinity demotion and the default CPU gate.

Previously-active
[`UI-001`](../archive/UI-001-sandbox-editor-shell-panels.md) — sandbox editor shell
and core panels on top of the runtime ImGui adapter/pass stack retired to
`tasks/done/` on 2026-06-03 at maturity `CPUContracted`. Slices A-D landed the
promoted editor shell, scene hierarchy, inspector/render-hint fields,
selected/hovered entity rows, refined primitive id/hit display, runtime-owned
local-transform edits, camera-controller replacement, mesh edge/vertex
primitive-view toggles, selected-entity spatial-debug and visualization-config
commands, visualization adapter-binding routing through engine-owned
render-extraction state, and file/import command execution through
`Engine::ImportAssetFromPath(...)` on top of the retired `ASSETIO-001`
asset/runtime ingest seams. Final file-backed visual/interactive proof remains
owned by `RUNTIME-095`.

Previously-active
[`RUNTIME-083`](../archive/RUNTIME-083-visualization-adapters.md) —
`Extrinsic.Runtime.VisualizationAdapters` runtime producer umbrella retired to
`tasks/done/` on 2026-06-02 at maturity `CPUContracted`. Slices A-E landed the
umbrella module, property-scalar, KMeans/color, vector-field, isoline, Htex
preview, and fragment-bake adapters, runtime-owned adapter registration/binding
state, scalar and non-scalar extraction selection into
`RuntimeRenderSnapshotBatch::Visualization*`, and extraction-side packet/error
stats with CPU `integration;runtime;graphics` coverage. `Operational`
visualization proof remains owned by `RUNTIME-095` or a later visualization
backend smoke.

Previously-active
[`GRAPHICS-079`](../archive/GRAPHICS-079-default-recipe-imgui-pass-wiring.md) —
default-recipe `Pass.ImGui` wiring (Theme A working-sandbox path, the consumer
half of the ImGui/UI leaves that gate `UI-001`) retired to `tasks/done/` on
2026-06-02. Slices A/B wired the renderer-side `ImGuiPass` executor route,
overlay handoff seam, pipeline lease, and runtime-owned overlay attachment.
Slice C added the retained font atlas, renderer-owned transient vertex/index
upload helper, runtime adapter payload copy, direct draw recording contracts,
and byte-identical atlas retention across rebuild. Slice D.1 promoted
`Pass.ImGui` to write `FrameRecipe.PresentSource` and proved the CPU/null
recorded path plus the default-recipe closing-cleanup assertion. Slice D.2 added
per-command user-texture bindless metadata/shader sampling and registered the
opt-in `ImGuiSurfaceGpuSmoke` `gpu;vulkan` fixture, which skips on hosts without
an operational GLFW/Vulkan lane. Maturity: `Operational` on Vulkan-capable
hosts, `CPUContracted` on this host. Final implementation commit `69f9b16c`;
full slice chain `8f1374c6`, `61192d50`, `84d16985`, `97d34aba`, `9e283c72`,
`69f9b16c`. Downstream editor panels remain owned by `UI-001`; final sandbox
acceptance remains owned by `RUNTIME-095`.

Previously-active
[`RUNTIME-090`](../archive/RUNTIME-090-imgui-platform-renderer-adapter.md) —
runtime-side Dear ImGui platform/renderer adapter
(`Extrinsic.Runtime.ImGuiAdapter`, Theme A working-sandbox path stage 4, the
producer half of the ImGui/UI leaves that gate `UI-001`) retired to
`tasks/done/` on 2026-06-02 at maturity `CPUContracted`. Slice A
(`claude/intrinsicengine-agent-onboarding-qu8wV`, PR #962, commits `3bd20f2` +
`4676a1d`) landed the standalone adapter module (ImGui 1.92 context lifecycle
with `ImGuiBackendFlags_RendererHasTextures`, `Platform::Event`→ImGui-IO pump,
`ImDrawData`→`ImGuiOverlayFrame` walk, editor hook, diagnostics) with
`FakeWindow`-driven `contract;runtime` coverage at `Scaffolded→CPUContracted`;
`imgui_lib` is linked **PRIVATE** to `ExtrinsicRuntime` and `imgui.h` stays out
of the `.cppm` interface. Slice B
(`claude/intrinsicengine-agent-onboarding-01gFi`, PR #963, commit `fdc3165`)
closed `CPUContracted` by wiring the adapter into `Engine`: `Engine` owns the
`Graphics::ImGuiOverlaySystem` instance (the allowed `runtime -> graphics` edge)
and constructs the adapter in `Initialize()` after the `Window`/`Renderer`;
`RunFrame` calls `BeginFrame(frameDt)` after `PollEvents` + the minimize/resize
early returns and before `OnVariableTick`, and `EndFrame()` after the variable
tick and before the render contract's `PrepareFrame()`, so exactly one
`ImGuiOverlayFrame` is produced per engine frame; the editor hook is exposed via
`Engine::SetImGuiEditorCallback` with a read-only `GetImGuiAdapter()` observer.
New `Test.ImGuiAdapterEngineWiring.cpp` `contract;runtime` coverage drives a
bounded `Engine::Run()` (static wiring cases run displayless; the live per-frame
loop + editor-hook cases are window-gated and verified under `xvfb-run`).
GRAPHICS-079 now consumes the adapter-produced payload through the renderer-side
retained font atlas, transient upload helper, `FrameRecipe.PresentSource`
topology, per-command bindless user-texture sampling, and the registered
`ImGuiSurfaceGpuSmoke` opt-in fixture. Final working-sandbox acceptance is
`RUNTIME-095`.

Previously-active
[`RUNTIME-093`](../archive/RUNTIME-093-primitive-selection-refinement.md) — runtime
primitive selection refinement for meshes, graphs, and point clouds (Theme A
working-sandbox path) retired to `tasks/done/` on 2026-06-01 at maturity
`CPUContracted`. Slice A (PR #959) delivered the standalone
`Extrinsic.Runtime.PrimitiveSelectionRefinement` module (result type +
fail-closed taxonomy + hint-based mesh/graph/point-cloud refinement against
authoritative `GeometrySources`, entity-transform local/world hit reporting,
`contract;runtime` fixtures) at `Scaffolded`. Slice B1 (commit `0cacfdf`, PR
#960) added the optional CPU ray fallback for missing (`None`-domain) hints
(`CpuFallbackResolved`/`CpuFallbackMiss`). Slice B2
(`claude/intrinsicengine-agent-onboarding-X3GCq`, commit `752b47f`) closed
`Scaffolded → CPUContracted` by wiring refinement into the runtime frame loop:
the new pure `RefinePickReadbackResult(scene, readback)` bridge resolves a pick
readback's render id to a live entity (decode + recycling-safe `registry.valid()`
check → deterministic `StaleEntity`), reads `Transform::WorldMatrix` as
`LocalToWorld`, builds the authoritative `GeometrySources::ConstSourceView`, and
delegates to `RefinePrimitiveSelection`; `Engine::RunFrame` caches the result in
`m_LastRefinedPrimitive` (`GetLastRefinedPrimitiveSelection()`) as the existing
readback-drain loop runs (newest pick wins, background clears, empty-drain
retains), alongside the unchanged `SelectionController` whole-entity mutation.
The editor-facing-cache arm was chosen over controller ownership to keep the
controller graphics-free (controller-owned variant recorded as a nonblocking
follow-up). No graphics mutation; ECS tag model unchanged. Ten new
`Test.PrimitiveSelectionRefinementWiring.cpp` `contract;runtime` cases pass;
`contract;runtime` gate 277/277, `unit;geometry` 1254/1254;
layering/test-layout/doc-links/task-policy checks clean; module inventory
regenerated (no diff). `Operational` interactive selection proof stays owned by
`RUNTIME-089`, `GRAPHICS-074`, and final sandbox acceptance (`RUNTIME-095`).

Previously-active
[`RUNTIME-092`](../archive/RUNTIME-092-stable-entity-lookup.md) — runtime stable
entity lookup sidecar (Theme A working-sandbox path) retired to `tasks/done/` on
2026-05-31 at maturity `CPUContracted`. Slice A landed the standalone
`Extrinsic.Runtime.StableEntityLookup` module (runtime-owned
`StableId -> entt::entity` winner-map realising the `HARDEN-068` Decision-3
deferred lookup, reversible render-id resolution, deterministic
smallest-render-id duplicate policy, lazy + bulk stale invalidation,
diagnostics; `Scaffolded`). Slice B
(`claude/intrinsicengine-agent-onboarding-8y1qR`) closed
`Scaffolded → CPUContracted` by wiring the sidecar into the runtime frame path:
`Engine` now owns a `StableEntityLookup`, attaches it to the
`SelectionController` in `Initialize()` (`SetStableEntityLookup`), and
`Rebuild`s it once per frame in `RunFrame` immediately before the pick-readback
drain. The controller's render-id resolution seam (`ConsumeHit`,
`SetSelectedByStableEntityId`) routes through the attached lookup's
`ResolveByRenderId` (decode + live-registry validation), so a pick naming a
recycled/destroyed slot is rejected by the single runtime authority instead of
mis-resolving to the recycled occupant; with no lookup attached the controller
falls back to the bare decode so standalone callers are unaffected. Slice B
decision: reference-scene entities remain transient (no generated `StableId`).
Five new `Test.SelectionStableLookupComposition.cpp` `contract;runtime` cases
plus the 13 Slice A cases pass; runtime gate 243/243, ECS gate 146/146;
layering/test-layout/doc-links/task-policy checks clean; module inventory
regenerated (no diff). `Operational` user-visible selection durability stays
owned by `RUNTIME-089`, UI tasks, and final sandbox acceptance (`RUNTIME-095`).

Previously-active
[`RUNTIME-089`](../archive/RUNTIME-089-selection-controller.md) — runtime selection
controller and snapshot handoff (Theme A working-sandbox path) retired to
`tasks/done/` on 2026-05-31 at maturity `CPUContracted`. Slice A landed the
standalone `Extrinsic.Runtime.SelectionController` module (input-facing
hover/click/programmatic APIs, per-frame pick coalescing, sequence-tracked
in-flight readback consumption, Replace/Add/Toggle `SelectedTag`/`HoveredTag`
mutation, stale/non-selectable rejection, the `uint32 ↔ entt::entity` lookup
seam, controller-owned selection-snapshot buffers, and the diagnostics block)
with pure-CPU `contract;runtime` tests in `Test.SelectionController.cpp`
(`Scaffolded`). Slice B (`claude/intrinsicengine-agent-onboarding-VBuRD`) closed
`Scaffolded → CPUContracted` by wiring the controller into the real runtime
frame path: `Engine` now owns a `SelectionController` (`GetSelectionController()`),
drains the coalesced pick into `RenderFrameInput::Pick`/`HasPendingPick` and
`SelectionSystem::RequestPick` before `ExtractRenderWorld`, consumes
`SelectionSystem::GetLastPickResult()` (oldest in-flight pick) in the maintenance
phase and clears it, and mirrors the controller snapshot into
`RenderWorld.Selection` through a new `const SelectionController*` argument to
`RenderExtractionCache::ExtractAndSubmit` → `RuntimeRenderSnapshotBatch::Selection*`
→ renderer stable storage → `ExtractRenderWorld` (graphics reporting-only, no
live ECS read). Five new `Test.SelectionSnapshotExtraction.cpp` `contract;runtime`
cases (selected/hovered/additive mirror, null-controller empty, cleared-empty)
plus the 23 Slice A cases pass; full contract gate 253/253 (221 runtime + 32
graphics); layering/test-layout/doc-links/task-policy/module-inventory (no diff)
checks clean. `Operational` outline/pick proof stays owned by `GRAPHICS-074` plus
the final working-sandbox acceptance task (`RUNTIME-095`); the real input→pick
binding is owned by a later editor/UI task.

Previously-active
[`RUNTIME-088`](../archive/RUNTIME-088-mesh-primitive-view-lifecycle.md) — mesh
primitive view lifecycle (Theme A working-sandbox path) retired to `tasks/done/`
on 2026-05-31 at maturity `CPUContracted`. Slice A landed the standalone
`Extrinsic.Runtime.MeshPrimitiveViewPacker` (edge line-list + vertex point
derivation packers, `MeshPrimitiveViewSettings` control surface, fail-closed
status taxonomy, pure-CPU `contract;runtime` packer tests) at `Scaffolded`.
Slice B (commit `69b3fb4`, `claude/intrinsicengine-agent-onboarding-RQtst`)
closed `Scaffolded → CPUContracted` by wiring the `RenderExtractionCache`
residency: a cache-owned `MeshPrimitiveViewSettings` map (runtime/editor state,
never in ECS components), per-view `GpuWorld` instance + `GpuGeometryHandle`
sidecars rendering edges/vertices as extra `GpuRender_Line`/`GpuRender_Point`
unlit lanes over the one authoritative mesh `GeometrySources`, repack on the
shared mesh dirty signal, release on disable/eligibility-flip/destruction/
shutdown through the `TickMeshPrimitiveViewGeometry` deferred-retire window
(wired in `Engine::RunFrame`), and fifteen `Mesh{Edge,Vertex}View*` +
`MeshPrimitiveViewFreeRetires` counters. Resolved deferred decisions: views are
runtime sidecars (not child ECS entities), and the settings live in a
cache-owned map. `IntrinsicRuntimeContractTests` 193/193 (12 new
`MeshPrimitiveViewExtraction.*` cases); layering/test-layout/doc-links/
task-policy/module-inventory checks clean. `Operational` visual proof of the
three lanes stays owned by `RUNTIME-095`.

Previously-active
[`RUNTIME-087`](../archive/RUNTIME-087-geometrysources-pointcloud-residency.md) —
`GeometrySources` point-cloud residency bridge (Theme A working-sandbox path)
retired to `tasks/done/` on 2026-05-30 at maturity `CPUContracted`. Landed as
one robust slice mirroring `RUNTIME-086`: standalone
`Extrinsic.Runtime.PointCloudGeometryPacker` plus `RenderExtractionCache`
residency wiring, deferred-retire window, and shutdown drain landed together
because the upload path is not leak-free without the retire/shutdown lifecycle.
`RenderExtractionCache` now routes `Domain::PointCloud` entities carrying
`RenderPoints` through `BindPointCloudGeometry` (upload/reuse/dirty-reupload),
owns the per-entity `PointCloudGeometry` handle, drains the cloud dirty-domain
tags (`DirtyVertexPositions`/`DirtyVertexAttributes`/`GpuDirty`), releases on
eligibility flip / destruction / shutdown through `EnqueuePointCloudRetire` +
the `TickPointCloudGeometry` deferred-retire window (maintenance-phase wired in
`Engine::RunFrame`), and reports eight `PointCloudGeometry*` counters. Only a
uniform float `RenderPoints::SizeSource` is supported (per-point size buffers
fail closed); a point-cloud-domain entity without `RenderPoints` is not a
renderable, so a mesh that loses topology to a bare vertex set is not re-bound
as points. New `contract;runtime` cases in `Test.PointCloudGeometryExtraction.cpp`
and `Test.PointCloudGeometryPacker.cpp` cover upload/reuse, `PopulateFromCloud`,
two-entity independence, deferred-retire on destruction, shutdown, procedural
preemption, fail-closed counters (missing positions, non-finite, unsupported
size source), eligibility flips, and parameterized dirty-tag reupload. Default
CPU gate: focused `-R 'PointCloudGeometry|GraphGeometry|MeshGeometry'` 96/96;
full gate 2394/2396 (only the two pre-existing unrelated
`IntrinsicBenchmarkSmoke.HalfedgeSmoke` Not-Run failures); layering/test-layout/
doc-links/task-policy/module-inventory checks clean. `Operational` visual proof
is owned by the final working-sandbox acceptance task (`RUNTIME-095`).

Previously-active
[`RUNTIME-086`](../archive/RUNTIME-086-geometrysources-graph-residency.md) —
`GeometrySources` graph residency bridge (Theme A working-sandbox path) retired
to `tasks/done/` on 2026-05-30 at maturity `CPUContracted`. Slice A (standalone
`Extrinsic.Runtime.GraphGeometryPacker`) landed earlier; Slices B and C landed
together on `claude/intrinsicengine-agent-onboarding-c9ql3` because the
extraction upload path is not leak-free without the retire/shutdown lifecycle,
so the smallest robust slice is the full residency mirror of `RUNTIME-085`.
`RenderExtractionCache` now routes `Domain::Graph` entities carrying
`RenderEdges`/`RenderPoints` through `BindGraphGeometry` (upload/reuse/
dirty-reupload), owns the per-entity `GraphGeometry` handle, drains the graph
dirty-domain tags, releases on eligibility flip / destruction / shutdown through
`EnqueueGraphRetire` + the `TickGraphGeometry` deferred-retire window
(maintenance-phase wired in `Engine::RunFrame`), and reports eight
`GraphGeometry*` counters. Fourteen new `contract;runtime` cases in
`Test.GraphGeometryExtraction.cpp` cover line/point/both-lane uploads, reuse,
two-entity independence, deferred-retire on destruction, shutdown, procedural
preemption, fail-closed counters, eligibility flips, and parameterized dirty-tag
reupload. Default CPU gate: focused `-R 'GraphGeometry|MeshGeometry'` 73/73;
full gate 2371/2373 (only the two pre-existing unrelated
`IntrinsicBenchmarkSmoke.HalfedgeSmoke` Not-Run failures); layering/test-layout/
doc-links/task-policy/module-inventory checks clean. `Operational` visual proof
is owned by the final working-sandbox acceptance task.

Previously-active
[`GEOM-012`](../archive/GEOM-012-symmetric-domain-views-property-sharing.md) —
symmetric mesh/graph/point-cloud domain views retired to `tasks/done/` on
2026-05-29 after Slice E (conversion/move/consume policy) landed on
`claude/intrinsicengine-agent-onboarding-YjhiR`. Maturity `CPUContracted`. Slice E
reviewed the conversion coverage and added no new APIs: the container copy
constructor is the same-domain borrow→owning hard-copy seam (copy-assigning into
a borrowed destination instead writes through to the source, so promotion uses
copy construction or assignment into an owning destination),
`Geometry.Mesh.Conversion`/`Geometry.PointCloud.Conversion` own the cross-domain
hard copies, move-assign is the ownership-transfer seam, and the Slice D
`Const*View` types are already non-copyable/non-movable. Six new tests pin the
policy (three `SubmeshViewDomainBorrows.HardCopyOf*BorrowOwnsIndependentStorage`
cases, `SubmeshViewDomainBorrows.CopyAssignIntoBorrowedDestinationWritesThroughToSource`,
plus `MeshConversion.ConvertedHalfedgeMeshOutlivesSourceViaMoveOwnershipTransfer`
and `PointCloudConversion.ConvertedCloudOutlivesAndDecouplesFromSource`); the
focused geometry suite passed 181/181 with the layering, test-layout, doc-links,
and module-inventory (no diff) checks clean.

[`BUG-013`](../archive/BUG-013-backbuffer-readback-contract-vtable-segv.md) —
backbuffer readback contract SEGV retired to `tasks/done/` on 2026-05-29 as
**not reproducible on a clean `ci` preset build**. On a freshly-cloned tree the
two `ConfiguredHandleRecordsReadbackTripletOnce` cases pass through the default
CPU gate (CTest #25/#87, label `contract`; 225/225 in
`IntrinsicGraphicsContractCpuTests`). The reported SEGV was a stale incremental
module-BMI artifact after `cc06edef`; the lasting prevention is the
clean-rebuild rule documented in `src/graphics/rhi/README.md`. Unblocks
`GRAPHICS-076E` CPU contract closure; no engine/test source was changed.

[`RUNTIME-085`](../archive/RUNTIME-085-geometrysources-mesh-residency.md) —
`GeometrySources` mesh residency bridge retired to `tasks/done/` on
2026-05-28 after the Slice D closure check. Slices A–C landed on
`claude/optimistic-hypatia-yJ5qw` / `claude/intrinsicengine-agent-onboarding-FLLuF`
/ `claude/gallant-knuth-Y4iFV`; Slice D closure ran on
`claude/serene-albattani-3KDrI`. Maturity is `CPUContracted`: the full
`IntrinsicTests` build under the `ci` preset and the default CPU gate
(`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) report 2322/2324 passed,
with only the two pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run`/
`.Validate` (Not Run) failures unchanged and unrelated to this task; all 44
`MeshGeometryExtraction`/`MeshGeometryPackerTest` cases pass. `Operational`
visual proof is deferred to `RUNTIME-095` (final working-sandbox acceptance).

[`GRAPHICS-077`](../archive/GRAPHICS-077-transient-debug-primitive-upload-helper.md) —
transient-debug upload helper retired to `tasks/done/` on 2026-05-28 after
Slice D added `TransientDebugSurfaceGpuSmoke`; maturity is `CPUContracted` on
CPU-only hosts and command-stream `Operational` on Vulkan-capable hosts. Pixel
readback parity is retired by
[`GRAPHICS-077E`](../archive/GRAPHICS-077E-transient-debug-pixel-readback.md).

[`GRAPHICS-078`](../archive/GRAPHICS-078-visualization-overlay-upload-helper.md) —
visualization-overlay upload helper retired to `tasks/done/` on 2026-05-28 after
Slice D added `VisualizationOverlaySurfaceGpuSmoke`; maturity is
`CPUContracted` on CPU-only hosts and command-stream `Operational` on
Vulkan-capable hosts. Pixel readback parity is retired by
[`GRAPHICS-078E`](../archive/GRAPHICS-078E-visualization-overlay-pixel-readback.md).

[`GEOM-015`](../archive/GEOM-015-gjk-termination-diagnostics.md) — GJK
termination diagnostics and scale-aware tolerance policy retired to
`tasks/done/` on 2026-05-22 after all four slices landed (PRs #915,
#917, #919). The next slice was picked per the priority rules
in [`docs/agent/prompt/prompt.md`](../../docs/agent/prompt/prompt.md):
no reproducible bugs are open, so the earliest unblocked Theme A leaf
in [`tasks/backlog/README.md`](../backlog/README.md) won —
`GRAPHICS-076`, gated only by the retired `GRAPHICS-075`.

[`RUNTIME-082`](../archive/RUNTIME-082-spatial-debug-adapters.md) —
`Extrinsic.Runtime.SpatialDebugAdapters` umbrella retired to
`tasks/done/` on 2026-05-27 after Slice D landed on
`claude/intrinsicengine-agent-onboarding-xnNIW`
(`ECS::Components::SpatialDebugBinding` + cache-owned adapters via
`std::unique_ptr` + `RuntimeRenderSnapshotBatch::SpatialDebug*` spans
+ per-frame stats; five new integration tests pass under the default
CPU/null gate; 2245/2247 overall, the two pre-existing
`IntrinsicBenchmarkSmoke.HalfedgeSmoke` failures unchanged).

[`GEOM-008`](../archive/GEOM-008-linear-algebra-solver-infrastructure.md) —
Geometry linear algebra and solver infrastructure retired to
`tasks/done/` on 2026-05-27 after Slice A landed in commit `c1aeafb`
(merged into the working tree via `cfe2f0c`). Slice A introduced the
Eigen3 dependency, the narrow `Geometry.Linalg` Eigen-backed dense/
adapter module, the reusable `Geometry.Sparse` CSR/builder/diagnostics/CG
module, and bridged `Geometry.DEC` CSR/CG to the new sparse layer.
Closes maturity at `CPUContracted`; no GPU/SuiteSparse/CHOLMOD backend
is owed by this task (recorded as later optional follow-ups in
`docs/architecture/geometry.md`). Verified on 2026-05-27 against the
default CPU gate (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`)
together with the layering, test-layout, docs-links, task-policy, and
module-inventory regeneration checks.

[`BUG-035`](../archive/BUG-035-vulkan-slot-recycling-smoke.md) — Vulkan
slot-recycling smoke retired to `tasks/done/` on 2026-06-12 at `Operational`.
The opt-in `gpu;vulkan` smoke advances the real promoted Vulkan frame loop past
the retirement window and observes destroyed buffer/texture slots being reused
with bumped generations through public handles.

[`BUG-034`](../archive/BUG-034-vulkan-resource-pool-reclamation.md) — Vulkan
ResourcePool reclamation retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`, with `BUG-035` providing the Vulkan operational proof.
`VulkanDevice` now processes buffer/image/sampler/pipeline pool deletions from
the frame loop, including fail-closed `EndFrame()` exits, while keeping
deferred Vulkan-object destruction in the existing deletion queue. The Null
device slot-recycling contract pins the backend-neutral behavior in the default
CPU gate.

[`BUG-033`](../archive/BUG-033-mesh-io-untrusted-header-counts.md) — mesh IO
untrusted header-count hardening retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`. OFF/PLY import now validates declared counts against payload
before allocation, uses overflow-safe byte checks, rejects invalid PLY list
count types/counts, and fails closed on degenerate OFF face rows. Malformed
input regressions pass without aborting.

[`BUG-032`](../archive/BUG-032-triangle-edge-point-vulkan-rendering.md) —
triangle edge/point Vulkan rendering retired to `tasks/done/` on 2026-06-12 at
`Operational`. The fix aligned `GpuGeometryRecord` ABI stride between C++ and
GLSL, removed double-applied vertex offsets from GpuScene shaders, propagated
runtime mesh sidecar point/edge config, and proved visible reference-triangle
edge/point lanes through Vulkan smoke/readback coverage.

[`BUG-031`](../archive/BUG-031-benchmark-smoke-not-in-intrinsictests-aggregate.md)
— benchmark smoke aggregate wiring retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`. The current tree registers `IntrinsicBenchmarkSmoke` through
the shared aggregate target property; building only `IntrinsicTests` produces
the smoke runner and the benchmark CTest pair passes.

[`BUG-030`](../archive/BUG-030-headless-engine-run-tests-red-gate.md) — headless
`Engine::Run()` red-gate retired to `tasks/done/` on 2026-06-12 at
`CPUContracted`. Live-window engine-loop tests now guard born-closed windows
with the house `ShouldClose() -> GTEST_SKIP()` pattern, and `tests/README.md`
records the rule. The broader headless execution restoration is retired by
`RUNTIME-107`.

[`BUG-029`](../archive/BUG-029-ray-aabb-slab-nan-poisoning.md) — ray/AABB slab
NaN poisoning retired to `tasks/done/` on 2026-06-12 at `CPUContracted`.
Analytic ray/AABB overlap and raycast now use NaN-free slab intervals for
axis-parallel/on-boundary rays, sphere raycasts use a finite center-origin
fallback normal, and BVH boundary-coincident ray traversal is pinned by tests.

## Satisfied cross-domain dependency anchors (history)

These anchors from `tasks/backlog/README.md` are fully satisfied (every
endpoint retired); they are preserved here verbatim for traceability.

- **GRAPHICS-034 ⇐ ASSETIO-001 ⇐ GEOIO-002.** Asset-backed mesh residency
  planning depends on promoted asset routing, which depends on geometry decoder
  parity. `GEOIO-002`, `ASSETIO-001`, and `GRAPHICS-034` are retired; the
  implementation children remain unopened.
- **RUNTIME-085..088 ⇐ HARDEN-065, GRAPHICS-030B, GRAPHICS-070/071.** Runtime
  mesh/graph/point-cloud residency depends on promoted `GeometrySources`, the
  proven runtime-to-`GpuWorld` upload/bind pattern, and retained surface/line/
  point pass contracts.
- **RUNTIME-089 ⇐ GRAPHICS-074; RUNTIME-093 ⇐ RUNTIME-089, RUNTIME-085..088.**
  Runtime selection policy consumes graphics readback, while primitive
  refinement requires both selected entities and authoritative geometry
  residency/source data.
- **UI-001 ⇐ RUNTIME-090, GRAPHICS-079, RUNTIME-089.** UI panels require ImGui
  frame production/presentation and runtime-owned selection state; panels must
  remain command/event producers, not owners of engine state. `UI-001` is
  retired at `CPUContracted`; final operational proof remains under
  `RUNTIME-095`.
- **RUNTIME-095 ⇐ GRAPHICS-072..079, GRAPHICS-081, ASSETIO-001 (texture/model
  ingest; `RUNTIME-080` retired into it), RUNTIME-085..089,
  RUNTIME-092..093, UI-001.**
  Satisfied 2026-06-04: the final working-sandbox acceptance composes the
  renderer, runtime residency, selection/refinement, asset/UI command surfaces,
  and UI paths for the scoped mesh/graph/point-cloud scene.
- **GRAPHICS-029..034 ⇐ HARDEN-060..062.** Sandbox renderable extraction needs
  promoted ECS scene/hierarchy/transform parity. `HARDEN-060`, `HARDEN-061`,
  and `HARDEN-062` are all retired to `tasks/done/`, so this gate is
  satisfied; the Theme A renderer leaves are unblocked on the ECS side.
- **RUNTIME-091 ⇐ HARDEN-061.** Runtime fixed-step ECS system activation depends
  on the promoted `TransformHierarchy` system and must keep composition in
  `runtime` rather than adding upward imports to `src/ecs`.
- **HARDEN-067 ⇐ RUNTIME-091 or equivalent scheduling decision.** Bounds
  propagation can be implemented independently, but default-runtime usefulness
  depends on a known ECS system activation path.
- **METHOD-001 ⇐ ARCH-001.** Satisfied 2026-06-05: the physics layer
  ownership decision is accepted and the deterministic `cpu_reference`
  rigid-body method package is retired at `CPUContracted`. Runtime/ECS
  integration remains out of scope for the method package and is owned by
  physics/runtime follow-ups.
- **HARDEN-064 ⇐ ARCH-001.** Satisfied 2026-06-05: ECS collider/rigid-body
  authoring shipped under ADR-0019 without storing solver handles in ECS.
- **PHYSICS-001 ⇐ HARDEN-064, METHOD-001.** Satisfied 2026-06-05:
  `PHYSICS-001` is retired at `CPUContracted` with the first
  `src/physics` world/body descriptor surface and runtime fixed-step bridge.
- **PHYSICS-002 ⇐ PHYSICS-001.** Satisfied 2026-06-06:
  collision broadphase/narrowphase contracts are retired at `CPUContracted`
  on top of the physics world/body descriptor surface.
- **GRAPHICS-093 ⇐ GRAPHICS-092 Slice B blocker.** Satisfied 2026-06-18:
  retained forward lines now have a backend-portable non-indexed `LineQuads`
  topology (`DrawIndirectCount()` / `TriangleList`) while edge-id selection keeps
  the indexed `Lines` bucket. Dynamic line-width residency and Vulkan operational
  proof are retired by `GRAPHICS-092`.
- **GRAPHICS-033B ⇐ GRAPHICS-033A (done).** Diagnostics counters and the
  `VulkanRequestedButNotOperational` startup breadcrumb depend on the
  status / reason enums and the reconciliation matrix wiring.
- **GRAPHICS-033C ⇐ GRAPHICS-033A (done), GRAPHICS-032 (done), GRAPHICS-031
  (done), GRAPHICS-018R (done).** Vulkan recording for the bootstrap
  visible recipe needed the gate seam plus the recipe, default material, and
  operational-transition reset seam already in `tasks/done/`; those artifacts
  are now retired by GRAPHICS-081.
- **GRAPHICS-033D ⇐ GRAPHICS-033A (done), GRAPHICS-033B, GRAPHICS-033C.**
  The opt-in `gpu;vulkan` visible-triangle smoke composed all three
  prior children and runs only on hosts with Vulkan + GLFW; its bootstrap
  fixture is retired and the default-recipe fixture is canonical.

[`HARDEN-083`](../archive/HARDEN-083-geometry-source-availability-contract.md) —
geometry source availability and provenance contract retired to `tasks/done/`
on 2026-06-19 at `CPUContracted`. `GeometrySources` now reports exact active
domain, source provenance, and available vertex/node, edge, halfedge, and face
CPU sources separately; topology markers can explain provenance without
pretending missing property sets exist.

[`RUNTIME-117`](../archive/RUNTIME-117-geometry-availability-render-lane-resolver.md) —
geometry availability and render-lane resolver retired to `tasks/done/` on
2026-06-19 at `CPUContracted`. Runtime now owns the standard resolver over ECS
source availability plus `RenderSurface`, `RenderEdges`, and `RenderPoints`,
including property-domain support, element counts, and lane diagnostics.

[`RUNTIME-118`](../archive/RUNTIME-118-geometry-availability-consumer-migration.md) —
geometry availability consumer migration retired to `tasks/done/` on
2026-06-19 at `CPUContracted`. Runtime packers, extraction, progressive
property resolution, selected bake validation, and primitive-selection
refinement now consume the availability/provenance model instead of using exact
`ActiveDomain` as the common capability gate.

[`RUNTIME-119`](../archive/RUNTIME-119-gpu-renderable-availability-snapshot.md) — GPU
renderable availability snapshot retired to `tasks/done/` on 2026-06-19 at
`CPUContracted`. `RenderExtractionCache` exposes a read-only
`GpuRenderableAvailabilityView` keyed by stable entity id, with independent
surface, edge, and point lane residency plus canonical named-buffer facts while
ECS remains free of GPU handles and renderer sidecars.

[`UI-021`](../archive/UI-021-sandbox-editor-geometry-availability-migration.md) —
sandbox editor geometry availability migration retired to `tasks/done/` on
2026-06-19 at `CPUContracted`. `Runtime.SandboxEditorUi` now consumes
`Extrinsic.Runtime.GeometryAvailability` for domain windows, visualization
targets, property catalogs, primitive-view commands, render hints, K-Means
affordances, and mesh UV/bake diagnostics while preserving source provenance
labels.

[`GEOM-026`](../archive/GEOM-026-cross-domain-vertex-normal-recompute.md) — cross-domain
vertex normal recomputation contracts retired to `tasks/done/` on 2026-06-21
at `CPUContracted`. Geometry now exposes domain-owned CPU normal recompute
modules for halfedge meshes, graphs, and point clouds:
`Geometry.HalfedgeMesh.Vertices.Normals`, `Geometry.Graph.Vertex.Normals`, and
`Geometry.PointCloud.Normals`. The old `Geometry.NormalEstimation` module is
removed in favor of KDTree/default and supplied-index point-cloud recompute
overloads that write canonical `v:normal` property data with diagnostics.

[`RUNTIME-120`](../archive/RUNTIME-120-vertex-attribute-binding-resolver.md) — reusable
vertex attribute binding resolver retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Runtime now has the GPU-agnostic
`Extrinsic.Runtime.VertexAttributeBinding` resolver for count-matched property
to vertex-channel binding, and the mesh packer routes normal and texcoord reads
through it without changing existing packed output. Follow-up vertex-channel
work remains tracked by `RUNTIME-121` through `RUNTIME-125`.

[`GRAPHICS-099`](../archive/GRAPHICS-099-rendering-contract-foundation.md) — rendering
contract foundation retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.RenderingContract`, a CPU-only public contract vocabulary
for renderer descriptors, scoped snapshot envelopes, binding intents, shared
recipe slots, view/output recipes, render artifact metadata, deterministic
diagnostics, and fail-closed validation helpers. Current renderer execution,
Vulkan, shaders, runtime integration, UI, and loadable-file behavior remain
unchanged; follow-up implementation stays split across `GRAPHICS-100`,
`GRAPHICS-101`, `GRAPHICS-102`, `RUNTIME-127`, `UI-023`, and `GRAPHICS-103`.

[`GRAPHICS-100`](../archive/GRAPHICS-100-current-renderer-contract-adapter.md) — minimal
current-renderer contract adapter retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.CurrentRendererContractAdapter`, a data-only adapter that
populates the current promoted renderer descriptor, immutable frame/snapshot
envelopes from `RenderFrameInput` or `RenderWorld`, binding intents for current
material/normal/color/texture/visualization lanes, a default frame-recipe
descriptor, view/output metadata, and deterministic compatibility diagnostics.
Renderer execution, Vulkan, shaders, runtime extraction, UI, and loadable
recipe behavior remain unchanged; operational proof stays owned by
`GRAPHICS-103`.

[`GRAPHICS-101`](../archive/GRAPHICS-101-loadable-render-recipe-configs.md) — loadable
rendering recipe config schema and validation retired to `tasks/done/` on
2026-06-24 at `CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.RenderRecipeConfig`, a CPU-only versioned JSON loader and
dry-run preview API that overlays optional recipe config onto caller-provided
renderer contract values. It produces `RenderRecipeDescriptor`,
`ViewOutputRecipeDescriptor`, and `BindingSet` copies without mutating active
renderer state, rejects undeclared slots, unsupported capabilities, unsafe
binding domains, invalid defaults, required-binding overrides, and fixed-core
replacement attempts, and reports distinct invalid/unsupported/stale/degraded/
fallback-applied states. Runtime activation, UI editing, shared recipe
execution, Vulkan, shaders, and backend behavior remain deferred to
`RUNTIME-127`, `UI-023`, `GRAPHICS-102`, and `GRAPHICS-103`.

[`RUNTIME-127`](../archive/RUNTIME-127-render-artifact-publication.md) — render artifact
publication and apply semantics retired to `tasks/done/` on 2026-06-24 at
`CPUContracted`. Runtime now exposes
`Extrinsic.Runtime.RenderArtifactPublication`, a CPU-only registry and command
surface for renderer-produced artifacts keyed by renderer, snapshot,
view/output recipe, source revisions, and output purpose. It records lifecycle
kinds, UI-facing unpublished/stale/canceled/failed/superseded/published/applied
states, diagnostics, provenance, undo metadata, and audit entries. Publish and
apply are explicit and provenance-gated; apply is limited to candidate project
results and authorizes caller-owned mutation without letting the registry,
graphics, or renderers mutate project data implicitly. UI editing remains owned
by `UI-023`, and image-producing Vulkan/render-graph proof remains owned by
`GRAPHICS-103`.

[`GRAPHICS-102`](../archive/GRAPHICS-102-shared-visibility-lighting-recipe-execution.md) —
shared visibility and lighting recipe execution retired to `tasks/done/` on
2026-06-24 at `CPUContracted`. Graphics now exposes
`Extrinsic.Graphics.SharedRenderRecipeExecution`, a CPU-only shared recipe
executor over immutable `RenderWorld` data and scoped `SnapshotEnvelope`
metadata. It produces renderer-neutral visibility/grouping products
(visible items, rejected diagnostics, grouping keys, batch and instance groups,
LOD selections, spatial partitions, and optional acceleration-structure build
requests) plus lighting/environment products (resolved lights, emissive
geometry identities, environment/probe/volume/tag/quality outputs, intents,
debug modes, and fallbacks). Renderer compatibility checks report missing
capabilities or missing produced products deterministically. Backend command
buffers, Vulkan resources, project mutation, UI activation, and operational
render-graph proof remain owned by `GRAPHICS-103` and `UI-023`.

[`UI-023`](../archive/UI-023-render-recipe-ui-editing.md) — sandbox render recipe editing
UI retired to `tasks/done/` on 2026-06-24 at `CPUContracted`.
`Extrinsic.Runtime.SandboxEditorUi` now exposes data-only render recipe editor
models, draft/validation/preview/activation command DTOs, and artifact
publish/apply command routing through runtime-owned state. The attached ImGui
panel lists current renderer descriptors, declared recipe slots, binding
overrides, view/output recipe data, draft diagnostics, preview/activation
state, and render artifact lifecycle rows without UI owning renderer state or
mutating graphics/backend resources directly.

[`GRAPHICS-103`](../archive/GRAPHICS-103-vulkan-rendergraph-contract-integration.md) —
Vulkan render-graph contract integration retired to `tasks/done/` on
2026-06-24 at `Operational`. The current renderer now evaluates the
contract-first descriptor, scoped snapshot, binding intent, shared recipe,
view/output, and declared artifact metadata path during frame execution,
fail-closes before render-graph execution on incompatibility, records
unsupported-product, missing-output, degraded-fallback, and artifact diagnostics,
and finalizes declared artifact availability from render-graph execution and
readback outcomes. Opt-in Vulkan smoke coverage proves a declared output
artifact can produce non-empty readback-backed evidence through the contract
path while runtime publication remains runtime-owned.

[`RUNTIME-121`](../archive/RUNTIME-121-vertex-color-channel-upload.md) — per-vertex mesh
color channel upload retired to `tasks/done/` on 2026-06-24 at `Operational`.
Runtime mesh packing now resolves count-matched `v:color` through the reusable
vertex-attribute resolver into packed unorm8 color data, graphics uploads that
optional stream beside the current mesh vertex bytes, `GpuGeometryRecord`
publishes `ColorBufferBDA`, and the active default-recipe GpuScene shader path
fetches/interpolates the stream for surface/GBuffer shading. CPU coverage proves
present/absent packer behavior, GpuWorld BDA publication, and
`DirtyVertexAttributes` structural color-stream reupload; the opt-in
`gpu;vulkan` runtime sandbox smoke proves the active deferred path shades a
mesh from `v:color`. Dormant `surface.vert`/`PtrVertexAttr` was deliberately not
used; RUNTIME-122 owns the later GPU SoA migration.

[`RUNTIME-122`](../archive/RUNTIME-122-gpu-soa-vertex-channel-storage-and-shader-fetch.md)
— GPU SoA vertex channel storage and shader fetch retired to `tasks/done/` on
2026-06-24 at `Operational`. Runtime mesh, graph, point-cloud, and mesh
primitive-view packers now emit explicit per-channel vertex streams; graphics
stores position, texcoord, normal, and color data as contiguous managed SoA
channel ranges, publishes per-channel BDAs through `GpuGeometryRecord`, and
keeps stable element offsets for draw/culling metadata. The active default
GpuScene surface, depth, selection, line, and point vertex shaders now fetch
from channel BDAs instead of interleaved vertex structs. Focused CPU
packer/GpuWorld/shader-contract coverage, the full CPU-supported CTest gate,
structural validators, and opt-in `gpu;vulkan` runtime sandbox surface plus
line/point smokes passed.

[`RUNTIME-123`](../archive/RUNTIME-123-editor-bind-property-as-channel.md) — editor
"bind any property as normals / colors" retired to `tasks/done/` on
2026-06-24 at `CPUContracted`. Runtime now has a
`VertexChannelBindingSet` ECS descriptor consumed by mesh, graph, and
point-cloud packers. The Sandbox Editor property catalog exposes normal/color
binding targets, validates candidate properties through the
`VertexAttributeBinding` resolver, persists per-entity bindings, and stamps
`DirtyVertexAttributes` without direct renderer/RHI upload calls. Focused
SandboxEditorUi, mesh/graph/point-cloud packer, and mesh extraction coverage,
the full CPU-supported CTest gate, structural validators, and regenerated
module inventory passed.

[`RUNTIME-124`](../archive/RUNTIME-124-per-channel-partial-uploads.md) — per-channel dirty
tracking and partial GPU uploads retired to `tasks/done/` on 2026-06-24 at
`Operational`. ECS now has fine-grained vertex-channel dirty tags for
positions, texcoords, normals, and colors, while the legacy broad
`DirtyVertexAttributes` path still maps to all non-position attribute streams.
Runtime extraction plans resident mesh, graph, and point-cloud updates as
channel masks and calls `GpuWorld::UpdateGeometryChannels` instead of releasing
and re-uploading geometry when topology, vertex count, and storage layout are
unchanged. Graphics writes only the changed contiguous SoA channel sub-ranges,
coalesces upload barriers for the managed vertex buffer, and reports
full-upload fallbacks for count/storage mismatches. Focused CPU extraction,
GpuWorld, dirty-tag, editor-command, and render-extraction tests passed, and an
opt-in `gpu;vulkan` runtime sandbox smoke proves a vertex-color mutation shades
through the active deferred GpuScene path without a full geometry rebind.

[`RUNTIME-126`](../archive/RUNTIME-126-gpu-readback-jobs-and-property-writeback.md) — GPU
readback jobs and result→property write-back retired to `tasks/done/` on
2026-06-25 at `Operational`. `StreamingExecutor` now has a
`WaitingForReadback` park/resume state, `DerivedJobRegistry` exposes readback
job diagnostics and `DrainReadbacks()` resume semantics, and
`Extrinsic.Runtime.GpuReadbackJob` schedules transfer-facade readbacks that
write dimension-checked byte payloads into typed geometry properties on the
main-thread apply phase. Dependent follow-up jobs remain blocked until the
readback job has resumed and applied, preserving existing `SubmitFollowUp` /
`DependsOn` ordering. Focused CPU readback/derived-job/binding tests, explicit
readback streaming integration tests, the full CPU-supported CTest gate, and an
opt-in `gpu;vulkan` readback round-trip smoke passed.

[`GEOM-027`](../archive/GEOM-027-property-name-lifetime-contract.md),
[`GEOM-028`](../archive/GEOM-028-property-registry-handle-safety.md),
[`GEOM-029`](../archive/GEOM-029-const-property-set-validity-contract.md),
[`GEOM-030`](../archive/GEOM-030-property-set-const-lookup-migration.md),
[`GEOM-031`](../archive/GEOM-031-property-set-naming-normalization.md),
[`GEOM-032`](../archive/GEOM-032-bool-property-access-contract.md),
[`GEOM-033`](../archive/GEOM-033-erased-property-metadata-catalog.md), and
[`GEOM-051`](../archive/GEOM-051-property-system-enhancements.md) — geometry property
system migration blockers retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Geometry.Properties` now uses a single borrowed
`std::string_view` name contract, fail-closed handle validity, const-correct
property lookup, canonical `ShrinkToFit`, non-bool contiguous span/data access,
bool proxy-safe indexed access, erased property descriptors, and reusable live
element ranges across mesh, graph, point-cloud, and const domain views. Focused
property tests cover names, copied handles, invalid handles, const lookup,
descriptor metadata, bool access, and live ranges.

[`GEOM-043`](../archive/GEOM-043-remeshing-reprojection-error-bounded-sizing.md) —
remeshing reprojection and error-bounded sizing retired to `tasks/done/` on
2026-06-28 at `CPUContracted`. Adaptive remeshing now exposes a reference
projector backed by closest-face queries, error-bounded Taubin-style sizing,
and uniform-remeshing projection options for split/move operations. Focused CPU
tests prove projection to a frozen reference surface and the error-bounded
sizing path on representative meshes.

[`GEOM-044`](../archive/GEOM-044-subdivision-sqrt3-loop-feature-masks.md) — subdivision
utility migration retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
Loop subdivision now preserves optional feature-edge masks through crease
stencils and tag propagation, and the new
`Geometry.HalfedgeMesh.SubdivisionSqrt3` module adds triangle-centered Sqrt(3)
subdivision. Focused CPU tests cover the single-triangle Sqrt(3) split and Loop
feature-mask propagation.

[`GEOM-046`](../archive/GEOM-046-mesh-topology-utilities.md) — mesh topology utilities
retired to `tasks/done/` on 2026-06-28 at `CPUContracted`. `HalfedgeMesh` now
has polygon-face triangulation, removal safety, Delaunay predicates/flips,
explicit edge-length cache publication, connected-component labeling and split
helpers, largest-component retention, dual/triangle-adjacency construction, and
deterministic nearest-face queries. Focused CPU tests cover triangulation,
component labels/splits, adjacency, nearest-face ordering, and the canonical
`e:length` cache.

[`GEOM-047`](../archive/GEOM-047-graph-pointcloud-query-noise-utilities.md) — graph and
point-cloud query/noise utilities retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. Graph utilities now publish and validate canonical `e:length`
edge caches, run closest/K/radius edge queries through deterministic BVH-backed
candidate search, support one-ring constrained closest-edge lookup, and apply
seeded Gaussian graph noise scaled by bounding-box diagonal. Point-cloud
utilities now apply seeded Gaussian noise scaled by average spacing and
fail-close on degenerate nonzero-noise requests. Focused CPU tests cover cache
publication, query ordering against brute force, one-ring search, deterministic
noise, identity cases, and degenerate diagnostics.

[`CORE-003`](../archive/CORE-003-engine-config-file-lane.md) — engine config file lane
retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Extrinsic.Core.Config.EngineLoad` now provides a versioned
`intrinsic.core.engine-config` JSON schema, side-effect-free preview,
file-load, serialization, typed diagnostics, and fallback-applied usability
state while keeping the value-type `EngineConfig` module free of IO imports.
Runtime exposes `ResolveEngineConfigForBoot(...)`, which starts from
`CreateReferenceEngineConfig()` and then applies CLI, environment, or existing
default-path config files before sandbox `Engine` construction. Focused core
tests cover every boot field, file round-trip, invalid-key/value fallback, and
diagnostics; runtime contract tests cover CLI selection and missing explicit
path fallback; the sandbox target builds with the new entry-point wiring.

[`GEOM-035`](../archive/GEOM-035-mesh-surface-point-sampling.md) — triangle-mesh surface
point sampling retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.PointCloud.SurfaceSampling` now samples triangle meshes into
point-clouds with area-weighted face selection, sqrt-corrected barycentric
samples, deterministic seeds, explicit diagnostics, and `p:normal` publication
from interpolated source `v:normal` or geometric fallback normals. Focused
geometry tests cover area proportions, determinism, normal handling, and
invalid-input diagnostics; the benchmark smoke runner includes a CPU-only
surface-sampling workload and manifest with no performance claim.

[`GEOM-036`](../archive/GEOM-036-sampling-quality-metrics.md) — blue-noise and sampling
quality metrics retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.PointCloud.QualityMetrics` now exposes deterministic CPU functions
for nearest-neighbor distances, NN histograms, coefficient of variation,
minimum pair distance, Poisson-disk ratio, coverage, RDF with rectangular-domain
edge correction, 2D periodograms, and radially averaged power spectra. Focused
geometry tests cover grid, jittered-grid, white-noise, regular-lattice, cloud
adapter, and fail-closed edge cases; the benchmark smoke runner includes a
CPU-only quality-metrics workload and manifest with no performance claim.

[`RUNTIME-133`](../archive/RUNTIME-133-method-figure-data-export.md) — method figure
data-export seam retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Extrinsic.Runtime.MethodFigureExport` now serializes copied metric series,
scalar summaries, run manifests, and point sets to deterministic CSV, JSON,
and ASCII PLY with stable column/property ordering and 17-digit scientific
float formatting. Writers validate inputs before opening output, commit via a
same-directory temporary file, and fail closed with explicit diagnostics.
Focused runtime unit tests cover metric round-trip parsing, byte-identical
manifest ordering, point-set CSV/PLY output, invalid arrays, duplicate keys,
and unwritable target paths.

[`RUNTIME-132`](../archive/RUNTIME-132-lift-runframe-hook-adapters.md) — RunFrame hook
adapter readability lift retired to `tasks/done/` on 2026-06-28. The six
single-use `Core.FrameLoop` hook adapters, fixed-step simulation loop, camera /
gizmo / selection input helpers, BUG-026 pick-context construction, and
completed pick-readback refinement now live as private `Runtime.Engine.cpp`
helpers. `Engine::RunFrame` remains the runtime composition point and preserves
the documented phase order, but now reads as the platform, simulation, UI,
render-input, render-contract, maintenance, pick-readback, and frame-retire
sequence instead of carrying adapter bodies inline. Text-based runtime layering
contracts were updated to follow the extracted helper body without requiring
the old inline fixed-step implementation.

[`GRAPHICS-106`](../archive/GRAPHICS-106-frame-recipe-override-seam.md) — fail-closed
IRenderer frame-recipe override seam retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `IRenderer` now exposes an active frame-recipe override lane
with side-effect-free projection diagnostics, optional-slot disable semantics,
and live null-renderer application immediately before `BuildDefaultFrameRecipe`.
Invalid overrides leave derived defaults untouched and publish diagnostics in
`RenderGraphFrameStats`; valid overrides can disable mapped optional slots such
as postprocess, debug view, picking, and lighting without widening
`RenderRecipeDescriptor` vocabulary or mutating the fixed core. Focused
graphics contract tests cover projection, live pass omission, and fail-closed
unknown-slot behavior; the default CPU-supported gate is green.

[`RUNTIME-130`](../archive/RUNTIME-130-route-recipe-activation-and-load-default-recipe.md)
— runtime render-recipe activation and startup default loading retired to
`tasks/done/` on 2026-06-28 at `Operational`. `Engine` now owns the active
render-recipe config state, builds recipe-config validation context from the
current renderer contract, applies validated previews through a single runtime
path, and translates accepted configs into the `GRAPHICS-106`
`FrameRecipeOverride` seam. `RenderConfig` carries a boot-time
`DefaultRecipeConfigPath` with empty-string opt-out, startup loads missing or
invalid config files fail closed to the derived default recipe while preserving
typed diagnostics, and the sandbox editor activation command now calls the
runtime apply path instead of keeping activation editor-local. Focused runtime
contract tests cover startup pass omission, missing and invalid fallback
diagnostics, and editor activation reaching the live frame; the default
CPU-supported gate is green.

[`RUNTIME-131`](../archive/RUNTIME-131-agent-cli-config-control-facade.md) — agent/CLI
config-control facade on `Engine` retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Engine` now exposes typed, ImGui-independent methods for
render-recipe preview, file preview, document activation, and validated preview
apply, plus engine-config preview/load and a deliberately narrow hot-apply
subset for `render.default_recipe_config_path`. Non-empty hot paths validate the
referenced `RenderRecipeConfig` before mutating live config or renderer state;
invalid hot recipe files reject without clearing an active override, and all
other engine-config differences are reported as boot-only rejections. The
Sandbox Editor recipe commands now call the same facade callbacks for preview
and activation while keeping widget/draft-buffer state local. Focused runtime
contract tests cover agent/CLI control without UI frames, boot-only rejection,
invalid-hot-file preservation, and editor/agent preview parity; the default
CPU-supported gate is green. This satisfies the `RUNTIME-131` dependency for
`RUNTIME-134`.

[`UI-022`](../archive/UI-022-sandbox-editor-vertex-normal-recompute.md) — Sandbox
EditorUI vertex-normal recompute windows retired to `tasks/done/` on
2026-06-28 at `CPUContracted`. Mesh, graph, and point-cloud
`Processing > Vertices > Normals` windows now route through runtime-owned
command/result DTOs that call `Geometry.HalfedgeMesh.Vertices.Normals`,
`Geometry.Graph.Vertex.Normals`, and `Geometry.PointCloud.Normals` rather than
UI-owned algorithms. Successful commands publish count-matched canonical
`v:normal`, stamp `DirtyVertexNormals`, and mark editor history dirty; invalid
targets, invalid settings, and typed `v:normal` conflicts fail closed without
touching unrelated properties or renderer/RHI state. Focused geometry,
runtime-editor, and dirty-tag extraction tests passed, and K-Means processing
capability coverage remains green.

[`UI-024`](../archive/UI-024-editor-mesh-denoise-window.md) — Sandbox EditorUI mesh
denoising window retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Mesh > Processing > Denoise` now exposes a runtime-owned command/result
surface that builds a scratch halfedge mesh from selected mesh
`GeometrySources`, calls the `GEOM-042` `Geometry.Smoothing::DenoiseBilateral`
kernel, and publishes finite count-matched `v:position` values only after the
geometry result succeeds. Successful commits are undoable through
`EditorCommandHistory`, stamp `DirtyVertexPositions` and
`DirtyVertexAttributes`, and leave renderer/RHI uploads to deferred extraction
without stamping broad `GpuDirty`. Contract tests cover menu/capability
advertising, successful denoise publication, undo/redo exact restoration,
dirty-tag behavior, wrong-domain and invalid-parameter fail-closed paths, and
the deterministic unavailable-kernel diagnostic lane.

[`UI-025`](../archive/UI-025-editor-remesh-subdivide-windows.md) — Sandbox EditorUI
remeshing and subdivision windows retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Mesh > Processing > Remesh` now exposes uniform/adaptive
remeshing controls for target edge length, iterations, project-to-surface, and
mean-curvature versus error-bounded Taubin sizing, with runtime-owned
feature-gated command/result DTOs that call `Geometry.Remeshing` and
`Geometry.HalfedgeMesh.AdaptiveRemeshing`. `Mesh > Processing > Subdivide`
exposes Loop, Catmull-Clark, and Sqrt(3) operators plus Loop feature-edge
preservation, calling the GEOM-044 geometry modules through the same runtime
command surface. Successful commands replace the selected mesh
`GeometrySources` through undoable `EditorCommandHistory` snapshots, stamp
`DirtyVertexPositions`, `DirtyVertexAttributes`, `DirtyEdgeTopology`, and
`DirtyFaceTopology`, and leave renderer/RHI uploads to deferred extraction
without stamping broad `GpuDirty`. Runtime contract tests cover menu/model
advertising, uniform and adaptive remesh, all three subdivision operators,
undo/redo, dirty tags, wrong-domain and invalid-parameter fail-closed paths,
and unavailable-kernel diagnostics.

[`GEOM-039`](../archive/GEOM-039-accelerated-mesh-closest-face-query.md) — accelerated mesh
closest-face query and consumer adoption retired to `tasks/done/` on 2026-06-28
at `CPUContracted`. `Geometry.MeshClosestFaceIndex` now provides a packaged
CPU exact nearest/k-nearest/radius face query over a `Geometry.BVH` of per-face
AABBs, returning face, closest point, normal, fan-triangle primitive index,
exact squared distance, status, and `Geometry.SpatialQueries` diagnostics.
Polygon faces are fan-triangulated; empty/no-usable-triangle meshes,
non-finite vertices, non-finite probes, and invalid parameters fail closed
without NaNs or asserts. Adaptive remeshing reference projection, implicit
plane-field closest-point evaluation, simplification Hausdorff redistribution,
and `Geometry.HalfedgeMesh.Utils::NearestFace` now share the packaged query
instead of private brute-force face scans. Focused geometry tests cover
brute-force parity, pruning diagnostics, k-nearest and radius results, subset
indices, boundary/on-surface queries, degenerate fail-closed behavior, and the
three adopted consumers.

[`RUNTIME-135`](../archive/RUNTIME-135-spatialdebug-closest-face-picking.md) —
SpatialDebug closest-face picking via accelerated mesh query retired to
`tasks/done/` on 2026-06-28 at `CPUContracted`. Runtime now exports
`Extrinsic.Runtime.SpatialDebugClosestFace`, a data-only closest-face overlay
consumer that accepts a caller-resolved active mesh descriptor, caches the
GEOM-039 `Geometry.MeshClosestFaceIndex` by stable mesh key/revision, and emits
the highlighted face, probe point, closest point, normal, exact distance,
primitive index, mesh identity, query status, and diagnostics. No closest-face
kernel is implemented in runtime; no renderer/RHI/Vulkan or editor widget path
is touched. Runtime contract coverage proves parity with the direct geometry
query, valid overlay output, rebuild on revision change, no-active/missing-mesh
fail-closed behavior, and empty/degenerate/non-finite-probe diagnostics.

[`CORE-004`](../archive/CORE-004-indexed-decrease-key-heap.md) — indexed decrease-key
min-heap container and Dijkstra adoption retired to `tasks/done/` on
2026-06-28 at `CPUContracted`. Core now exports
`Extrinsic.Core.IndexedHeap`, a generic deterministic indexed binary min-heap
with fail-closed `Top`/`TryTop`/`Pop`/`TryPop`, duplicate-safe `Push`,
O(log n) `DecreaseKey`, O(log n) `Remove`, and value-token tie-breaking.
`Geometry.Graph.ShortestPath` now uses the core heap as its true decrease-key
Dijkstra frontier instead of `std::priority_queue` lazy re-insertion, while
preserving distance, predecessor, traversal-count, goal-stop, and
budget-exhaustion diagnostics against the prior priority-queue reference.
Focused core heap randomized operation parity and geometry shortest-path
priority-queue parity tests cover the migration.

[`DOCS-003`](../archive/DOCS-003-reconcile-algorithm-variant-dispatch-doc.md) —
algorithm variant dispatch documentation reconciliation retired to
`tasks/done/` on 2026-06-28. `docs/architecture/algorithm-variant-dispatch.md`
now identifies itself as a target Strategy x Backend template pending
`GEOM-052`, describes the CPU-only owning-layer function plus the
`Extrinsic::RHI::IDevice&` GPU-capable integration overload, maps
`Backend::CPU` / `Backend::GPU` to the method backend-policy tokens, and
requires honest requested-vs-actual fallback telemetry instead of a silent
phantom GPU path. The architecture index now records the doc as target guidance
pending `GEOM-052` before canonical promotion, satisfying the documentation
gate for `GEOM-052` and `PROC-011`.

[`GEOM-052`](../archive/GEOM-052-shared-cpu-gpu-backend-seam-kmeans-exemplar.md) —
shared CPU/GPU backend seam and KMeans exemplar retired to `tasks/done/` on
2026-06-28 at `CPUContracted`. `Geometry.KMeans::Backend` now exposes
`CPU`/`GPU` rather than the old phantom GPU token, `KMeansParams::Compute`
remains the backend request field, and `KMeansResult` reports
`RequestedBackend`, `ActualBackend`, and `FellBackToCPU` so CPU fallback is
observable. The CPU geometry entry point remains RHI-free and reports CPU as
the actual backend even when GPU was requested. Runtime now exports
`Extrinsic.Runtime.KMeansBackend`, whose `ClusterKMeans(...)` overloads accept
`Extrinsic::RHI::IDevice&`, evaluate `IDevice::IsOperational()` for GPU
requests, and fall back to the geometry CPU reference because no KMeans GPU
kernel is implemented in this slice. Focused geometry and runtime contract tests
cover CPU telemetry, non-operational GPU requests, operational-but-unimplemented
GPU requests, and unchanged editor KMeans publication behavior.

[`GEOM-037`](../archive/GEOM-037-so3-rotation-primitives.md) — SO(3) rotation primitives
retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.Rotation` now owns hat/vee, exp/log, geodesic and chordal distances,
deterministic seeded random rotations, `ProjectOnSO3`, and optimal-rotation
helpers for both `glm::vec3` and `glm::dvec3` correspondences. `ProjectOnSO3`
delegates orthogonal projection to
`Geometry.Linalg::ComputePolarDecomposition(...).Orthogonal` and determinant
corrects into SO(3). `Geometry.Registration` imports the module for
point-to-point ICP alignment, removing the private Kabsch/Umeyama
eigensolver copy. Unit coverage exercises round trips, distances, deterministic
random rotations, SO(3) projection, optimal-rotation recovery, reflection
correction, non-finite input, and under-determined fail-closed behavior; existing
registration tests pass unchanged.

[`GEOM-038`](../archive/GEOM-038-rotation-averaging-means-medians.md) — SO(3) rotation
averaging retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.RotationAveraging` now exposes result/status-returning chordal,
Karcher, and quaternion L2 means plus geodesic and quaternion Weiszfeld L1
medians. The chordal mean builds the Markley 4x4 quaternion moment matrix and
solves it through `Geometry.Linalg::ComputeSymmetricEigen`, with deterministic
sample canonicalization/sorting and a polar projection fallback for solver
failure. Shared options carry optional weights, convergence controls, and an
outlier-rejection threshold; shared results report validity, convergence,
iterations, residual radians, and explicit fail-closed status. Unit coverage
proves repeated-rotation identity, clustered chordal/Karcher agreement, weighted
Karcher/slerp parity, median robustness against gross outliers, deterministic
permutation behavior, and explicit empty/single/antipodal/weight/non-finite
status handling.

[`GEOM-045`](../archive/GEOM-045-first-class-mesh-quantity-accessors.md) — first-class mesh
geometric-quantity accessors retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Geometry.HalfedgeMesh.Utils` now exposes property-backed
`FaceArea`, `FaceAreaVector`, `FaceCentroid`,
`ComputeBarycentricVertexAreas`, `FaceScalarGradient`, and
`VertexOneRingPCA` contracts with canonical `f:` / `v:` property names.
The heat-method geodesic implementation consumes the public unnormalized
gradient and keeps the local normalize/negate step, `Geometry.UvAtlas` routes
its degenerate triangle check through the canonical triangle-area helper,
`Geometry.HalfedgeMesh.Builder` exports `ProjectToUnitSphere`, and
`Geometry.HalfedgeMesh.Vertices.Normals` adds `AreaAngleWeighted`. Unit
coverage pins analytic area, closed-mesh area-vector conservation, property
publication, linear-field face gradients, origin-safe unit-sphere projection,
area-times-angle normals, 1-ring PCA normal alignment, and fail-closed invalid
inputs; focused geodesic tests pass through the promoted gradient path.

[`GEOM-048`](../archive/GEOM-048-statistics-robust-estimation-kernels.md) — statistics
accumulators and robust estimation kernels retired to `tasks/done/` on
2026-06-28 at `CPUContracted`. `Geometry.Statistics` now owns mergeable scalar
streaming moments, a two-heap running median, generic finite-sample
`Median`/`Quantile` helpers, and fail-closed `SafeAcos`/`SafeAsin` domain
clamps. `Geometry.Robust` now owns L2, L1, Huber, Tukey, Welsch, Lorentzian,
and Cauchy M-estimator kernels with `Rho`/`Psi`/`Weight` entry points.
`Geometry.Registration` exposes default-off robust ICP weighting through
`RegistrationParams::RobustKernelKind` and `RobustScale`; when selected, the
existing percentile trim remains in place and the surviving correspondences
feed weighted point-to-point Kabsch or point-to-plane normal equations. Unit
coverage proves streaming-vs-batch moments, merge associativity, order
statistics over double and non-double scalar vectors, robust-kernel analytic
forms/fail-closed inputs, invalid robust ICP params, and Tukey improvement over
percentile trimming alone on a gross-outlier registration case.

[`GEOM-049`](../archive/GEOM-049-numeric-linalg-utilities.md) — numeric / linear-algebra
utilities retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.Linalg` now exports strided Eigen map aliases plus `MapAsMatrix` and
`MapVectorAsMatrix` adapters for aliasing scalar buffers and fixed-size GLM
vector arrays as `N x dim` matrices. `Geometry.Properties` now owns the
one-way adapter into `Geometry.Linalg`: arithmetic property columns map as
aliasing `N x 1` views, GLM vector columns map as aliasing `N x dim` views, and
`bool` property columns return copied numeric columns instead of reinterpreting
`std::vector<bool>`. `RobustPCA` implements deterministic Principal Component
Pursuit / ADMM on top of `ComputeSVD`, reports recovered rank, iteration count,
residuals, and `NumericDiagnostics`, and fails closed on empty, zero, non-finite,
invalid-option, or hard SVD-failure inputs. Unit coverage proves strided-map and
property-map aliasing, the bool fallback, synthetic low-rank-plus-sparse
recovery, bitwise determinism, and degenerate-input diagnostics.

[`GEOM-050`](../archive/GEOM-050-primitive-curve-utilities.md) — primitive and curve
utilities retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
`Geometry.Curve` keeps its existing span-based Bezier evaluators and now also
exports `BezierCurve`, `GetDegree`, `EvaluateBernstein`, and
`EvaluateDeCasteljau`. `Geometry.Triangle` now exposes opposite-edge lengths,
perimeter, per-vertex angles, stable-Heron area, and `SafeAcos`, with
non-finite or degenerate triangles returning finite zero metrics. `Geometry.Sphere`
adds `FittingMethod::IterativeGeometric` plus convergence controls on the
existing `ToSphere` path; the branch rejects fewer than four, non-finite, or
coincident samples and keeps the best point-to-surface residual reached from
the algebraic seed/refinement. `Geometry.AABB` now supports `MakeCubic`,
`OctantCenter`, and `ChildOctant` with invalid-box sentinels. Unit coverage
exercises Bezier endpoint/linear/Bernstein-vs-de Casteljau behavior, triangle
analytic metrics and degenerate guards, iterative sphere residual comparison
against least squares, and AABB cubification/octant tiling.

[`GEOM-020`](../archive/GEOM-020-sparse-direct-factorization-seam.md) — sparse direct
factorization solver seam retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Geometry.Sparse` now exposes `SparseLDLT` and `SparseLLT`
solver objects over the existing CSR matrix type, with `factor`, span-based
single-RHS solves, `Eigen::Ref` multi-RHS solves, and solve-in-place overloads.
Factorization returns `SparseFactorizationDiagnostics` carrying status, pivot
count, smallest absolute pivot, and a reserved condition-estimate field. LDLT
classifies negative pivots as `NonSPD` and near-zero pivots as `ZeroPivot`; LLT
uses Eigen status plus an LDLT probe for failure classification. DEC/geodesic
callers keep the existing CG path, while future method packages now have the
factor-once / solve-many SPD reference seam they were gated on. Unit coverage
proves SPD Poisson solves, a mass-plus-Laplacian method-shaped solve,
indefinite/singular diagnostics, multi-RHS solve parity, solve-in-place storage
reuse, invalid-input rejection, and bit-stable repeated solves from one
factorization.

[`GEOM-023`](../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md) — sparse
non-symmetric iterative solver seam retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Geometry.Sparse` now exposes `SparseBiCGSTAB` over the
existing CSR matrix type, with span-based single-RHS solves and `Eigen::Ref`
multi-RHS solves. Solver params report max iterations, relative tolerance, and
preconditioner choice (`None`, `Diagonal`, or `IncompleteLUT`), while
diagnostics report status, iterations, final relative residual, and the
preconditioner used. Invalid CSR/matrix shape, non-finite RHS, invalid
tolerances, unsupported preconditioner enum values, non-convergence, and
numerical failures return structured statuses and do not mutate caller output on
failed solves. The module pins Eigen's single-threaded path with
`EIGEN_DONT_PARALLELIZE`, and unit coverage proves a genuinely non-symmetric
advection-diffusion solve, SPD parity with CG, singular-system diagnostics,
input rejection, multi-RHS parity, preconditioner agreement, and bit-stable
repeated solves. METHOD-003 can now promote against
`Geometry.Sparse::SparseBiCGSTAB` for its non-symmetric closest-point-extension
operator.

[`METHODS-001`](../archive/METHODS-001-signed-heat-pathfinder.md) — signed-heat
pathfinder planning task retired to `tasks/done/` on 2026-06-28 at `Retired`.
The task pins METHOD-002 as the first concrete method to drive the full
methods pipeline from paper intake through CPU reference, correctness tests,
benchmark harness, and docs. It records retired GEOM-020 as the LDLT seam that
satisfies METHOD-002's direct-solver gate, preserves Variant A (surface signed
heat) as the public-facing default, and points future method work at the
resulting `methods/geometry/signed_heat/` package as the canonical package
pattern.

[`METHOD-002`](../archive/METHOD-002-signed-heat-method-reference-backend.md) — Signed Heat
Method reference backend retired to `tasks/done/` on 2026-06-28 at
`CPUContracted`. `Geometry.SignedHeatMethod` now exposes a CPU reference surface
backend that computes per-vertex signed distance from an oriented halfedge curve
on a triangle mesh. The implementation reuses `Geometry.DEC` vertex mass/cotan
operators and `Geometry.Sparse::SparseLDLT` for the heat and regularized
Poisson solves, writes `v:signed_heat_distance` and
`v:is_signed_heat_source`, and reports explicit invalid-input,
degenerate-boundary, factorization/solve, and non-finite-result diagnostics.
The method package `methods/geometry/signed_heat/` records the paper intake,
backend status, and known limitation that this is a vertex-based approximation
of the paper's edge-based Crouzeix-Raviart connection discretization. Unit
coverage proves flat-grid signed-distance sign/quality, orientation sign flip,
open-boundary finite diagnostics, invalid-input failure, and bitwise
determinism. The smoke benchmark emits schema-valid runtime and
`quality_error_l2` metrics without making a performance claim.

[`DOCS-005`](../archive/DOCS-005-feature-module-playbook-minimal-floor.md) —
feature-module playbook minimal floor and config command artifact retired to
`tasks/done/` on 2026-06-29 at `Retired`. The playbook now opens with a
minimal-feature floor for one-caller, synchronous, data-driven research probes;
softens the full vertical-slice contract so it is required once a second
caller, backend split, scheduled work, persisted config, command routing, UI
control, or telemetry-backed diagnostics appears; and adds a serializable
config/command entry to the discoverability artifacts for UI-backed features.

[`PROC-011`](../archive/PROC-011-route-contract-to-architecture-index-and-author-checks.md)
— contract routing and backend/config authoring checks retired to `tasks/done/`
on 2026-06-29 at `Retired`. `AGENTS.md` now routes subsystem architecture,
backend-split, config/command, and recipe/frame-composition design questions to
the canonical `docs/architecture/index.md` instead of linking directly to
legacy-background docs. The architecture review checklist now asks for a
declared backend axis and round-trippable config/command reachability, while
the task template and task-format guide carry optional `## Control surfaces`
and `## Backends` prompts.

[`GEOM-016`](../archive/GEOM-016-point-cloud-filtering-density-contracts.md) —
point-cloud filtering and density diagnostics contracts retired to
`tasks/done/` on 2026-06-29 at `CPUContracted`. `Geometry.PointCloud.Utils`
gained explicit `RemoveStatisticalOutliers` and `RemoveRadiusOutliers`
operators returning a shared `OutlierRemovalResult` (owned filtered cloud,
ascending kept/rejected index partitions, an `OutlierRemovalStatus` fail-closed
taxonomy, and statistical mean/std-dev/threshold diagnostics), hardening the
pre-existing voxel/random downsampling, bilateral, outlier-score, KDE, and
radius-estimation surfaces rather than replacing the module. Unit coverage
(`Test.PointCloudOutlierRemoval.cpp`) proves known two-cluster + isolated-outlier
rejection, deterministic ascending partitions, non-finite rejection, and
invalid/insufficient/overflow input handling; the
`geometry_pointcloud_filtering_smoke` benchmark emits schema-valid metrics
without a performance claim. CPU-only contract with no backend seam; the editor
wire-up of these operators is owned by `UI-027`.

[`UI-027`](../archive/UI-027-editor-pointcloud-outlier-removal-window.md) — Sandbox
EditorUI point-cloud outlier-removal window retired to `tasks/done/` on
2026-06-29 at `CPUContracted`. The `PointCloud > Processing > Remove Outliers`
window and its undoable editor command drive the `GEOM-016`
`RemoveStatisticalOutliers` / `RemoveRadiusOutliers` operators on the selected
point-cloud entity. New `SandboxEditorGeometryProcessingAlgorithm` members
(`StatisticalOutlierRemoval` / `RadiusOutlierRemoval`) join every exhaustive
switch; a new command/result pair plus
`ApplySandboxEditorPointCloudOutlierRemovalCommand` run the geometry operators
and — because removal changes the point count — rebuild the entity's point
`GeometrySources` via `GeometrySources::PopulateFromCloud` (preserving surviving
attributes) rather than rewriting a count-matched property. Publication is
undoable through `EditorCommandHistory::Execute` (undo republishes the original
cloud, redo the kept cloud) and stamps coarse `GpuDirty` plus position/attribute/
normal tags for a full deferred point-cloud repack. Headless Null-backend
contract tests cover statistical publication + undo/redo (published count equals
`KeptCount`), radius publication, and fail-closed lanes. Continues the
`bcg_code_base` geometry-processing port into interactive Sandbox workflows;
geometry owns the algorithm and app imports runtime only. Verification caveat:
the C++/CTest gate could not run in the authoring sandbox (vcpkg bootstrap needs
a github clone the egress proxy denies, so the `ci` preset cannot configure
there); the runnable structural checks pass and CI owns the compile + contract
tests.

[`GRAPHICS-108`](../archive/GRAPHICS-108-vulkan-compute-parallel-primitives.md) —
reusable Vulkan compute parallel primitives retired to `tasks/done/` on
2026-06-30 at `Operational`. `Extrinsic.Graphics.ComputeParallelPrimitives`
now owns deterministic CPU references, backend-neutral dispatch/scratch plans,
RHI command recording, and promoted Vulkan smoke parity for `uint32`
exclusive/inclusive prefix scan and stable stream compaction by nonzero flags.
The GPU path uses four BDA-based compute shaders:
`parallel_prefix_scan.comp`, `parallel_scan_add_offsets.comp`,
`parallel_compact_by_flags.comp`, and `parallel_count_to_dispatch_args.comp`.
Compaction publishes `OutputCount` to caller-owned readback buffers and/or
`ParallelDispatchIndirectArgs` buffers, enabling downstream GPU consumers
without a CPU count round trip. Default-gate contract tests cover CPU parity,
planning, command recording, fail-closed statuses, count publication, and
descriptor builders; the opt-in `gpu;vulkan` smoke validates Vulkan scan,
compaction, count readback, dispatch-args publication, and repeated-input
determinism. The seam remains graphics/RHI-only with no Vulkan handle leakage,
ECS/runtime/app/platform imports, CUDA path, or method-specific kernels.
`METHOD-013` is now unblocked for the progressive Poisson Vulkan backend.

[`GEOM-017`](../archive/GEOM-017-point-cloud-descriptors-registration-seams.md) —
point-cloud descriptor, correspondence, and coarse-registration seams retired to
`tasks/done/` on 2026-06-30 at `CPUContracted`. The generic geometry layer now
owns ISS keypoint selection, FPFH descriptor storage, deterministic
mutual-best/Lowe-ratio matching, and RANSAC coarse alignment with explicit
status, inlier, RMSE, and iteration diagnostics. Existing ICP remains reachable
and unchanged; paper-specific robust/global registration remains deferred to
future `methods/geometry` packages that depend on this seam. The closure session
fixed explicit C++23 module imports for `Geometry.Properties`, corrected the ICP
translation oracle, and isolated CTest benchmark smoke output from CI benchmark
result validation so the full local CI pipeline runs through cleanly.

[`GEOM-056`](../archive/GEOM-056-kmeans-gpu-vulkan-compute-backend.md) — KMeans Vulkan
compute backend retired to `tasks/done/` on 2026-07-02 at `ParityProven`. The
runtime layer now owns the explicit `Extrinsic.Runtime.KMeansGpuBackend`
execution surface: persistent `(n,k)` resource caching, one-time SoA position
and seed-centroid upload, reset/assign/update Lloyd-loop recording, portable
assignment plus per-cluster centroid scans, and post-submit async
labels/distances/centroids drain through `AsyncBufferReadback` without
`vkDeviceWaitIdle`. The current promoted shader path avoids optional Vulkan
float-atomic and int64-atomic feature requirements; GRAPHICS-111 owns the fast
segmented-reduction replacement. The synchronous `Extrinsic.Runtime.KMeansBackend` overload
continues to fall back honestly when it lacks command/cache/readback ownership.
Default-gate KMeans/readback contract coverage passed, opt-in `ci-vulkan`
KMeans parity and benchmark/JSON validation passed, and the broader
`gpu`+`vulkan` CTest selection passed on the Vulkan-capable host; the full CPU
gate still reports the unrelated pre-existing
`SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms` registration
failure.

[`RUNTIME-136`](../archive/RUNTIME-136-sandbox-method-backend-selectors.md) — Sandbox
method backend selectors retired to `tasks/done/` on 2026-07-02 at
`CPUContracted`. The Sandbox UI now exposes CPU reference vs Vulkan compute
selectors for every currently exposed method with a GPU variant: K-Means and
Progressive Poisson. K-Means command/result DTOs report requested backend,
actual backend, stable ids/display names, and fallback reason; the synchronous
Sandbox K-Means path preserves deterministic CPU behavior and reports an honest
CPU fallback when it lacks an operational device or async GPU command/readback
ownership. Progressive Poisson's existing config/backend telemetry is surfaced
as a visible selector in both point-cloud and mesh processing controls. Focused
runtime contract coverage passed for default CPU selection and Vulkan-requested
CPU fallback telemetry; the full CPU gate still reports the unrelated
pre-existing `SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms`
registration failure.

[`BUG-052`](../archive/BUG-052-sandbox-selection-visualization-regressions.md) — Sandbox
selection and visualization regressions retired to `tasks/done/` on
2026-07-02 at `CPUContracted`. Outline-only selected/hovered frames now record
only the entity-ID pass and skip face/edge/point primitive-picking subpasses
plus readback work unless an actual pick request is pending. SciVis
visualization color-source overrides no longer set the legacy `Unlit` flag, so
uniform color, scalar colormaps/isolines, and KMeans/per-element label buffers
continue to shade from assigned normals. Runtime auto property-buffer
extraction now resolves mesh, graph, and point-cloud domains: graph vertex
properties map to `Nodes`, graph edge properties to `Edges`, point-cloud
vertex properties to `Vertices`, and unsupported domains fail closed with
diagnostics. Focused graphics/runtime CPU contract coverage passed.
The full CPU-supported gate still reports the unrelated pre-existing
`SandboxEditorUi.RegistrationCommandAlignsAcrossEntityTransforms` registration
failure.

[`BUG-053`](../archive/BUG-053-sandbox-kmeans-gpu-backend-queue.md) — Sandbox K-Means GPU
backend queue retired to `tasks/done/` on 2026-07-02 at `CPUContracted`.
Sandbox Vulkan K-Means requests now submit to the runtime-owned
`Extrinsic.Runtime.KMeansGpuJobQueue` instead of calling the synchronous
`Extrinsic.Runtime.KMeansBackend` fallback seam. The queue owns the Vulkan
compute pipeline handles, persistent KMeans GPU resources, transfer helper, and
async readbacks; `Engine` installs it as the renderer runtime frame-command hook
so command work records into the normal frame context without an extra swapchain
present, then drains completions during maintenance. Accepted Sandbox requests report
`Pending` until a later-frame completion publishes the same KMeans label/color
properties as the CPU path. Device-unavailable cases still fall back honestly to
the CPU reference, and the synchronous backend overload remains nonblocking.
Focused runtime contract coverage passed for CPU fallback telemetry and queued
Sandbox GPU submission routing.

[`GRAPHICS-111`](../archive/GRAPHICS-111-float-segmented-reduction-primitive.md) — Float
segmented/per-key reduction primitive retired to `tasks/done/` on 2026-07-04 at
`Operational` on Vulkan-capable hosts and `CPUContracted` everywhere else. The
graphics/renderer `Extrinsic.Graphics.ComputeParallelPrimitives` seam now owns a
deterministic CPU reference and RHI record path for per-segment float sums,
`uint32` counts, and count-normalized means, with empty segments producing count
0 and mean 0. The shipped GPU shader is the deterministic missing-feature path:
one 256-lane workgroup per segment scans the key/value stream in fixed order, so
no optional float-atomic Vulkan features or Vulkan-native API leakage are
introduced. Contract tests cover CPU fixtures, empty segments, invalid shapes,
dispatch planning, pipeline descriptors, mock-RHI recording, and fail-closed
resource validation; the opt-in Vulkan smoke compares GPU output against the CPU
reference within the declared tolerance and repeats the same input for bit
stability. KMeans integration remains a separate consumer task; this slice only
ships the shared primitive and docs.

[`GRAPHICS-112`](../archive/GRAPHICS-112-work-efficient-workgroup-scan.md) —
Work-efficient workgroup scan and overflow guard retired to `tasks/done/` on
2026-07-04 at `Operational` on promoted Vulkan hosts with subgroup arithmetic
and `CPUContracted` elsewhere. `parallel_prefix_scan.comp` now uses subgroup
arithmetic plus a small shared scan over per-subgroup totals instead of a
workgroup-wide Hillis-Steele scan. Prefix scan and add-offset accumulation clamp
to `UINT32_MAX` on GPU overflow, while the CPU reference continues to report
`SumOverflow` before wrap; the public scan/compaction API, scratch layout, and
dispatch planning contract are unchanged. The Vulkan smoke covers existing
scan/compaction parity plus local and multiblock overflow fixtures, and the
architecture/audit docs record the saturation semantics.

[`GRAPHICS-113`](../archive/GRAPHICS-113-selection-outline-id-work-pruning.md) —
Selection outline ID work pruning retired to `tasks/done/` on 2026-07-04 at
`Operational` on Vulkan-capable hosts and `CPUContracted` for the default
frame-recipe/command-route contracts. The implemented path splits outline-only
selection frames from pending click-picking: selected/hovered frames declare and
write only `EntityId` through the one-target
`Renderer.SelectionEntityId.OutlineOnly` pipeline, while pending picks keep the
full `EntityId` + `PrimitiveId` + `Picking.Readback` route and primitive
subpasses. The remaining candidate-narrowing question was evaluated and closed
without local implementation: current graphics seams expose whole
`SurfaceOpaque` indirect buckets and CPU snapshot selected IDs, so further
selected/hovered-only draw narrowing needs a separate filtered indirect
draw/shader-visible selected-ID contract rather than a safe local pass tweak.

[`GRAPHICS-114`](../archive/GRAPHICS-114-retained-imgui-overlay-copy-upload-path.md) —
Retained ImGui overlay copy/upload path retired to `tasks/done/` on 2026-07-04
at `Operational` on Vulkan-capable hosts and `CPUContracted` for backend-neutral
copy/reuse/upload contracts. Runtime `ImGuiAdapterDiagnostics` now reports
per-frame font-atlas, vertex, index, command, and total overlay copy bytes;
graphics `ImGuiUploadResult` reports vertex/index/total upload bytes. The
retained atlas path copies atlas bytes only on dirty payload changes, steady
frames retain atlas payload metadata/revision, accepted draw lists move through
the graphics boundary after validation, and command upload records are built
once per draw list. The measurement report is
`docs/reports/2026-07-04-graphics-114-imgui-overlay-retention.md`; focused
CPU/null contracts and the opt-in
`ImGuiSurfaceGpuSmoke.LargeSelectedEntityPayloadRetainsAtlasOnOperationalVulkan`
smoke passed.

[`GRAPHICS-116`](../archive/GRAPHICS-116-recipe-pass-contribution-seam.md) —
Frame-recipe pass contribution seam and typed record-path resolution retired to
`tasks/done/` on 2026-07-04 at `Operational` for the default renderer
composition seam. `BuildDefaultFrameRecipe(...)` now delegates through the
contribution-aware path, and the renderer registers SelectionOutline, DebugView,
ImGui, and VisualizationOverlay as typed overlay contributions before graph
construction. The fixed core can also be compiled with an empty contribution
registry; contract coverage proves the overlay-absent graph validates, executes
through the headless render-graph executor, and omits overlay passes/resources,
while the default contributed graph matches the compatibility build's compiled
debug dump.

[`GRAPHICS-117`](../archive/GRAPHICS-117-render-graph-compile-cache.md) —
Render-graph compile caching and gated debug dump retired to `tasks/done/` on
2026-07-04 at `Operational` for the default renderer frame recipe. The renderer
now owns a structural compile key over recipe features, sizing, import shape,
and contribution descriptors, reuses a cached `CompiledRenderGraph` on
steady-state frames, rebinds current imported handles before execution, and
invalidates on operational rebuild, shutdown, and resize. Debug dump generation
is opt-in through the renderer request seam. Contract tests prove zero
steady-state compile attempts, feature/resize recompilation, imported-handle
rebinding on reuse, and lazy debug dumps; the PR-fast
`rendering.frame_recipe_compile_cache.smoke` benchmark records the default
1280x720 recipe rebuild declare+compile baseline and the cached steady-state
compile-attempt contract.

[`GRAPHICS-118`](../archive/GRAPHICS-118-placed-transient-memory-aliasing.md) —
Placed transient resource allocation with real memory aliasing retired to
`tasks/done/` on 2026-07-04 at `Operational` on Vulkan-capable hosts and
`CPUContracted` for backend-neutral planning/fallback paths. The render-graph
compiler now emits deterministic transient placements and alias-reuse hazards,
the RHI seam exposes placed memory blocks, Null records CPU bookkeeping, Vulkan
binds placed images/buffers behind the RHI contract, and the renderer lowers the
plan only when transient aliasing is explicitly enabled. The closing Vulkan
smoke compares aliasing-on readback against aliasing-off output, keeps validation
counters stable across the aliasing-on frame, and records the default sandbox
recipe reduction from 263168 bytes naive/fallback to 197632 bytes placed peak.

[`UI-028`](../archive/UI-028-editor-mesh-simplification-window.md) — Sandbox EditorUI mesh
simplification retired to `tasks/done/` on 2026-07-05 at `CPUContracted`.
`Mesh > Processing > Simplify` drives the `GEOM-014` classical QEM / FA-QEM
simplification kernel through a runtime-owned undoable command, replaces the
selected mesh `GeometrySources`, preserves UV seam inputs when texcoords are
present, reports collapse/rejection/pin diagnostics, and leaves renderer
synchronization to geometry dirty tags. Focused geometry/runtime CTest coverage
passed, including the UV seam regression.

[`UI-031`](../archive/UI-031-editor-domain-ui-reorganization.md) — Sandbox EditorUI
domain-window reorganization retired to `tasks/done/` on 2026-07-05 at
`CPUContracted`. Mesh, Graph, and PointCloud `Appearance` windows now co-locate
render hints, visualization controls, uniform/lane color edits, property and
vertex-channel binding, bound-state inspection, and texture baking; the
corresponding `Properties` windows draw only property catalog rows and
diagnostics. Domain processing menu leaves now open focused method windows for
K-Means, denoise, curvature, remesh, subdivide, simplify, vertex normals,
progressive Poisson, and point-cloud outlier removal instead of the old
omnibus per-domain processing surface. The broader generation-keyed async
selected-analysis cache/job pipeline remains owned by `RUNTIME-138`.

[`GEOM-054`](../archive/GEOM-054-registration-pipeline-stage-extraction.md) —
Registration pipeline named ICP stage extraction retired to `tasks/done/` on
2026-07-05 at `Operational`. The implementation landed in `335e05b7` and
restructures `Geometry::Registration::AlignICP` behind an internal
`RunIcpLoop` driver with explicit correspondence, rejection, optional robust
weighting, transform-solve, and convergence stages. GEOM-054 introduces no
public `Geometry.Registration` surface change; the later observer seam retired
under `GEOM-055`.

[`GEOM-055`](../archive/GEOM-055-registration-iteration-observer.md) — Registration
per-iteration observer retired to `tasks/done/` on 2026-07-05 at
`Operational`. The implementation landed in `811a1677` and exports
`IterationTrace` plus `IterationObserver` from `Geometry.Registration`;
`AlignICP` accepts a trailing null-default observer, and `RunIcpLoop` emits one
trace per completed iteration after the cumulative transform is updated.
Contract tests prove observed and unobserved runs match exactly and trace
indices/RMSE/final-transform invariants line up with `RegistrationResult`.

[`UI-029`](../archive/UI-029-editor-registration-convergence-visualization.md) — Editor
ICP registration panel and convergence visualization retired to `tasks/done/`
on 2026-07-05 at `Operational`. The implementation landed in `d3a839cf`,
`433953e7`, and `2f1abf71`: `Extrinsic.Runtime.RegistrationAlignment` runs
ICP with the `GEOM-055` per-iteration observer, the Sandbox editor exposes a
top-level `ICP Registration` panel for selected point-cloud entities, and the
source transform is updated through undoable runtime-owned commands while the
trajectory step scrubs intermediate poses. Focused runtime unit and Sandbox UI
contract tests cover degenerate input, trajectory capture/scrubbing, successful
alignment, failure states, undo/redo, and transformed source/target entities.

[`RORG-031F`](../archive/RORG-031F-ui-integration.md) — UI integration backlog seed retired
to `tasks/done/` on 2026-07-05 at `Scaffolded`. The planning-only umbrella now
records the promoted `SandboxEditorUi` inventory through retired `UI-031`, keeps
the remaining deferred workflows reserved as prospective `UI-009..012` tasks
with external triggers, and explicitly states that no `Operational` follow-up is
owed for the seed itself. The UI backlog now has no concrete open UI task; future
children open directly from their trigger and cite the retired seed or the UI
backlog README.

[`BUG-057`](../archive/BUG-057-entity-scalar-properties-render-black.md) — Entity scalar
properties rendering black retired to `tasks/done/` on 2026-07-05 at
`CPUContracted`. Runtime scalar property-buffer extraction already produced a
valid scalar packet, but renderer prep did not pass those packets into
`VisualizationSyncSystem`; prepared entity configs kept the default scalar
range instead of the computed auto range. Renderer prep now threads scalar
packets through visualization sync so scalar-field entity configs receive the
resolved BDA, element count, colormap ID, and computed range.

[`BUG-058`](../archive/BUG-058-entity-isolines-render-black.md) — Entity isolines rendering
black retired to `tasks/done/` on 2026-07-05 at `CPUContracted`. The editor
Isoline preset rides the scalar-field `VisualizationConfig` path, so the same
missing prepared-config scalar range caused isoline shader config to operate on
the wrong range. The regression now enables isolines and proves the prepared
entity config receives the computed scalar range plus isoline count, width, and
color.

[`RUNTIME-140`](../archive/RUNTIME-140-remove-global-waitforall-from-import-apply.md) —
Remove global scheduler barrier from import apply retired to `tasks/done/` on
2026-07-05 at `CPUContracted`. `AssetService` now exposes a per-asset
completion/flush primitive, and runtime import materialization uses it instead
of `Core::Tasks::Scheduler::WaitForAll()`. `AssetLoadPipeline` also rejects
duplicate in-flight CPU decode completion without dropping the pending upload
stage. Focused regressions prove direct asset-service completion and
`Engine::ImportAssetFromPath` both return while an unrelated scheduler sentinel
remains in flight, with imported payloads and events still delivered exactly
once.

[`RUNTIME-142`](../archive/RUNTIME-142-async-modelscene-texture-scenefile-io.md) — Async
model-scene/texture import and scene-file IO retired to `tasks/done/` on
2026-07-05 at `Operational`. Dropped model-scene/texture imports, Sandbox
editor model-scene/texture imports, and Sandbox editor scene save/load now
queue `StreamingExecutor` work, keep file IO and decode/parse/serialize work
off the frame callback path, and apply results on the bounded main-thread
drain. A slow fake IO backend regression proves the frame loop advances while
queued texture reads remain blocked.

[`RUNTIME-143`](../archive/RUNTIME-143-frame-hook-registry-and-kmeans-decoupling.md) —
Multi-subscriber frame-command hook and K-Means decoupling from Engine retired
to `tasks/done/` on 2026-07-05 at `Operational`. `IRenderer` now exposes
deterministic add/remove runtime frame-command hook registration, `Engine`
owns a domain-free runtime GPU participant registry for frame command
recording, maintenance drains, in-flight checks, and post-device-idle
teardown, and `SandboxEditorUi` owns the concrete `RuntimeKMeansGpuJobQueue`
while attached. `Runtime.Engine.cppm` and `.cpp` are grep-clean for K-Means,
and the existing Sandbox K-Means GPU command surface continues to submit and
consume through the attached queue.

[`BUG-059`](../archive/BUG-059-curvature-scalar-isoline-viz-black-end-to-end.md) —
Curvature scalar/isoline visualization renders black, diagnosed end to end and
retired to `tasks/done/` on 2026-07-06 at `CPUContracted`. New regression
tests mirror the real editor flow (Appearance surface-lane override presets,
double-typed `v:mean_curvature`, late property arrival, steady-state frames)
and proved the promoted extraction→sync→config contract healthy; the
reproducible all-black modes were heavy-tailed min/max auto ranges compressing
the surface bulk into the colormap's darkest bin and degenerate manual ranges
reaching the prepared config. Auto ranges now clamp to the [2%, 98%]
quantiles for fields with ≥64 samples (with a dedicated clamp counter), and
`BuildEntityConfig` sanitizes degenerate/non-finite ranges before the shader
can normalize every fragment to t=0.

[`UI-032`](../archive/UI-032-appearance-scalar-isoline-visualization-controls.md) —
Appearance scalar/isoline visualization controls retired to `tasks/done/` on
2026-07-06 at `CPUContracted`. The Appearance panel gains colormap selection,
auto/manual range clamping, binning, isoline count/width/color, and up to
eight explicit highlight isovalues; preset buttons now preserve configured
styling and tuned ranges instead of resetting to defaults. The editor
command/model surface, `ScalarFieldConfig`, scene serialization,
`RHI::GpuEntityConfig` (176-byte layout), `gpu_scene.glsl`, and
`VisualizationSyncSystem` plumb the new fields end to end with fail-closed
validation, locked by editor-command, extraction, and serialization
regressions.

[`RUNTIME-145`](../archive/RUNTIME-145-runtime-frame-path-efficiency-polish.md) —
Runtime frame-path steady-state efficiency polish retired to `tasks/done/` on
2026-07-06 at `Operational`. The runtime now maintains `StableEntityLookup`
incrementally instead of rebuilding it every steady-state frame, recycles
`StreamingExecutor` records with priority ready queues and batched queue
snapshot reads, gates the pre-render transform flush behind a conservative
post-sim dirty bit, reuses render-extraction live-key scratch storage, and keeps
decoded geometry import payloads shared across worker-to-apply and reload
captures. Focused runtime regressions plus the full default CPU gate prove the
covered frame-path behavior remains unchanged while the recurring idle-frame
work is removed.

[`BUG-060`](../archive/BUG-060-scalar-colormap-lut-1d-view-black-on-gpu.md) —
Scalar/isoline surface black-output fix retired to `tasks/done/` on
2026-07-06 at `CPUContracted`. The GPU-only root cause was the colormap LUT
being created as a 1D image view while all promoted shader consumers sample the
bindless heap as `sampler2D`; the LUT is now a 256x1 `Tex2D`, and evenly
spaced isolines use raw normalized scalar values instead of binned values.
The retirement adds a promoted-Vulkan sandbox readback smoke for the
surface-lane Scalar/Isolines preset-equivalent state, alongside the existing
line/point scalar LUT smoke, so the operational check is deterministic rather
than a manual UI click.

[`BUG-056`](../archive/BUG-056-extrinsic-sandbox-default-vulkan-validation-gate.md) —
ExtrinsicSandbox default Vulkan validation gate fallback retired to
`tasks/done/` on 2026-07-06 at `Operational`. The default deferred GBuffer
fragment now consumes the full shared default-debug-surface vertex interface,
including config, visualization, and instance slots, and applies the same
visualization color resolve helpers used by the forward path. Frame-pacing
captures now include final `IDevice::IsOperational()` state, and the CTest
validator fails shader-interface validation warnings or a final
non-operational `BarrierValidationFailed` path. The selected promoted Vulkan
sandbox/ImGui/frame-pacing envelope passes 18/18, and the default sandbox
fixture now installs the same runtime default policy bundle as the production
`ExtrinsicSandbox` app before custom smoke apps run.

[`GRAPHICS-119`](../archive/GRAPHICS-119-parallel-pass-command-recording.md) — Parallel
render-pass command recording via the task scheduler retired to `tasks/done/`
on 2026-07-07 at `Operational`. Render-graph pass recording now fans out by
compiled topological layer through `Core::Tasks`, acquires backend-neutral
parallel command contexts, joins them back in deterministic submit order, and
keeps the serial fallback/debug selector as the reference path. Null and Mock
contracts prove deterministic CPU behavior, promoted Vulkan records graphics
and async-compute queue-family-local secondary command buffers under validation
smokes, and benchmark smoke parity is recorded without claiming a performance
win. Transfer render-graph scheduling remains out of scope because no
production transfer pass is enabled.

[`PROC-015`](../archive/PROC-015-diagnosis-playbook-skills-wave-1.md) — Recurring
diagnosis playbooks codified as skills (wave 1) retired to `tasks/done/` on
2026-07-08. The 2026-07-08 agentic-workflow review mined the 601 retired
tasks and found three playbooks re-derived across many sessions: Vulkan
frame-content triage (≥14 tasks, with the bindless bridge slot defect
shipping three times), the opt-in `gpu;vulkan` readback smoke shape (~14
hand-rolled instances), and stale C++23-module/ccache artifact triage (three
whole-session sinks). They now exist as self-contained discipline skills —
`intrinsicengine-vulkan-frame-triage`, `intrinsicengine-gpu-smoke-authoring`,
and `intrinsicengine-stale-build-triage` — registered in the
`intrinsicengine-core` routing table, the skills README, and routed from
`intrinsicengine-diagnose`. Wave-2 playbooks are seeded as
`PROC-018`/`PROC-019`/`PROC-020`.

[`PROC-016`](../archive/PROC-016-skills-docs-mirror-drift-fixes.md) — Skills/docs mirror
drift and dead routings fixed, retired to `tasks/done/` on 2026-07-08. The
skills README no longer claims 13 source docs, describes the real
`sync_skills.py` mechanism instead of a harmful `cp` script, and documents the
symlink layout; `clean-workshop-review.md` and `drift-audit-checklist.md` are
now mirrored into `intrinsicengine-review` (REFERENCE_MAP 14→16 files); the
`intrinsicengine-zoom-out` routing no longer asks the model to invoke a
user-only skill; the test-category taxonomy is unified on `AGENTS.md` §7
across `contract.md` and both review checklists; `contract.md`'s layering
table gained the missing `graphics/assets`/`graphics/vulkan`/`platform` rows;
`docs/agent/prompt/prompt.md` is indexed from the `AGENTS.md` routing table;
and `tasks/README.md` now states the single-slice-from-backlog rule.
Skill-body canonicalization is deferred to `PROC-023`.

[`PROC-017`](../archive/PROC-017-workflow-convention-gaps.md) — Undocumented workflow
conventions written down, retired to `tasks/done/` on 2026-07-08. Four
conventions the workflow already practiced now have owning text: branch
naming and task claiming in `docs/agent/prompt/prompt.md`, CI-failure→`BUG-`
task intake in `prompt.md` §"When CI fails" anchored from `AGENTS.md` §10
(the `BUG-062`/`063`/`064` pattern), and batch-seeding ID allocation plus the
canonical-prefix pointer in `docs/agent/task-format.md` (the
`GEOM-027`/`PROC-012` collision lesson).

[`BUG-062`](../archive/BUG-062-warm-configure-budget-flaky-runner-variance.md) — Warm-
configure CI budget flake closed on 2026-07-08 (PR #1010). The 10 s
warm-cache configure guard, calibrated at the shared-runner median, killed
five merge-gating workflows across three PR heads (including a
markdown-only diff) before their build steps. The budget moved to 20 s in
all seven invocations across six workflows with guard semantics and timing
telemetry unchanged; three consecutive PR CI rounds then completed every
configure step without a budget kill.

[`ARCH-007`](../archive/ARCH-007-kernel-command-bus-single-drain-point.md) — Kernel
command bus with a single pre-sim drain point retired to `tasks/done/` on
2026-07-08 at `CPUContracted`. Runtime now owns a domain-free
`Extrinsic.Runtime.CommandBus` with plain-data payloads, correlation IDs,
thread-safe enqueue from any phase/thread, fail-closed missing-handler
diagnostics, a post-success inverse/history hook seam, and a built-in
`QuitRequested` command. `Engine::RunFrame()` drains the bus exactly once
between platform input and fixed-step simulation, and `Engine::Shutdown()`
discards pending commands so teardown/reset cannot replay stale work. Headless
contract tests prove enqueue ordering, cross-thread enqueue, payload copying,
handler failures, missing handlers, follow-up deferral, history inverse
re-enqueue, and pending discard. `ARCH-012` later closed the `Operational`
proof through a real module flow.

[`ARCH-008`](../archive/ARCH-008-kernel-event-bus-queued-only.md) — Queued-only kernel
event bus retired to `tasks/done/` on 2026-07-08 at `CPUContracted`. Runtime
now owns `Extrinsic.Runtime.KernelEvents`, a domain-free queued event bus with
typed subscriptions, removable handles, worker-safe publish into an inbox,
main-thread-only `Pump()`, and stats for published, delivered, and listener
invocation counts. `Engine::RunFrame()` pumps exactly twice per frame: once
after the command-bus drain and once after fixed-step simulation. Publishes
made by listeners are bounded to the next pump, not recursively dispatched in
the current batch. Six runtime contract tests cover next-pump delivery,
cascade deferral, cross-thread publish under the sanitizer-enabled `ci` preset,
unsubscribe behavior, unsubscribe-during-pump safety, and the two engine pump
positions. `ARCH-012` later closed the `Operational` proof through a real
module flow.

[`ARCH-012`](../archive/ARCH-012-clusteringmodule-proving-extraction.md) —
ClusteringModule proving extraction retired to `tasks/done/` on 2026-07-08 at
`Operational`. Runtime now owns `Extrinsic.Runtime.ClusteringModule` under
`src/runtime/Modules/Clustering/`, with `RunKMeans`, `KMeansRunCompleted`,
`ClusterLabelsChanged`, `ClusteringService`, and `ClusteringModule`. Sandbox
composition registers the module from app startup, not from `Engine`: the editor
submits a `RunKMeans` command, the module snapshots active-world mesh/graph/point
cloud positions, submits a world-scoped CPU reference job through `JobService`,
publishes completion at the main-thread job gate, commits labels during kernel
event pump B, and publishes `ClusterLabelsChanged` so the standing visualization
reaction marks vertex attributes dirty. Headless contract tests cover
end-to-end `Engine::Run()` label commits/change events, active-world switch
stale-result suppression, and fail-closed missing-module command drain behavior;
the focused K-Means/editor/backend filter, runtime seam CTest subset, full
`IntrinsicTests` build, default CPU-supported CTest gate, and strict layering
gate all passed. `Runtime.Engine.cppm` and `Runtime.Engine.cpp` contain no
`KMeans` or `Runtime.ClusteringModule` tokens. The explicit Vulkan
`KMeansGpuJobQueue` participant path intentionally remains in runtime/editor
ownership until `RUNTIME-137` moves GPU dispatch onto the kernel GPU-job target.

[`ARCH-013`](../archive/ARCH-013-post-seam-collision-rereview.md) — Post-seam collision
re-review retired to `tasks/done/` on 2026-07-08 as task-governance work. Every
front-matter-gated row (`RUNTIME-150`, `RUNTIME-151`, `ARCH-006`, `UI-034`,
`RUNTIME-137`, `RUNTIME-138`) and every audit row (`RUNTIME-129`,
`RUNTIME-146..149`, `CORE-005`/`006`/`007`/`008`/`009`, `GRAPHICS-105`,
`PLATFORM-004`) now carries a dated `ARCH-013 re-review` note with an
unchanged/re-scoped/re-gated decision. `RUNTIME-129` was re-gated on
`RUNTIME-137`; `RUNTIME-137` is now explicitly the `JobService` `GpuQueue` and
async-readback substrate, and the remaining K-Means GPU participant migration is
owned there. The backlog sweep found no additional open task prescribing
ADR-0024-rejected mechanisms without a recorded justification, and ADR-0024 plus
the backlog/category READMEs now summarize the result.

[`RUNTIME-146`](../archive/RUNTIME-146-extract-engine-config-boot-module.md) — Engine
config boot extraction retired to `tasks/done/` on 2026-07-08 at `Operational`.
Runtime now owns `Extrinsic.Runtime.EngineConfigBoot`, a free-standing module
exporting `CreateReferenceEngineConfig()`, `EngineConfigBootSource`,
`EngineConfigBootOptions`, `EngineConfigBootResult`, and
`ResolveEngineConfigForBoot(...)` without importing the full `Engine`
interface. Sandbox startup and runtime config-control tests import the module
directly, while `Runtime.Engine.cppm` and `Runtime.Engine.cpp` contain no
boot-helper declarations or definitions. Focused runtime config-boot/control
tests passed 11/11, `IntrinsicRuntimeContractTests` and `IntrinsicTests` built,
the full default CPU-supported CTest gate passed 3636/3636, strict layering and
test-layout checks passed, and the module inventory was regenerated.

[`RUNTIME-147`](../archive/RUNTIME-147-extract-asset-import-pipeline-subsystem.md) —
Asset-import pipeline extraction retired to `tasks/done/` on 2026-07-08 at
`Operational`. Runtime now owns `Extrinsic.Runtime.AssetImportPipeline`, an
engine-owned subsystem carrying the import/reimport/queue/cancel facade, ingest
state records, import event log, post-import processor / import-authoring-policy
/ import-completed registries, decode/materialize helpers, queue
snapshot/cancel/clear state, and import dirty-state marking through
`EditorCommandHistory`. `Runtime.Engine.cppm` now exposes only
`Engine::GetAssetImportPipeline()` for this surface, and `Runtime.Engine.cpp`
keeps only platform drop delegation plus lifecycle wiring for the pipeline.
Sandbox default policies, Sandbox editor UI, and runtime import tests call the
pipeline directly. Focused runtime import/editor tests passed 31/31,
`IntrinsicRuntimeContractTests` and `IntrinsicTests` built, the full default
CPU-supported CTest gate passed 3638/3638, strict layering, test-layout,
task-policy, docs-link, docs-sync, and diff-whitespace checks passed, and the
module inventory was regenerated.

[`RUNTIME-149`](../archive/RUNTIME-149-extract-engine-config-control-subsystem.md) —
Render-recipe and hot-config control extraction retired to `tasks/done/` on
2026-07-08 at `Operational`. Runtime now owns
`Extrinsic.Runtime.EngineConfigControl`, an engine-owned subsystem carrying
render-recipe preview/apply/clear, startup recipe activation, engine-config
hot-subset preview/apply, boot-only rejection diagnostics, active control state,
and the moved helper routines. `Runtime.Engine.cppm` now exposes only
`Engine::GetConfigControl()` for this surface, and `Runtime.Engine.cpp` keeps
only lifecycle dependency wiring plus startup default-recipe delegation through
the subsystem. Sandbox editor UI and runtime config-control/render-recipe tests
call the subsystem directly. Focused runtime config-control/render-recipe tests
passed 9/9, `IntrinsicRuntimeContractTests` and `IntrinsicTests` built, the
full default CPU-supported CTest gate passed 3638/3638, strict layering,
test-layout, task-policy, docs-link, docs-sync, and diff-whitespace checks
passed, and the module inventory was regenerated.

[`RUNTIME-148`](../archive/RUNTIME-148-extract-scene-document-subsystem.md) —
Scene-document facade extraction retired to `tasks/done/` on 2026-07-08 at
`Operational`. Runtime now owns `Extrinsic.Runtime.SceneDocument`, an
engine-owned subsystem carrying direct and queued scene save/load, new/close
document behavior, the scene-file event log, serializable-scene snapshot
helpers, and the scene replacement cleanup/rebuild ordering. `Runtime.Engine.cppm`
now exposes only `Engine::GetSceneDocument()` for this surface, and
`Runtime.Engine.cpp` keeps only lifecycle dependency wiring plus the active-world
maintenance call into `SceneDocument::ClearSceneRuntimeState()`. Sandbox editor
UI and runtime scene lifecycle/stable-lookup tests call the document subsystem
directly. Focused runtime scene/editor tests passed 6/6,
`IntrinsicRuntimeContractTests` and `IntrinsicTests` built, the full default
CPU-supported CTest gate passed 3638/3638, strict layering, test-layout,
task-policy, docs-link, docs-sync, and diff-whitespace checks passed, and the
module inventory was regenerated.

[`RUNTIME-150`](../archive/RUNTIME-150-split-engine-frame-loop-implementation-unit.md) —
Frame-loop partition split retired to `tasks/done/` on 2026-07-08 at
`Operational`. Runtime now owns the private
`Extrinsic.Runtime.Engine:FrameLoop` partition carrying `Core.FrameLoop` hook
adapters and `RunFrame()`-only per-frame helpers for platform polling,
operational transitions, render prep/execution, transfer/streaming/assets,
selection readback, gizmo and viewport input, fixed-step simulation, camera
population, and runtime input-action dispatch. `Runtime.Engine.cpp` imports the
partition, `Engine::RunFrame()` remains on `Engine`, the partition exports no
declarations, and no frame phase/order/control-surface behavior changed.
`Runtime.Engine.cpp` no longer defines the moved hook adapters or listed
per-frame helper routines, and the module inventory was regenerated to 367
modules. Focused runtime frame-loop/selection/gizmo/input/render-world-pool
tests, `IntrinsicRuntimeContractTests`, `IntrinsicTests`, the full default
CPU-supported CTest gate, strict layering, test-layout, task-policy,
docs-link, docs-sync, root-hygiene warning-mode, and diff-whitespace checks
passed.

[`RUNTIME-151`](../archive/RUNTIME-151-slim-engine-interface-and-remove-entt-leak.md) —
Engine-interface slimming retired to `tasks/done/` on 2026-07-08 at
`Operational`. Runtime now owns `StableEntityLookupSceneBinding` in
`Extrinsic.Runtime.StableEntityLookup`; the binding owns the StableId
construct/update/destroy signal connections and preserves the same incremental
`Track`/`Forget` behavior plus whole-scene rebuild behavior on scene
replacement. `SceneDocument` calls the binding for disconnect/connect/rebuild,
and `Engine` holds the binding without declaring EnTT-scoped connections or
callbacks in `Runtime.Engine.cppm`. The direct task audit moved
`Runtime.Engine.cppm` from 733 lines / 50 imports / 2 EnTT includes to 721
lines / 46 imports / 0 EnTT tokens. Focused StableEntityLookup, selection
snapshot, and runtime scene-lifecycle tests passed, `IntrinsicRuntimeContractTests`
and `IntrinsicTests` built, the full default CPU-supported CTest gate passed
3638/3638, strict structural/docs/diff checks passed, and the module inventory
was regenerated.

[`RUNTIME-137`](../archive/RUNTIME-137-async-gpu-readback-helper-for-compute-backends.md) —
Async GPU readback helper and JobService `GpuQueue` substrate retired to
`tasks/done/` on 2026-07-09 at `Operational`. Runtime now owns
`Extrinsic.Runtime.AsyncBufferReadback`, a pooled `Graphics.GpuTransfer`
readback wrapper that records the `ShaderWrite -> TransferRead` drain and lets
compute backends poll/collect without `IDevice::ReadBuffer` or per-drain host
allocation. `JobService` now owns the `GpuQueue` participant registry; Engine
installs one renderer frame-command bridge, drains GPU queue transfer/readback
completions during Maintenance, and shuts participants down through JobService
after unregistering the bridge and waiting for device idle when required.
`SandboxEditorUi` registers `RuntimeKMeansGpuJobQueue` through
`engine.Jobs().RegisterGpuQueueParticipant(...)`, so `Runtime.Engine.cppm` no
longer exposes `RuntimeGpuJobParticipant*` types or registration functions.
Focused runtime GTest passed 33/33, `RuntimeEngineLayering.*` passed 11/11,
focused CTest passed 169/169, `IntrinsicTests` built, the full default
CPU-supported CTest gate passed 3640/3640, the opt-in `gpu;vulkan` readback /
KMeans smoke selection passed 5/5, and the module inventory/session brief were
regenerated.

[`UI-034`](../archive/UI-034-framework24-editor-interaction-layout-conventions.md) —
Decentralized editor-window contribution, capture, visibility, and generic
property-plot contracts retired to `tasks/done/` on 2026-07-13 at
`CPUContracted`. `Runtime.SandboxEditorUi` now exposes a mutation-safe registry
with stable IDs, structured menu paths, lazy callbacks, transition observation,
and an external composition seam. One end-of-editor-frame capture snapshot
gates viewport consumers, while the global visibility command preserves window
open state and immediately clears stale capture. Mesh Appearance and Mesh
Simplify are registry-owned exemplars sharing one lazy mesh model; Appearance
uses the generic scalar-property selector and ImPlot histogram. Focused editor
tests passed 15/15, `IntrinsicTests` built, the full default CPU-supported CTest
gate passed 3675/3675, strict structural/docs checks passed, and the five-frame
promoted-Vulkan composition smoke completed before the separately tracked
`BUG-083` LeakSanitizer shutdown finding.

[`BUG-079`](../archive/BUG-079-coretasks-abandoned-wait-continuation-leak.md) —
CoreTasks parked-continuation reclamation retired to `tasks/done/` on
2026-07-13 at `CPUContracted`. Wait-token release and scheduler shutdown now
transfer every still-parked single-use coroutine handle under the wait mutex,
then cancel and destroy the frame after unlocking; normal signal/unpark and
cancellation therefore cannot both claim the same frame. Frame-owned
destructor sentinels failed deterministically before the fix, then the release
and shutdown regressions each passed 100 consecutive ASan/UBSan repetitions,
all 19 `CoreTasks.*` contracts passed, `IntrinsicTests` built, the full default
CPU-supported gate passed 3680/3680, and strict structural/docs checks passed.

[`BUG-068`](../archive/BUG-068-asset-scene-handoff-not-rebound-on-active-world-change.md)
— active-world scene-borrower rebinding retired to `tasks/done/` on 2026-07-13
at `CPUContracted`. Runtime now rebuilds asset-residency handoffs and import
pipeline dependencies during the active-world switch Maintenance pass, before
the previous registry can reach its later deferred-destruction pass; a null
active scene also detaches stable-entity selection lookup, and initialization
clears stale worlds before creating the boot world. Removing the rebind made
the real-Engine model-ready/reload regression fail deterministically with an
ASan heap-use-after-free after previous-world teardown. Restored code passed
the regression 50 consecutive times, the focused lifecycle selection 13/13,
the full default CPU-supported gate 3681/3681, and strict structural/docs
checks.

[`BUG-084`](../archive/BUG-084-transform-sync-test-mock-device-namespace.md) —
TransformSyncSystem contract-test namespace repair retired on 2026-07-14 at
`CPUContracted`. The two mock-device declarations now use the declared
`Extrinsic::Tests::MockDevice` name, so the sanitizer-enabled graphics CPU
contract and aggregate test targets compile without changing production code.
The default CPU-supported CTest gate passed 3,698/3,698, including all nine
headless Sandbox acceptance tests.

[`ARCH-006`](ARCH-006-sandbox-editor-content-out-of-runtime.md) — Sandbox
editor ownership migration retired to `tasks/done/` on 2026-07-14 at the
structural `Retired` endpoint. App-owned `Extrinsic.Sandbox.Editor.Shell` and
its method, mesh-processing, and domain panel modules now own all Sandbox
windows, menus, ImGui state, and presentation. The former
`Extrinsic.Runtime.SandboxEditorUi` module is deleted; generic host, registry,
and property widgets remain under `src/runtime/Editor`, while the surviving
engine-facing models, commands, jobs, session wiring, and result records are
presentation-free `Extrinsic.Runtime.SandboxEditorFacades`. Method and
render-recipe/artifact bodies compile in separate private implementation
units. The former 12,254-line runtime contract monolith is split into six
runtime subject files plus app-linked presentation coverage. The endpoint
inventory contains 155 focused tests: 132 original suite/name pairs remain
exact, 15 presentation tests moved or were renamed with their app-owned
subject, and 8 endpoint contracts cover session lifetime and domain-panel
registration/cache behavior. The obsolete selected-mesh scalar-plot helper
regression is intentionally absent. The replayed exact-head build passed
without diagnostics, the focused editor inventory passed 155/155, the full
default CPU-supported gate passed 3,706/3,706, and strict structural/docs
evidence is recorded in the retired task.
