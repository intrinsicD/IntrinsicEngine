# GRAPHICS-031 — Default debug surface material and missing-material fallback policy

## Goal
Define and add a deterministic default/debug surface material plus an explicit missing-material fallback policy that the minimal surface pass (GRAPHICS-032) can rely on, so that the first reference triangle does not silently skip pass execution because of absent material/pipeline state.

## Non-goals
- No implementation of the surface or present pass command bodies (those land in GRAPHICS-032).
- No expansion of the material registry contract from GRAPHICS-006/006Q beyond what is needed for the default debug material.
- No textured/PBR/clustered/lit material work — the default is intentionally untextured/unlit.
- No editor UI surfacing for material selection.
- No shader hot reload or asset-backed material loading (covered by GRAPHICS-023 / GRAPHICS-015 / GRAPHICS-034).
- No live ECS access from graphics layers; material selection observed by graphics is snapshot/view-only.

## Context
- Owner layer: `graphics`, with runtime configuration support. Final material lives in graphics-owned material/pipeline registry from GRAPHICS-006; runtime supplies the default selection through reference engine config.
- Geometry alone is insufficient for a surface draw. The renderer pass needs a pipeline, descriptor set / push-constant layout, and shader inputs. Without a default, a pass body that lands in GRAPHICS-032 must soft-skip on missing material — which is exactly the failure mode the 2026-05-08 review flags.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, section "Exact missing pieces / 4. Material and shader/default surface policy") requires a deterministic default material with explicit, testable fallback when material assets are absent.
- `src/graphics/renderer/Graphics.GpuWorld.cppm` already exposes `SetInstanceMaterialSlot`; this task decides what slot value the default occupies and how its descriptor/binding state is owned.
- `src/legacy/Graphics` has reference-only debug material code; it must not be copied wholesale into the promoted layer (per GRAPHICS-001 and AGENTS.md section 5).

## Required changes
- Decide default material identity and ownership:
  - One graphics-owned default debug surface material with a fixed material slot, a minimal SPIR-V/HLSL shader pair (vertex transform + flat/vertex-color or constant fragment color), and explicit pipeline state.
  - Whether the shader sources live under `src/graphics/renderer/Shaders/` (or the canonical promoted shader root) and how they are compiled in CI without breaking GRAPHICS-018T / GRAPHICS-026 contracts.
- Define the missing-material fallback contract:
  - When a renderable's material slot is unset or refers to a missing material, graphics must substitute the default debug material and increment a named diagnostic counter (mirroring the `InvalidSnapshotRecordCount` / `Picking.Readback` patterns from GRAPHICS-002 / GRAPHICS-012Q).
  - The fallback never silently skips the draw; it produces a deterministic visible result so missing-material conditions are testable.
- Wire the runtime side:
  - Reference engine config or `Runtime.RenderExtraction` populates the default material slot for renderables that have no material authored, without introducing `graphics/*` imports into ECS or live-registry access in graphics.
- Cross-link decisions with GRAPHICS-006/006Q, GRAPHICS-008/008Q, GRAPHICS-015, GRAPHICS-029, GRAPHICS-030, and GRAPHICS-032.

## Tests
- Add `contract;graphics` tests asserting that:
  - The default debug material is registered after renderer construction with a stable slot.
  - A renderable submitted with an unset/missing material slot is rendered with the default material in the snapshot/view path, and the missing-material diagnostic counter increments.
  - The default material's pipeline state survives material-registry rebuild without identity churn.
- Add a `contract;runtime` test asserting that runtime extraction populates the default material slot for renderables that lack authored material data when the reference engine config opts in.
- Verification gate: default CPU-supported correctness target.
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md` with the default debug material and missing-material fallback policy.
- Update `src/graphics/renderer/README.md` describing the default material slot and diagnostic counter.
- Update `tasks/backlog/rendering/README.md` DAG to insert this task between GRAPHICS-030 and GRAPHICS-032.

## Acceptance criteria
- The renderer registers a deterministic default debug surface material at construction; identity and slot are documented.
- Missing-material fallback is explicit, testable, and visible (no silent skip).
- Layering invariants hold: ECS does not import graphics, graphics does not access live ECS state, runtime owns the default-slot population path.
- All new tests pass under the default CPU gate.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No copying from `src/legacy/Graphics` material code; reimplement against current registry contracts.
- No PBR/clustered/lit/textured material expansion in this slice.
- No shader hot reload or asset-backed material work.
- No live ECS access from graphics layers.
- No mixing of mechanical file moves with semantic refactors.
