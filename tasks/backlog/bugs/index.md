# IntrinsicEngine — Active Bug Notes

This file tracks **currently reproducible correctness bugs, flaky tests, and test-harness defects**.
Each entry includes the observed repro, the likely affected symbols, and a fix plan aimed at a robust engine-level correction rather than a one-off patch.

## Active Issues

- [`BUG-097` — Progressive model-scene UV job publishes a zero atlas](BUG-097-progressive-model-scene-zero-uv-atlas.md):
  the default-off progressive enrichment path labels an all-zero authoritative
  `v:texcoord` property as an atlas and can publish it after newer UV/topology
  edits; replace it with real atlas output plus generation-safe stale discard.
- [`BUG-096` — ICP point-to-plane ignores target normals](BUG-096-icp-point-to-plane-target-normals.md):
  synchronous and queued runtime registration pass an empty target-normal span,
  so geometry silently executes point-to-point while the editor reports the
  requested point-to-plane variant.
- [`BUG-095` — Direct-mesh postprocess can overwrite newer editor geometry](BUG-095-direct-mesh-postprocess-stale-overwrite.md):
  deferred import enrichment validates only entity liveness before replacing
  live geometry, allowing newer position, topology, UV, or property edits to be
  lost; apply must be generation-keyed and stale-safe.
- [`BUG-094` — Model-scene import drops node semantics and standard selection behavior](../../active/BUG-094-model-scene-node-semantics-selection.md):
  glTF decode flattens meshes without active-scene traversal, hierarchy,
  transforms, or instancing, and model primitives bypass standard selectable,
  completion, selection, and focus authoring.
- [`BUG-091` — GoogleTest PRE_TEST discovery times out on a cold start](BUG-091-gtest-pretest-discovery-cold-timeout.md):
  CMake's implicit five-second PRE_TEST discovery limit can abort CTest while
  an unrelated cold sanitizer binary enumerates tests, before the selected
  tests run; collect cold/warm/contention evidence and set an explicit,
  evidence-backed discovery policy without weakening per-test timeouts.
- [`BUG-088` — Benchmark smoke hard timeout flakes under host contention](BUG-088-benchmark-smoke-hard-timeout-host-contention.md):
  the required default CPU gate timed out the monolithic 21-result smoke at its
  hard 30-second limit under concurrent load, while the same pair passed in
  14.71 seconds in isolation; collect a timing population and choose an
  evidence-backed PR-fast split or slow-lane classification without weakening
  strict result validation.
- [`BUG-081` — Warm-configure CI budget still flakes on hosted-runner variance](BUG-081-warm-configure-budget-runner-variance.md):
  an exact-vcpkg-hit configure took 22.002 s against the recalibrated 20 s
  budget and stopped the job before ccache restore or compilation; collect a
  comparable hosted population and set an evidence-backed budget with explicit
  headroom while preserving fail-closed semantics.

## Verified / Closed

- Closed 2026-07-16: [`BUG-093` — File / Import prerequisite gating and disabled-reason tooltips](../../done/BUG-093-file-import-prerequisite-gating-tooltips.md).
  One runtime evaluator now owns route, promoted-importer, and payload-hint
  readiness for both presentation and dispatch. The real Null-window File /
  Import workflow exposes runtime-owned disabled reasons on hover and prevents
  invalid requests from reaching the import callback.

- Closed 2026-07-16: [`BUG-083` — Vulkan Sandbox shutdown reports driver and DBus leaks under LeakSanitizer](../../done/BUG-083-vulkan-sandbox-shutdown-lsan-leaks.md).
  The exact NVIDIA report is partitioned across three diagnosed external
  retention call paths. A no-skip five-frame process contract applies only
  those entries, validates renderer completion plus final device operation,
  and first proves an unrelated 4,096-byte engine leak remains visible;
  general test binaries embed no suppression.

- Closed 2026-07-16: [`BUG-082` — GLFW X11 input-method initialization leaks under LeakSanitizer](../../done/BUG-082-glfw-x11-input-method-lsan-leak.md).
  The unchanged GLFW 3.4/Xlib shutdown path reaches input-method unregister and
  close before normal exit. A standalone, unsuppressed live-X11 process now
  proves process-static teardown calls `glfwTerminate()` exactly once and
  detects a named 4,096-byte synthetic leak, without production lifetime or
  sanitizer-suppression changes.

- Closed 2026-07-16: [`BUG-092` — Scene lifecycle async wait exhausts its frame budget under delayed I/O](../../done/BUG-092-scene-lifecycle-async-wait-frame-budget.md).
  The test-local helper now uses a ten-second steady-clock budget, yields one
  millisecond after unsuccessful polls, and reports explicit success/timeout
  state. A 257-call regression pins the retired frame ceiling; repeated scene
  I/O and a five-second injected worker-write delay pass without production
  runtime changes.

- Closed 2026-07-16: [`BUG-090` — Async-work layering test asserts stale shutdown call spelling](../../done/BUG-090-async-work-layering-test-stale-shutdown-owner.md).
  The source contract now recognizes `ShutdownHooks::AsyncWork` delegation
  without changing production shutdown behavior or weakening Engine/service
  ownership checks. The exact regression passed 1/1 and all 24 layering
  contracts passed.

- Closed 2026-07-16: [`BUG-089` — Root-hygiene strict mode rejects canonical and ignored state](../../done/BUG-089-root-hygiene-rejects-canonical-and-ignored-state.md).
  Root ownership now comes from one repository policy shared by both checker
  entrypoints. Exact `ara/` tracking and named `imgui.ini`/`.ruff_cache/`
  local state pass, while unknown roots, missing roots, malformed or broad
  policy, and global Git-ignore hiding fail closed. Twelve focused regressions
  and the strict repository structural checks passed.

