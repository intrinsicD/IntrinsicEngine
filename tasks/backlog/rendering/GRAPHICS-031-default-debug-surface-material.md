# GRAPHICS-031 — Default debug surface material and missing-material fallback (planning)

## Goal
Lock down the design for a deterministic default/debug surface material plus an explicit, testable missing-material fallback policy that GRAPHICS-032's minimal surface pass can rely on, before any shader, registry, or runtime code is written. This is a planning task; no shaders, no registry changes, no runtime behavior land here.

## Non-goals
- No implementation, no shader source added, no `MaterialRegistry` changes, no pipeline-state additions in this slice.
- No surface or present pass command bodies (GRAPHICS-032).
- No material registry contract changes from GRAPHICS-006/006Q beyond what the default debug material specifies as a consumer.
- No textured / PBR / clustered / lit material work; default is intentionally untextured/unlit.
- No shader hot reload or asset-backed material loading (GRAPHICS-023 / GRAPHICS-015 / GRAPHICS-034).
- No editor UI or live-registry access from graphics.

## Context
- Owner layer: `graphics` (registry + pipeline state + shader assets); runtime configures default-slot population on extraction. Final material lives in graphics-owned material/pipeline registry from GRAPHICS-006.
- Geometry alone is insufficient for a surface draw; without a default material, GRAPHICS-032's pass body must soft-skip on missing material — exactly the failure mode the 2026-05-08 review forbids.
- The 2026-05-08 review (section "Exact missing pieces / 4") requires a deterministic default with explicit, testable fallback when material assets are absent.
- `src/graphics/renderer/Graphics.GpuWorld.cppm` already exposes `SetInstanceMaterialSlot`; this task decides what slot value the default occupies.
- `src/legacy/Graphics` debug material code is reference-only and must not be copied.

## Design decisions to record
1. **Material identity and slot.** Decide between (a) a fixed well-known slot value owned by graphics (e.g. `MaterialSystem::DefaultDebugSurfaceSlot`) registered at renderer construction, or (b) a runtime-allocated slot whose value is published via diagnostics. Recommend (a) so the slot is a compile-time constant for tests; record stability guarantees across renderer rebuilds.
2. **Material name and namespace.** Record the canonical name (suggested `Material.DefaultDebugSurface`), its display label policy, and rule that it lives under graphics-owned material naming. Forbid any user-authored material colliding with this name.
3. **Shader pair.** Decide vertex + fragment shader sources: a minimal vertex transform (object → clip) and a flat color-or-vertex-color fragment. Record the source language (HLSL or GLSL per current toolchain), file location under `src/graphics/renderer/Shaders/` (or canonical promoted shader root), and the rule that shaders are compiled at build time, no runtime shader compilation in this slice.
4. **Pipeline state.** Lock the deterministic state vector: cull mode, depth-test, depth-write, blend (off), polygon mode (fill), primitive topology (triangle list), msaa, dynamic-state set. Record the rule that these align with GRAPHICS-008Q renderpass attachment ownership and GRAPHICS-007Q `VisibilityMask`/`Layer` bucket policy without redefining either.
5. **Vertex format.** Specify the expected vertex layout: position (vec3), color (vec3 or u32 packed). Record interaction with GRAPHICS-030's procedural triangle packer so the packer emits exactly this layout.
6. **Descriptor / push-constant layout.** Decide what set/binding/push-range the material uses (likely scene-table BDA via GRAPHICS-008Q descriptor-bind seam, plus per-instance index). Record the rule that no per-material descriptor expansion is required for this default.
7. **Fallback selection ownership.** Decide where the fallback decision is made: (a) `Runtime.RenderExtraction` populates the default slot when a renderable has no authored material; or (b) graphics applies the substitution at snapshot consumption when slot is missing/invalid. Recommend (b) so the runtime can stay agnostic of graphics-side slot identity, plus (a) optionally for renderables that explicitly request the default. Record both paths and the canonical answer.
8. **Diagnostic counters.** Name the counters explicitly: `MissingMaterialFallbackCount`, `InvalidMaterialSlotCount`, `DefaultDebugSurfaceUses`. Decide their location (`MaterialDiagnostics` snapshot, mirroring GRAPHICS-002 `InvalidSnapshotRecordCount` and GRAPHICS-012Q `Picking.Readback` patterns). Forbid silent skip — every fallback path increments a counter.
9. **Visibility guarantee.** Decide what "visible" means for the default debug material: a deterministic non-black color so missing-material conditions can be asserted in pixel-readback tests (when GPU is opt-in) and in command-stream tests (CPU-mock).
10. **Extensibility surface.** Identify how follow-up debug materials (line, point, wireframe, normals, UVs, depth, instance-id) attach without forking the registry: enumerate per-kind material entries with the same descriptor layout family. Limit this slice to one (`DefaultDebugSurface`); identify but do not open follow-ups.
11. **Performance bounds.** Record: shader stays under a documented instruction-count budget (no loops, no texture samples, scalar push constants only); pipeline creation happens at renderer init (one cost) with no per-frame state churn.
12. **Layering audit.** Confirm the default material lives in graphics; runtime never imports the shader or pipeline state; ECS never references the material; no `assets` dependency for the default (it is registry-owned, not asset-loaded).

## Required changes
- Capture all twelve decisions above as explicit recorded answers.
- Cross-link decisions with GRAPHICS-006/006Q (material registry), GRAPHICS-007Q (buckets/visibility), GRAPHICS-008/008Q (depth/surface/G-buffer renderpass ownership), GRAPHICS-013AQ (descriptor flush patterns), GRAPHICS-015 / 015Q (texture residency / fallback), GRAPHICS-029 (renderable bootstrap), GRAPHICS-030 (vertex format consumer), GRAPHICS-032 (consumer of the default).
- Identify follow-up implementation children (do **not** open here):
  - **GRAPHICS-031-Impl-A** — shader sources + build wiring + minimal pipeline registration; no fallback logic yet.
  - **GRAPHICS-031-Impl-B** — fallback logic + diagnostic counters + contract tests.
  - **GRAPHICS-031-Impl-C** (optional) — line / point / wireframe debug material variants gated behind GRAPHICS-010Q backend readiness.

## Tests
- Planning slice: validators only.
- Implementation children must add `contract;graphics` tests asserting:
  - Default material is registered at renderer construction with a stable slot.
  - A renderable submitted with an unset/missing material slot is rendered with the default and the fallback counter increments.
  - The default's pipeline state survives material-registry rebuild without identity churn.
- A `contract;runtime` test must cover the optional extraction-side default-slot population path if option (a) above is selected.
- GPU coverage is opt-in `gpu;vulkan` (pixel readback for the documented default color), outside the CPU gate.

## Docs
- Update `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md` with the default debug material and missing-material fallback policy once decisions are recorded.
- Update `src/graphics/renderer/README.md` describing the slot and diagnostic counters.
- Update `tasks/backlog/rendering/README.md` DAG between GRAPHICS-030 and GRAPHICS-032.

## Acceptance criteria
- All twelve decisions are recorded with explicit answers.
- Implementation children are identified with scope and dependency gates but not opened.
- Architecture and README cross-links updated.
- No engine behavior, no shaders, no pipeline state added to the build in this slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No copying from `src/legacy/Graphics` material code.
- No PBR / clustered / lit / textured material expansion.
- No shader hot reload or asset-backed material work.
- No live ECS access from graphics.
- No silent-skip fallback paths; every fallback must surface a named counter.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
