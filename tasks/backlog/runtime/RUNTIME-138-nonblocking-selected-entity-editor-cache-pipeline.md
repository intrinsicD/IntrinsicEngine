---
id: RUNTIME-138
theme: F
depends_on:
  - ARCH-007
  - ARCH-009
  - RUNTIME-192
  - RUNTIME-194
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
maturity_target: Operational
---
# RUNTIME-138 — Nonblocking selected-entity editor cache pipeline

## Status

- Re-gated on 2026-07-30: this task remains open pending a separate retirement
  decision, but it is no longer a static prerequisite of `RUNTIME-202`,
  `REVIEW-003`, or `UI-037`. Its previously completed checkboxes remain the
  downstream baseline; those tasks must not assume that this umbrella will add
  a broad selected-analysis module.
- If a downstream workflow or a future measurement identifies a concrete
  full-buffer selected-analysis problem, its owner should add the smallest
  feature-local cached or `JobService` derivation needed for that case, with
  generation/staleness tests, rather than blocking on this umbrella.

## Goal
- Make the Sandbox selected-entity editor path nonblocking: the main loop reads cached editor/render state, applies cheap commands, submits generation-keyed async jobs for heavy derivations, and never scans large geometry/property buffers synchronously from the ImGui editor callback.

## Non-goals
- Do not reorganize the domain-window information architecture; `UI-031` owns user-facing window/menu restructuring after this task establishes the nonblocking model.
- Do not change geometry algorithms, method outputs, scalar colormap semantics,
  texture-bake outputs, or visualization recipe semantics.
- Do not move live ECS registry access, renderer ownership, asset ownership, or platform/window ownership into the UI layer.
- Do not use the fixed-step `Core::FrameGraph` for optional inspector analysis; it remains frame-critical ECS/system work that blocks the current frame by design.
- Do not claim a responsiveness improvement without before/after diagnostics from the selected-entity path.

## Context
- Owning subsystem/layer: `runtime`, as an implementation-owned selected
  analysis cache that publishes pointer-free copied snapshots through the
  relevant feature/scene owners. App-owned editor code consumes cached models
  but owns no live runtime state; `RUNTIME-202` later removes the transitional
  Sandbox facade.
- Current selected-entity path does actual work inside `ImGuiAdapter::EndFrame()`: broad panel/domain models are rebuilt, property catalogs are enumerated, normal/color candidate bindings allocate scratch buffers and scan all selected vertices/properties, UV diagnostics scan texcoords, and open domain windows duplicate selected-model work.
- Desired frame model: poll input, read cached UI/editor state, enqueue commands/job requests, apply only cheap frame-critical changes, extract/render from committed state, then drain/apply bounded async completions and launch background work.
- The ECS registry is single-threaded. Async workers must operate on generation-stamped immutable snapshots captured on the main thread, and main-thread apply must discard stale results.
- `RUNTIME-194` makes `JobService` the sole persistent work lifecycle. The
  remaining slices must use that service for immutable snapshot execution,
  generation-aware bounded apply, dependencies, cancellation, and
  previous-output retention; no selected-editor scheduler or registry is
  allowed.
- ARCH-013 re-review (2026-07-08): Decision confirmed with a re-scope note.
  The `ARCH-007` and `ARCH-009` gates held until `CommandBus` and `JobService`
  retired. Remaining slices must express "enqueue request, snapshot on the
  main thread, apply bounded current results" through
  `CommandBus`/`JobService` semantics. The temporary derived-job/streaming
  implementation is migrated and removed by `RUNTIME-194`; no new
  selected-editor bespoke queue or immediate dispatcher trigger path is
  allowed.

## Control surfaces
- Config: optional diagnostic/config knobs for editor timing capture and async apply budget may be added if needed, but defaults must keep the sandbox responsive without user configuration.
- UI: selected-entity inspector/domain windows display cached/pending/ready/stale/failed states and issue explicit commands/job requests.
- Agent/CLI: optional frame-pacing capture command is allowed if it reuses existing runtime diagnostics/control surfaces.