- Closed 2026-07-16: [`BUG-087` — Documented task-validator root silently validates zero tasks](../../done/BUG-087-task-validator-documented-root-silent-noop.md).
  The canonical invocation now names the `tasks` root, strict mode rejects
  zero-file discovery with every searched lifecycle directory, and a CI-run
  tooling regression pins both the real repository invocation and an empty
  task root. Task, mirror, link, and workflow-policy checks passed.

- Closed 2026-07-15: [`BUG-086` — ImGui adapter omits the vertex-offset renderer capability](../../done/BUG-086-imgui-adapter-omits-vtx-offset-capability.md).
  The runtime adapter now advertises `RendererHasVtxOffset`, and the existing
  pointer-free overlay/upload/pass path preserves each non-zero command base
  vertex. A generated draw list above 65,535 vertices and a dense-mesh live
  Vulkan replay completed without the former Dear ImGui assertion.

- Closed 2026-07-15: [`BUG-085` — ImGui overlay drops draw-command clip rectangles](../../done/BUG-085-imgui-overlay-drops-command-clip-rectangles.md).
  Runtime now converts finite Dear ImGui clip rectangles to framebuffer
  scissors, graphics preserves and validates them through upload, and
  `Pass.ImGui` applies the renderer-convention Y transform before each draw.
  CPU command-order contracts and the live Vulkan `UI-036` replay verified
  that checker/grid content remains inside its UV child pane.

- Closed 2026-07-14: [`BUG-084` — TransformSyncSystem contract test uses an unqualified test namespace](../../archive/BUG-084-transform-sync-test-mock-device-namespace.md).
  The two mock-device declarations now name their existing
  `Extrinsic::Tests` namespace, restoring the sanitizer-enabled graphics CPU
  contract and aggregate builds. The default CPU-supported gate passed
  3,698/3,698; no production source changed.

- Closed 2026-07-13: [`BUG-074` — Orphaned GpuAssetCache slot causes per-entity bake retry livelock](../../archive/BUG-074-object-space-normal-bake-orphaned-cache-slot-livelock.md).
  Both post-open failure paths now retire only the exact GPU-produced texture
  generation they own, so cleanup cannot destroy a replacement or recreate a
  removed slot. After forced record and ready-frame failures, an explicit
  second schedule succeeds immediately; the six-test causal selection passed
  100 repetitions and the complete 3,692-test default CPU gate passed.

- Closed 2026-07-13: [`BUG-064` — ci-vulkan FramePacingDiagnosticCapture cannot run headless](../../archive/BUG-064-ci-vulkan-framepacing-headless-display.md).
  The strict capture now runs under isolated Xvfb with Mesa lavapipe rather
  than producing a zero-frame report or taking a capability skip. Three
  sequential hosted runs at exact head `7e735868` —
  [29277091536](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29277091536),
  [29278614647](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29278614647),
  and [29280699135](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29280699135) —
  passed the capture, broader `gpu;vulkan` batch, strict timing-result
  validation, and artifact upload.

- Closed 2026-07-13: [`BUG-067` — JobService completion state lost-update race](../../archive/BUG-067-jobservice-completion-state-lost-update-race.md).
  Production fix `ce1f590c` stores `AwaitingGate` under the completion-queue
  lock before insertion. A real-service condition-variable interlock now forces
  the drain/worker-return window without sleeps; restoring the old ordering
  deterministically reproduces the terminal-state clobber and phantom stats,
  while the fixed path passes 100 repetitions and the 3,679-test default CPU
  gate.

- Closed 2026-07-13: [`BUG-073` — Object-space normal bake may be sampled before its GPU write completes](../../archive/BUG-073-object-space-normal-bake-read-before-gpu-write.md).
  Ready-frame accounting now has a named `issueFrame + FramesInFlight`
  contract. A deterministic three-frame-in-flight regression proves the cache
  remains uploading and the material remains unbound through issue+1/+2, then
  promotes and binds at issue+3. The existing graphics Vulkan bake/readback
  passed on an RTX 3050; runtime `Operational` closure remains `RUNTIME-129`.

- Closed 2026-07-13: [`BUG-071` — Sim-systems registered during OnResolve bypass FinalizeForBoot](../../archive/BUG-071-onresolve-sim-systems-bypass-finalizeforboot.md).
  The module schedule now remains mutable through every resolve callback and
  finalizes the complete register-plus-resolve contribution set before boot
  returns. A real-engine cross-phase duplicate regression fails five of five
  times on the exact pre-fix parent and passes on the production fix, while the
  existing schedule contracts pin cycle and unprovided-signal rejection.

- Closed 2026-07-13: [`BUG-076` — AsyncWorkService::ShutdownAndDrain does not drain the derived job registry](../../archive/BUG-076-asyncworkservice-shutdown-skips-derived-job-registry.md).
  Shutdown now joins executor work, drains and applies ready derived results,
  then cancels every non-terminal survivor. Ready and unreadied readback
  regressions each pass 100 repetitions under ASan/UBSan, and the complete CPU
  gate passes.

- Closed 2026-07-13: [`BUG-075` — A world can be made active while its destroy is pending](../../archive/BUG-075-worldregistry-activate-while-destroy-pending.md).
  Destruction now wins over activation: direct activation rejects pending or
  announced destruction, and Maintenance drops a queued activation whose target
  stopped being live. Two deferred-ordering regressions and the complete CPU
  gate pass under ASan/UBSan.

