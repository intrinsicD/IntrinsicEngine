# Sandbox & Graphics Gap Analysis (2026-05-11)

## Scope

This review records the implementation gap between the planning-only retired
tasks `GRAPHICS-029..033` and the current branch state. It is the follow-up to
[2026-05-08 sandbox geometry rendering gap analysis](2026-05-08-sandbox-geometry-rendering-gap-analysis.md):
that earlier review framed the four-layer gap; this one inventories what
implementation work has and has not landed since, and opens implementation-child
tasks for each piece.

## Executive summary

`ExtrinsicSandbox` builds, links, and runs the full runtime frame loop, but it
cannot render a single visible pixel. Each layer between the empty ECS scene
and the swapchain finalization step is wired only as a CPU-testable contract:

- the device factory always returns the **Null** device (`Runtime.Engine.cpp:49–73`);
- the renderer factory always returns a single `NullRenderer` class (`Graphics.Renderer.cpp:1059–1062`);
- the executor only routes commands for `CullingPass` and `DepthPrepass`; every
  other pass is `SkippedNonOperational`/`SkippedUnavailable`
  (`Graphics.Renderer.cpp:733–786`);
- no renderable content exists in the scene to extract;
- the procedural-geometry residency bridge is unimplemented (zero
  `GpuWorld::UploadGeometry()`/`SetInstanceGeometry()` calls in
  `Runtime.RenderExtraction.cppm`);
- there is no default surface material/shader on disk and no SPIR-V build
  wiring for the promoted `ExtrinsicSandbox`;
- the Vulkan backend is intentionally fail-closed with
  `IsOperational()==false` until the `GRAPHICS-033` nine-step gate is satisfied.

## Per-task implementation status (2026-05-11)

| Planning parent (done) | Impl-A | Impl-B | Impl-C | Impl-D |
|---|---|---|---|---|
| GRAPHICS-029 | done → [GRAPHICS-029A](../../tasks/done/GRAPHICS-029A-reference-scene-skeleton.md) | done → [GRAPHICS-029B](../../tasks/done/GRAPHICS-029B-triangle-provider-and-camera.md) | optional, not opened | n/a |
| GRAPHICS-030 | done → [GRAPHICS-030A](../../tasks/done/GRAPHICS-030A-procedural-geometry-descriptor-cache.md) | done → [GRAPHICS-030B](../../tasks/done/GRAPHICS-030B-extraction-procedural-geometry-binding.md) | done → [GRAPHICS-030C](../../tasks/done/GRAPHICS-030C-procedural-geometry-retire-ordering.md) | optional (cube/quad packer), not opened |
| GRAPHICS-031 | done → [GRAPHICS-031A](../../tasks/done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md) | done → [GRAPHICS-031B](../../tasks/done/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md) | optional (debug variant), not opened | n/a |
| GRAPHICS-032 | done → [GRAPHICS-032A](../../tasks/done/GRAPHICS-032A-minimal-debug-surface-recipe.md) | done → [GRAPHICS-032B](../../tasks/done/GRAPHICS-032B-minimal-debug-surface-pass-body.md) | done → [GRAPHICS-032C](../../tasks/done/GRAPHICS-032C-minimal-debug-present-pass-and-acceptance.md) | not started → [GRAPHICS-032D](../../tasks/backlog/rendering/GRAPHICS-032D-gpu-vulkan-minimal-recipe-smoke.md) |
| GRAPHICS-033 | done → [GRAPHICS-033A](../../tasks/done/GRAPHICS-033A-vulkan-operational-status-evaluator.md) | in-progress → [GRAPHICS-033B](../../tasks/active/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md) | not started → [GRAPHICS-033C](../../tasks/backlog/rendering/GRAPHICS-033C-vulkan-minimal-recipe-recording.md) | not started → [GRAPHICS-033D](../../tasks/backlog/rendering/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md) |

Standalone infrastructure / runtime tasks newly opened:

