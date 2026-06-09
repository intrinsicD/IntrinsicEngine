---
id: LEGACY-011
theme: F
depends_on: []
---
# LEGACY-011 — Value-gated legacy feature reimplementation map

## Goal
- Create the cross-domain decision map required before the remaining `src/legacy/` subtrees can retire: each legacy feature candidate is either accepted because it is necessary or improves the current promoted architecture, deferred behind a concrete trigger, or retired as legacy clutter.

## Non-goals
- No source deletion from `src/legacy/`.
- No implementation work in this planning task.
- No revival of legacy module names, compatibility re-exports, or global registries.
- No feature is accepted only because it existed in legacy code.
- No broad parity sweep without a concrete current-state gap and a measurable improvement.

## Context
- Owner/layer: legacy retirement planning across `core`, `assets`, `ecs`, `platform`, `runtime`, `graphics`, and `ui`.
- Evidence sources: `src/legacy/**`, `docs/migration/nonlegacy-parity-matrix.md`, `tasks/backlog/README.md`, and the existing `LEGACY-001..010` subtree deletion tasks.
- Existing promoted seams must be reused before new abstractions are introduced: `Core.FrameLoop`, `Runtime.Engine`, `Runtime.RenderExtraction`, `Runtime.StreamingExecutor`, `Runtime.SceneSerialization`, `Runtime.SelectionController`, `Runtime.StableEntityLookup`, `Runtime.VisualizationAdapters`, `Graphics.GpuAssetCache`, `Graphics.RenderPrepPipeline`, `Graphics.VisualizationOverlayUploadHelper`, `Graphics.TransientDebugUploadHelper`, and the asset bridges.
- This map intentionally separates feature work from mechanical deletion work so `LEGACY-*` subtree removal tasks can stay purely mechanical.
- Value-gate rule: implementation tasks must show the current promoted state, the user-visible or architecture-quality improvement, and the smallest layer-safe owner split. If a candidate would recreate a legacy interdependency, it is retired or deferred instead.

## Required changes
- [ ] Keep the triage table and child task inventory below current as feature gaps retire, defer, or are explicitly rejected.
- [ ] Ensure each child task records current promoted state, intended improvement, and scope decision before implementation starts.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` rows to link each remaining retained/deferred gap to its owning task or retirement decision.
- [ ] Update `docs/migration/legacy-retirement.md` when a child task unblocks a specific `LEGACY-*` subtree deletion.
- [ ] Re-run the consumer-grep gates in `LEGACY-001`, `LEGACY-004`, `LEGACY-005`, `LEGACY-006`, `LEGACY-008`, `LEGACY-009`, and `LEGACY-010` after the child tasks retire.

## Tests
- [ ] `python3 tools/agents/validate_tasks.py --root tasks --strict` validates this map and all child tasks.
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` accepts the new backlog state.
- [ ] `python3 tools/docs/check_doc_links.py --root .` validates every cross-link.

## Docs
- [ ] `tasks/backlog/README.md` links this map under Theme F or legacy retirement follow-ups.
- [ ] Per-category backlog READMEs list the child tasks under their owning queues.
- [ ] `docs/migration/nonlegacy-parity-matrix.md` records the task IDs in the relevant missing-behavior cells.

## Acceptance criteria
- [ ] Every missing or unproven behavior called out by `docs/migration/nonlegacy-parity-matrix.md` has a value-gated outcome: retained task, deferred task/trigger, or explicit retirement decision.
- [ ] Child tasks preserve `AGENTS.md` layer ownership: lower layers do not import runtime, graphics does not read live ECS, assets stay CPU-only, and runtime owns composition/wiring.
- [ ] Mechanical deletion tasks remain blocked only by concrete consumer-grep or parity gates, not by unnamed feature gaps.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding compatibility imports from promoted modules back to bare legacy module names.
- Treating this map as permission to delete any legacy subtree.

## Value-gated feature triage