- Closed 2026-07-13: [`BUG-068` — AssetModelSceneHandoff not rebound on active-world change](../../archive/BUG-068-asset-scene-handoff-not-rebound-on-active-world-change.md).
  Active-world maintenance now rebuilds asset/import scene borrowers during
  the switch pass, before the previous registry can retire. A real-Engine ASan
  regression failed with the rebind removed, then passed 50 repetitions and
  the complete CPU gate after restoration.

- Closed 2026-07-13: [`BUG-079` — CoreTasks abandoned wait continuation leaks coroutine frame](../../archive/BUG-079-coretasks-abandoned-wait-continuation-leak.md).
  Wait-token release and scheduler shutdown now transfer parked single-use
  continuation handles under the wait mutex and destroy their frames after
  unlocking. Deterministic destructor sentinels failed before the fix, then
  passed 100 repetitions each under ASan/UBSan and the complete CPU gate.

- Closed 2026-07-10: [`BUG-080` — UV-atlas promotion smoke flakes on one-sided scheduler stalls](../../archive/BUG-080-uv-atlas-promotion-smoke-timing-flake.md).
  The promotion gate now times five alternating fast-staged/xatlas pairs per
  fixture and gates on their median ratio while retaining every raw sample.
  Twenty-five loaded-host runs, strict result validation, and the complete
  default CPU gate pass with the stable benchmark ID and 1.0/1.25 thresholds
  unchanged.

- Closed 2026-07-10: [`BUG-070` — RuntimeModule schedule dropped BUG-066 fail-closed guards](../../archive/BUG-070-runtime-module-schedule-failclosed-guards-regressed.md).
  Schedule finalization again returns deterministic errors for duplicate
  identities, cycles, and unprovided signals. Direct contracts pin each error,
  and a real-engine death test proves invalid duplicates terminate boot before
  any fixed-step pass can execute.

- Closed 2026-07-10: [`BUG-072` — Declarative sim-system signal fields create no per-tick FrameGraph edge](../../archive/BUG-072-declarative-sim-signal-fields-no-per-tick-edge.md).
  `WaitForSignals` and `SignalLabels` now materialize as named edges in every
  fixed-step `FrameGraph`. A direct schedule regression registers the consumer
  first, enables parallel execution on both systems, and proves the compiled
  graph and execution still order producer before consumer.

- Closed 2026-07-10: [`BUG-069` — RuntimeModule sim-systems scheduled before the baseline ECS bundle](../../archive/BUG-069-runtime-module-systems-scheduled-before-ecs-bundle.md).
  Runtime now registers the promoted ECS bundle before module sim systems,
  accepts its external signal labels at boot, and materializes declarative
  waits per tick. A real-engine regression proves a module waiting on
  `TransformUpdate` observes the current substep's `WorldMatrix`. `BUG-072`
  subsequently closed the durable signal-unification and parallel-pass audit.

- Closed 2026-07-10: [`BUG-077` — Architecture backlog index links retired ARCH tasks](../../archive/BUG-077-architecture-backlog-index-links-retired-arch-tasks.md).
  Commit `09183ea1` promoted the `Retired seam tasks` lead-in to a recognized
  history heading, so all seven `ARCH-007`..`ARCH-013` links remain available
  without being classified as active backlog. The strict task-state-link
  validator passes with zero findings.

- Closed 2026-07-10: [`BUG-078` — CoreTasks CounterEvent rearm can race coroutine destruction](../../archive/BUG-078-coretasks-counterevent-rearm-uaf.md).
  Detached task frames now self-destroy at final suspend, and scheduler workers
  never inspect or destroy a handle after `resume()` returns. A deterministic
  unit regression forces another worker to resume and destroy the frame before
  the original `await_suspend()` unwinds; its destructor sentinel passed 100
  repetitions under the sanitizer-enabled `ci` preset.

- Closed 2026-07-09: [`BUG-063` — Streaming-import contract tests flaky on main](../../archive/BUG-063-streaming-import-contract-tests-flaky-on-main.md).
  Three parallel format-coverage CTest processes shared
  `assetio004_triangle.bin`; the fast representative test could remove it while
  the two streaming decoders still needed it, producing terminal
  `AssetDecodeFailed` results. Each fixture now owns a distinct matching glTF
  buffer URI/path; the exact three-process repro and broader repeated contract
  coverage pass.

- Closed 2026-07-09: [`BUG-066` — RuntimeModule system order followed module registration order](../../archive/BUG-066-runtime-module-system-registration-order.md).
  `ModuleRegistrationSink` now canonicalizes unique system pass names under
  explicit named signal edges before appending passes to the sequential-hazard
  FrameGraph; duplicate names and signal cycles fail closed. The reversed
  registration regressions pass, and the default CPU gate passed 3635/3635.

- Closed 2026-07-08: [`BUG-062` — Warm-configure CI budget flakes on shared-runner variance](../../archive/BUG-062-warm-configure-budget-flaky-runner-variance.md).
  The 10 s warm-cache configure budget was calibrated at the runner median and
  killed five merge-gating workflows across three PR #1010 heads (including a
  markdown-only diff) before any build step ran. Budget raised to 20 s in all
  seven invocations across six workflows; guard semantics and telemetry
  unchanged. Three consecutive PR #1010 CI rounds completed every configure
  step with zero budget kills.

- Closed 2026-07-06: [`BUG-056` — ExtrinsicSandbox default Vulkan validation gate fallback](../../archive/BUG-056-extrinsic-sandbox-default-vulkan-validation-gate.md). The default deferred GBuffer fragment now consumes the full `default_debug_surface.vert` interface and resolves visualization color from the shared config stream, eliminating the SPIR-V interface warnings that blocked promoted Vulkan readiness. The frame-pacing report now records final `IDevice::IsOperational()`, and the validator fails shader-interface warnings or a non-operational `BarrierValidationFailed` result while still allowing documented environment capability skips. The selected promoted Vulkan sandbox/ImGui/frame-pacing envelope passes 18/18.

