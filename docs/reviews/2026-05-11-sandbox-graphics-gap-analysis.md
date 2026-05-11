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
| GRAPHICS-029 | not started → [GRAPHICS-029A](../../tasks/backlog/rendering/GRAPHICS-029A-reference-scene-skeleton.md) | not started → [GRAPHICS-029B](../../tasks/backlog/rendering/GRAPHICS-029B-triangle-provider-and-camera.md) | optional, not opened | n/a |
| GRAPHICS-030 | active → [GRAPHICS-030A](../../tasks/active/GRAPHICS-030A-procedural-geometry-descriptor-cache.md) | not started → [GRAPHICS-030B](../../tasks/backlog/rendering/GRAPHICS-030B-extraction-procedural-geometry-binding.md) | not started → [GRAPHICS-030C](../../tasks/backlog/rendering/GRAPHICS-030C-procedural-geometry-retire-ordering.md) | optional (cube/quad packer), not opened |
| GRAPHICS-031 | not started → [GRAPHICS-031A](../../tasks/backlog/rendering/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md) | not started → [GRAPHICS-031B](../../tasks/backlog/rendering/GRAPHICS-031B-default-debug-surface-substitution-and-diagnostics.md) | optional (debug variant), not opened | n/a |
| GRAPHICS-032 | not started → [GRAPHICS-032A](../../tasks/backlog/rendering/GRAPHICS-032A-minimal-debug-surface-recipe.md) | not started → [GRAPHICS-032B](../../tasks/backlog/rendering/GRAPHICS-032B-minimal-debug-surface-pass-body.md) | not started → [GRAPHICS-032C](../../tasks/backlog/rendering/GRAPHICS-032C-minimal-debug-present-pass-and-acceptance.md) | not started → [GRAPHICS-032D](../../tasks/backlog/rendering/GRAPHICS-032D-gpu-vulkan-minimal-recipe-smoke.md) |
| GRAPHICS-033 | not started → [GRAPHICS-033A](../../tasks/backlog/rendering/GRAPHICS-033A-vulkan-operational-status-evaluator.md) | not started → [GRAPHICS-033B](../../tasks/backlog/rendering/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md) | not started → [GRAPHICS-033C](../../tasks/backlog/rendering/GRAPHICS-033C-vulkan-minimal-recipe-recording.md) | not started → [GRAPHICS-033D](../../tasks/backlog/rendering/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md) |

Standalone infrastructure / runtime tasks newly opened:

- [BUILD-001](../../tasks/backlog/runtime/BUILD-001-sandbox-shader-compile-wiring.md) — Sandbox shader compile wiring.
- [RUNTIME-070](../../tasks/backlog/runtime/RUNTIME-070-fallback-texture-bootstrap.md) — `GpuAssetCache` fallback texture bootstrap.
- [GRAPHICS-080](../../tasks/backlog/rendering/GRAPHICS-080-enable-promoted-vulkan-by-default.md) — Reference config + `ci-vulkan` preset.

Beyond-triangle (default-recipe pass operational wiring + runtime adapter umbrellas) newly opened:

- Pass wiring: [GRAPHICS-070..079](../../tasks/backlog/rendering/) (forward surface, line/point, deferred GBuffer + lighting, shadows, selection + outline + picking readback, postprocess chain, debug view + canonical present, transient debug upload helper, visualization overlay helper, ImGui pass).
- Runtime adapters: [RUNTIME-080..084, RUNTIME-090](../../tasks/backlog/runtime/) (texture asset bridge, camera controllers, spatial debug adapters, visualization adapters, gizmo interaction, ImGui adapter).

## Triangle-path dependency order

Recommended pickup order for the Theme A triangle path:

1. `GRAPHICS-030A` (active) — finish the procedural geometry descriptor + cache + triangle packer.
2. `BUILD-001` — wire shader compilation to `ExtrinsicSandbox` (CMake-only; can land in parallel with #1).
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