- [BUILD-001](../../tasks/done/BUILD-001-sandbox-shader-compile-wiring.md) — Sandbox shader compile wiring (done).
- [RUNTIME-070](../../tasks/done/RUNTIME-070-fallback-texture-bootstrap.md) — `GpuAssetCache` fallback texture bootstrap (done).
- [GRAPHICS-080](../../tasks/backlog/rendering/GRAPHICS-080-enable-promoted-vulkan-by-default.md) — Reference config + `ci-vulkan` preset.

Beyond-triangle (default-recipe pass operational wiring + runtime adapter umbrellas) newly opened:

- Pass wiring: [GRAPHICS-070..079](../../tasks/backlog/rendering/) (forward surface, line/point, deferred GBuffer + lighting, shadows, selection + outline + picking readback, postprocess chain, debug view + canonical present, transient debug upload helper, visualization overlay helper, ImGui pass).
- Runtime adapters: [RUNTIME-080..084, RUNTIME-090](../../tasks/backlog/runtime/) (texture asset bridge, camera controllers, spatial debug adapters, visualization adapters, gizmo interaction, ImGui adapter).

## Triangle-path dependency order

Recommended pickup order for the Theme A triangle path:

1. `GRAPHICS-030A` (active) — finish the procedural geometry descriptor + cache + triangle packer.
2. `BUILD-001` — done; shader compilation is wired to `ExtrinsicSandbox`.
3. `RUNTIME-070` — bootstrap `GpuAssetCache` fallback texture (can land in parallel).
4. `GRAPHICS-029A` — reference scene skeleton.
5. `GRAPHICS-029B` — TriangleProvider + camera substitution (depends on #1 + #4).
6. `GRAPHICS-030B` — wire extraction to procedural residency bridge (depends on #1 + #5).
7. `GRAPHICS-030C` — refcount/retire ordering (depends on #6).
8. `GRAPHICS-031A` — default debug surface shaders + pipeline (depends on #2).
9. `GRAPHICS-031B` — substitution + counters (depends on #8).
10. `GRAPHICS-032A` — minimal debug surface recipe (depends on #8).
11. `GRAPHICS-032B` — surface pass body (depends on #10 + #6 + #8).
12. `GRAPHICS-032C` — present pass body + e2e CPU acceptance test (depends on #11).
13. `GRAPHICS-033A` — Vulkan operational-status evaluator.
14. `GRAPHICS-033B` — diagnostics snapshot + runtime breadcrumb (depends on #13).
15. `GRAPHICS-033C` — Vulkan command-recording bodies for the minimal recipe (depends on #12 + #9 + #14).
16. `GRAPHICS-080` — flip reference config + add `ci-vulkan` preset (can land any time after #15).
17. `GRAPHICS-032D` + `GRAPHICS-033D` — opt-in `gpu;vulkan` smoke fixtures (depend on #15).

After step #17, on a Vulkan-capable Linux host: `ExtrinsicSandbox` started under
the `ci-vulkan` preset displays one visible triangle.

## Beyond-triangle order

Once the triangle path lands, the default frame recipe wires up pass-by-pass.
The recommended order is the rendering DAG's Theme B′ ordering:

1. `GRAPHICS-070` — Forward.Surface (reuses GRAPHICS-031 default debug pipeline as fallback for missing materials).
2. `GRAPHICS-071` — Forward.Line / Forward.Point (retained renderables).
3. `GRAPHICS-073` — Shadows (atlas + depth-only pipeline).
4. `GRAPHICS-072` — Deferred GBuffer + lighting (consumes shadow atlas from #3).
5. `GRAPHICS-074` — Selection ID passes + outline + picking readback drain.
6. `GRAPHICS-075` — Postprocess chain (Histogram → Bloom → ToneMap → FXAA/SMAA).
7. `GRAPHICS-076` — DebugView + canonical Present.
8. `GRAPHICS-077` — Transient debug primitive upload helper.
9. `GRAPHICS-078` — Visualization overlay upload helper.
10. `GRAPHICS-079` — ImGui pass.

Runtime adapter umbrellas can land in parallel:

- `RUNTIME-080` — Texture asset bridge.
- `RUNTIME-081` — Camera controllers.
- `RUNTIME-082` — Spatial debug adapters.
- `RUNTIME-083` — Visualization adapters.
- `RUNTIME-084` — Gizmo interaction.
- `RUNTIME-090` — Dear ImGui platform/renderer adapter (gates `GRAPHICS-079`).

Asset-backed mesh residency (`GRAPHICS-034`) gates on `ASSETIO-001` and remains
planning-only until that ingest ownership decision lands.

## Bootstrap scaffold retirement

The `MinimalDebugSurface` recipe and the `Pass.Surface.MinimalDebug` /
`Pass.Present.MinimalDebug` classes introduced by `GRAPHICS-032` exist only to
derisk the triangle path. They must not outlive the operational default
recipe. Once `GRAPHICS-070..076` retire and the default-recipe equivalent of
the `GRAPHICS-033D` `gpu;vulkan` visible-triangle smoke is green, the scaffold
is deleted by
[`GRAPHICS-081`](../../tasks/backlog/rendering/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md):
recipe + factory + two pass classes + renderer members + executor branches +
three diagnostics counters + CMake entries + tests + doc rows all removed.
`Material.DefaultDebugSurface` at slot 0 (GRAPHICS-031), the reference scene
(GRAPHICS-029), the procedural geometry residency bridge (GRAPHICS-030), and
the Vulkan operational-status evaluator (GRAPHICS-033A/B) all stay — they are
the canonical architecture, not scaffolding.

### Other intermediate solutions and their retirement arcs

Beyond the MinimalDebug scaffold, the triangle-path tasks introduce four
smaller intermediate solutions. Each has an owning retirement task; the
`GRAPHICS-081` companion audit verifies that none of them survive the
full-pipeline completion.

| Intermediate solution | Owner of retirement | Form of retirement |
|---|---|---|
| `GRAPHICS-029A` no-op default `IReferenceSceneProvider` for unregistered selectors | `GRAPHICS-029B` (closing checkbox) | Replaced by `std::terminate` on unknown-selector resolve, matching `GRAPHICS-029` Decision 7 double-install policy. |
| `GRAPHICS-029B` direct `m_ReferenceCamera → RenderFrameInput::Camera` substitution in `Engine::BuildRenderFrameInput` | `RUNTIME-081` (CameraControllers) | Deleted in the same commit that wires the controller-driven update. Reference-scene `CameraViewInput` survives only as the controller's initial seed. |
| `GRAPHICS-029B` `#if __has_include(...)` (or CMake-flag) test guard around the `ProceduralGeometryRef` assertion | `GRAPHICS-030A` retirement | Guard removed; assertion becomes unconditional. |
| `GRAPHICS-080` acceptance pointer to "renders through GRAPHICS-032 minimal recipe" | `GRAPHICS-076` + `GRAPHICS-081` | Staged: initially the minimal recipe (validates the Vulkan operational gate in isolation); finally the default recipe (canonical). |
| Renderer executor's `"everything else returns SkippedNonOperational/SkippedUnavailable"` default branch (pre-existing, not introduced here) | `GRAPHICS-079` (closing-cleanup assertion) | The default branch is preserved as a safety net for non-operational devices, but a `contract;graphics` test asserts no canonical default-recipe pass name falls through it once `GRAPHICS-070..079` retire. |

The `GRAPHICS-081` companion audit section lists each of these as a
prerequisite checkbox before the minimal recipe is deleted, so the team
cannot accidentally cement an intermediate solution past the full-pipeline
completion.

## Validation notes

- This review introduces no source code changes.
- Tasks added under `tasks/backlog/rendering/` and `tasks/backlog/runtime/`;
  rendering DAG (`tasks/backlog/rendering/README.md`) and runtime backlog
  (`tasks/backlog/runtime/README.md`) updated to cite the new tasks; the
  parent backlog README's "Theme A" section updated.
- `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`, and
  `python3 tools/repo/check_layering.py --root src --strict` should pass after
  this review lands; the new tasks themselves do not introduce any code or
  module-surface change yet.