- Closed 2026-07-04: [`BUG-055` — TaskGraph::Execute / CounterEvent latch-destruction race](../../archive/BUG-055-taskgraph-counterevent-latch-destruction-race.md). Parallel `TaskGraph::Execute()` now keeps completion state alive through shared ownership by the caller and dispatched worker closures, stores completion callbacks on that state instead of stack captures, and hardens `CounterEvent::Signal()` so publishing zero is its last event-member access. The focused `CoreTaskGraph` repeat gate passed 50/50 under the sanitizer-enabled `ci` preset, and the default CPU gate passed 3476/3476.

- Closed 2026-07-02: [`BUG-054` — Sandbox window close shutdown ordering](../../archive/BUG-054-sandbox-window-close-shutdown.md). Sandbox window close requests now emit an `[INFO]` runtime breadcrumb, stop `Engine::Run()`, and keep runtime-owned K-Means GPU job resources alive until after the shutdown device-idle wait before renderer/device teardown.

- Closed 2026-07-02: [`BUG-053` — Sandbox K-Means GPU backend queue](../../archive/BUG-053-sandbox-kmeans-gpu-backend-queue.md). Sandbox Vulkan K-Means requests now submit to a runtime-owned frame-driven GPU queue instead of the synchronous CPU fallback seam; completions publish the same label/color properties as CPU K-Means while device-unavailable cases still report honest fallback telemetry.

- Closed 2026-07-02: [`BUG-052` — Sandbox selection and visualization regressions](../../archive/BUG-052-sandbox-selection-visualization-regressions.md). Selection outline frames now avoid primitive picking/readback work unless a click-pick request is pending, visualization override materials stay lit by default so normals continue shading scalar/label colors, and runtime auto property-buffer extraction covers mesh, graph, and point-cloud scalar/color domains with fail-closed diagnostics.

- Closed 2026-06-24: [`BUG-046` — Flaky `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering`](../../archive/BUG-046-flaky-coretaskgraph-mainthread-ready-queue-ordering.md). `TaskGraph::Execute()` now batches simultaneously-ready main-thread successors under one ready-queue lock before the executor can drain them, so priority/cost ordering is applied to the full batch. The regression no longer relies on the fixed `40ms` `WorkerBlocker` sleep, preserved the `[HighHeavyMain, HighMain, LowMain]` assertions, passed 50/50 under `--repeat until-fail`, and the default CPU-supported gate passed 3024/3024.

- Closed 2026-06-21 (retired from backlog 2026-06-22): [`BUG-049` — GpuWorld geometry rebind lacks upload-to-read barriers](../../archive/BUG-049-gpuworld-geometry-rebind-upload-barriers.md). `GpuWorld` now tracks one-shot pending upload barriers for direct buffer writes, renderer drains them before consumers, and focused geometry-rebind plus dirty-extraction coverage passed during the 2026-06-22 backlog audit.

- Closed 2026-06-21 (retired from backlog 2026-06-22): [`BUG-048` — Direct mesh post-process overwrites recomputed normals](../../archive/BUG-048-direct-mesh-postprocess-overwrites-recomputed-normals.md). Direct mesh post-process apply now preserves count-matched current `v:normal` values so editor-authored normals survive deferred materialization, with focused sandbox editor regressions passing during the 2026-06-22 backlog audit.

- Closed 2026-06-21 (retired from backlog 2026-06-22): [`BUG-047` — Surface normal texture overrides vertex-normal shading](../../archive/BUG-047-surface-normal-texture-overrides-vertex-normals.md). Promoted surface shader contracts now use packed vertex normals for current shading and assert absence of `mat.NormalID` / `normalTex` sampling; focused renderer lifecycle coverage passed during the 2026-06-22 backlog audit.

- Closed 2026-06-22: [`BUG-051` — Mesh color visualization lacks automatic property-buffer extraction](../../archive/BUG-051-mesh-color-visualization-property-buffer.md). Runtime extraction now auto-emits mesh `glm::vec4` color property-buffer packets from mesh `GeometrySources` for per-element color-buffer visualizations, and graphics sync forwards the selected vertex/face/edge domain into `GpuEntityConfig::VisDomain`.

- Closed 2026-06-22: [`BUG-050` — Direct mesh first upload lacks computed normals](../../archive/BUG-050-direct-mesh-first-upload-normals.md). Geometry-only runtime mesh materialization now publishes explicit or area-weighted fallback `v:normal` data before the first ECS/render extraction upload for direct mesh imports and progressive raw model-scene primitives.

- Closed 2026-06-16: [`BUG-044` — Runtime mesh import blocks on derived post-processing](../../archive/BUG-044-runtime-import-postprocess-queue.md). Direct mesh import now publishes decoded raw geometry before derived missing-normal, UV-atlas, and generated-texture work. The derived work runs through `Runtime.StreamingExecutor`, applies back to the same entity, stamps geometry dirty tags, and registers the generated normal material binding after the deferred result is ready.

- Closed 2026-06-14: [`BUG-043` — Dropped OBJ without UVs loads but is invisible](../../archive/BUG-043-dropped-obj-missing-uvs-invisible.md). Runtime mesh materialization now preserves authored `v:texcoord`; after `ASSETIO-008`, missing or invalid source UVs are replaced by generated xatlas-backed atlas UVs before ECS population and generated texture bakes. Direct OBJ imports without `vt` lines upload surface geometry under CPU/null extraction instead of reporting `MeshGeometryMissingTexcoords`.