| Candidate | Current promoted state | Improvement if retained | Decision / planning owner |
|---|---|---|---|
| Command and feature catalogs | `Core.CallbackRegistry`, DAG scheduling, config, runtime/UI command surfaces already cover focused needs. | Avoids a global service locator while preserving only dependency-free descriptors or runtime/UI command history where a real consumer exists. | Retired as a global catalog by `CORE-002`; runtime/editor command history is owned by `RUNTIME-102`. |
| Asset errors, reload, and destroy ordering | `AssetService`, payload store, event bus, import bridges, runtime handoffs, operation-status classification, payload-ticket reload atomicity, and destroy-time same-asset event draining exist. | Deterministic fail-closed asset operations and no second asset manager. | Retired by `ASSETIO-002`. |
| KTX/KTX2 textures | Import routing recognizes KTX/KTX2, but no checked-in assets, renderer tests, or current material workflows need compressed/mip texture payloads. `Asset.ModelTexturePayload`, `Asset.ModelTextureIOBridge`, and `Runtime.AssetModelTextureIO` reject KTX/KTX2 with `AssetUnsupportedFormat`. | Deterministic unsupported diagnostics avoid a speculative KTX/Basis/transcoding pipeline while preserving a clear future trigger. | Retired by `ASSETIO-003`; future compressed texture work must open a new scoped task. |
| File-format visual coverage | Geometry/model/texture import routes exist; `UI-007`/`RUNTIME-095` prove scoped sandbox use; `ASSETIO-004` adds the representative CPU/null fixture matrix for OBJ, TGF, PLY point cloud, GLTF model-scene, and PNG texture imports plus material texture re-resolution after upload/reload. | Representative fixture coverage prevents regressions without adding large datasets or every legacy format. | Retired by `ASSETIO-004`; future broad GPU readback requires a new value-gated `Operational` task. |
| ECS legacy components | Promoted scene, hierarchy, transform, metadata, geometry sources, dirty tags, selection, physics authoring, and stable IDs exist. `HARDEN-081` maps legacy `NameTag` to `MetaData::EntityName`, retires `AxisRotator` as demo/sample behavior, keeps DEC caches outside canonical ECS, and keeps feature catalogs out of ECS. | Retires demo/component clutter; keeps only mappings that current consumers need, such as `MetaData::EntityName`. | Retired by `HARDEN-081`; remaining bare `ECS` consumers are `LEGACY-012` / subtree-deletion cleanup. |
| Platform events and dialogs | Null/GLFW backends, input, resize/minimize, and drop events exist. | Clarifies editor event/file boundary while preserving headless tests. | Retain current-workflow events; defer IME/multi-window/native dialogs unless justified. `PLATFORM-006`, `UI-008`. |
| Runtime lifecycle composition | `Engine::RunFrame`, ECS bundle, extraction, prep pipeline, ImGui adapter, physics bridge, asset handoffs, internal `RuntimeFrameContext`, and promoted `Core.FrameLoop` phase contracts exist. | Makes frame/shutdown order explicit and testable without legacy orchestrator coupling. | Retired by `RUNTIME-099`; broader legacy runtime deletion remains blocked by ingest, algorithm queue, overlay, consumer-test, and subtree deletion gates. |
| Scene lifecycle and persistence | `RUNTIME-098` covers current sandbox JSON save/load; `RUNTIME-100` adds one runtime scene replacement boundary, render-extraction/selection/physics reset contracts, and supported/deferred/retired persistence diagnostics. | Avoids stale scene sidecars and avoids pretending every legacy component should serialize. | Retired by `RUNTIME-100`; arbitrary asset-source reimport remains under `RUNTIME-101`. |
| Asset ingest state machine | Manual import and drag/drop use promoted bridges and `StreamingExecutor`; shared request/status policy is incomplete. | One deterministic ingest path for manual, drag/drop, reimport, cancellation, and stale completions. | Retain narrow runtime state machine. `RUNTIME-101`. |
| Editor command history and dirty state | UI commands exist, but undo/redo and document dirty source of truth are missing. | Practical editor behavior owned by runtime instead of `core` or UI mutation. | Retain for current editor commands only. `RUNTIME-102`, `UI-008`. |
| Geometry algorithm execution queue | CPU K-Means is synchronous and already promoted; CUDA and broader workflows are not required by default. | Prevents UI stalls only if the existing workflow needs async; avoids a speculative algorithm framework. | Conditional retain; CUDA defer/retire unless a method/backend task justifies it. `RUNTIME-103`, `GRAPHICS-086`. |
| Derived overlays | Transient debug and visualization packet/upload paths exist; persistent legacy overlay entities coupled graphics to ECS. | Keeps persistent overlays only where runtime-owned producers improve current visualization/selection workflows. | Conditional retain; prefer existing packet lanes. `RUNTIME-104`, `GRAPHICS-085`. |
| Visualization property buffers | UI can choose scalar/isoline/color presets; generic GPU residency for property arrays is deferred. | Enables current promoted visualization presets to render without external GPU-buffer ownership by runtime/UI. | Retain selected property domains/types only. `GRAPHICS-084`. |
| RHI/CUDA retirement gaps | Promoted RHI/Vulkan surfaces and tests exist; legacy convenience APIs and CUDA remain undecided. | Converts deletion blockers into tests or retirement decisions without making CUDA mandatory. | Decision/audit first. `GRAPHICS-086`. |
| Editor file/debug workflows | Editor shell, domain windows, K-Means, visualization presets, rendergraph panel, and drag/drop status exist. | Adds dirty/undo/path-entry status to make current editor coherent; avoids native-dialog and sample-scene sprawl by default. | Retain workflow model; defer native dialogs/debug scene clones. `UI-008`. |
| Legacy consumer tests | Some tests still import bare legacy names. | Lets deletion gates become mechanical after retained features are implemented or retired. | Retain cleanup. `LEGACY-012`. |