## Backends
- Backend axis: CPU/runtime asynchronous jobs for selected-entity analysis.
  GPU/Vulkan proof is only for end-to-end responsiveness smoke; GPU compute
  derivations are out of scope unless routed through the canonical
  JobService/readback seams.

## Required changes
- [ ] Slice A: add low-overhead selected-entity timing/counter diagnostics for editor callback time, panel/model build time, inspector/property catalog time, vertex-channel validation time, UV diagnostics time, visualization model time, ImGui copy/upload counts, scanned element counts, scratch allocation bytes, cache hit/miss counts, queued job counts, stale-discard counts, and bounded apply time.
- [x] Slice A partial: expose deterministic selected-frame counters for full vertex-channel resolver scans and their scratch-buffer allocation bytes.
- [x] Slice A partial: expose deterministic selected-frame counters for UV texcoord finite-check element scans and texture-bake source-row enumeration.
- [x] Slice A partial: expose scalar visualization adapter value-scan counters for scalar/isoline finite and range validation, folded into render-extraction frame stats.
- [x] Slice A partial: expose nanosecond selected model-build timing diagnostics for panel frame, inspector, selected analysis, property catalog, vertex-channel validation, UV diagnostics, texture-bake, visualization, and domain-window construction.
- [x] Slice A partial: mirror ImGui editor-frame draw-list counts, font-atlas copy/reuse counters, user-texture state, and copied font/vertex/index/command payload bytes into runtime frame-pacing diagnostics.
- [x] Slice A partial: expose aggregate derived-job queue status, stale-discard, and main-thread apply timing/delta diagnostics through `DerivedJobQueueSnapshot`.
- [x] Slice A: add a deterministic capture path or test seam that records selected-entity frame samples without requiring Vulkan, and document the Vulkan-host smoke procedure for the real sandbox.
- [x] Slice B: make editor model construction visibility-gated so hidden panels and closed domain windows do not build selected-entity models; build only the visible model section requested by the current ImGui window/section.
- [x] Slice B: prevent open domain windows from rebuilding shared selected-entity models independently; either share one per-frame cached model view or request section-specific cached submodels.
- [x] Slice C partial: add a selected-entity editor model cache for steady selected inspector analysis and visualization models, keyed by stable selected ids, selection generation, refined-primitive generation for primitive-sensitive analysis, geometry domain/count shape, vertex-channel binding generation, geometry source/property metadata signature, selected render-lane hint signature for bound-state rows, progressive presentation binding generation for selected analysis, command-history revision, viewport, visualization target, visualization command availability, effective visualization config/spatial-debug signature for visualization model entries, and visualization adapter binding revision for visualization model entries.
- [x] Slice C partial: partition selected-analysis cache entries by visible inspector/domain-window consumer and include that visible consumer in the selected-model cache key.
- [ ] Slice C: extend the selected-entity editor model cache with remaining entity/source value/non-vertex binding generations and viewport/config revision where relevant.
- [ ] Slice C: ensure cache-hit frames reuse immutable model data and perform no selected geometry/property scans or scratch-buffer allocations.
- [ ] Slice D: split cheap metadata queries from heavy derivations. Reuse the
      `RUNTIME-192` `GeometryPropertyCatalogSnapshot` for
      names/domains/counts/value kinds; full normal/color resolver scans,
      scalar domain scans, color packing validation, and UV finite checks must
      not run for every candidate property every frame.
- [ ] Slice E: route heavy selected-entity derivations through `JobService`
      with immutable input snapshots, generation validation, bounded
      main-thread apply, stale-result discard, previous-output retention, and
      observable pending/ready/failure state.
- [ ] Slice E: add async jobs for active normal binding validation, active color binding validation, scalar min/max/domain analysis, isoline scalar domain analysis, UV diagnostics, color-buffer pack validation, and large property preview sampling where those results are actually visible or requested.
- [ ] Slice F: move selected-entity job submission to the canonical runtime
      JobService submission phase so `ImGuiAdapter::EndFrame()` only reads
      cached state and enqueues requests.
- [x] Slice F partial: the pre-consolidation executors gained count-limited
      apply overloads and diagnostics; `RUNTIME-194` must preserve those
      contracts while migrating them to `JobService`.