- Closed 2026-06-12: [`BUG-041` — Asset mesh vertex normals are lost during runtime materialization](../../archive/BUG-041-asset-mesh-vertex-normals.md). Runtime mesh materialization now copies explicit decoded `v:normal` vectors, computes deterministic area-weighted fallback normals when source normals are absent, and applies the shared path to direct mesh imports and model-scene primitive handoff; surface mesh U/V packing is now owned by `RUNTIME-108` and carries texture coordinates only.

- Closed 2026-06-12: [`BUG-040` — Orbit camera vertical drag sign](../../archive/BUG-040-orbit-camera-vertical-drag-sign.md). Orbit pitch drag now uses `+yDelta` in the quaternion trackball update, so mouse-up moves the camera above the target and mouse-down moves it below while keeping target centering, yaw, cross-pole rotation, focus, and other camera-controller coverage passing.

- Closed 2026-06-12: [`BUG-039` — Orbit camera rotation lock](../../archive/BUG-039-orbit-camera-rotation-lock.md). Promoted orbit now matches the legacy trackball model: seed forward/up become accumulated orientation state, drag deltas rotate around the current camera-local up/right axes, view up is no longer fixed world-up, and the focused regression proves a vertical drag can cross the pitch pole and invert camera up while existing orbit/focus/controller coverage still passes.

- Closed 2026-06-12: [`BUG-038` — Dropped file imports fail silently in the sandbox](../../archive/BUG-038-sandbox-dropped-file-diagnostics.md). Runtime now logs file-drop receipt, per-path routing/queue decisions, and shared import completion. Focused contract coverage pins a missing OBJ drop producing receipt/queue/failure logs plus a failed `RuntimeAssetImportEvent`, while existing drop/import coverage keeps valid OBJ/OFF/materialization paths covered.

- Closed 2026-06-12: [`BUG-035` — Vulkan slot-recycling smoke](../../archive/BUG-035-vulkan-slot-recycling-smoke.md). Added an opt-in `gpu;vulkan` smoke that destroys buffer/texture resources, advances the real promoted Vulkan frame loop past the retirement window, and observes the destroyed slots return through public handle reuse with bumped generations. This upgrades BUG-034's Vulkan proof to `Operational`.

- Closed 2026-06-12: [`BUG-034` — VulkanDevice ResourcePool reclamation](../../archive/BUG-034-vulkan-resource-pool-reclamation.md). `VulkanDevice` now runs resource-pool deletion processing for buffer/image/sampler/pipeline pools from the frame loop, including fail-closed `EndFrame()` exits, while keeping Vulkan-object destruction in the existing deferred deletion queue. A CPU Null-device contract pins slot reuse/generation bump behavior.

- Closed 2026-06-12: [`BUG-033` — Mesh IO untrusted header counts](../../archive/BUG-033-mesh-io-untrusted-header-counts.md). OFF/PLY import now bounds untrusted header counts against available payload before reserve, uses overflow-safe byte-count checks, rejects non-integral and negative PLY list counts, and fails closed on degenerate OFF face rows. Malformed-input regressions cover huge ASCII/binary counts and valid comment-before-row behavior.

- Closed 2026-06-12: [`BUG-032` — Triangle edge and point rendering is invisible on Vulkan](../../archive/BUG-032-triangle-edge-point-vulkan-rendering.md). The root cause was a C++/GLSL `GpuGeometryRecord` stride mismatch plus shader-side double application of vertex offsets and too-small default screen-space point size. Runtime sidecars, shader indexing, point sizing, and readback diagnostics now prove reference-triangle edge/point lanes on Vulkan.

- Closed 2026-06-12: [`BUG-031` — Benchmark smoke missing from `IntrinsicTests`](../../archive/BUG-031-benchmark-smoke-not-in-intrinsictests-aggregate.md). The current tree registers `IntrinsicBenchmarkSmoke` through the shared aggregate target path; `cmake --build --preset ci --target IntrinsicTests` now builds the smoke runner and the focused benchmark CTest pair passes.

- Closed 2026-06-12: [`BUG-030` — Headless `Engine::Run()` tests red-gate](../../archive/BUG-030-headless-engine-run-tests-red-gate.md). Guarded live-window `Engine::Run()` tests with the house `ShouldClose() -> GTEST_SKIP()` pattern and documented the rule in `tests/README.md`; the broader headless execution follow-up is retired by [`RUNTIME-107`](../../archive/RUNTIME-107-headless-engine-loop-coverage.md).

- Closed 2026-06-12: [`BUG-029` — Ray/AABB slab NaN poisoning](../../archive/BUG-029-ray-aabb-slab-nan-poisoning.md). Ray/AABB overlap and raycast now use NaN-free slab intervals for axis-parallel/on-boundary rays, `RayCast(Ray, Sphere)` uses a deterministic finite center-origin fallback normal, and BVH/query regressions pin boundary-coincident traversal.

- Closed 2026-06-11: [`BUG-028` — Mesh primitive view UI toggles do not render](../../archive/BUG-028-mesh-primitive-view-ui-rendering.md). The promoted mesh primitive-view implementation is runtime sidecar state, not legacy ECS `MeshEdgeView` / `MeshVertexView` components. BUG-028 fixed the sidecar mechanics through the then-current `MeshPrimitiveViewSettings` command path: vertex mode/radius, halfedge-derived wireframe when explicit edge rows are absent, retained point `GpuEntityConfig::PointMode` / `PointSize`, and flat-circle / impostor-sphere / normal-aligned surfel shader modes. RUNTIME-106 later made `RenderEdges` / `RenderPoints` component presence the authoritative toggle while retaining the same sidecar implementation. Focused CPU/null coverage proves UI command routing, edge/vertex sidecar extraction, imported OBJ mesh primitive views, point config propagation, and shader compilation.

