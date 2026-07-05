---
id: RUNTIME-138
theme: F
depends_on: []
maturity_target: Operational
---
# RUNTIME-138 — Nonblocking selected-entity editor cache pipeline

## Goal
- Make the Sandbox selected-entity editor path nonblocking: the main loop reads cached editor/render state, applies cheap commands, submits generation-keyed async jobs for heavy derivations, and never scans large geometry/property buffers synchronously from the ImGui editor callback.

## Non-goals
- Do not reorganize the domain-window information architecture; `UI-031` owns user-facing window/menu restructuring after this task establishes the nonblocking model.
- Do not change geometry algorithms, method outputs, scalar colormap semantics, texture-bake outputs, or visualization adapter semantics.
- Do not move live ECS registry access, renderer ownership, asset ownership, or platform/window ownership into the UI layer.
- Do not use the fixed-step `Core::FrameGraph` for optional inspector analysis; it remains frame-critical ECS/system work that blocks the current frame by design.
- Do not claim a responsiveness improvement without before/after diagnostics from the selected-entity path.

## Context
- Owning subsystem/layer: `runtime`, primarily `Extrinsic.Runtime.SandboxEditorUi`, `Extrinsic.Runtime.Engine`, `Extrinsic.Runtime.DerivedJobGraph`, `Extrinsic.Runtime.StreamingExecutor`, and selected-entity command/model helpers.
- Current selected-entity path does actual work inside `ImGuiAdapter::EndFrame()`: broad panel/domain models are rebuilt, property catalogs are enumerated, normal/color candidate bindings allocate scratch buffers and scan all selected vertices/properties, UV diagnostics scan texcoords, and open domain windows duplicate selected-model work.
- Desired frame model: poll input, read cached UI/editor state, enqueue commands/job requests, apply only cheap frame-critical changes, extract/render from committed state, then drain/apply bounded async completions and launch background work.
- The ECS registry is single-threaded. Async workers must operate on generation-stamped immutable snapshots captured on the main thread, and main-thread apply must discard stale results.
- `StreamingExecutor` and `DerivedJobRegistry` already provide persistent async worker execution, generation-aware main-thread apply, dependency edges, snapshots, and previous-output retention. This task should reuse that shape instead of adding a parallel ad hoc scheduler.

## Control surfaces
- Config: optional diagnostic/config knobs for editor timing capture and async apply budget may be added if needed, but defaults must keep the sandbox responsive without user configuration.
- UI: selected-entity inspector/domain windows display cached/pending/ready/stale/failed states and issue explicit commands/job requests.
- Agent/CLI: optional frame-pacing capture command is allowed if it reuses existing runtime diagnostics/control surfaces.

## Backends
- Backend axis: CPU/runtime asynchronous jobs for selected-entity analysis. GPU/Vulkan proof is only for end-to-end responsiveness smoke; GPU compute derivations are out of scope unless routed through existing runtime derived-job/readback seams.

## Required changes
- [ ] Slice A: add low-overhead selected-entity timing/counter diagnostics for editor callback time, panel/model build time, inspector/property catalog time, vertex-channel validation time, UV diagnostics time, visualization model time, ImGui copy/upload counts, scanned element counts, scratch allocation bytes, cache hit/miss counts, queued job counts, stale-discard counts, and bounded apply time.
- [x] Slice A: add a deterministic capture path or test seam that records selected-entity frame samples without requiring Vulkan, and document the Vulkan-host smoke procedure for the real sandbox.
- [x] Slice B: make editor model construction visibility-gated so hidden panels and closed domain windows do not build selected-entity models; build only the visible model section requested by the current ImGui window/section.
- [x] Slice B: prevent open domain windows from rebuilding shared selected-entity models independently; either share one per-frame cached model view or request section-specific cached submodels.
- [x] Slice C partial: add a selected-entity editor model cache for steady selected inspector analysis and visualization models, keyed by stable selected ids, selection generation, geometry domain/count shape, vertex-channel binding generation, command-history revision, viewport, visualization target, and visualization command availability.
- [ ] Slice C: introduce a selected-entity editor model cache keyed by stable entity id, selection generation, primitive-selection generation, entity/source/property/binding/visualization generations, viewport/config revision where relevant, and the visible section/window key.
- [ ] Slice C: ensure cache-hit frames reuse immutable model data and perform no selected geometry/property scans or scratch-buffer allocations.
- [ ] Slice D: split cheap metadata queries from heavy derivations. Property catalogs may enumerate names/domains/counts/value kinds, but full normal/color resolver scans, scalar domain scans, color packing validation, and UV finite checks must not run for every candidate property every frame.
- [ ] Slice E: route heavy selected-entity derivations through `DerivedJobRegistry`/`StreamingExecutor` or a runtime editor-analysis registry with the same contract: immutable input snapshot, worker execution, generation validation, bounded main-thread apply, stale-result discard, previous-output retention, and observable pending/ready/failure state.
- [ ] Slice E: add async jobs for active normal binding validation, active color binding validation, scalar min/max/domain analysis, isoline scalar domain analysis, UV diagnostics, color-buffer pack validation, and large property preview sampling where those results are actually visible or requested.
- [ ] Slice F: move selected-entity job submission to a runtime frame-work seam (`IStreamingFrameHooks::SubmitFrameWork()` or equivalent) so `ImGuiAdapter::EndFrame()` only reads cached state and enqueues requests.
- [ ] Slice F: add an explicit per-frame apply budget for editor/derived completions, by count or elapsed time, so a burst of completed jobs cannot stall the main loop.
- [ ] Slice F: keep transform/gizmo/selection commands that are required for same-frame render as cheap main-thread commands, and document which work remains frame-critical.
- [x] File or update focused graphics follow-ups for selected outline GPU work and ImGui overlay copy/upload churn rather than expanding this runtime task into renderer implementation work.

## Tests
- [x] Add contract tests proving hidden inspector/domain sections do not build selected-entity property, progressive, texture-bake, or visualization models.
- [x] Add contract tests proving steady selected cache-hit frames skip property-catalog, vertex-channel target, progressive, bound-state, UV diagnostics, texture-bake, and visualization model builders, and proving selection-generation changes invalidate same-entity selected-analysis cache entries.
- [ ] Add contract tests proving cache-hit selected frames do not rebuild property catalogs, do not call full vertex-channel resolvers, and do not allocate geometry-sized scratch buffers.
- [ ] Add contract tests proving property option listing uses metadata compatibility only, while explicit active/requested validations use async job results.
- [ ] Add tests proving async selected-analysis results apply only when the generation key is current and stale geometry/property/binding results are discarded.
- [ ] Add tests proving repeated selected frames enqueue at most one job per cache key while a matching job is pending or ready.
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
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|DerivedJob|StreamingExecutor|EditorCommandHistory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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
- Current implementation state (2026-07-05): partial `CPUContracted` Slice C.
  The editor visibility-gates selected-entity model sections, shares per-frame
  domain-window models, exposes deterministic model-build/cache counters, and
  now keeps a persistent selected-model cache for steady inspector analysis and
  visualization frames. The cache key includes stable selected ids and the
  selection controller's selected-set generation, and cache-hit/invalidation
  tests prove the cached frames skip the heavy selected-model builders listed
  above while same-entity reselection invalidates stale entries. Full generation
  stamps for primitive/property/source/visualization revisions,
  allocation/scanned-element counters, async analysis jobs, bounded apply,
  selected-frame timing/copy diagnostics, and Vulkan responsiveness smoke remain
  open; this task is not ready to retire.