- [ ] Slice F: add an explicit per-frame apply budget for editor/derived completions, by count or elapsed time, so a burst of completed jobs cannot stall the main loop.
- [ ] Slice F: keep transform/gizmo/selection commands that are required for same-frame render as cheap main-thread commands, and document which work remains frame-critical.
- [x] File or update focused graphics follow-ups for selected outline GPU work and ImGui overlay copy/upload churn rather than expanding this runtime task into renderer implementation work.

## Tests
- [x] Add contract tests proving hidden inspector/domain sections do not build selected-entity property, progressive, texture-bake, or visualization models.
- [x] Add contract tests proving steady selected cache-hit frames skip property-catalog, vertex-channel target, progressive, bound-state, UV diagnostics, texture-bake, and visualization model builders, and proving selection-generation, refined-primitive generation, geometry source/property metadata signature, selected render-lane hint signature, progressive presentation binding generation, effective visualization config/spatial-debug signature, and visualization adapter binding revision changes invalidate matching same-entity cache entries.
- [x] Add contract tests proving inspector and visible domain-window selected-analysis cache entries are partitioned by window/consumer key and cached domain-window hits skip selected-analysis rebuilds.
- [x] Add contract tests proving cache-hit selected frames do not rebuild property catalogs, do not call full vertex-channel resolvers, and do not allocate geometry-sized scratch buffers.
- [x] Add contract tests proving cache-hit selected frames perform zero UV texcoord finite-check scans and zero texture-bake source-row enumeration.
- [x] Add contract tests proving selected model-build timing diagnostics are populated on visible cache-miss work and stay zero for hidden or cache-hit heavy submodels.
- [x] Add contract tests proving runtime frame-pacing diagnostics mirror ImGui editor-frame copy/count diagnostics on the Null backend.
- [x] Add contract tests proving derived-job queue diagnostics count queued/applying/terminal jobs and per-drain apply complete/failed/stale deltas.
- [x] Add contract/integration tests proving scalar visualization adapters and render extraction report exact scalar value-scan counts for scalar and isoline paths.
- [ ] Add contract tests proving property option listing uses metadata compatibility only, while explicit active/requested validations use async job results.
- [ ] Add tests proving async selected-analysis results apply only when the generation key is current and stale geometry/property/binding results are discarded.
- [ ] Add tests proving repeated selected frames enqueue at most one job per cache key while a matching job is pending or ready.
- [x] Historical contracts prove the pre-consolidation executors process only
      the requested number of ready completions while preserving queued work;
      `RUNTIME-194` owns their migration.
- [ ] Add a canonical `JobService` integration contract proving the runtime
      selected-analysis phase uses the count-limited apply budget.
- [ ] Add tests proving bounded main-thread apply processes a limited number/time of completions per frame.
- [x] Run the default CPU-supported correctness gate after implementation.
- [ ] On a Vulkan-capable host, run a sandbox responsiveness smoke with a large selected mesh/point cloud and record before/after selected-frame diagnostics.

## Docs
- [x] Update `src/runtime/README.md` with the factual nonblocking selected-entity editor/cache/job pipeline once implemented.
- [x] Update `tasks/backlog/runtime/README.md`, `tasks/backlog/ui/README.md`, and any touched rendering README entries so follow-up ownership stays clear.
- [ ] If a new diagnostic capture format is added, document the schema or report location.
- [ ] Add or update a short report under `docs/reports/` with before/after selected-entity bottleneck evidence and links to renderer follow-up task IDs.