- Closed 2026-06-11: [`BUG-026B` — Vulkan click-pick readback smoke (entity id + depth round trip)](../../archive/BUG-026B-vulkan-click-pick-readback-smoke.md). The opt-in `gpu;vulkan` smoke `ClickPickReadbackSelectsReferenceTriangleAndBackgroundClears` waits for the promoted Vulkan device to become operational, submits a real center-pixel click pick against `ReferenceTriangle`, verifies the GPU readback selects the entity and refines a mesh face with depth-derived cursor data on the triangle plane, then submits a far-background click and verifies the no-hit clear. Passed on NVIDIA RTX 3050 / driver 590.48.01, upgrading the BUG-026 fix to `Operational`.

- Closed 2026-06-11: [`BUG-027` — Sandbox drag/drop, close, and mesh primitive-view regression](../../archive/BUG-027-sandbox-dragdrop-close-mesh-views.md). Platform `WindowCloseEvent` now requests engine exit, `Engine::RunFrame()` aborts immediately after `PollEvents()` observes close before entering ImGui/render work, and standalone geometry materialization records/selects the imported entity after direct and dropped imports. Drag/drop platform-event regression coverage imports OBJ and OFF meshes through the runtime event handler, verifies the imported mesh becomes the active selection, drives the promoted mesh primitive-view command surface, and proves edge/vertex view uploads through `RenderExtractionCache`; frame-loop/layering regressions pin the live close-button timing.

- Closed 2026-06-10: [`BUG-026` — Viewport click selection dead: render-id zero collision, UINT clear punning, and missing depth readback](../../archive/BUG-026-click-pick-readback-entity-zero-and-depth.md). Clicking selected nothing because the raw-entt render id of the first registry entity (the default triangle) collided with the `EntityId == 0` background sentinel, and the light-blue float clear bit-punned the R32_UINT background into a phantom-hit garbage id. Render ids are now `entt handle + 1` (0 reserved, owned by `StableEntityLookup::ToRenderId`), the ID targets clear to zero with format-aware Vulkan clear conversion, and the picking readback gained the designed `SceneDepth` sample: 16-byte slots, per-`Sequence` camera context replay, `UnprojectPickDepth` world/local cursor reconstruction, and depth-anchored closest face/edge/vertex (mesh), edge/node (graph), and point (cloud) refinement. `Operational` Vulkan click smoke owned by `BUG-026B`.

- Closed 2026-06-10: [`BUG-025` — Geometry contact manifold normals violate the documented A→B convention](../../archive/BUG-025-contact-manifold-normal-convention.md). `EPA_Solver` negated the A−B polytope's closest-face outward normal (already the A→B direction) and the sphere-AABB analytic path computed B→A normals in both branches; both now honor the documented convention with per-pair `ContactManifold.Convention_*` unit tests across analytic, reversed-dispatch, and GJK/EPA fallback paths in both argument orders. The physics-layer orientation guard stays as defense in depth.

- Closed 2026-06-10: [`BUG-024B` — Vulkan pixel-shift smoke for sandbox transform edits](../../archive/BUG-024B-sandbox-transform-edit-vulkan-pixel-shift-smoke.md). The opt-in `gpu;vulkan` smoke `InspectorTransformEditShiftsReferenceTrianglePixels` applies the promoted Inspector transform-edit command mid-run (after the fixed-step phase) and proves the rendered `ReferenceTriangle` moves: the frame center returns to background and the projected shifted sample contains the triangle. Passed on NVIDIA RTX 3050 / driver 590.48.01 (suite 6/6), upgrading the BUG-024 fix to `Operational`.

- Closed 2026-06-10: [`BUG-024` — Sandbox transform UI edits do not move rendered triangle](../../archive/BUG-024-sandbox-transform-edit-rendering.md). Post-fixed-step Inspector/gizmo transform edits were never flushed through `TransformHierarchy`/`BoundsPropagation`/`RenderSync` before render extraction, so the snapshot model matrix stayed stale. `Engine::RunFrame()` now runs the runtime-owned `FlushPreRenderTransformState` after the variable tick, ImGui editor hook, and gizmo drive — before gizmo packet build and extraction — so UI edits reach the rendered model matrix in the same frame. Closed at `CPUContracted`; upgraded to `Operational` by `BUG-024B`.

- Closed 2026-06-08: [`BUG-022` — Sandbox reference triangle camera frustum visibility](../../archive/BUG-022-sandbox-reference-triangle-camera-frustum-visibility.md). The reference camera seed and all controller modes now prove the triangle vertices are inside clip space, and promoted triangle-list/backface-culling pipelines use clockwise front-face winding to match the Vulkan Y-flipped camera projection so the centered triangle is not culled.

- Closed 2026-06-08: [`BUG-021` — Sandbox camera scene-table shader wiring](../../archive/BUG-021-sandbox-camera-scene-table-shader-wiring.md). The promoted runtime camera remains controller-backed rather than an ECS camera entity; the renderer now publishes the extracted camera into `GpuSceneTable` before `GpuWorld::SyncFrame()`, active BDA vertex shaders transform through `scene.CameraViewProj`, and focused CPU plus Vulkan smoke coverage proves default triangle/readback/sandbox visibility.