## Child task inventory

- `CORE-002` (done): decision-first command / feature catalog replacement; retained promoted dependency-free utility/telemetry seams and retired the global catalog shape.
- `ASSETIO-002` (done): asset error taxonomy, load-state, reload, and destroy-order rules that improve deterministic promoted asset behavior.
- `ASSETIO-003` (done): KTX/KTX2 is route-recognized but explicitly unsupported for current promoted workflows; no runtime decoder or GPU handoff is retained.
- `ASSETIO-004` (done): representative file-format coverage and material re-resolution, not an exhaustive legacy format sweep.
- `HARDEN-081` (done): ECS legacy component decisions, with demo/clutter behavior retired or reassigned by default.
- `PLATFORM-006`: currently needed platform event/file-boundary contracts; IME, multi-window, and native dialogs require explicit follow-up triggers.
- `RUNTIME-099` (done): explicit runtime lifecycle composition pipeline from begin-frame through shutdown.
- `RUNTIME-100` (done): scene lifecycle, world reset, sidecar cleanup, stable identity, and explicit supported/deferred/retired persistence boundaries.
- `RUNTIME-101`: shared runtime ingest state machine over promoted `AssetService` / `StreamingExecutor` handoffs.
- `RUNTIME-102`: editor command history, undo/redo, recursive delete/orphan policy, and dirty-state services for current editor commands.
- `RUNTIME-103`: conditional geometry algorithm queue, with CUDA and topology/centroid outputs retained only behind concrete triggers.
- `RUNTIME-104`: conditional derived overlay producer lifecycle, preferring existing transient/debug/visualization packet lanes where sufficient.
- `GRAPHICS-084`: selected visualization property-buffer GPU residency for current promoted visualization presets.
- `GRAPHICS-085`: overlay packet backend proof only for overlay classes retained by `RUNTIME-104`.
- `GRAPHICS-086`: RHI retirement audit and CUDA keep/remove/defer decision before any backend implementation.
- `UI-008`: editor dirty-state, undo/redo, path-entry/file-boundary, and headless-safe workflow model; native dialogs/debug scene clones are deferred by default.
- `LEGACY-012`: migration or retirement of tests and non-legacy consumers that still import bare legacy module names after semantic replacements exist.

## Maturity
- Target: `Scaffolded` planning map.
- `CPUContracted` / `Operational` gates are owned by the child tasks named above.