## Acceptance criteria
- [ ] The Sandbox main loop selected-entity path reads cached editor state and submits commands/jobs; it does not synchronously scan large geometry/property buffers from `ImGuiAdapter::EndFrame()`.
- [x] Hidden panels and closed domain windows perform no selected-entity model work.
- [ ] Holding the same selection steady for multiple frames produces cache hits with zero full-buffer normal/color/UV/scalar scans.
- [ ] Heavy selected-entity analysis runs asynchronously from generation-stamped immutable snapshots and applies on the main thread only when current.
- [ ] Completed async result apply is bounded per frame and cannot monopolize the main loop.
- [ ] Diagnostics can distinguish editor CPU cost, cache misses, async job cost, apply cost, ImGui copy/upload cost, and renderer selected-outline cost.
- [ ] The selected-entity responsiveness smoke on a Vulkan-capable host records the path as `Operational`; default CPU/null tests cover the cache/job contracts.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|SelectedAnalysis|JobService|EditorCommandHistory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
# On a Vulkan-capable host, after instrumentation and async cache work lands:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Sandbox|ImGui|RendererFrameLifecycle' --timeout 180
```

## Forbidden changes
- Mixing mechanical UI/window restructuring with the cache/job behavior changes beyond the minimum needed to visibility-gate models.
- Adding geometry, assets, graphics, platform, or app ownership to `SandboxEditorUi`.
- Running optional selected-entity analysis in the fixed-step `Core::FrameGraph`.
- Async workers touching `entt::registry`, live ECS component storage, renderer state, asset services, or ImGui objects.
- Disabling selection outline, picking, visualization, texture baking, or property binding as a substitute for nonblocking scheduling.
- Introducing unrelated feature work.

## Maturity
- Target: `Operational` for the real sandbox selected-entity path on a Vulkan-capable host; `CPUContracted` for backend-neutral cache/job contracts.
- Renderer-specific GPU work pruning was retired by `GRAPHICS-113`.
- ImGui overlay copy/upload cleanup was retired by `GRAPHICS-114`, after
  `GRAPHICS-110` resolved in-flight upload-buffer safety.
- Current implementation state (2026-07-09): partial `CPUContracted` Slice C.
  The editor visibility-gates selected-entity model sections, shares per-frame
  domain-window models, exposes deterministic model-build/cache counters and
  nanosecond selected model-build timing diagnostics, and
  now keeps a persistent selected-model cache for steady inspector analysis,
  per-domain-window selected analysis, and visualization frames. Frame stats count full vertex-channel resolver scans
  and scratch-buffer allocation bytes, UV texcoord finite-check element scans,
  texture-bake source-row enumeration, and render-extraction scalar
  visualization adapter value scans. The selected-model cache-hit tests can
  prove the selected-model counters stay at zero. Derived-job snapshots now
  expose aggregate queue/apply diagnostics for queued/applying/terminal/stale
  counts and per-drain main-thread apply timing/result deltas. The streaming
  and derived-job apply seams now expose count-limited main-thread drain
  overloads with processed-count diagnostics, and the Engine streaming
  maintenance hook uses a count-limited drain. Selected-analysis job routing
  and editor-specific budget policy remain open; `RUNTIME-194` migrates that
  historical implementation evidence to `JobService`. The cache key includes
  stable selected ids, the
  selection controller's selected-set generation, the engine-owned
  refined-primitive generation for primitive-sensitive selected analysis, a
  runtime-computed metadata signature over selected geometry source/property
  descriptors, a selected render-lane hint signature for bound-state rows, and
  the runtime-owned progressive presentation binding generation plus the
  visible inspector/domain-window consumer for selected analysis; the
  visualization model cache key also includes the
  runtime-owned visualization adapter binding revision. Visualization model
  entries also include an effective visualization-config/spatial-debug
  signature, so direct `VisualizationConfig`, `VisualizationLaneOverrides`, or
  `SpatialDebugBinding` mutation cannot reuse stale visualization rows.
  Cache-hit/invalidation tests prove the cached frames skip the heavy
  selected-model builders listed above while same-entity reselection and
  same-entity primitive refinement, source/property metadata mutation,
  direct render-lane hint mutation, progressive presentation binding mutation,
  effective visualization config/spatial-debug mutation, or visualization
  adapter binding mutation invalidate stale entries. `RUNTIME-192` canonical
  property catalog/reference adoption, full generation stamps for
  source/property values and remaining non-vertex binding revisions, remaining
  selected-analysis scanned-element counters, async selected-analysis jobs on
  the post-`RUNTIME-194` `JobService`, editor-specific bounded apply behavior,
  renderer selected-outline cost diagnostics, and Vulkan responsiveness smoke
  remain open; this task is not ready to retire.