- Closed 2026-06-08: [`BUG-020` — Sandbox reference triangle camera modes](../../archive/BUG-020-sandbox-reference-triangle-camera-modes.md). The default `ReferenceTriangle` now round-trips through the promoted scene-document seam as a mesh-domain authored renderable with stable/selectable identity and white `VisualizationConfig`, and top-down camera seeding now uses the seed focus point so orbit, fly, free-look, and top-down modes keep the triangle centered.

- Closed 2026-06-07: [`BUG-019` — Sandbox selection, camera, and outline regressions](../../archive/BUG-019-sandbox-selection-camera-outline-regressions.md). Selection outline now samples `EntityId` from dedicated frame-sampled descriptor slot 3, promoted Vulkan real bindless texture leases start at slot 4, camera controls accept right- or middle-mouse rotation with visible UI help, and the sandbox selection acceptance test covers the runtime input-to-pick bridge without depending on a concrete platform backend.

- Closed 2026-06-07: [`BUG-018` — Sandbox hierarchy selection Vulkan ID target validation](../../archive/BUG-018-sandbox-hierarchy-selection-vulkan-id-targets.md). Hierarchy selection now keeps the selection-ID producer active without importing picking readback, marks readback-enabled ID targets as transfer sources, and binds `EntityId` explicitly for the outline shader so promoted Vulkan records a visible selected frame without `EntityId` / `PrimitiveId` validation errors.

- Closed 2026-06-07: [`BUG-017` — Sandbox selection click and outline black frame](../../archive/BUG-017-sandbox-selection-click-and-outline-black-frame.md). Viewport left-clicks now submit runtime selection pick requests unless ImGui or a gizmo owns the click, and `SelectionOutlinePass` alpha-blends into the current present source instead of replacing it with an outline-only texture.

- Closed 2026-06-06: [`BUG-016` — ExtrinsicSandbox operational frame reads back black](../../archive/BUG-016-extrinsic-sandbox-operational-frame-black-readback.md). The black readback was caused by two output-stage defects: frame-sampled bridge slot 0 was overwritten by late barrier/ImGui descriptor writes before submission, making the tonemap sample the wrong image, and recipe clear colors were dropped during framegraph compilation. The renderer now owns explicit per-pass frame-sampled bindings, barriers no longer auto-bind slot 0, ImGui no longer clobbers the shared bridge slot, and compiled render-pass attachments preserve the light-blue clear. Focused `gpu;vulkan` smokes passed 20/20; the default CPU gate passed 2787/2787 during retirement verification.

- Closed 2026-06-06: [`BUG-015` — ExtrinsicSandbox clustered Vulkan validation cascade](../../archive/BUG-015-extrinsic-sandbox-clustered-vulkan-validation-cascade.md). Clustered compute shaders moved to the engine BDA convention, record helpers pass buffer device addresses via push constants, `CreateBuffer` guards the validation debug-name function pointer, default-recipe clears are light blue, and graphics-only framegraph profiles collapse async/transfer ownership transfers so the QFOT validation cascade is gone. The promoted Vulkan sandbox reaches an operational frame with visible ImGui after BUG-016's downstream output fix. The remaining ccache/modules vtable hardening is not part of this bug and stays tracked by `HARDEN-073`.

- Closed 2026-06-05: [`BUG-014` — ExtrinsicSandbox ImGui black window regression](../../archive/BUG-014-extrinsic-sandbox-imgui-black-window.md). The black frame was caused by a Vulkan descriptor collision: framegraph bridge slots for DebugView/Present overlapped real bindless texture leases, so `Pass.Present` could overwrite the retained ImGui font-atlas slot. The promoted Vulkan bindless allocator now reserves framegraph bridge slots before real texture leases; BUG-019 expands the reserved range to slots 0..3 and starts real leases at slot 4. The app-default `gpu;vulkan` regression asserts recorded `Present`/`ImGuiPass` plus non-black backbuffer readback with validation enabled.

- Closed 2026-05-29: [`BUG-013` — Default-recipe + minimal-debug backbuffer readback contract tests SEGV under clang-20 modules](../../archive/BUG-013-backbuffer-readback-contract-vtable-segv.md). **Not reproducible on a clean `ci` preset build.** In a freshly-cloned tree the two `ConfiguredHandleRecordsReadbackTripletOnce` cases pass through the default CPU gate (CTest #25/#87, label `contract`; the full `IntrinsicGraphicsContractCpuTests` binary is 225/225). The single module-owned `ICommandContext` vtable shows no cross-TU divergence, and the exact crash site (`CopyTextureToBuffer` dispatched through a base `ICommandContext&` to a non-overriding `MockCommandContext`) executes correctly. The reported SEGV was a stale incremental module-BMI artifact after `cc06edef` added the inline-bodied `BindFrameSampledTexture` virtual — a known clang-20 / C++23-modules hazard that a clean preset rebuild (the authoritative verification per AGENTS.md §7) eliminates. Prevention documented in `src/graphics/rhi/README.md`; no engine/test source changed. Unblocks `GRAPHICS-076E` CPU contract closure.

- Closed 2026-05-28 (record retired 2026-06-06): [`BUG-012` — Default-recipe `vkCmdPipelineBarrier2` SEGV in NVIDIA driver](../../archive/BUG-012-default-recipe-vkcmdpipelinebarrier2-segv-nvidia.md). The default-recipe Vulkan command-stream blocker was resolved under GRAPHICS-076's Slice D graduation on 2026-05-28: synthetic framegraph transient handles were replaced with real per-frame RHI allocations before barrier submission, attachment access scopes are preserved through RHI/Vulkan Sync2 translation, staging uploads moved to dedicated one-shot command buffers, `drawIndirectCount` was enabled, and default-recipe draw passes declare dynamic-rendering attachments. CPU-visible contract coverage (`FrameRecipeContract.DefaultRecipeDoesNotDepthTransitionColorResources`, `…DrawPassesDeclareRenderPassAttachments`, `RHICommandContext.MemoryAccessCombinesAttachmentBitsWithoutTruncation`) pins the barrier classes. On the NVIDIA RTX 3050 / driver 590.48.01 host the GPU smoke gate passed 4/4 and `IsOperational()` flips true within the first frame. The resolved task file lingered in `tasks/backlog/bugs/`; this entry records its retirement to `tasks/done/`.

- Closed 2026-05-17: [`BUG-011` — `docs-validation` rejects `ci-vulkan.yml` as an unexpected workflow file](../../archive/BUG-011-ci-vulkan-workflow-allowlist.md). `tools/ci/check_workflow_names.py::ALLOWED_WORKFLOW_FILES` now includes `ci-vulkan.yml` (mirroring the `nightly-deep.yml` allowed-but-not-required precedent), and `docs/migration/target-repo-layout.md` lists the file in the canonical `.github/workflows/` layout. The GRAPHICS-080-introduced workflow now passes the `ci-docs` row's "Validate workflow file naming policy" step under both default and `--strict` modes.
- Closed 2026-05-14: [`BUG-010` — Minimal recipe present-pass barrier acceptance asserts wrong layout transition](../../archive/BUG-010-minimal-recipe-present-barrier-contract.md). The acceptance test now scans for the first backbuffer barrier with `After == Present` and asserts the canonical `Undefined -> Present` shape, matching the framegraph's imported-backbuffer policy.
- Closed 2026-05-14: [`BUG-009` — Minimal recipe surface pass executes when culling output is unavailable](../../archive/BUG-009-minimal-recipe-surface-pass-culling-prerequisite.md). `RecordMinimalDebugSurfacePass` now also gates on `m_CullingOutputAvailable` so a failed culling-pipeline create skips the recipe's `DrawIndexedIndirectCount` rather than recording against bucket buffers the culling dispatch never wrote.
- Closed 2026-05-14: [`BUG-008` — Vulkan `:Device` partition cannot name `VulkanOperationalInputs` under clang-20](../../archive/BUG-008-vulkan-device-partition-operational-types.md). The operational-status surface is extracted into a `:OperationalStatus` partition that the umbrella and `:Device` partition both re-export; `EvaluateVulkanDeviceOperationalStatus` is a friend of `VulkanDevice` so it can read the private gate inputs without widening `IDevice`. `ExtrinsicBackendsVulkan` and `IntrinsicGraphicsVulkanContractTests` build cleanly under clang-20.
- Closed 2026-05-13: [`BUG-007` — GpuAssetCache uploads remain pending in default CPU gate](../../archive/BUG-007-gpu-asset-cache-default-gate-failures.md). `RHI::ITransferQueue::UploadTextureFullChain(...)` now remains appended after the original upload/poll/collect virtuals, preserving the `IsComplete()` slot used by existing module consumers; the focused `GpuAssetCache`/material-system repro and default CPU CTest gate pass.
- Closed 2026-05-09: [`BUG-002` — CI full build compiles ImGuizmo upstream target without ImGui includes](../../archive/BUG-002-ci-full-build-imguizmo-upstream-target.md). ImGuizmo is populated as source-only and repository consumers use `imguizmo_lib` with the ImGui dependency wired explicitly.
- Closed 2026-05-09: [`BUG-003` — FetchContent cache corruption breaks dependency checkouts during CI retries](../../archive/BUG-003-fetchcontent-cache-corrupts-shared-dependency-checkouts.md). Dependency source trees are validated before reuse and incomplete online caches are removed before repopulation.
- Closed 2026-05-09: [`BUG-004` — Compile-hotspot gate baseline references stale runtime source paths](../../archive/BUG-004-compile-hotspot-baseline-stale-runtime-paths.md). The current baseline uses promoted `src/geometry/` paths only; the retired `src/legacy/` target was removed during the 2026-07-01 legacy sweep.
- Closed 2026-05-09: [`BUG-005` — CI dependent steps report missing artifacts as primary failures](../../archive/BUG-005-ci-dependent-steps-report-missing-artifacts-as-primary-failures.md). CI dependent steps now run explicit prerequisite guards and benchmark validation reports missing result roots as blocked prerequisites.
- Closed 2026-05-09: [`BUG-006` — Mesh-backed graph views abort ShortestPath tests on connectivity type collision](../../archive/BUG-006-shortest-path-mesh-backed-graph-connectivity-view.md). Mesh-backed graph view construction now uses the correct property-set order and graph-specific compatibility connectivity until `GEOM-003` performs the semantic split.
- Closed: the older `BuildDefaultPipelineRecipe(...)` link-failure note is stale in the current tree. The symbol is declared in `Graphics.Pipelines.cppm`, defined in `Graphics.Pipelines.cpp`, and referenced by the runtime graphics tests. Full local link verification is currently blocked in this container because CMake configure stops in GLFW dependency discovery before test targets are generated (`libxrandr` development headers missing).
- Verified: mesh vertex indices are recovered from picked local-space points via KD-tree lookup.
- Verified: mesh edge/face, graph node/edge, and point-cloud point indices are covered by the focused picker regression suite.
- Closed: pick-domain policy now enforces mesh→surface face IDs, graph→edge IDs, and point-cloud→point IDs in `PickingPass`; GPU primitive IDs are authoritative while CPU is refinement-only in `ResolveGpuSubElementPick`.
